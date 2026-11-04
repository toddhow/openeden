#include "nif.hpp"

#include <cstring>
#include <iostream>
#include <mutex>
#include <set>
#include <stack>

#include <vsg/nodes/Switch.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
// #include <vsg/nodes/LOD.h>
#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/commands/Commands.h>
#include <vsg/commands/Draw.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/billboard.hpp>
#include <components/animation/bones.hpp>
#include <components/animation/color.hpp>
#include <components/animation/cullparticles.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/morph.hpp>
#include <components/animation/roll.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/stateswitch.hpp>
#include <components/animation/switch.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/texmat.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/effect.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/nif/particle.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/particle.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/materialoverrides.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/distancelod.hpp>

#include "../tangents.hpp"
#include "internaltexture.hpp"
#include <components/vsgutil/group.hpp>
#include <components/vsgutil/id.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/shareimage.hpp>

#include "anim.hpp"
#include "channel.hpp"
#include "particle.hpp"

namespace
{
    template <class Array, class Source>
    vsg::ref_ptr<Array> copyArray(const Source& vec)
    {
        vsg::ref_ptr<Array> array = Array::create(vec.size());
        std::memcpy(static_cast<void*>(array->data()), &vec.front(), vec.size() * sizeof(typename Source::value_type));
        return array;
    }
}

namespace Pipeline::Descriptors
{
#include <files/shaders/comp/particle/bindings.glsl>
}
namespace
{
    static const vsg::ref_ptr<vsg::ArrayState> sNullArrayState = vsg::NullArrayState::create();

    bool isTypeGeometry(int type)
    {
        switch (type)
        {
            case Nif::RC_NiTriShape:
            case Nif::RC_NiTriStrips:
            case Nif::RC_NiLines:
            case Nif::RC_BSLODTriShape:
                return true;
        }
        return false;
    }

    VkBlendFactor convertBlendFactor(int mode)
    {
        switch (mode)
        {
            case 0:
                return VK_BLEND_FACTOR_ONE;
            case 1:
                return VK_BLEND_FACTOR_ZERO;
            case 2:
                return VK_BLEND_FACTOR_SRC_COLOR;
            case 3:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case 4:
                return VK_BLEND_FACTOR_DST_COLOR;
            case 5:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case 6:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case 7:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case 8:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case 9:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case 10:
                return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            default:
                return VK_BLEND_FACTOR_SRC_ALPHA;
        }
    }

    int convertTextureSlot(size_t nifSlot, Pipeline::Mode& mode)
    {
        switch (nifSlot)
        {
            case Nif::NiTexturingProperty::DarkTexture:
                mode = Pipeline::Mode::DARK_MAP;
                return Pipeline::Descriptors::DARK_UNIT;
            case Nif::NiTexturingProperty::BaseTexture:
                mode = Pipeline::Mode::DIFFUSE_MAP;
                return Pipeline::Descriptors::DIFFUSE_UNIT;
            case Nif::NiTexturingProperty::DetailTexture:
                mode = Pipeline::Mode::DETAIL_MAP;
                return Pipeline::Descriptors::DETAIL_UNIT;
            case Nif::NiTexturingProperty::DecalTexture:
                mode = Pipeline::Mode::DECAL_MAP;
                return Pipeline::Descriptors::DECAL_UNIT;
            case Nif::NiTexturingProperty::GlowTexture:
                mode = Pipeline::Mode::GLOW_MAP;
                return Pipeline::Descriptors::GLOW_UNIT;
            case Nif::NiTexturingProperty::BumpTexture:
                mode = Pipeline::Mode::BUMP_MAP;
                return Pipeline::Descriptors::BUMP_UNIT;
            default:
                return -1;
        }
    }

    template <class List, class Func>
    void handleList(const List& list, Func f)
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (!list[i].empty())
                f(*list[i].getPtr());
    }

    // Enough for the few keys a particle colour curve has; the sampler interpolates between steps.
    constexpr int sColorCurveSteps = 64;

    bool canOptimize(const std::string& f)
    {
        std::string filename = Misc::StringUtils::lowerCase(f);
        size_t slashpos = filename.find_last_of("\\/");
        if (slashpos != std::string::npos && slashpos + 1 < filename.size())
        {
            std::string basename = filename.substr(slashpos + 1);
            // openeden-vsg: sheathing finds nodes by name in skeleton additions and _sh meshes (Npc::addCustomBones).
            if (filename.starts_with("animations/") || basename.ends_with("_sh.nif"))
                return false;
            // xmesh.nif can not be optimized because there are keyframes added in post
            if (!basename.empty() && basename[0] == 'x')
                return false;
            // NPC skeleton files can not be optimized because of keyframes added in post
            // (most of them are usually named like 'xbase_anim.nif' anyway, but not all of them :( )
            if (basename.compare(0, 9, "base_anim") == 0 || basename.compare(0, 4, "skin") == 0)
                return false;
            // For spell VFX, DummyXX nodes must remain intact. Not adding those to reservedNames to avoid being overly
            // cautious - instead, decide on filename
            if (basename.find("vfx_pattern") != std::string::npos)
                return false;
        }
        return true;
    }
}

namespace vsgAdapters
{
    class nifImpl
    {
        const vsg::Options& mOptions;
        const vsg::ref_ptr<const vsg::Options>& mImageOptions;
        bool mCanOptimize = true;
        bool mShowMarkers = false;
        int mComputeBin;
        int mDepthSortedBin;
        uint32_t mPhaseGroups = 0;
        std::string mFilename;
        std::unique_ptr<Nif::NIFFile> mNifFile;
        std::unique_ptr<Nif::FileView> mNif;
        std::string mSkinFilter;
        std::string mSkinFilter2;
        const Pipeline::Builder& mBuilder;
        const Pipeline::Override* mOverride;

    public:
        // openeden-vsg: the reader's normal and specular map settings (task 0012, V4h); set before load().
        const AutoMapSettings* mAutoMaps = nullptr;
        const Render::Pbr::OverrideTable* mMaterialTable = nullptr;
        const std::function<bool(std::string_view)>* mTextureExists = nullptr;
        // openeden-vsg: `[Shaders] soft particles` (task 0012, V4k).
        bool mSoftParticles = false;
        const std::string* mOccludedParticleModel = nullptr;
        const vsg::ref_ptr<vsg::Descriptor>* mOccludedParticleMap = nullptr;

        nifImpl(std::istream& stream, const vsg::Options& options, const vsg::ref_ptr<const vsg::Options>& imageOptions,
            bool showMarkers, int computeBin, int depthSortedBin, const Pipeline::Builder& builder)
            : mOptions(options)
            , mImageOptions(imageOptions)
            , mShowMarkers(showMarkers)
            , mComputeBin(computeBin)
            , mDepthSortedBin(depthSortedBin)
            , mBuilder(builder)
        {
            options.getValue("filename", mFilename);
            options.getValue("skinfilter", mSkinFilter);
            mOverride = Pipeline::Override::get(options);
            if (!mSkinFilter.empty())
                mSkinFilter2 = std::string("tri ") + mSkinFilter;
            mCanOptimize = canOptimize(mFilename);
            mNifFile.reset(new Nif::NIFFile({}));
            Nif::Reader reader(*mNifFile);
            // vsgopenmw-nif-istream
            reader.parse(Files::IStreamPtr(new std::istream(stream.rdbuf()))); // reader.parse(stream);
                                                                               // //vsgopenmw-fixme
            mNif.reset(new Nif::FileView(*mNifFile));
        }

        void warn(const std::string& msg) { std::cerr << msg << " (" << mFilename << ")" << std::endl; }

        template <class T>
        void share(vsg::ref_ptr<T>& obj)
        {
            vsgUtil::share_if(mOptions.sharedObjects, obj);
        }

        Anim::Contents mContents;

        using MaterialValue = vsg::Value<Pipeline::Data::Material>;
        struct NodeOptions : public Pipeline::Options
        {
            // Task 0045: for the height file's depth. The product of the scales of the nodes down to here, and whether
            // a NiUVController above animates the texture coordinates.
            float nodeScale = 1.f;
            bool animatedTextureTransform = false;
            // Task 0072, stage h: a NiVertexColorProperty applies at or above this node (a particle system without one
            // takes Pipeline::particleOwnColorMode).
            bool hasVertexColorProperty = false;
            vsg::ref_ptr<vsg::StateSwitch> stateSwitch;
            vsg::Descriptors descriptors;
            std::vector<vsg::Descriptors> textureSets;
            vsg::ref_ptr<MaterialValue> material;
            vsg::ref_ptr<Anim::Color> materialController;
            float alphaTestCutoff = 1.f;
            // The base texture's resolved path, kept only to look up material overrides (task 0027).
            std::string diffuseTexture;
            bool depthSorted = false;
            uint32_t animFlags = 0;
            uint32_t phaseGroup = 0;
            bool filterMatched = false;
        };
        std::stack<NodeOptions> mNodeStack;
        struct ScopedPushPop
        {
            ScopedPushPop(std::stack<NodeOptions>& stack)
                : mStack(stack)
            {
                if (mStack.empty())
                    mStack.emplace();
                else
                    mStack.emplace(mStack.top());
            }
            ~ScopedPushPop() { mStack.pop(); }
            std::stack<NodeOptions>& mStack;
        };
        NodeOptions& getNodeOptions() { return mNodeStack.top(); }

