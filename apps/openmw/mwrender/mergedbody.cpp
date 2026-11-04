#include "mergedbody.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <vsg/core/Visitor.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/contents.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/skin.hpp>
#include <components/body/bodycache.hpp>
#include <components/body/bodymesh.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/sets.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/readnode.hpp>

#include "../bodybaker.hpp"
#include "../bodycommon.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/esmstore.hpp"

#include "bodyparts.hpp"

namespace MWRender::MergedBodies
{
    namespace
    {
        // The look of one stock state group: its pipeline options and its descriptors without bone matrices.
        struct Look
        {
            Pipeline::Options mOptions;
            vsg::Descriptors mDescriptors;
        };

        struct CacheState
        {
            bool mLoaded = false;
            std::optional<Body::PackageFiles> mFiles;
            std::optional<Body::CacheManifest> mManifest;
            std::string mError;
        };

        std::mutex sMutex;
        std::filesystem::path sUserDataFolder;
        CacheState sCache;
        std::map<std::string, TemplateResult> sTemplates;
        std::set<std::string> sReported;
        bool sMessageShown = false;

        std::string sexName(bool female)
        {
            return female ? "female" : "male";
        }

        std::string templateKey(const ESM::Race& race, bool female)
        {
            return Misc::StringUtils::lowerCase(race.mId.getRefIdString()) + "|" + sexName(female);
        }

        // Main thread, under sMutex: reads `current`, the pack it names and the manifest, once per session.
        void loadCache()
        {
            if (sCache.mLoaded)
                return;
            sCache.mLoaded = true;
            if (sUserDataFolder.empty())
            {
                sCache.mError = "the user data folder is not known";
                return;
            }
            const std::filesystem::path folder = sUserDataFolder / "openeden" / "bodies";
            std::string error;
            std::optional<Body::PackageFiles> files = Body::loadCurrentCache(folder, error);
            if (!files)
            {
                sCache.mError = error;
                return;
            }
            const auto manifest = files->mFiles.find("manifest.txt");
            if (manifest == files->mFiles.end())
            {
                sCache.mError = "the pack has no manifest.txt";
                return;
            }
            std::optional<Body::CacheManifest> parsed
                = Body::readManifest(std::string(manifest->second.begin(), manifest->second.end()), error);
            if (!parsed)
            {
                sCache.mError = "the manifest does not read: " + error;
                return;
            }
            sCache.mFiles = std::move(files);
            sCache.mManifest = std::move(parsed);
            Log(Debug::Info) << "Merged bodies: the cache in " << folder.string() << " has "
                             << sCache.mManifest->mEntries.size() << " bodies";
        }

        class CollectStateGroups : public vsg::Visitor
        {
        public:
            std::vector<vsg::StateGroup*> mGroups;

            void apply(vsg::Node& node) override { node.traverse(*this); }

            void apply(vsg::StateGroup& group) override
            {
                mGroups.push_back(&group);
                group.traverse(*this);
            }
        };

        // The descriptors of a stock state group's texture set, without its bone matrices. False when the group's
        // textures switch (a StateSwitch) or it has no descriptor set.
        bool descriptorsOf(const vsg::StateGroup& group, vsg::Descriptors& out)
        {
            for (const auto& command : group.stateCommands)
            {
                const auto* bind = dynamic_cast<const vsg::BindDescriptorSet*>(command.get());
                if (bind == nullptr || bind->firstSet != Pipeline::TEXTURE_SET || !bind->descriptorSet)
                    continue;
                for (const auto& descriptor : bind->descriptorSet->descriptors)
                {
                    if (descriptor && descriptor->dstBinding != Pipeline::Descriptors::BONE_BINDING)
                        out.push_back(descriptor);
                }
                return true;
            }
            return false;
        }

