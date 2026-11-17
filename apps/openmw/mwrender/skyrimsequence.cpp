#include "skyrimsequence.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>

#include <vsg/nodes/Group.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/nifscene/build.hpp>
#include <components/vsgutil/name.hpp>

namespace MWRender::SkyrimSequence
{
    namespace
    {
        namespace Seq = NifScene::Sequences;

        std::mutex sCacheMutex;
        std::map<std::string, std::shared_ptr<const Model>>& cache()
        {
            static std::map<std::string, std::shared_ptr<const Model>> models;
            return models;
        }

        std::string lowerKey(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
    }

    std::shared_ptr<const Model> makeModel(NifScene::Scene scene, const NifScene::Flat::Limits& baseLimits)
    {
        const std::size_t open = Seq::find(scene, "Open");
        if (open == Seq::sNoSequence)
            return nullptr;
        const NifScene::Sequence& openSequence = scene.mSequences[open];
        std::vector<std::uint32_t> nodes = Seq::animatedNodes(scene, openSequence);
        const std::size_t close = Seq::find(scene, "Close");
        if (close != Seq::sNoSequence)
        {
            // A node only the Close sequence moves is moved by Open too (as its rest pose): list it as well.
            for (std::uint32_t node : Seq::animatedNodes(scene, scene.mSequences[close]))
                if (std::find(nodes.begin(), nodes.end(), node) == nodes.end())
                    nodes.push_back(node);
        }
        if (nodes.empty() || nodes.size() > kMaxParts)
            return nullptr;

        NifScene::Flat::Limits limits = baseLimits;
        limits.mAnimatedNodes = nodes;
        const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene, limits);

        auto model = std::make_shared<Model>();
        model->mOpen = open;
        model->mClose = close;
        const float seconds = Seq::duration(openSequence);
        model->mSeconds = std::clamp(seconds > 0.f ? seconds : 1.f, kMinSeconds, kMaxSeconds);
        model->mStatic.mReport = flat.mReport;
        model->mStatic.mBoundsMin = flat.mBoundsMin;
        model->mStatic.mBoundsMax = flat.mBoundsMax;
        std::map<std::uint32_t, std::size_t> partOfNode;
        for (const NifScene::Flat::Shape& shape : flat.mShapes)
        {
            if (shape.mAnimNode == 0xFFFFFFFFu)
            {
                model->mStatic.mShapes.push_back(shape);
                continue;
            }
            auto found = partOfNode.find(shape.mAnimNode);
            if (found == partOfNode.end())
            {
                found = partOfNode.emplace(shape.mAnimNode, model->mParts.size()).first;
                Model::Part part;
                part.mNode = shape.mAnimNode;
                part.mFlat.mBoundsMin = flat.mBoundsMin;
                part.mFlat.mBoundsMax = flat.mBoundsMax;
                model->mParts.push_back(std::move(part));
            }
            model->mParts[found->second].mFlat.mShapes.push_back(shape);
        }
        if (model->mParts.empty())
            return nullptr; // the sequence moves nodes that draw nothing: there is nothing to animate
        // Only the nodes and sequences are kept.
        scene.mGeometries.clear();
        scene.mSkins.clear();
        scene.mTextureSets.clear();
        scene.mMaterials.clear();
        scene.mAlphaProperties.clear();
        scene.mWarnings.clear();
        model->mScene = std::move(scene);
        return model;
    }