        vsg::ref_ptr<vsg::Object> load()
        {
            vsg::Group::Children nodes;
            for (size_t i = 0; i < mNif->numRoots(); ++i)
            {
                if (const Nif::NiAVObject* nifNode = dynamic_cast<const Nif::NiAVObject*>(mNif->getRoot(i)))
                {
                    if (auto node = handleNode(*nifNode))
                        nodes.emplace_back(node);
                }
            }
            vsgUtil::removeGroup(nodes);

            vsg::ref_ptr<vsg::Node> ret;
            if (!mContents.contains(Anim::Contents::TransformControllers | Anim::Contents::DynamicBounds
                    | Anim::Contents::Placeholders))
            {
                ret = vsgUtil::createCullNode(nodes);
                vsgUtil::addLeafCullNodes(ret, 2);
            }
            else
            {
                ret = vsgUtil::getNode(nodes);
                vsgUtil::addLeafCullNodes(ret);
            }
            if (!mContents.empty())
            {
                auto meta = vsg::ref_ptr{ new Anim::Meta(mContents) };
                meta->attachTo(*ret);
            }
            return ret;
        }

        bool canSkipGeometry(bool isMarker, const Nif::NiAVObject& nifNode) const
        {
            static const std::string markerName = "tri editormarker";
            static const std::string shadowName = "shadow";
            static const std::string shadowName2 = "tri shadow";
            isMarker = isMarker && Misc::StringUtils::ciCompareLen(nifNode.mName, markerName, markerName.size()) == 0
                && !mShowMarkers;
            return isMarker || Misc::StringUtils::ciCompareLen(nifNode.mName, shadowName, shadowName.size()) == 0
                || Misc::StringUtils::ciCompareLen(nifNode.mName, shadowName2, shadowName2.size()) == 0;
        }

        bool canOptimizeTransform(const Nif::NiAVObject& nifNode)
        {
            if (!nifNode.mTransform.isIdentity() || !this->mCanOptimize || hasTransformController(nifNode)
                || nifNode.useFlags & Nif::NiAVObject::Bone)
                return false;
            if (Misc::StringUtils::ciEqual(nifNode.mName, "BoneOffset"))
            {
                addAnimContents(Anim::Contents::Placeholders);
                return false;
            }
            if (Misc::StringUtils::ciEqual(nifNode.mName, "AttachLight"))
            {
                addAnimContents(Anim::Contents::Placeholders);
                return false;
            }
            return true;
        }

        bool canOptimizeBillboard(const Nif::NiAVObject& nifNode)
        {
            if (auto niNode = dynamic_cast<const Nif::NiNode*>(&nifNode))
            {
                const auto& children = niNode->mChildren;
                for (size_t i = 0; i < children.size(); ++i)
                {
                    if (children[i].empty())
                        continue;
                    auto& child = *children[i].getPtr();
                    if (!canOptimizeTransform(child) || !canOptimizeBillboard(child))
                        return false;
                }
            }
            return true;
        }

        template <class ArrayType, class SourceType>
        vsg::ref_ptr<ArrayType> copyModeArray(const SourceType& vec, Pipeline::Mode mode)
        {
            getNodeOptions().shader.addMode(mode);
            return copyArray<ArrayType>(vec);
        }

        template <class Mode>
        void addModeDescriptor(vsg::ref_ptr<vsg::Descriptor> descriptor, Mode mode)
        {
            auto& nodeOptions = getNodeOptions();
            nodeOptions.shader.addMode(mode);
            nodeOptions.descriptors.emplace_back(descriptor);
        }