        // The stock look of the material `materialKey` in the part of `slot`: the part is read through the engine's
        // reader with the stock part's own options (and a fresh object cache, so the reader runs), while an override
        // records the pipeline options of each state group in the order the reader creates them.
        std::optional<Look> findLook(int slot, const ESM::BodyPart& record, const std::string& materialKey,
            const MWAnim::Context& context, const VFS::Manager& vfs, std::string& error)
        {
            const auto type = static_cast<ESM::PartReferenceType>(slot);
            if (slot < 0 || static_cast<std::size_t>(slot) >= context.partBoneOptions.size())
            {
                error = "slot " + std::to_string(slot) + " has no read options";
                return {};
            }
            auto options = vsg::Options::create(*context.partBoneOptions[slot]);
            options->sharedObjects = vsg::SharedObjects::create();
            std::vector<Pipeline::Options> captured;
            auto capture = vsg::ref_ptr{ new Pipeline::Override };
            capture->pipelineOptions = [&captured](Pipeline::Options& o) { captured.push_back(o); };
            capture->composite(*options);
            vsg::ref_ptr<vsg::Node> node = vsgUtil::readNode(record.mModel, options);
            if (!node)
            {
                error = "the part '" + record.mModel + "' does not read";
                return {};
            }
            CollectStateGroups collect;
            node->accept(collect);
            if (collect.mGroups.size() != captured.size() || collect.mGroups.empty())
            {
                error = "the part '" + record.mModel + "' made " + std::to_string(collect.mGroups.size())
                    + " state groups and " + std::to_string(captured.size()) + " pipeline requests";
                return {};
            }
            std::size_t index = 0;
            if (collect.mGroups.size() > 1)
            {
                // Several shapes: the one whose material key matches, in the order the bake's reader lists them.
                const std::string model
                    = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(record.mModel)).value();
                const OMW::BodyCommon::ParsedFile parsed = OMW::BodyCommon::parseFile(vfs, model);
                std::vector<std::string> errors;
                const std::vector<BodyNif::Shape> shapes
                    = OMW::BodyCommon::shapesOf(parsed, MWAnim::sPartBones.at(type), BodyNif::Placement{}, errors);
                if (shapes.size() != collect.mGroups.size())
                {
                    error = "the part '" + record.mModel + "' has " + std::to_string(shapes.size()) + " shapes and "
                        + std::to_string(collect.mGroups.size()) + " state groups";
                    return {};
                }
                const auto found = std::find_if(shapes.begin(), shapes.end(),
                    [&](const BodyNif::Shape& shape) { return OMW::BodyCommon::materialKey(shape) == materialKey; });
                if (found == shapes.end())
                {
                    error = "the part '" + record.mModel + "' has no shape of material '" + materialKey + "'";
                    return {};
                }
                index = static_cast<std::size_t>(found - shapes.begin());
            }
            Look look;
            look.mOptions = captured[index];
            if (!descriptorsOf(*collect.mGroups[index], look.mDescriptors))
            {
                error = "the part '" + record.mModel + "' switches its textures";
                return {};
            }
            return look;
        }

