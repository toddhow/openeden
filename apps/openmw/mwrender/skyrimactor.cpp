#include "skyrimactor.hpp"

#include <algorithm>
#include <atomic>
#include <sstream>

#include <vsg/nodes/Group.h>

#include <components/animation/contents.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/skeleton.hpp>
#include <components/nifscene/skinned.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/name.hpp>

namespace MWRender::SkyrimActor
{
    namespace
    {
        // Process-wide totals for the log line of a cell. Relaxed atomics: a count, not a synchronisation.
        struct Totals
        {
            std::atomic<std::uint64_t> mActors{ 0 };
            std::atomic<std::uint64_t> mActorsFailed{ 0 };
            std::atomic<std::uint64_t> mParts{ 0 };
            std::atomic<std::uint64_t> mPartsDrawn{ 0 };
            std::atomic<std::uint64_t> mPartsUnreadable{ 0 };
            std::atomic<std::uint64_t> mPartsWithoutSkin{ 0 };
            std::atomic<std::uint64_t> mBones{ 0 };
            std::atomic<std::uint64_t> mDraws{ 0 };
            std::atomic<std::uint64_t> mTriangles{ 0 };
        };

        Totals& totals()
        {
            static Totals sTotals;
            return sTotals;
        }

        // The file as a scene, or a message. Never throws.
        bool readScene(const VFS::Manager& vfs, const std::string& model, NifScene::Scene& scene, std::string& error)
        {
            try
            {
                const VFS::Path::Normalized path = Misc::ResourceHelpers::correctMeshPath(VFS::Path::toNormalized(model));
                Files::IStreamPtr stream = vfs.get(path);
                if (!stream)
                {
                    error = "the file is not in the VFS";
                    return false;
                }
                scene = NifScene::readScene(std::move(stream), path, NifScene::SceneOptions{});
                return true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                return false;
            }
        }
    }

    vsg::ref_ptr<vsg::Group> buildSkeletonNodes(const NifScene::Skeleton::Plan& plan)
    {
        auto root = vsg::Group::create();
        std::vector<vsg::ref_ptr<Anim::Transform>> made;
        made.reserve(plan.mNodes.size());
        for (const NifScene::Skeleton::BoneNode& bone : plan.mNodes)
        {
            auto transform = Anim::Transform::create();
            transform->translation
                = vsg::vec3(bone.mLocal.mTranslation[0], bone.mLocal.mTranslation[1], bone.mLocal.mTranslation[2]);
            transform->setScale(bone.mLocal.mScale);
            // The file's rotation is row-major; Anim::Transform's is transposed (vsgAdapters::nif::convertTrafo).
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    transform->rotation(j, i) = bone.mLocal.mRotation[static_cast<std::size_t>(i * 3 + j)];
            if (!bone.mName.empty())
                vsgUtil::setName(*transform, bone.mName);
            // Parents come before children in a plan, so the parent is made.
            if (bone.mParent == NifScene::sNone || bone.mParent >= made.size())
                root->addChild(transform);
            else
                made[bone.mParent]->addChild(transform);
            made.push_back(transform);
        }
        return root;
    }

    vsg::ref_ptr<vsg::Node> buildActorNode(const openeden::sdk::game::ActorAppearance& appearance,
        const VFS::Manager& vfs, const SkyrimModel::BuildContext& context, Report& report)
    {
        report = {};
        Totals& sum = totals();

        NifScene::Scene skeletonScene;
        std::string error;
        if (!readScene(vfs, appearance.skeletonPath, skeletonScene, error))
        {
            report.mFailure = "skeleton '" + appearance.skeletonPath + "': " + error;
            sum.mActorsFailed.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        const NifScene::Skeleton::Plan plan = NifScene::Skeleton::plan(skeletonScene);
        if (plan.mNodes.empty())
        {
            report.mFailure = "skeleton '" + appearance.skeletonPath + "' has no bones";
            sum.mActorsFailed.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        report.mBones = static_cast<std::uint32_t>(plan.mNodes.size());
        report.mSkeletonProblems = plan.mUnreachable + plan.mNonFiniteTransforms + plan.mDuplicateNames;

        vsg::ref_ptr<vsg::Group> root = buildSkeletonNodes(plan);

        const bool lipSync = Settings::game().mNeutralLipSync;
        for (const openeden::sdk::game::ActorPart& part : appearance.parts)
        {
            ++report.mPartsAsked;
            NifScene::Scene scene;
            if (!readScene(vfs, part.modelPath, scene, error))
            {
                ++report.mPartsUnreadable;
                Log(Debug::Verbose) << "Skyrim actor part '" << part.modelPath << "': " << error;
                continue;
            }
            const NifScene::Skinned::SkinnedModel skinned = NifScene::Skinned::flattenSkinned(scene);
            if (skinned.mShapes.empty())
            {
                ++report.mPartsWithoutSkin;
                continue;
            }
            SkyrimModel::BuildContext partContext = context;
            partContext.mFilename = part.modelPath;
            partContext.mMorphableFace = lipSync;
            SkyrimModel::BuildResult built = SkyrimModel::buildSkinnedNode(skinned, partContext);
            for (const vsg::ref_ptr<vsg::vec3Array>& face : built.mFaceShapes)
            {
                if (report.mLip)
                    break;
                // The head: the face shader's shape whose vertex count a race head file has. The file's morph deltas are
                // added to the shape's own vertices, so the NPC's own face is kept (judged in frames, task 0146).
                report.mLip = SkyrimLip::HeadMorph::make(SkyrimLip::headFileFor(vfs, face->size()), face);
                if (!report.mLip)
                    Log(Debug::Verbose) << "Skyrim lip sync: no head morph file for a face of " << face->size() << " vertices";
            }
            if (!built.mNode)
            {
                ++report.mPartsWithoutSkin;
                continue;
            }
            root->addChild(built.mNode);
            ++report.mPartsDrawn;
            report.mDraws += built.mDraws;
            report.mTriangles += skinned.mReport.mTriangles;
        }

        Anim::Contents contents;
        contents.add(Anim::Contents::Skins | Anim::Contents::Controllers | Anim::Contents::DynamicBounds);
        auto meta = vsg::ref_ptr{ new Anim::Meta(contents) };
        meta->attachTo(*root);

        sum.mActors.fetch_add(1, std::memory_order_relaxed);
        sum.mParts.fetch_add(report.mPartsAsked, std::memory_order_relaxed);
        sum.mPartsDrawn.fetch_add(report.mPartsDrawn, std::memory_order_relaxed);
        sum.mPartsUnreadable.fetch_add(report.mPartsUnreadable, std::memory_order_relaxed);
        sum.mPartsWithoutSkin.fetch_add(report.mPartsWithoutSkin, std::memory_order_relaxed);
        sum.mBones.fetch_add(report.mBones, std::memory_order_relaxed);
        sum.mDraws.fetch_add(report.mDraws, std::memory_order_relaxed);
        sum.mTriangles.fetch_add(report.mTriangles, std::memory_order_relaxed);
        return root;
    }

    std::string describeTotals()
    {
        const Totals& t = totals();
        std::ostringstream out;
        out << "Skyrim actors: " << t.mActors.load() << " built (" << t.mActorsFailed.load() << " failed), "
            << t.mBones.load() << " bones, " << t.mPartsDrawn.load() << " of " << t.mParts.load()
            << " parts drawn (" << t.mPartsUnreadable.load() << " unreadable, " << t.mPartsWithoutSkin.load()
            << " with no skinned shape), " << t.mDraws.load() << " draws, " << t.mTriangles.load() << " triangles";
        return out.str();
    }
}