        void handleSkin(const Nif::NiSkinInstance& skin, vsg::ref_ptr<vsg::vec4Array> boneIndices,
            vsg::ref_ptr<vsg::vec4Array> weights)
        {
            const Nif::NiSkinData& data = *skin.mData.getPtr();
            size_t numVertices = boneIndices->size();
            constexpr size_t maxInfluenceCount = 4;
            using IndexWeightList = std::vector<std::pair<uint32_t, float>>;
            std::vector<IndexWeightList> vertexWeights(numVertices);
            auto ctrl = Anim::Skin::create();
            auto& bones = ctrl->bones;
            bones.reserve(skin.mBones.size());
            if (!data.mTransform.isIdentity())
            {
                // openmw-4437-niskininstance-transformation
                Anim::Transform skinTransform;
                convertTrafo(skinTransform, data.mTransform);
                ctrl->transform = skinTransform.t_transform(vsg::mat4());
            }
            for (unsigned char i = 0; i < skin.mBones.size(); ++i)
            {
                const auto& boneData = data.mBones[i];
                size_t boneIndex = bones.size();
                Anim::Transform t;
                convertTrafo(t, boneData.mTransform);
                vsg::dsphere bounds(boneData.mBoundSphere.center, boneData.mBoundSphere.radius);
                bones.push_back({ t.t_transform(vsg::mat4()), bounds, skin.mBones[i].getPtr()->mName });
                for (const auto& [vertex, weight] : boneData.mWeights)
                {
                    if (vertex < numVertices)
                        vertexWeights[vertex].emplace_back(boneIndex, weight);
                }
            }
            for (size_t vertex = 0; vertex < numVertices; ++vertex)
            {
                auto& vertexWeight = vertexWeights[vertex];
                std::sort(
                    vertexWeight.begin(), vertexWeight.end(),
                    [](auto& left, auto& right) -> auto{ return left.second > right.second; });
                vsg::vec4& indices = (*boneIndices)[vertex];
                vsg::vec4& weight = (*weights)[vertex];
                size_t influenceCount = std::min(maxInfluenceCount, vertexWeight.size());
                for (size_t i = 0; i < influenceCount; ++i)
                {
                    indices[i] = vertexWeight[i].first;
                    weight[i] = vertexWeight[i].second;
                }
            }
            ctrl->boneIndices = boneIndices;
            ctrl->boneWeights = weights;

            // With the G-buffer, the previous frame's matrices follow the current ones (Anim::Skin).
            auto boneMatrices = vsg::mat4Array::create(bones.size() * (Anim::Skin::keepPreviousMatrices ? 2 : 1));
            ctrl->attachTo(*boneMatrices);
            auto descriptor = vsg::DescriptorBuffer::create(
                boneMatrices, Pipeline::Descriptors::BONE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            addModeDescriptor(descriptor, Pipeline::Mode::SKIN);
            addAnimContents(Anim::Contents::Skins | Anim::Contents::Controllers | Anim::Contents::DynamicBounds);
        }

        // openeden-vsg: tangents for a normal map (task 0012, V4h), from the triangles or each strip and the normal
        // map's UV set, as OpenMW's ShaderVisitor generated them; null when the geometry has no triangles, normals or
        // that UV set.
        vsg::ref_ptr<vsg::vec4Array> createTangents(
            const Nif::NiGeometry& niGeometry, const Nif::NiGeometryData& data, const NodeOptions& nodeOptions)
        {
            std::vector<std::vector<uint16_t>> indexLists;
            bool strip = false;
            if ((niGeometry.mRecordType == Nif::RC_NiTriShape || niGeometry.mRecordType == Nif::RC_BSLODTriShape)
                && data.mRecordType == Nif::RC_NiTriShapeData)
                indexLists.push_back(static_cast<const Nif::NiTriShapeData&>(data).mTriangles);
            else if (niGeometry.mRecordType == Nif::RC_NiTriStrips && data.mRecordType == Nif::RC_NiTriStripsData)
            {
                indexLists = static_cast<const Nif::NiTriStripsData&>(data).mStrips;
                strip = true;
            }
            // The normal map uses the diffuse texture's UV set.
            auto uvSetIt = nodeOptions.nonStandardUvSets.find(Pipeline::Descriptors::DIFFUSE_UNIT);
            const size_t uvSet = uvSetIt != nodeOptions.nonStandardUvSets.end() ? uvSetIt->second : 0;
            if (indexLists.empty() || data.mNormals.empty() || uvSet >= data.mUVList.size())
                return {};

            std::vector<vsg::vec3> vertices, normals;
            std::vector<vsg::vec2> texCoords;
            vertices.reserve(data.mVertices.size());
            for (const auto& v : data.mVertices)
                vertices.push_back(v);
            normals.reserve(data.mNormals.size());
            for (const auto& n : data.mNormals)
                normals.push_back(n);
            texCoords.reserve(data.mUVList[uvSet].size());
            for (const auto& uv : data.mUVList[uvSet])
                texCoords.push_back(uv);
            const std::vector<vsg::vec4> tangents = generateTangents(vertices, normals, texCoords, indexLists, strip);
            if (tangents.empty())
                return {};
            auto array = vsg::vec4Array::create(static_cast<uint32_t>(tangents.size()));
            std::copy(tangents.begin(), tangents.end(), array->begin());
            return array;
        }

        // Task 0045: the material of the geometry being built reads this (createMaterialData); 0 outside
        // handleGeometry.
        float mMeshWorldPerUv = 0.f;

        // Game units per unit of the diffuse texture's coordinates, over the whole mesh.
        float meshWorldPerUvOf(
            const Nif::NiGeometry& niGeometry, const Nif::NiGeometryData& data, const NodeOptions& nodeOptions)
        {
            auto uvSetIt = nodeOptions.nonStandardUvSets.find(Pipeline::Descriptors::DIFFUSE_UNIT);
            const size_t uvSet = uvSetIt != nodeOptions.nonStandardUvSets.end() ? uvSetIt->second : 0;
            // An animated texture transform changes the density as it plays: only the derivatives can follow it.
            if (uvSet >= data.mUVList.size() || nodeOptions.animatedTextureTransform)
                return 0.f;
            // In the units of the object: the mesh's vertices are scaled by the nodes above it.
            if ((niGeometry.mRecordType == Nif::RC_NiTriShape || niGeometry.mRecordType == Nif::RC_BSLODTriShape)
                && data.mRecordType == Nif::RC_NiTriShapeData)
            {
                const std::vector<std::vector<uint16_t>> lists{
                    static_cast<const Nif::NiTriShapeData&>(data).mTriangles
                };
                return nodeOptions.nodeScale
                    * Render::Parallax::meshWorldPerUv(data.mVertices, data.mUVList[uvSet], lists, false);
            }
            if (niGeometry.mRecordType == Nif::RC_NiTriStrips && data.mRecordType == Nif::RC_NiTriStripsData)
                return nodeOptions.nodeScale
                    * Render::Parallax::meshWorldPerUv(data.mVertices, data.mUVList[uvSet],
                        static_cast<const Nif::NiTriStripsData&>(data).mStrips, true);
            return 0.f;
        }

        vsg::ref_ptr<vsg::Node> handleGeometry(const Nif::NiGeometry& niGeometry)
        {
            if (niGeometry.mData.empty())
                return {};

            const Nif::NiGeometryData& data = *niGeometry.mData.getPtr();
            handleGeometryControllers(niGeometry.mController, data.mVertices.size());

            vsg::DataList dataList;
            dataList.reserve(!data.mVertices.empty() + !data.mNormals.empty() + !data.mColors.empty()
                + data.mUVList.size() + (!niGeometry.mSkin.empty()) * 2);

            static_assert(static_cast<int>(Pipeline::Mode::VERTEX) == 0);
            if (!data.mVertices.empty())
                dataList.emplace_back(copyModeArray<vsg::vec3Array>(data.mVertices, Pipeline::Mode::VERTEX));

            static_assert(static_cast<int>(Pipeline::Mode::NORMAL) == 1);
            if (!data.mNormals.empty())
            {
                dataList.emplace_back(copyModeArray<vsg::vec3Array>(data.mNormals, Pipeline::Mode::NORMAL));
                vsgUtil::setID(*dataList.back(), static_cast<int>(Pipeline::Mode::NORMAL));
            }

            static_assert(static_cast<int>(Pipeline::Mode::COLOR) == 2);
            if (!data.mColors.empty())
                dataList.emplace_back(copyModeArray<vsg::vec4Array>(data.mColors, Pipeline::Mode::COLOR));

            static_assert(static_cast<int>(Pipeline::Mode::SKIN) == 3);
            if (!niGeometry.mSkin.empty())
            {
                auto weights = vsg::vec4Array::create(data.mVertices.size());
                auto boneIndices = vsg::vec4Array::create(data.mVertices.size());
                handleSkin(*niGeometry.mSkin.getPtr(), boneIndices, weights);
                dataList.emplace_back(boneIndices);
                dataList.emplace_back(weights);
            }
            // openeden-vsg: the tangent comes after the bone weights (Pipeline::Graphics, lib/attributes.glsl).
            const size_t tangentPosition = dataList.size();

            static_assert(static_cast<int>(Pipeline::Mode::TEXCOORD) == 4);
            for (size_t uvSet = 0; uvSet < data.mUVList.size(); ++uvSet)
                dataList.emplace_back(copyModeArray<vsg::vec2Array>(data.mUVList[uvSet], Pipeline::Mode::TEXCOORD));
            auto& nodeOptions = getNodeOptions();
            nodeOptions.numUvSets = data.mUVList.size();
            if (nodeOptions.shader.hasMode(Pipeline::Mode::NORMAL_MAP))
            {
                if (auto tangents = createTangents(niGeometry, data, nodeOptions))
                    dataList.insert(dataList.begin() + tangentPosition, tangents);
                else
                {
                    // Without tangents the normal map can't be used; a specular map still can.
                    nodeOptions.shader.removeMode(Pipeline::Mode::NORMAL_MAP);
                    nodeOptions.shader.removeMode(Pipeline::Mode::PARALLAX);
                    nodeOptions.shader.removeMode(Pipeline::Mode::RECONSTRUCT_NORMAL_Z);
                }
            }

            auto& topology = nodeOptions.primitiveTopology;
            auto geom = vsg::VertexIndexDraw::create();
            geom->assignArrays(dataList);
            vsg::ref_ptr<vsg::ushortArray> indices;
            if ((niGeometry.mRecordType == Nif::RC_NiTriShape || niGeometry.mRecordType == Nif::RC_BSLODTriShape)
                && data.mRecordType == Nif::RC_NiTriShapeData)
            {
                auto triangles = static_cast<const Nif::NiTriShapeData&>(data).mTriangles;
                if (triangles.empty())
                    return {};
                indices = copyArray<vsg::ushortArray>(triangles);
            }
            else if (niGeometry.mRecordType == Nif::RC_NiTriStrips && data.mRecordType == Nif::RC_NiTriStripsData)
            {
                auto triStrips = static_cast<const Nif::NiTriStripsData&>(data);
                std::vector<unsigned short> mergedIndices;
                for (const auto& strip : triStrips.mStrips)
                {
                    if (strip.size() < 3)
                        continue;
                    mergedIndices.insert(mergedIndices.end(), strip.begin(), strip.end());
                }
                indices = copyArray<vsg::ushortArray>(mergedIndices);
                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            }
            else if (niGeometry.mRecordType == Nif::RC_NiLines && data.mRecordType == Nif::RC_NiLinesData)
            {
                const auto& line = static_cast<const Nif::NiLinesData&>(data).mLines;
                if (line.empty())
                    return {};
                indices = copyArray<vsg::ushortArray>(line);
                topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            }
            else
                return {};

            geom->instanceCount = 1;
            geom->indexCount = indices->size();
            geom->assignIndices(indices);

            if (nodeOptions.shader.hasMode(Pipeline::Mode::HEIGHT_MAP))
                mMeshWorldPerUv = meshWorldPerUvOf(niGeometry, data, nodeOptions);
            auto sg = createStateGroup(geom);
            mMeshWorldPerUv = 0.f;
            if (nodeOptions.depthSorted)
            {
                // Alpha sorting uses the calculated bounding sphere centre.
                return vsg::DepthSorted::create(mDepthSortedBin, vsgUtil::getBounds(sg), sg);
            }
            return sg;
        }

        vsg::ref_ptr<vsg::Node> handleParticleSystemController(
            const Nif::NiParticles& particles, const Nif::NiParticleSystemController& partctrl)
        {
            if (particles.mData.empty() || particles.mData->mRecordType != Nif::RC_NiParticlesData)
                return {};
            auto particledata = static_cast<const Nif::NiParticlesData&>(*particles.mData.getPtr());
            size_t maxParticles = particledata.mNumParticles;
            if (maxParticles == 0)
                return {};

            auto sw = vsg::Switch::create();
            auto particleArray = vsg::Array<Pipeline::Data::Particle>::create(maxParticles);
            // particleArray->properties.dataVariance = DYNAMIC_DATA_COMPUTE;
            // particleArray->setObject(Anim::Controller::sAttachKey, vsg::ref_ptr<vsg::Object>());
            particleArray->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;

            handleInitialParticles(*particleArray, particledata, partctrl);

            auto particlesDescriptor = vsg::DescriptorBuffer::create(
                particleArray, Pipeline::Descriptors::PARTICLE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            addModeDescriptor(particlesDescriptor, Pipeline::Mode::PARTICLE);
            getNodeOptions().colorMode
                = Pipeline::colorModeOfParticles(getNodeOptions().hasVertexColorProperty, getNodeOptions().colorMode);
            // openeden-vsg: particles have no tangents or per-pixel lighting (task 0012, V4h).
            removeAutoMapModes(getNodeOptions().shader);
            if (mSoftParticles)
                getNodeOptions().shader.addMode(Pipeline::Mode::SOFT_PARTICLE);
            if (mOccludedParticleMap && mOccludedParticleMap->valid()
                && VFS::Path::Normalized(mFilename).value() == *mOccludedParticleModel)
                addModeDescriptor(*mOccludedParticleMap, Pipeline::Mode::PARTICLE_OCCLUSION);

            auto maxLifetime = ParticleSystem::calculateMaxLifetime(partctrl);
            Pipeline::Data::SimulateArgs args{};
            args.emit = handleEmitterData(partctrl);
            vsg::ref_ptr<vsg::Data> colorCurve;
            auto affectors = partctrl.mModifier;
            Pipeline::ParticleModeFlags modes{};
            for (; !affectors.empty(); affectors = affectors->mNext)
            {
                if (affectors->mRecordType == Nif::RC_NiParticleGrowFade)
                {
                    const auto& gf = static_cast<const Nif::NiParticleGrowFade&>(*affectors.getPtr());
                    args.size = { gf.mGrowTime, gf.mFadeTime, partctrl.mInitialSize, 0 };
                    modes |= Pipeline::ParticleMode_Size;
                }
                else if (affectors->mRecordType == Nif::RC_NiGravity)
                {
                    const auto& gr = static_cast<const Nif::NiGravity&>(*affectors.getPtr());
                    args.gravity = { .positionDecay = { gr.mPosition, -gr.mDecay },
                        .directionForce = { vsg::normalize(gr.mDirection), gr.mForce * 1.6f } };
                    if (gr.mType == Nif::ForceType::Wind)
                        modes |= Pipeline::ParticleMode_GravityPlane;
                    else
                        modes |= Pipeline::ParticleMode_GravityPoint;
                }
                else if (affectors->mRecordType == Nif::RC_NiParticleColorModifier)
                {
                    const auto& cl = static_cast<const Nif::NiParticleColorModifier&>(*affectors.getPtr());
                    if (!cl.mData.empty())
                    {
                        colorCurve = createColorCurve(*cl.mData.getPtr(), sColorCurveSteps);
                        modes |= Pipeline::ParticleMode_Color;
                    }
                }
                else if (affectors->mRecordType == Nif::RC_NiParticleRotation)
                    ; // unused
                else
                    warn("Unhandled particle modifier " + affectors->mRecordName);
            }

            auto colliders = partctrl.mCollider;
            for (; !colliders.empty(); colliders = colliders->mNext)
            {
                if (colliders->mRecordType == Nif::RC_NiPlanarCollider)
                {
                    const auto& cl = static_cast<const Nif::NiPlanarCollider&>(*colliders.getPtr());
                    args.collide = { .positionBounce = { cl.mPosition, cl.mBounceFactor },
                        .xVectorRadius = { cl.mXVector, cl.mExtents.x / 2 },
                        .yVectorRadius = { cl.mYVector, cl.mExtents.y / 2 },
                        .plane = { cl.mPlaneNormal, cl.mPlaneDistance } };
                    modes |= Pipeline::ParticleMode_CollidePlane;
                }
                else if (colliders->mRecordType == Nif::RC_NiSphericalCollider)
                {
                    ; // const auto& cl = static_cast<const Nif::NiSphericalCollider&>(*colliders.getPtr());
                }
                else
                    warn("Unhandled particle collider " + colliders->mRecordName);
            }

            auto frameArgsData = vsg::Value<Pipeline::Data::FrameArgs>::create();
            auto frameArgsDescriptor = vsg::DescriptorBuffer::create(
                frameArgsData, Pipeline::Descriptors::FRAME_ARGS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            auto argsValue = vsg::Value<Pipeline::Data::SimulateArgs>::create(args);
            share(argsValue);
            auto argsDescriptor = vsg::DescriptorBuffer::create(
                argsValue, Pipeline::Descriptors::SIMULATE_ARGS_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            share(argsDescriptor);
            vsg::Descriptors descriptors{ argsDescriptor, frameArgsDescriptor, particlesDescriptor };
            // if (colorCurve)
            {
                if (!colorCurve)
                {
                    static const auto dummyColorCurve
                        = vsg::vec4Array::create(1, vsg::Data::Properties(VK_FORMAT_R32G32B32A32_SFLOAT));
                    colorCurve = dummyColorCurve;
                }
                auto sampler = mBuilder.createSampler();
                sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                share(sampler);
                descriptors.emplace_back(
                    vsg::DescriptorImage::create(sampler, colorCurve, Pipeline::Descriptors::COLOR_CURVE_BINDING));
            }

            auto& nodeOptions = getNodeOptions();
            nodeOptions.numUvSets = 1;
            nodeOptions.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            nodeOptions.cullMode = VK_CULL_MODE_NONE;
            bool worldSpace = !(nodeOptions.animFlags & Nif::NiNode::ParticleFlag_LocalSpace);
            if (worldSpace)
                modes |= Pipeline::ParticleMode_WorldSpace;

            auto bindComputePipeline = mBuilder.particle->getOrCreate(modes);
            auto computeBds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,
                bindComputePipeline->pipeline->layout, Pipeline::TEXTURE_SET, descriptors);
            auto computeGroup = vsg::Commands::create(); // vsg::StateGroup::create();
            computeGroup->children = { bindComputePipeline, computeBds, createParticleDispatch(maxParticles) };

            auto draw = vsg::Draw::create(maxParticles * 6, 1, 0, 0);
            auto drawGroup = createStateGroup(draw);
            drawGroup->prototypeArrayState = sNullArrayState;
            double maxRadius = ParticleSystem::calculateRadius(partctrl);
            vsg::dsphere bound(vsg::dvec3(), maxRadius);
            // bound.dataVariance = DYNAMIC;

            sw->children = { { vsg::MASK_ALL, vsg::DepthSorted::create(mDepthSortedBin, bound, drawGroup) },
                { vsg::MASK_ALL, vsg::DepthSorted::create(mComputeBin, bound, computeGroup) } };

            auto ctrl = ParticleSystem::create(partctrl, particles.mRecordIndex, worldSpace);
            setup(partctrl, *ctrl, *frameArgsData);
            // OpenEden: particles simulate on frame time whatever the node's AutoPlay flag, as osgParticle did in
            // OpenMW; otherwise an object with no animation of its own (a torch) keeps time 0 and its flame never
            // moves.
            ctrl->hints.autoPlay = true;

            auto cullCtrl = Anim::CullParticles::create();
            cullCtrl->active = ctrl->active;
            cullCtrl->maxLifetime = maxLifetime;
            cullCtrl->drawIndex = 0;
            cullCtrl->dispatchIndex = 1;
            setup(partctrl, *cullCtrl, *sw);
            cullCtrl->hints.autoPlay = true;

            addAnimContents(Anim::Contents::Particles | Anim::Contents::DynamicBounds);
            // return sw;
            return vsg::CullNode::create(bound, sw);
        }

        vsg::ref_ptr<vsg::Node> handleParticles(const Nif::NiParticles& particles)
        {
            const Nif::NiParticleSystemController* partctrl = nullptr;
            callActiveControllers(particles.mController, [&partctrl](auto& ctrl) {
                if (ctrl.mRecordType == Nif::RC_NiParticleSystemController
                    || ctrl.mRecordType == Nif::RC_NiBSPArrayController)
                    partctrl = &static_cast<const Nif::NiParticleSystemController&>(ctrl);
            });
            if (partctrl)
                return handleParticleSystemController(particles, *partctrl);
            return {};
        }

        void handleFlipController(const Nif::NiFlipController& flipctrl)
        {
            auto& stateSwitch = getNodeOptions().stateSwitch = vsg::StateSwitch::create();
            auto ctrl = Anim::StateSwitch::create();
            ctrl->index = Anim::make_channel<FlipChannel>(flipctrl.mDelta, flipctrl.mSources.size());
            setup(flipctrl, *ctrl, *stateSwitch);
        }

        vsg::ref_ptr<vsg::Data> handleSourceTexture(const Nif::NiSourceTexture& st, float alphaCutoff = -1.f)
        {
            if (!st.mExternal && !st.mData.empty())
            {
                // openeden-vsg: internal textures (task 0012).
                std::string error;
                auto image = readInternalTexture(*st.mData.getPtr(), error);
                if (!image)
                    warn(error);
                return image;
            }
            else
            {
                auto use = objectImageUse(vsgUtil::TextureCap::Kind::Colour);
                use.alphaCutoff = alphaCutoff;
                return vsgUtil::readImage(st.mFile, mImageOptions, use);
            }
        }

        // Task 0049: models opt in to the texture downscaler; their extra maps follow the same cap as the base
        // texture. `floor` keeps a height map from going below 256 texels.
        static vsgUtil::ImageUse objectImageUse(vsgUtil::TextureCap::Kind kind, int floor = 0)
        {
            vsgUtil::ImageUse use;
            use.category = vsgUtil::TextureCap::Category::Object;
            use.kind = kind;
            use.floor = floor;
            return use;
        }

        vsg::ref_ptr<vsg::DescriptorImage> handleTexture(const Nif::NiSourceTexture& st, int clamp, uint32_t binding)
        {
            // Task 0049: a cutout (alpha tested) base texture that has to be shrunk keeps the share of texels that pass
            // the test (foliage, fences), so it does not thin out.
            const auto& nodeOptions = getNodeOptions();
            const float cutoff = (binding == 0 && nodeOptions.alphaTestMode != 0) ? nodeOptions.alphaTestCutoff : -1.f;
            auto textureData = handleSourceTexture(st, cutoff);
            if (!textureData)
                return {};
            return createTextureDescriptor(textureData, clamp, binding);
        }

        vsg::ref_ptr<vsg::DescriptorImage> createTextureDescriptor(
            vsg::ref_ptr<vsg::Data> textureData, int clamp, uint32_t binding)
        {
            auto sampler = mBuilder.createSampler();
            sampler->addressModeU
                = (clamp >> 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler->addressModeV
                = clamp & 0x1 ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

            auto descriptor = vsgUtil::sharedDescriptorImage(mOptions.sharedObjects, sampler, textureData, binding);
            return descriptor;
        }

        // openeden-vsg: an external texture's path as the texture options resolve it (the .dds correction), the name
        // OpenMW's ShaderVisitor derived normal and specular maps from (task 0012, V4h).
        std::string resolveTexturePath(const Nif::NiTexturingProperty& texprop, size_t slot) const
        {
            if (slot >= texprop.mTextures.size() || !texprop.mTextures[slot].mEnabled
                || texprop.mTextures[slot].mSourceTexture.empty())
                return {};
            const Nif::NiSourceTexture& st = *texprop.mTextures[slot].mSourceTexture.getPtr();
            if (!st.mExternal)
                return {};
            if (mImageOptions && mImageOptions->findFileCallback)
                return mImageOptions->findFileCallback(st.mFile, mImageOptions.get()).string();
            return st.mFile;
        }

        // openeden-vsg: normal and specular maps next to the base texture (task 0012, V4h, as OpenMW's
        // Shader::ShaderVisitor::applyStateSet), with the base texture's wrap mode and UV set.
        void handleAutoMaps(const Nif::NiTexturingProperty& texprop, vsg::Descriptors& textures)
        {
            if (!mAutoMaps || !mTextureExists || !*mTextureExists
                || (!mAutoMaps->autoUseNormalMaps && !mAutoMaps->autoUseSpecularMaps && !mAutoMaps->autoUseMaterialMaps
                    && !mAutoMaps->autoUseHeightMaps))
                return;
            const std::string diffusePath = resolveTexturePath(texprop, Nif::NiTexturingProperty::BaseTexture);
            if (diffusePath.empty())
                return;
            const std::string bumpPath = resolveTexturePath(texprop, Nif::NiTexturingProperty::BumpTexture);
            const AutoMaps maps = findAutoMaps(diffusePath, bumpPath, *mAutoMaps, *mTextureExists);

            auto& nodeOptions = getNodeOptions();
            const auto& base = texprop.mTextures[Nif::NiTexturingProperty::BaseTexture];
            auto addMap
                = [&](const std::string& path, uint32_t binding, vsg::ref_ptr<vsg::Data> data = {},
                      vsgUtil::TextureCap::Kind mapKind = vsgUtil::TextureCap::Kind::Data) -> vsg::ref_ptr<vsg::Data> {
                if (!data)
                    data = vsgUtil::readOptionalImage(path, mImageOptions, objectImageUse(mapKind));
                if (!data)
                {
                    warn("!readImage(" + path + ")");
                    return {};
                }
                // The shader samples it with the diffuse texture's UV set.
                textures.emplace_back(createTextureDescriptor(data, base.mClamp, binding));
                return data;
            };
            bool hasNormalMap = false;
            if (!maps.normalMap.empty())
            {
                if (auto data
                    = addMap(maps.normalMap, Pipeline::Descriptors::NORMAL_UNIT, {}, vsgUtil::TextureCap::Kind::Normal))
                {
                    hasNormalMap = true;
                    nodeOptions.shader.addMode(Pipeline::Mode::NORMAL_MAP);
                    if (isTwoChannelFormat(data->properties.format))
                        nodeOptions.shader.addMode(Pipeline::Mode::RECONSTRUCT_NORMAL_Z);
                    else if (maps.normalHeight)
                        nodeOptions.shader.addMode(Pipeline::Mode::PARALLAX);
                }
            }
            if (!maps.specularMap.empty())
            {
                if (addMap(maps.specularMap, Pipeline::Descriptors::SPECULAR_UNIT))
                    nodeOptions.shader.addMode(Pipeline::Mode::SPECULAR_MAP);
            }
            if (!maps.materialMap.empty())
            {
                if (addMap(maps.materialMap, Pipeline::Descriptors::MATERIAL_MAP_UNIT))
                {
                    nodeOptions.shader.addMode(Pipeline::Mode::MATERIAL_MAP);
                    if (maps.materialLayout == Render::Pbr::MapLayout::Rmaos)
                        nodeOptions.shader.addMode(Pipeline::Mode::MATERIAL_MAP_RMAOS);
                }
            }
            // Task 0045: the height file, for parallax occlusion mapping. It needs the tangent frame of a normal map
            // (the shader reads both with the diffuse texture's UV set).
            // A file that cannot be read or is not a height image counts as absent (logged once), and then the material
            // has no parallax from it (Render::Parallax::decideHeightSource).
            if (!maps.heightMap.empty() && hasNormalMap)
            {
                const auto height = vsgUtil::readOptionalImage(maps.heightMap, mImageOptions,
                    objectImageUse(vsgUtil::TextureCap::Kind::Data, vsgUtil::TextureCap::heightMapFloor));
                const bool usableFormat = height && Render::Parallax::usableHeightFormat(height->properties.format);
                const bool usable = usableFormat
                    && Render::Parallax::heightMipsUsable(height->properties.format,
                        height->width() * height->properties.blockWidth,
                        height->height() * height->properties.blockHeight, vsgUtil::detail::mipLevels(*height).size());
                if (!usable)
                    warnHeightMapOnce(maps.heightMap,
                        !height             ? "cannot be read"
                            : !usableFormat ? "is not a one-channel or plain colour image"
                                            : "has no mip levels");
                if (usable && addMap(maps.heightMap, Pipeline::Descriptors::HEIGHT_UNIT, height))
                {
                    nodeOptions.shader.addMode(Pipeline::Mode::HEIGHT_MAP);
                    // Dropping fragments that leave the texture only suits a texture that does not tile.
                    if (mAutoMaps->parallaxDiscardEdges && base.mClamp)
                        nodeOptions.shader.addMode(Pipeline::Mode::PARALLAX_DISCARD);
                }
            }
        }

        // Task 0045: one line in the log for each height file that is not used.
        void warnHeightMapOnce(const std::string& path, const char* why)
        {
            static std::mutex mutex;
            static std::set<std::string> reported;
            const std::lock_guard<std::mutex> lock(mutex);
            if (reported.insert(path).second)
                warn("Height map " + path + " " + why + "; the material has no parallax");
        }

        static void removeAutoMapModes(Pipeline::ShaderOptions& shader)
        {
            shader.removeMode(Pipeline::Mode::HEIGHT_MAP);
            shader.removeMode(Pipeline::Mode::PARALLAX_DISCARD);
            shader.removeMode(Pipeline::Mode::NORMAL_MAP);
            shader.removeMode(Pipeline::Mode::PARALLAX);
            shader.removeMode(Pipeline::Mode::RECONSTRUCT_NORMAL_Z);
            shader.removeMode(Pipeline::Mode::SPECULAR_MAP);
            shader.removeMode(Pipeline::Mode::MATERIAL_MAP);
            shader.removeMode(Pipeline::Mode::MATERIAL_MAP_RMAOS);
        }

        void handleTextureProperty(const Nif::NiTexturingProperty& texprop)
        {
            auto& nodeOptions = getNodeOptions();
            nodeOptions.textureSets.clear();
            nodeOptions.nonStandardUvSets.clear();
            nodeOptions.stateSwitch = {};
            removeAutoMapModes(nodeOptions.shader);
            if (mMaterialTable)
                nodeOptions.diffuseTexture = resolveTexturePath(texprop, Nif::NiTexturingProperty::BaseTexture);
            vsg::Descriptors textures;
            auto flipctrl = searchController<Nif::NiFlipController>(texprop.mController, Nif::RC_NiFlipController);
            size_t i = 0;
            if (flipctrl)
            {
                handleFlipController(*flipctrl);
                i = 1;
            }
            for (; i < texprop.mTextures.size(); ++i)
            {
                if (texprop.mTextures[i].mEnabled && !texprop.mTextures[i].mSourceTexture.empty())
                {
                    Pipeline::Mode mode = Pipeline::Mode::DIFFUSE_MAP;
                    int binding = convertTextureSlot(i, mode);
                    if (binding == -1)
                    {
                        warn("!convertTextureSlot(NiTexturingProperty::" + std::to_string(i) + ")");
                        continue;
                    }
                    auto tex = texprop.mTextures[i];
                    if (tex.mUVSet != 0)
                    {
                        nodeOptions.nonStandardUvSets[binding] = tex.mUVSet;
                        warn("vsgopenmw-testing(tex.uvSet)");
                    }
                    if (auto descriptor = handleTexture(*tex.mSourceTexture.getPtr(), tex.mClamp, binding))
                    {
                        nodeOptions.shader.addMode(mode);
                        textures.emplace_back(descriptor);
                    }
                }
            }
            handleAutoMaps(texprop, textures);
            if (flipctrl)
            {
                handleList(flipctrl->mSources, [this, &textures, &nodeOptions, texprop](auto& n) {
                    if (auto tex = handleTexture(n, texprop.mTextures[0].mClamp, 0))
                    {
                        nodeOptions.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
                        vsg::Descriptors textureSet{ tex };
                        std::copy(textures.begin(), textures.end(), std::back_inserter(textureSet));
                        nodeOptions.textureSets.emplace_back(textureSet);
                    }
                });
            }
            else
                nodeOptions.textureSets.emplace_back(textures);
        }

        void handleMaterialProperty(const Nif::NiMaterialProperty& matprop)
        {
            auto& nodeOptions = getNodeOptions();
            nodeOptions.material = MaterialValue::create(Pipeline::Material::createDefault());
            auto& val = nodeOptions.material->value();
            val.diffuse = vsg::vec4(matprop.mDiffuse, matprop.mAlpha);
            val.ambient = vsg::vec4(matprop.mAmbient, 1.f);
            val.emissive = vsg::vec4(matprop.mEmissive, 1.f);
            // if (mNif->getVersion() > Nif::NIFFile::NIFVersion::VER_MW)
            // val.specular = vsg::vec4(matprop.data.specular, 1.f);
            // val.shininess = matprop.data.glossiness;
            if (!matprop.mController.empty())
                handleMaterialController(matprop.mController);
            else
                nodeOptions.materialController = nullptr;
        }

        void handleMaterialController(const Nif::NiTimeControllerPtr& ptr)
        {
            auto color = Anim::Color::create();
            callActiveControllers(ptr, [this, &color](auto& ctrl) {
                if (ctrl.mRecordType == Nif::RC_NiMaterialColorController)
                {
                    auto matctrl = static_cast<const Nif::NiMaterialColorController&>(ctrl);
                    if (matctrl.mData.empty())
                        return;
                    auto targetColor = matctrl.mTargetColor;
                    if (targetColor == Nif::NiMaterialColorController::TargetColor::Specular
                        && mNif->getVersion() <= Nif::NIFFile::NIFVersion::VER_MW)
                        return;
                    size_t offset = 0;
                    switch (targetColor)
                    {
                        case Nif::NiMaterialColorController::TargetColor::Ambient:
                        default:
                            offset = offsetof(Pipeline::Data::Material, ambient);
                            break;
                        case Nif::NiMaterialColorController::TargetColor::Diffuse:
                            offset = offsetof(Pipeline::Data::Material, diffuse);
                            break;
                        case Nif::NiMaterialColorController::TargetColor::Specular:
                            offset = offsetof(Pipeline::Data::Material, specular);
                            break;
                        case Nif::NiMaterialColorController::TargetColor::Emissive:
                            offset = offsetof(Pipeline::Data::Material, emissive);
                            break;
                    }
                    color->colorOffset = offset;
                    color->color = handleKeyframes<vsg::vec3>(matctrl, matctrl.mData->mKeyList);
                    setHints(matctrl, color->hints);
                }
                else if (ctrl.mRecordType == Nif::RC_NiAlphaController)
                {
                    auto alphactrl = static_cast<const Nif::NiAlphaController&>(ctrl);
                    if (alphactrl.mData.empty())
                        return;
                    color->alphaOffset = offsetof(Pipeline::Data::Material, diffuse);
                    color->alpha = handleKeyframes<float>(alphactrl, alphactrl.mData->mKeyList);
                    setHints(alphactrl, color->hints);
                }
            });
            addAnimContents();
            getNodeOptions().materialController = color;
        }

        void handleAlphaProperty(const Nif::NiAlphaProperty& alpha)
        {
            auto& options = getNodeOptions();
            options.blend = alpha.useAlphaBlending();
            if (options.blend)
            {
                options.srcBlendFactor = options.srcAlphaBlendFactor = convertBlendFactor(alpha.sourceBlendMode());
                options.dstBlendFactor = options.dstAlphaBlendFactor = convertBlendFactor(alpha.destinationBlendMode());
                if (/* d3d_8_1_compat && */ options.dstBlendFactor == VK_BLEND_FACTOR_DST_ALPHA)
                    options.dstBlendFactor = options.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                options.depthSorted = !alpha.noSorter();
            }
            if (alpha.useAlphaTesting())
            {
                options.alphaTestMode = alpha.alphaTestMode();
                options.alphaTestCutoff = alpha.mThreshold / 255.f;
            }
            else
                options.alphaTestMode = 0;
        }

        void handleStencilProperty(const Nif::NiStencilProperty& stencilprop)
        {
            auto& nodeOptions = getNodeOptions();
            nodeOptions.frontFace = stencilprop.mDrawMode == Nif::NiStencilProperty::DrawMode::Clockwise
                ? VK_FRONT_FACE_CLOCKWISE
                : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            nodeOptions.cullMode = stencilprop.mDrawMode == Nif::NiStencilProperty::DrawMode::Both
                ? VK_CULL_MODE_NONE
                : VK_CULL_MODE_BACK_BIT;
        }

        void handleWireframeProperty(const Nif::NiWireframeProperty& wireprop)
        {
            getNodeOptions().polygonMode = wireprop.mEnable ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE;
        }

        void handleZBufferProperty(const Nif::NiZBufferProperty& zprop)
        {
            auto& nodeOptions = getNodeOptions();
            nodeOptions.depthTest = zprop.depthTest();
            nodeOptions.depthWrite = zprop.depthWrite();
            // if (!mw_compat) nodeOptions.depthFunc =
        }

        void handleVertexColorProperty(const Nif::NiVertexColorProperty& vertprop)
        {
            auto vertmode = static_cast<int>(vertprop.mVertexMode);
            if (static_cast<int>(vertprop.mLightingMode) == 0)
                vertmode = 0;
            getNodeOptions().colorMode = vertmode;
            getNodeOptions().hasVertexColorProperty = true;
        }

        void handleSpecularProperty(const Nif::NiSpecularProperty& specprop)
        {
            // if (mNif->getVersion() > Nif::NIFFile::NIFVersion::VER_MW)
            // getNodeOptions().specular = specprop.isEnabled() && color != {0,0,0}
        }

        void handleProperty(const Nif::NiProperty& property)
        {
            switch (property.mRecordType)
            {
                case Nif::RC_NiStencilProperty:
                    handleStencilProperty(static_cast<const Nif::NiStencilProperty&>(property));
                    break;
                case Nif::RC_NiWireframeProperty:
                    handleWireframeProperty(static_cast<const Nif::NiWireframeProperty&>(property));
                    break;
                case Nif::RC_NiZBufferProperty:
                    handleZBufferProperty(static_cast<const Nif::NiZBufferProperty&>(property));
                    break;
                case Nif::RC_NiAlphaProperty:
                    handleAlphaProperty(static_cast<const Nif::NiAlphaProperty&>(property));
                    break;
                case Nif::RC_NiMaterialProperty:
                    handleMaterialProperty(static_cast<const Nif::NiMaterialProperty&>(property));
                    break;
                case Nif::RC_NiVertexColorProperty:
                    handleVertexColorProperty(static_cast<const Nif::NiVertexColorProperty&>(property));
                    break;
                case Nif::RC_NiSpecularProperty:
                    handleSpecularProperty(static_cast<const Nif::NiSpecularProperty&>(property));
                    break;
                case Nif::RC_NiTexturingProperty:
                    handleTextureProperty(static_cast<const Nif::NiTexturingProperty&>(property));
                    break;
                default:
                    break;
            }
        }

        vsg::ref_ptr<vsg::Data> createMaterialData()
        {
            auto& options = getNodeOptions();
            vsg::ref_ptr<MaterialValue> data;
            if (options.material && !Pipeline::Material::isDefault(*options.material))
                data = MaterialValue::create(*options.material);
            else
                data = MaterialValue::create(Pipeline::Material::createDefault());
            data->value().alphaTestCutoff = options.alphaTestCutoff;
            if (mMaterialTable)
            {
                // Task 0027, tier 2 and the skin marks: values no layer sets stay "derive" (roughness and reflectance
                // below zero).
                const auto values = mMaterialTable->resolveValues(mFilename, options.diffuseTexture);
                auto& pbr = data->value().pbr;
                if (values.roughness)
                    pbr.x = static_cast<float>(*values.roughness);
                if (values.metalness)
                    pbr.y = static_cast<float>(*values.metalness);
                if (values.reflectance)
                    pbr.z = static_cast<float>(*values.reflectance);
                if (values.occlusion)
                    pbr.w = static_cast<float>(*values.occlusion);
                if (values.subsurface)
                    data->value().subsurface = *values.subsurface ? 1.f : 0.f;
                // Task 0044: a True PBR set's scales. The specular level is the F0 before the map's alpha, stored as
                // a reflectance unless a file sets one; the roughness scale goes in the spare float (0: none).
                if (values.specularLevel && !values.reflectance)
                    pbr.z = Render::Pbr::reflectanceFromF0(static_cast<float>(*values.specularLevel));
                if (values.roughnessScale)
                    data->value().align_dummy2 = static_cast<float>(*values.roughnessScale);
            }
            // Task 0045: with a height file the float carries two half floats, the roughness scale and the displacement
            // scale (the material's, or the default) times `parallax scale`. Without the material table (no pbr) there
            // are no per-material values. A scale of 0 would read as "not set", so the depth is never below 0.001.
            if (options.shader.hasMode(Pipeline::Mode::HEIGHT_MAP))
            {
                const auto values = mMaterialTable ? mMaterialTable->resolveValues(mFilename, options.diffuseTexture)
                                                   : Render::Pbr::OverrideValues{};
                const double displacement = values.displacementScale.value_or(
                    static_cast<double>(Render::Parallax::defaultDisplacementScale));
                const float userScale = mAutoMaps ? mAutoMaps->parallaxScale : 1.f;
                const float scale = std::max(static_cast<float>(displacement) * userScale, 0.001f);
                // One depth for the whole mesh, in texture coordinates: the shader's derivatives would change from one
                // triangle to the next. A mesh with no usable number leaves the shader to use them.
                data->value().align_dummy2 = Render::Parallax::packScales(
                    values.roughnessScale ? static_cast<float>(*values.roughnessScale) : 0.f,
                    Render::Parallax::meshDepthField(scale, mMeshWorldPerUv));
            }
            if (mOverride && mOverride->materialData)
                mOverride->materialData(data->value());
            // if (isDefault) return {}; // Increases shader variations.
            return data;
        }

        vsg::ref_ptr<vsg::DescriptorBuffer> getMaterialDescriptor()
        {
            if (mOverride && mOverride->material)
                return mOverride->material;
            vsg::ref_ptr<vsg::DescriptorBuffer> descriptor;
            auto matData = createMaterialData();
            if (matData)
            {
                auto& matCtrl = getNodeOptions().materialController;
                if (matCtrl)
                    matCtrl->attachTo(*matData);
                else
                    share(matData);

                descriptor = vsg::DescriptorBuffer::create(
                    matData, Pipeline::Descriptors::MATERIAL_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                if (!matCtrl)
                    share(descriptor);
            }
            return descriptor;
        }

        vsg::ref_ptr<vsg::BindDescriptorSet> createBindDescriptorSet(vsg::PipelineLayout* layout, size_t i)
        {
            vsg::Descriptors descriptors;
            auto& nodeOptions = getNodeOptions();
            for (auto& descriptor : nodeOptions.descriptors)
                descriptors.emplace_back(descriptor);
            if (nodeOptions.textureSets.size() > i)
            {
                for (auto& texture : nodeOptions.textureSets[i])
                    descriptors.emplace_back(texture);
            }

            auto isDynamic = [](vsg::Descriptors& list) -> bool {
                for (auto& d : list)
                {
                    auto db = d->cast<vsg::DescriptorBuffer>();
                    if (db && !db->bufferInfoList.empty() && db->bufferInfoList[0]->data
                        && db->bufferInfoList[0]->data->dynamic())
                        return true;
                }
                return false;
            };
            bool dynamic = isDynamic(descriptors);

            auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
            if (!dynamic)
                share(descriptorSet);

            auto bds = vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);
            if (!dynamic)
                share(bds);

            return bds;
        }

        vsg::ref_ptr<vsg::StateGroup> createStateGroup(vsg::ref_ptr<vsg::Node> child)
        {
            auto stateGroup = vsg::StateGroup::create();
            auto& options = getNodeOptions();
            if (mOverride && mOverride->pipelineOptions)
                mOverride->pipelineOptions(options);
            if (auto mat = getMaterialDescriptor())
                addModeDescriptor(mat, Pipeline::Mode::MATERIAL);
            auto bindGraphicsPipeline = mBuilder.graphics->getOrCreate(options);
            vsg::ref_ptr<vsg::PipelineLayout> layout = bindGraphicsPipeline->pipeline->layout;
            vsg::ref_ptr<vsg::StateCommand> descriptorState;
            if (options.stateSwitch)
            {
                auto stateSwitch = vsg::StateSwitch::create(*options.stateSwitch);
                for (size_t i = 0; i < options.textureSets.size(); ++i)
                {
                    auto bds = createBindDescriptorSet(layout, i);
                    stateSwitch->add(vsg::boolToMask(i == 0), bds);
                    stateSwitch->slot = bds->slot;
                }
                descriptorState = stateSwitch;
            }
            else
                descriptorState = createBindDescriptorSet(layout, 0);
            stateGroup->children = { child };
            stateGroup->stateCommands = { bindGraphicsPipeline, descriptorState };
            return stateGroup;
        }

        void setHints(const Nif::NiTimeController& nictrl, Anim::Controller::Hints& hints)
        {
            hints.duration = std::max(hints.duration, nictrl.mTimeStop);
            auto& nodeOptions = getNodeOptions();
            hints.autoPlay = nodeOptions.animFlags & Nif::NiNode::AnimFlag_AutoPlay;
            hints.phaseGroup = nodeOptions.phaseGroup;
        }

        void addAnimContents(int c = Anim::Contents::Controllers) { mContents.add(c); }

        template <class Ctrl, class Target>
        void setup(const Nif::NiTimeController& nictrl, Ctrl& controller, Target& target)
        {
            setHints(nictrl, controller.hints);
            controller.attachTo(target);
            addAnimContents();
        }

        void handleTransformControllers(const Nif::NiTimeControllerPtr controller, Anim::Transform& transform)
        {
            callActiveControllers(controller, [this, &transform](auto& ctrl) {
                if (ctrl.mRecordType == Nif::RC_NiKeyframeController)
                {
                    if (auto keyctrl = handleKeyframeController(static_cast<const Nif::NiKeyframeController&>(ctrl)))
                    {
                        setup(ctrl, *keyctrl, transform);
                        addAnimContents(Anim::Contents::TransformControllers);
                    }
                }
                else if (ctrl.mRecordType == Nif::RC_NiPathController)
                {
                    if (auto pathctrl = handlePathController(static_cast<const Nif::NiPathController&>(ctrl)))
                    {
                        setup(ctrl, *pathctrl, transform);
                        addAnimContents(Anim::Contents::TransformControllers);
                    }
                }
                else if (ctrl.mRecordType == Nif::RC_NiRollController)
                {
                    if (auto rollctrl = handleRollController(static_cast<const Nif::NiRollController&>(ctrl)))
                    {
                        setup(ctrl, *rollctrl, transform);
                        addAnimContents(Anim::Contents::TransformControllers);
                    }
                }
            });
        }

        void handleUVController(const Nif::NiUVController& uvctrl)
        {
            getNodeOptions().animatedTextureTransform = true;
            if (uvctrl.mData.empty())
                return;
            auto ctrl = Anim::TexMat::create();
            for (int i = 0; i < 2; ++i)
            {
                ctrl->translate[i] = handleKeyframes<float>(uvctrl, uvctrl.mData->mKeyList[i], { 0.f });
                ctrl->scale[i] = handleKeyframes<float>(uvctrl, uvctrl.mData->mKeyList[i + 2], { 1.f });
            }

            if (uvctrl.mUvSet != 0)
                getNodeOptions().nonStandardUvSets[Pipeline::Descriptors::TEXMAT_BINDING] = uvctrl.mUvSet;
            auto matrix = vsg::mat4Value::create();
            setup(uvctrl, *ctrl, *matrix);
            auto descriptor = vsg::DescriptorBuffer::create(
                matrix, Pipeline::Descriptors::TEXMAT_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            addModeDescriptor(descriptor, Pipeline::Mode::TEXMAT);
        }

        void handleGeomMorpherController(const Nif::NiGeomMorpherController& morpher, size_t numVertices)
        {
            if (morpher.mData.empty())
                return;
            const auto& morphs = morpher.mData->mMorphs;
            if (morphs.empty())
                return;
            size_t numMorphs = morphs.size();
            auto array = vsg::vec4Array::create(numMorphs * numVertices);
            for (size_t vertex = 0; vertex < numVertices; ++vertex)
            {
                for (size_t j = 0; j < numMorphs; ++j)
                {
                    if (vertex < std::min(numVertices, morphs[j].mVertices.size()))
                        (*array)[vertex * numMorphs + j]
                            = vsg::vec4(morphs[j].mVertices[vertex], static_cast<float>(numMorphs));
                }
            }

            getNodeOptions().descriptors.emplace_back(vsg::DescriptorBuffer::create(
                array, Pipeline::Descriptors::MORPH_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

            auto weights = vsg::floatArray::create(numMorphs, 0.f);
            auto ctrl = Anim::Morph::create();
            ctrl->weights.resize(numMorphs);
            for (size_t i = 0; i < numMorphs; ++i)
            {
                float defaultWeight = i == 0 ? 1.f : 0.f; // i<relativeTargets
                weights->at(i) = defaultWeight;
                ctrl->weights[i] = handleKeyframes<float>(morpher, morphs[i].mKeyFrames, defaultWeight);
            }
            auto weightsDescriptor = vsg::DescriptorBuffer::create(
                weights, Pipeline::Descriptors::MORPH_WEIGHTS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            setup(morpher, *ctrl, *weights);
            addModeDescriptor(weightsDescriptor, Pipeline::Mode::MORPH);
        }

        void handleGeometryControllers(const Nif::NiTimeControllerPtr controller, size_t numVertices)
        {
            callActiveControllers(controller, [this, &numVertices](auto& ctrl) {
                if (ctrl.mRecordType == Nif::RC_NiUVController)
                    handleUVController(static_cast<const Nif::NiUVController&>(ctrl));
                else if (ctrl.mRecordType == Nif::RC_NiGeomMorpherController)
                    handleGeomMorpherController(static_cast<const Nif::NiGeomMorpherController&>(ctrl), numVertices);
            });
        }

        void handleVisController(const Nif::NiVisController& visctrl, vsg::Switch& sw)
        {
            auto ctrl = Anim::Switch::create(Anim::make_channel<VisChannel>(visctrl.mData->mKeys));
            setup(visctrl, *ctrl, sw);
        }

        bool filterMatches(const std::string& nodeName)
        {
            return Misc::StringUtils::ciCompareLen(nodeName, mSkinFilter, mSkinFilter.size()) == 0
                || Misc::StringUtils::ciCompareLen(nodeName, mSkinFilter2, mSkinFilter2.size()) == 0;
        }

        void handleEffect(const Nif::NiAVObject& nifNode)
        {
            if (nifNode.mRecordType != Nif::RC_NiTextureEffect)
            {
                warn("Unhandled effect " + nifNode.mRecordName);
                return;
            }

            auto& textureEffect = static_cast<const Nif::NiTextureEffect&>(nifNode);
            if (textureEffect.mTextureType != Nif::NiTextureEffect::TextureType::EnvironmentMap)
            {
                warn("Unhandled NiTextureEffect type " + std::to_string(static_cast<int>(textureEffect.mTextureType)));
                return;
            }
            if (textureEffect.mTexture.empty())
                return;

            addModeDescriptor(handleTexture(*textureEffect.mTexture.getPtr(), textureEffect.mClampMode,
                                  Pipeline::Descriptors::ENV_UNIT),
                Pipeline::Mode::ENV_MAP);
        }

        void handleEffects(const Nif::NiAVObjectList& effects)
        {
            handleList(effects, [this](auto& n) { handleEffect(n); });
        }

        void handleProperties(const Nif::NiPropertyList& props)
        {
            // Task 0049: the alpha test is read first, since the texture property may come before it in the list and
            // its image is read when it is handled.
            handleList(props, [this](auto& n) {
                if (n.mRecordType == Nif::RC_NiAlphaProperty)
                    handleAlphaProperty(static_cast<const Nif::NiAlphaProperty&>(n));
            });
            handleList(props, [this](auto& n) { handleProperty(n); });
        }

        vsg::ref_ptr<vsg::Node> handleNiNodeChildren(
            const Nif::NiAVObjectList& children, vsg::ref_ptr<vsg::Group> group, bool hasMarkers, bool skipMeshes)
        {
            vsg::Group::Children vsgchildren;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (children[i].empty() || Misc::StringUtils::ciEqual(children[i]->mName, "Bounding Box"))
                    continue;
                if (auto child = handleNode(*children[i].getPtr(), hasMarkers, skipMeshes))
                    vsgchildren.emplace_back(child);
            }
            if (group)
                std::copy(vsgchildren.begin(), vsgchildren.end(), std::back_inserter(group->children));
            else if (!vsgchildren.empty())
                return vsgUtil::getNode(vsgchildren);
            return {};
        }

        template <class T>
        vsg::ref_ptr<T> createGroup(const Nif::NiAVObject& nifNode) const
        {
            auto node = T::create();
            vsgUtil::setName(*node, std::string(nifNode.mName));
            vsgUtil::setID(*node, nifNode.mRecordIndex);
            return node;
        }

        vsg::ref_ptr<Anim::Transform> handleTransform(const Nif::NiAVObject& nifNode)
        {
            auto trans = createGroup<Anim::Transform>(nifNode);
            convertTrafo(*trans, nifNode.mTransform);
            handleTransformControllers(nifNode.mController, *trans);
            return trans;
        }

        void handleAnimFlags(const Nif::NiAVObject& nifNode)
        {
            if (nifNode.mRecordType == Nif::RC_NiBSAnimationNode || nifNode.mRecordType == Nif::RC_NiBSParticleNode)
            {
                auto& nodeOptions = getNodeOptions();
                nodeOptions.animFlags = nifNode.mFlags;
                nodeOptions.phaseGroup = (nifNode.mFlags & Nif::NiNode::AnimFlag_NotRandom)
                    ? 0u
                    : ++mPhaseGroups; // openmw-6455-controller-random-phase
            }
        }

        vsg::ref_ptr<vsg::Node> handleNode(
            const Nif::NiAVObject& nifNode, bool hasMarkers = false, bool skipMeshes = false)
        {
            ScopedPushPop spp(mNodeStack);

            handleAnimFlags(nifNode);

            auto& nodeOptions = getNodeOptions();
            nodeOptions.nodeScale *= nifNode.mTransform.mScale;
            if (mNif->getUseSkinning() && !mSkinFilter.empty() && !nodeOptions.filterMatched)
            {
                nodeOptions.filterMatched = filterMatches(nifNode.mName);
                if (!nodeOptions.filterMatched)
                {
                    if (const Nif::NiNode* ninode = dynamic_cast<const Nif::NiNode*>(&nifNode))
                        return handleNiNodeChildren(ninode->mChildren, {}, false, false);
                    else
                        return {};
                }
            }

            handleProperties(nifNode.mProperties);

            vsg::ref_ptr<vsg::Group> group; // node_to_add_children_to
            vsg::ref_ptr<vsg::Node> retNode; // topmost_node
            if (!canOptimizeTransform(nifNode))
            {
                auto trans = handleTransform(nifNode);
                trans->subgraphRequiresLocalFrustum = false; // nodeOptions.depthSorted;
                retNode = group = trans;
            }
            if (nifNode.useFlags & Nif::NiAVObject::Emitter /* || animatedCollision*/)
            {
                if (!group)
                    retNode = group = createGroup<vsg::Group>(nifNode);
            }

            if (nifNode.mRecordType == Nif::RC_NiBillboardNode)
            {
                if (!canOptimizeBillboard(nifNode))
                {
                    auto billboard = Anim::Billboard::create();
                    billboard->subgraphRequiresLocalFrustum = false;
                    if (group)
                        billboard->addChild(group);
                    else
                        group = billboard;
                    retNode = billboard;
                }
                else
                    nodeOptions.shader.addMode(Pipeline::Mode::BILLBOARD);
            }

            for (Nif::ExtraPtr e = nifNode.mExtra; !e.empty(); e = e->mNext)
            {
                if (e->mRecordType == Nif::RC_NiTextKeyExtraData)
                {
                    if (!group)
                        group = createGroup<vsg::Group>(nifNode);
                    handleTextKeys(static_cast<const Nif::NiTextKeyExtraData&>(*e.getPtr()))->attachTo(*group);
                }
                else if (e->mRecordType == Nif::RC_NiStringExtraData)
                {
                    const Nif::NiStringExtraData* sd = static_cast<const Nif::NiStringExtraData*>(e.getPtr());
                    if (sd->mData == "MRK")
                    {
                        // Marker objects. These meshes are only visible in the editor.
                        hasMarkers = true;
                    }
                    else if (sd->mData == "BONE")
                    {
                        // openeden-vsg: a bone for Anim::Bones::addCustomBones, as OpenMW's nifloader.cpp "CustomBone"
                        // (task 0012).
                        if (group)
                            group->setValue(Anim::sCustomBoneKey, true);
                    }
                }
            }

            if (const Nif::NiNode* ninode = dynamic_cast<const Nif::NiNode*>(&nifNode))
            {
                handleEffects(ninode->mEffects);

                if (nifNode.mRecordType == Nif::RC_NiSwitchNode)
                {
                    auto& niSwitchNode = static_cast<const Nif::NiSwitchNode&>(nifNode);
                    auto switchNode = vsg::Switch::create();
                    switchNode->setValue("switch", std::string(nifNode.mName));
                    const Nif::NiAVObjectList& children = niSwitchNode.mChildren;
                    switchNode->children.reserve(children.size());
                    for (size_t i = 0; i < children.size(); ++i)
                    {
                        auto child = children[i].empty() ? vsg::Node::create()
                                                         : handleNode(*children[i].getPtr(), hasMarkers, skipMeshes);
                        if (!child)
                            child = vsg::Node::create();
                        switchNode->addChild(i == niSwitchNode.mInitialIndex ? true : false, child);
                    }
                    if (group)
                        group->addChild(switchNode);
                    else
                        retNode = switchNode;
                }
                else if (nifNode.mRecordType == Nif::RC_RootCollisionNode)
                {
                    // Hide collision shapes, but don't skip the subgraph
                    // We still need to animate the hidden bones so the physics system can access them
                    // addOffSwitch(
                    skipMeshes = true;
                }
                else if (nifNode.mRecordType == Nif::RC_NiLODNode)
                {
                    // openeden-vsg: distance ranges from the eye, as OpenMW's osg::LOD (task 0012).
                    auto& niLodNode = static_cast<const Nif::NiLODNode&>(nifNode);
                    auto lodNode = vsgUtil::DistanceLOD::create();
                    lodNode->setValue("lod", std::string(nifNode.mName));
                    lodNode->center = vsg::vec3(niLodNode.mLODCenter.x, niLodNode.mLODCenter.y, niLodNode.mLODCenter.z);
                    for (const auto& range : niLodNode.mLODLevels)
                        lodNode->ranges.push_back({ range.mMinRange, range.mMaxRange });
                    // Empty child references are skipped, so range i applies to the i-th present child, as in OpenMW.
                    const Nif::NiAVObjectList& children = niLodNode.mChildren;
                    lodNode->children.reserve(children.size());
                    for (size_t i = 0; i < children.size(); ++i)
                    {
                        if (children[i].empty())
                            continue;
                        auto child = handleNode(*children[i].getPtr(), hasMarkers, skipMeshes);
                        if (!child)
                            child = vsg::Node::create();
                        lodNode->addChild(child);
                    }
                    if (group)
                        group->addChild(lodNode);
                    else
                        retNode = lodNode;
                }
                else if (auto createdNode = handleNiNodeChildren(ninode->mChildren, group, hasMarkers, skipMeshes))
                    retNode = createdNode;
            }
            else if (isTypeGeometry(nifNode.mRecordType) && !skipMeshes && !canSkipGeometry(hasMarkers, nifNode))
            {
                auto child = handleGeometry(static_cast<const Nif::NiGeometry&>(nifNode));
                if (group && child)
                    group->addChild(child);
                else
                    retNode = child;
            }
            else if (nifNode.mRecordType == Nif::RC_NiParticles)
            {
                auto child = handleParticles(static_cast<const Nif::NiParticles&>(nifNode));
                if (group && child)
                    group->addChild(child);
                else if (child)
                {
                    vsgUtil::setID(*child, nifNode.mRecordIndex);
                    retNode = child;
                }
            }
            if (group && !retNode)
                retNode = group;
            if (!retNode)
                return {};

            bool hidden = nifNode.isHidden();
            auto visctrl = searchController<Nif::NiVisController>(nifNode.mController, Nif::RC_NiVisController);
            if (hidden || (visctrl && !visctrl->mData.empty()))
            {
                auto sw = vsg::Switch::create();
                sw->children = { { !hidden, retNode } };
                if (visctrl && !visctrl->mData.empty())
                    handleVisController(*visctrl, *sw);
                retNode = sw;
            }
            return retNode;
        }
    };

    vsg::ref_ptr<vsg::Object> nif::read(std::istream& stream, vsg::ref_ptr<const vsg::Options> options) const
    {
        if (!vsg::compatibleExtension(options.get(), ".nif"))
            return {};
        try
        {
            nifImpl impl(stream, *options, mImageOptions, showMarkers, mComputeBin, mDepthSortedBin, mBuilder);
            impl.mAutoMaps = &autoMaps;
            impl.mMaterialTable = materialTable.get();
            impl.mTextureExists = &textureExists;
            impl.mSoftParticles = softParticles;
            impl.mOccludedParticleModel = &occludedParticleModel;
            impl.mOccludedParticleMap = &occludedParticleMap;
            return impl.load();
        }
        catch (std::exception& e)
        {
            std::string filename;
            options->getValue("filename", filename);
            std::cerr << "!nif::read(" << filename << "): " << e.what() << std::endl;
            return {};
        }
    }

    bool nif::getFeatures(Features& features) const
    {
        features.extensionFeatureMap[".nif"] = READ_ISTREAM;
        // features.supportedOptions = "skinfilter"
        return true;
    }
}