        // Row-major [M3 | t] (v' = M3 v + t) to VSG's column-major mat4, element by element ([column][row]).
        vsg::mat4 toMat4(const Body::Affine34& m)
        {
            vsg::mat4 result; // identity
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                    result[column][row] = static_cast<float>(m[static_cast<std::size_t>(row * 4 + column)]);
                result[3][row] = static_cast<float>(m[static_cast<std::size_t>(row * 4 + 3)]);
            }
            return result;
        }

        vsg::dvec3 applyAffine(const Body::Affine34& m, const vsg::dvec3& v)
        {
            return { m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3], m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
                m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11] };
        }

        Body::Affine34 toAffine(const BodyNif::Matrix34& m)
        {
            Body::Affine34 result{};
            std::copy(m.begin(), m.end(), result.begin());
            return result;
        }

        // The modes the merged geometry cannot carry: it has no vertex colours, morph targets or tangents.
        bool adaptOptions(Pipeline::Options& options, bool normals, bool uvs)
        {
            using Pipeline::Mode;
            for (const Mode refused : { Mode::PARTICLE, Mode::BILLBOARD, Mode::INSTANCE })
            {
                if (options.shader.hasMode(refused))
                    return false;
            }
            if (options.shader.hasMode(Mode::COLOR))
            {
                options.shader.removeMode(Mode::COLOR);
                options.colorMode = 0;
            }
            for (const Mode removed : { Mode::MORPH, Mode::NORMAL_MAP, Mode::PARALLAX, Mode::RECONSTRUCT_NORMAL_Z })
                options.shader.removeMode(removed);
            options.shader.addMode(Mode::VERTEX);
            options.shader.addMode(Mode::SKIN);
            if (normals)
                options.shader.addMode(Mode::NORMAL);
            else
                options.shader.removeMode(Mode::NORMAL);
            options.nonStandardUvSets.clear();
            if (uvs)
            {
                options.shader.addMode(Mode::TEXCOORD);
                options.numUvSets = 1;
            }
            else
            {
                options.shader.removeMode(Mode::TEXCOORD);
                options.numUvSets = 0;
            }
            // The merger already reversed the winding of mirrored parts; the stock "Left" parts' clockwise front face
            // (animcontext.cpp, partBoneOptions) is for a mirrored transform the merged body does not have.
            options.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            return true;
        }

        vsg::ref_ptr<vsg::Node> buildNode(const Body::MergedBody& body, const BodyNif::RestPose& rest,
            const ESM::Race& race, bool female, const MWAnim::Context& context, std::string& error)
        {
            const std::vector<Body::MergedDraw> draws = Body::planMergedDraws(body);
            if (draws.empty())
            {
                error = "the body has no draws";
                return {};
            }
            MWBase::World* world = MWBase::Environment::get().getWorld();
            Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
            if (world == nullptr || resources == nullptr || resources->getVFS() == nullptr || !resources->builder)
            {
                error = "the engine is not ready";
                return {};
            }
            const std::vector<const ESM::BodyPart*>& parts
                = getBodyParts(race.mId, female, false, false, world->getStore().get<ESM::BodyPart>());

            // Bones: the inverse of each bone's rest matrix, the same rest pose the bake placed the parts with.
            std::vector<Anim::BoneData> bones;
            bones.reserve(body.mBones.size());
            constexpr double huge = std::numeric_limits<double>::max();
            vsg::dvec3 lower(huge, huge, huge);
            vsg::dvec3 upper(-huge, -huge, -huge);
            for (const auto& p : body.mPositions)
            {
                for (int k = 0; k < 3; ++k)
                {
                    lower[k] = std::min(lower[k], static_cast<double>(p[static_cast<std::size_t>(k)]));
                    upper[k] = std::max(upper[k], static_cast<double>(p[static_cast<std::size_t>(k)]));
                }
            }
            const vsg::dvec3 centre = (lower + upper) * 0.5;
            const double radius = vsg::length(upper - lower) * 0.5;
            for (const std::string& name : body.mBones)
            {
                const auto found = rest.find(Misc::StringUtils::lowerCase(name));
                Body::Affine34 inverse{};
                if (found == rest.end() || !Body::invertAffine(toAffine(found->second), inverse))
                {
                    error = "the bone '" + name + "' has no usable rest matrix";
                    return {};
                }
                const vsg::dvec3 boneCentre = applyAffine(inverse, centre);
                bones.push_back({ toMat4(inverse), vsg::dsphere(boneCentre, radius), name });
            }

            const bool normals = !body.mNormals.empty();
            const bool uvs = !body.mUvs.empty();
            std::map<std::pair<int, std::uint32_t>, Look> looks;
            auto root = vsg::Group::create();
            for (const Body::MergedDraw& draw : draws)
            {
                const auto lookKey = std::make_pair(draw.mSlot, draw.mMaterial);
                auto lookIt = looks.find(lookKey);
                if (lookIt == looks.end())
                {
                    if (draw.mSlot < 0 || static_cast<std::size_t>(draw.mSlot) >= parts.size()
                        || parts[static_cast<std::size_t>(draw.mSlot)] == nullptr)
                    {
                        error = "slot " + std::to_string(draw.mSlot) + " has no stock part";
                        return {};
                    }
                    std::optional<Look> look = findLook(draw.mSlot, *parts[static_cast<std::size_t>(draw.mSlot)],
                        body.mMaterials[draw.mMaterial], context, *resources->getVFS(), error);
                    if (!look)
                        return {};
                    lookIt = looks.emplace(lookKey, std::move(*look)).first;
                }
                Pipeline::Options options = lookIt->second.mOptions;
                if (!adaptOptions(options, normals, uvs))
                {
                    error = "the look of slot " + std::to_string(draw.mSlot) + " is not a plain mesh";
                    return {};
                }

                const auto count = static_cast<std::uint32_t>(draw.mVertices.size());
                auto positions = vsg::vec3Array::create(count);
                auto normalArray = normals ? vsg::vec3Array::create(count) : vsg::ref_ptr<vsg::vec3Array>{};
                auto uvArray = uvs ? vsg::vec2Array::create(count) : vsg::ref_ptr<vsg::vec2Array>{};
                auto boneIndices = vsg::vec4Array::create(count);
                auto boneWeights = vsg::vec4Array::create(count);
                // Only the bones this draw's vertices use, in the body's order: every bone of a skin costs a matrix
                // per frame and actor (all 28 per draw made 20 merged actors 3.5 ms slower than the stock parts).
                std::vector<int> localBone(bones.size(), -1);
                std::vector<Anim::BoneData> drawBones;
                for (const std::uint32_t v : draw.mVertices)
                {
                    for (std::size_t i = 0; i < Body::maxMergedInfluences; ++i)
                    {
                        if (body.mBoneWeights[v][i] > 0.f)
                            localBone[body.mBoneIndices[v][i]] = 0;
                    }
                }
                for (std::size_t b = 0; b < bones.size(); ++b)
                {
                    if (localBone[b] == 0)
                    {
                        localBone[b] = static_cast<int>(drawBones.size());
                        drawBones.push_back(bones[b]);
                    }
                }
                for (std::uint32_t k = 0; k < count; ++k)
                {
                    const std::uint32_t v = draw.mVertices[k];
                    const auto& p = body.mPositions[v];
                    (*positions)[k] = vsg::vec3(p[0], p[1], p[2]);
                    if (normals)
                    {
                        const auto& n = body.mNormals[v];
                        (*normalArray)[k] = vsg::vec3(n[0], n[1], n[2]);
                    }
                    if (uvs)
                    {
                        const auto& uv = body.mUvs[v];
                        (*uvArray)[k] = vsg::vec2(uv[0], uv[1]);
                    }
                    for (std::size_t i = 0; i < Body::maxMergedInfluences && i < 4; ++i)
                    {
                        const float weight = body.mBoneWeights[v][i];
                        const int local = weight > 0.f ? localBone[body.mBoneIndices[v][i]] : 0;
                        (*boneIndices)[k][static_cast<int>(i)] = static_cast<float>(local);
                        (*boneWeights)[k][static_cast<int>(i)] = weight;
                    }
                }

                // Every draw has its own skin (the stock skinned part's structure, nif.cpp handleSkin): the controller
                // fills its bone matrices, which the vertex shader reads at BONE_BINDING.
                auto skin = Anim::Skin::create();
                skin->bones = drawBones;
                skin->boneIndices = boneIndices;
                skin->boneWeights = boneWeights;
                auto boneMatrices = vsg::mat4Array::create(
                    static_cast<std::uint32_t>(drawBones.size() * (Anim::Skin::keepPreviousMatrices ? 2 : 1)));
                skin->attachTo(*boneMatrices);
                vsg::Descriptors descriptors = lookIt->second.mDescriptors;
                descriptors.push_back(vsg::DescriptorBuffer::create(
                    boneMatrices, Pipeline::Descriptors::BONE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

                auto bindPipeline = resources->builder->graphics->getOrCreate(options);
                vsg::ref_ptr<vsg::PipelineLayout> layout = bindPipeline->pipeline->layout;
                auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
                auto bindDescriptors = vsg::BindDescriptorSet::create(
                    VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);

                // The attribute order of the default shader (nif.cpp handleGeometry): vertex, normal, bone indices and
                // weights, texture coordinates.
                vsg::DataList arrays{ positions };
                if (normals)
                    arrays.push_back(normalArray);
                arrays.push_back(boneIndices);
                arrays.push_back(boneWeights);
                if (uvs)
                    arrays.push_back(uvArray);
                auto geometry = vsg::VertexIndexDraw::create();
                geometry->assignArrays(arrays);
                if (count <= std::numeric_limits<std::uint16_t>::max())
                {
                    auto indices = vsg::ushortArray::create(static_cast<std::uint32_t>(draw.mIndices.size()));
                    for (std::size_t i = 0; i < draw.mIndices.size(); ++i)
                        (*indices)[i] = static_cast<std::uint16_t>(draw.mIndices[i]);
                    geometry->assignIndices(indices);
                }
                else
                {
                    auto indices = vsg::uintArray::create(static_cast<std::uint32_t>(draw.mIndices.size()));
                    std::copy(draw.mIndices.begin(), draw.mIndices.end(), indices->begin());
                    geometry->assignIndices(indices);
                }
                geometry->indexCount = static_cast<std::uint32_t>(draw.mIndices.size());
                geometry->instanceCount = 1;

                auto stateGroup = vsg::StateGroup::create();
                stateGroup->stateCommands = { bindPipeline, bindDescriptors };
                stateGroup->children = { geometry };
                root->addChild(vsg::CullNode::create(vsg::dsphere(centre, radius), stateGroup));
            }

            Anim::Contents contents;
            contents.add(Anim::Contents::Skins | Anim::Contents::Controllers | Anim::Contents::DynamicBounds);
            auto meta = vsg::ref_ptr{ new Anim::Meta(contents) };
            meta->attachTo(*root);
            return root;
        }

        TemplateResult buildTemplate(const ESM::Race& race, bool female, const MWAnim::Context& context)
        {
            TemplateResult result;
            Body::MergedBodyFacts facts;
            facts.mSettingOn = true;
            facts.mThirdPerson = true;
            facts.mWerewolf = false;
            facts.mDefaultSkeleton = true; // decided per actor
            facts.mSlotsCovered = false; // decided per actor
            std::string detail;
            std::optional<Body::MergedBody> body;
            loadCache();
            facts.mCacheLoaded = sCache.mFiles.has_value() && sCache.mManifest.has_value();
            if (!facts.mCacheLoaded)
                detail = sCache.mError;
            MWBase::World* world = MWBase::Environment::get().getWorld();
            Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
            if (facts.mCacheLoaded && world != nullptr && resources != nullptr && resources->getVFS() != nullptr)
            {
                const OMW::StockBodyFacts stock
                    = OMW::computeStockBodyFacts(race, female, world->getStore(), *resources->getVFS());
                const auto file = sCache.mFiles->mFiles.find(stock.mEntryName);
                const bool listed = std::any_of(sCache.mManifest->mEntries.begin(), sCache.mManifest->mEntries.end(),
                    [&](const Body::CacheEntry& entry) { return entry.mName == stock.mEntryName; });
                facts.mEntryPresent = listed && file != sCache.mFiles->mFiles.end();
                facts.mKeyComputed = stock.mError.empty();
                if (!facts.mKeyComputed)
                    detail = stock.mError;
                facts.mKeyCurrent
                    = facts.mKeyComputed && Body::isEntryCurrent(*sCache.mManifest, stock.mEntryName, stock.mKey);
                if (facts.mEntryPresent && facts.mKeyCurrent)
                {
                    Body::BodyMeshReadResult read = Body::readBodyMesh(file->second.data(), file->second.size());
                    facts.mPayloadRead = read.mBody.has_value();
                    if (facts.mPayloadRead)
                        body = std::move(read.mBody);
                    else
                        detail = read.mError;
                }
                if (facts.mPayloadRead)
                {
                    try
                    {
                        result.mNode = buildNode(*body, stock.mRest, race, female, context, detail);
                    }
                    catch (const std::exception& e)
                    {
                        result.mNode = {};
                        detail = e.what();
                    }
                    facts.mBuilt = result.mNode != nullptr;
                }
            }
            result.mReason = Body::decideMergedBody(facts);
            if (result.mReason == Body::MergedBodyReason::Merged)
                result.mSlots = Body::mergedSlotsOf(*body);
            else
                result.mNode = {};
            if (!detail.empty())
            {
                Log(Debug::Verbose) << "Merged bodies: " << race.mId.getRefIdString() << " " << sexName(female) << ": "
                                    << detail;
            }
            return result;
        }
    }

    void setUserDataFolder(const std::filesystem::path& folder)
    {
        std::lock_guard lock(sMutex);
        sUserDataFolder = folder;
    }

    TemplateResult getTemplate(const ESM::Race& race, bool female, const MWAnim::Context& context)
    {
        std::lock_guard lock(sMutex);
        const std::string key = templateKey(race, female);
        auto found = sTemplates.find(key);
        if (found == sTemplates.end())
            found = sTemplates.emplace(key, buildTemplate(race, female, context)).first;
        return found->second;
    }

    void report(const ESM::Race& race, bool female, Body::MergedBodyReason reason)
    {
        if (reason == Body::MergedBodyReason::Merged || reason == Body::MergedBodyReason::SettingOff
            || reason == Body::MergedBodyReason::EquipmentCovers)
            return;
        std::lock_guard lock(sMutex);
        const std::string key = templateKey(race, female) + "|" + std::string(Body::describe(reason));
        if (!sReported.insert(key).second)
            return;
        Log(Debug::Warning) << "Merged bodies: " << race.mId.getRefIdString() << " " << sexName(female)
                            << " draws the stock parts: " << Body::describe(reason);
        const bool cacheProblem = reason == Body::MergedBodyReason::NoCache
            || reason == Body::MergedBodyReason::StaleKey || reason == Body::MergedBodyReason::PayloadRefused;
        if (cacheProblem && !sMessageShown)
        {
            if (MWBase::WindowManager* windows = MWBase::Environment::get().getWindowManager())
            {
                sMessageShown = true;
                windows->messageBox("Merged bodies: " + std::string(Body::describe(reason)));
            }
        }
    }
}