    std::shared_ptr<const Model> loadModel(const VFS::Manager& vfs, const std::string& path, bool surfaceMaps)
    {
        const std::string key = lowerKey(path);
        {
            std::scoped_lock lock(sCacheMutex);
            const auto found = cache().find(key);
            if (found != cache().end())
                return found->second;
        }
        std::shared_ptr<const Model> model;
        try
        {
            // The model name of a placed object already starts with "meshes/".
            const VFS::Path::Normalized named = VFS::Path::toNormalized(path);
            const VFS::Path::Normalized normalized = key.rfind("meshes/", 0) == 0 || key.rfind("meshes\\", 0) == 0
                ? named
                : Misc::ResourceHelpers::correctMeshPath(named);
            Files::IStreamPtr stream = vfs.get(normalized);
            if (stream)
            {
                NifScene::Scene scene = NifScene::readScene(std::move(stream), normalized, NifScene::SceneOptions{});
                NifScene::Flat::Limits limits;
                limits.mSurfaceMaps = surfaceMaps;
                limits.mSkinnedAtRest = true;
                model = makeModel(std::move(scene), limits);
                if (model)
                    Log(Debug::Info) << "Skyrim sequence model " << path << ": " << model->mParts.size()
                                     << " moving part(s), Open takes " << model->mSeconds << " s"
                                     << (model->mClose != Seq::sNoSequence ? ", has Close" : ", no Close");
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Skyrim sequence model " << path << " not animated: " << e.what();
        }
        std::scoped_lock lock(sCacheMutex);
        return cache().emplace(key, model).first->second;
    }

    Instance::Instance(std::shared_ptr<const Model> model, PartList parts)
        : mModel(std::move(model))
        , mParts(std::move(parts))
    {
        hints.autoPlay = true;
    }

    void Instance::setProgress(float progress, bool closing)
    {
        mTimed.store(false, std::memory_order_relaxed);
        mClosing.store(closing, std::memory_order_relaxed);
        mProgress.store(progress, std::memory_order_relaxed);
    }

    void Instance::setTarget(bool open)
    {
        mTargetOpen.store(open, std::memory_order_relaxed);
        mTimed.store(true, std::memory_order_relaxed);
    }

    void Instance::apply(float progress, bool closing) const
    {
        const NifScene::Scene& scene = mModel->mScene;
        const Seq::Pose pose = Seq::poseFor(scene, mModel->mOpen, mModel->mClose, progress, closing);
        if (pose.mSequence == Seq::sNoSequence)
            return;
        const NifScene::Sequence& sequence = scene.mSequences[pose.mSequence];
        for (const auto& [node, transform] : mParts)
        {
            if (!transform)
                continue;
            const Seq::Mat4 m = Seq::delta(scene, sequence, node, pose.mTime);
            float s = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            if (!std::isfinite(s) || s < 1e-6f)
                s = 1.f;
            transform->translation = vsg::vec3(m[12], m[13], m[14]);
            for (int column = 0; column < 3; ++column)
                transform->rotation[column]
                    = vsg::vec3(m[column * 4] / s, m[column * 4 + 1] / s, m[column * 4 + 2] / s);
            transform->setScale(s);
        }
        mApplied.store(progress, std::memory_order_relaxed);
    }

    void Instance::run(vsg::Object&, float time) const
    {
        const float dt = mLastTime < 0.f ? 0.f : time - mLastTime;
        mLastTime = time;
        float progress;
        bool closing;
        if (mTimed.load(std::memory_order_relaxed))
        {
            const bool open = mTargetOpen.load(std::memory_order_relaxed);
            mTimedProgress = Seq::stepProgress(mTimedProgress, open, dt, mModel->mSeconds);
            progress = mTimedProgress;
            closing = !open;
        }
        else
        {
            progress = mProgress.load(std::memory_order_relaxed);
            mTimedProgress = progress; // a timed motion that follows starts from where the door is
            closing = mClosing.load(std::memory_order_relaxed);
        }
        if (mHaveApplied && progress == mApplied.load(std::memory_order_relaxed) && closing == mAppliedClosing)
            return;
        mHaveApplied = true;
        mAppliedClosing = closing;
        apply(progress, closing);
    }

    Built build(const std::shared_ptr<const Model>& model, const SkyrimModel::BuildContext& context)
    {
        Built built;
        if (!model)
            return built;
        auto root = vsg::Group::create();
        if (!model->mStatic.mShapes.empty())
        {
            SkyrimModel::BuildResult statics = SkyrimModel::buildNode(model->mStatic, context);
            if (statics.mNode)
                root->addChild(statics.mNode);
        }
        Instance::PartList parts;
        for (const Model::Part& part : model->mParts)
        {
            SkyrimModel::BuildResult drawn = SkyrimModel::buildNode(part.mFlat, context);
            if (!drawn.mNode)
                continue;
            auto transform = Anim::Transform::create();
            transform->addChild(drawn.mNode);
            if (part.mNode < model->mScene.mNodes.size())
                vsgUtil::setName(*transform, model->mScene.mNodes[part.mNode].mName);
            root->addChild(transform);
            parts.emplace_back(part.mNode, transform);
        }
        if (root->children.empty())
            return built;
        built.mNode = root;
        built.mInstance = vsg::ref_ptr<Instance>(new Instance(model, std::move(parts)));
        return built;
    }
}
