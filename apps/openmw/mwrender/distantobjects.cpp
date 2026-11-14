#include "distantobjects.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

#include <vsg/commands/Dispatch.h>
#include <vsg/core/Visitor.h>
#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>
#include <vsg/threading/OperationThreads.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/bones.hpp>
#include <components/animation/context.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/lod/clusterplan.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/sceneryanim.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/distancelod.hpp>
#include <components/vsgutil/operation.hpp>

#include "../mwworld/esmstore.hpp"

namespace MWRender
{
    namespace
    {
        // Built chunks kept after their cell leaves range; chosen here, about a ring of cells at a viewing distance of
        // six cells (OpenMW expired its chunk cache by time instead).
        constexpr std::size_t sCachedChunks = 64;
        // How far the camera moves before the cells in range are worked out again (an eighth of a cell).
        constexpr float sUpdateFraction = 1.f / 8.f;

        // Finds whether a node holds a compute dispatch, which only particle systems have.
        class FindDispatch : public vsg::Visitor
        {
        public:
            bool found = false;
            void apply(vsg::Object& object) override
            {
                if (!found)
                    object.traverse(*this);
            }
            void apply(vsg::Command& command) override
            {
                found = found || dynamic_cast<vsg::Dispatch*>(&command) != nullptr;
            }
        };

        // Switches off a NIF particle system's switch. Nothing here runs the particles' controllers (MWAnim::create
        // does for placed objects), and these models are read from this object's own cache, so changing them is safe.
        class HideParticles : public vsg::Visitor
        {
        public:
            void apply(vsg::Object& object) override { object.traverse(*this); }
            void apply(vsg::Switch& node) override
            {
                FindDispatch find;
                for (const auto& child : node.children)
                    if (child.node)
                        child.node->accept(find);
                if (!find.found)
                {
                    node.traverse(*this);
                    return;
                }
                for (auto& child : node.children)
                    child.mask = vsg::MASK_OFF;
            }
        };

        // Task 0096 stage d: the most animated objects one chunk holds. Every one has a copy of its model of its own
        // (the clone of what carries a controller) and keeps controllers running; the rest of a crowded cell stays
        // frozen as before. A cell of Morrowind holds a handful of lava planes or Ghostgate fence segments, so this is
        // a guard against a cell of a mod, not a limit the vanilla data reaches.
        constexpr std::size_t sMaxAnimatedPerChunk = 128;

        // Whether a model has a particle system (a compute dispatch): such models stay frozen with their particles
        // off, as before; their controllers need the particle system's own link, which a chunk does not make.
        bool hasParticles(vsg::Node& node)
        {
            FindDispatch find;
            node.accept(find);
            return find.found;
        }

        // What makeAnimatedCopy found for one object.
        struct AnimatedCopy
        {
            // The model to draw: the prototype itself when nothing moves.
            vsg::ref_ptr<vsg::Node> node;
            AnimatedChunk::Controllers controllers;
            AnimatedChunk::Controllers skins;
            // Why nothing moves, for the census (OPENEDEN_FARANIM_CENSUS); null when something does.
            const char* reason = nullptr;
            // Census only: the bones animated by the model's keyframe file (skinned models).
            std::size_t kfBones = 0;
        };

        // Task 0096 stage e: whether a census line is written for each object (a developer trace, off by default).
        bool farAnimationCensus()
        {
            static const bool census = std::getenv("OPENEDEN_FARANIM_CENSUS") != nullptr;
            return census;
        }

        // Gives an object a copy of its model whose auto-play controllers a chunk can run, and returns them as
        // (controller, target) pairs; returns `prototype` itself, and no controllers, when the model has none that play
        // by themselves, or has particles or a skin whose bones cannot be found. A skinned model (banners and flags,
        // stage e) is read again with the actor options, as a placed object that animates is, so that its bones are
        // there; its skin controllers are returned apart, to be run after the others. Runs on an operation thread, like
        // the rest of buildChunk, and reads only the prototypes.
        AnimatedCopy makeAnimatedCopy(const MWAnim::Context& context, const vsg::ref_ptr<vsg::Node>& prototype,
            const std::string& path, std::uint64_t id, bool restOnly = false)
        {
            AnimatedCopy out;
            out.node = prototype;
            vsg::ref_ptr<vsg::Node> node = prototype;
            MWAnim::CloneResult result = MWAnim::cloneIfRequired(node);
            // Task 0096 stage f: a model with a keyframe file is read as an animated model even without a skin, so
            // that its keyframes have bones to move (the setting decides; the rule is SceneryKeyframes::applies).
            const bool keyframes = (restOnly || Settings::game().mAnimatePlacedSceneryKeyframes)
                && context.hasKeyframeFile && context.hasKeyframeFile(path);
            if (result.controllers.empty() && result.clonedControllers.empty() && !keyframes)
            {
                out.reason = "no controllers";
                return out;
            }
            const auto isSkin = [](const Anim::Controller* controller) {
                return dynamic_cast<const Anim::Skin*>(controller) != nullptr;
            };
            const bool skinned = std::any_of(result.clonedControllers.begin(), result.clonedControllers.end(),
                [&isSkin](const auto& pair) { return isSkin(pair.first); });
            std::optional<Anim::Bones> bones;
            // Task 0096 stage g: the rest pose only (no controllers) is for skinned models alone.
            if (restOnly && !skinned)
            {
                out.reason = "no skin";
                return out;
            }
            if (skinned || keyframes)
            {
                vsg::ref_ptr<vsg::Node> actor;
                try
                {
                    actor = context.readActor(path);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to load animated distant object model '" << path
                                        << "': " << e.what();
                }
                // These models are shared with the placed objects of the cells, so nothing in them is changed here
                // (HideParticles is not applied): a skinned model with particles stays frozen.
                if (!actor || hasParticles(*actor))
                {
                    out.reason = actor ? "skinned with particles" : "skinned, unreadable";
                    return out;
                }
                if (farAnimationCensus())
                {
                    // How many bones the model's own keyframe file animates (the file a placed object reads with
                    // readAnimation): what would move the skin if anything played it.
                    vsg::ref_ptr<Anim::ControllerMap> kf = context.readAnimation(path);
                    out.kfBones = kf ? kf->map.size() : 0;
                }
                node = actor;
                result = MWAnim::cloneIfRequired(node);
                bones.emplace(MWAnim::Bones(*node));
            }
            Anim::Context linkContext{ {}, { node.get() }, bones ? &*bones : nullptr, &context.mask };
            AnimatedChunk::Controllers found;
            AnimatedChunk::Controllers skins;
            result.link(linkContext, [&](const Anim::Controller* controller, vsg::Object* target) {
                if (controller == nullptr || target == nullptr)
                    return;
                if (isSkin(controller))
                    skins.emplace_back(
                        vsg::ref_ptr<const Anim::Controller>(controller), vsg::ref_ptr<vsg::Object>(target));
                else if (controller->hints.autoPlay)
                    found.emplace_back(
                        vsg::ref_ptr<const Anim::Controller>(controller), vsg::ref_ptr<vsg::Object>(target));
            });
            if (restOnly)
            {
                // A model drawn in its bind pose lies folded flat (a flag); a placed Activator is posed by the first
                // frame of its idle (refreshIdleAnims). Pose this copy the same way, once, and move nothing after.
                if (!bones || skins.empty())
                {
                    out.reason = "no skin";
                    return out;
                }
                MWAnim::Play play(*bones);
                vsg::ref_ptr<Anim::ControllerMap> sources = context.readAnimation(path);
                if (!sources)
                {
                    out.reason = "no keyframes";
                    return out;
                }
                play.addSingleAnimSource(sources, path);
                if (!MWAnim::SceneryKeyframes::startAtRest(play))
                {
                    out.reason = "no idle";
                    return out;
                }
                for (const auto& [skin, target] : skins)
                    skin->run(*target, 0.f);
                out.node = node;
                out.reason = "rest pose";
                return out;
            }
            if (found.empty() && keyframes && bones)
            {
                // The model has a keyframe file and no animation of its own: loop its group, in a Play this copy owns.
                auto play = std::make_shared<MWAnim::Play>(*bones);
                vsg::ref_ptr<Anim::ControllerMap> sources = context.readAnimation(path);
                if (sources)
                    play->addSingleAnimSource(sources, path);
                const std::string group
                    = sources ? MWAnim::SceneryKeyframes::chooseGroup(*sources->tags) : std::string();
                if (MWAnim::SceneryKeyframes::start(*play, group, id))
                {
                    if (farAnimationCensus())
                        Log(Debug::Info) << "far animation census: keyframes of " << path << " play group '" << group
                                         << "' from phase " << MWAnim::SceneryKeyframes::phase(id);
                    auto driver = vsg::ref_ptr<MWAnim::SceneryKeyframes::Driver>(
                        new MWAnim::SceneryKeyframes::Driver(play.get(), play));
                    found.emplace_back(vsg::ref_ptr<const Anim::Controller>(driver), vsg::ref_ptr<vsg::Object>(node));
                }
            }
            if (found.empty())
            {
                out.reason = skins.empty() ? "no auto-play controllers" : "skinned, no auto-play controllers";
                return out;
            }
            // The skin writes the bone matrices from the first frame on: run it once now (the chunk is not shared
            // yet), so the pose before the first update is the model's own, not the arrays' initial values.
            for (const auto& [skin, target] : skins)
                skin->run(*target, 0.f);
            out.node = node;
            out.controllers = std::move(found);
            out.skins = std::move(skins);
            return out;
        }

        std::string_view getModel(int type, const ESM::RefId& id, const MWWorld::ESMStore& store)
        {
            switch (type)
            {
                case ESM::REC_STAT:
                    return store.get<ESM::Static>().searchStatic(id)->mModel;
                case ESM::REC_ACTI:
                    return store.get<ESM::Activator>().searchStatic(id)->mModel;
                case ESM::REC_DOOR:
                    return store.get<ESM::Door>().searchStatic(id)->mModel;
                default:
                    return {};
            }
        }

        // One cell's references as the content files leave them: OpenMW's collectESM3References.
        std::map<ESM::RefNum, ESM::CellRef> collectReferences(
            const std::pair<int, int>& cellIndex, const MWWorld::ESMStore& store)
        {
            std::map<ESM::RefNum, ESM::CellRef> refs;
            const ESM::Cell* cell = store.get<ESM::Cell>().searchStatic(cellIndex.first, cellIndex.second);
            if (!cell)
                return refs;
            ESM::ReadersCache readers;
            for (std::size_t i = 0; i < cell->mContextList.size(); ++i)
            {
                try
                {
                    const auto index = static_cast<std::size_t>(cell->mContextList[i].index);
                    const ESM::ReadersCache::BusyItem reader = readers.get(index);
                    cell->restore(*reader, static_cast<int>(i));
                    ESM::CellRef ref;
                    ESM::MovedCellRef movedRef;
                    bool deleted = false;
                    bool moved = false;
                    while (ESM::Cell::getNextRef(
                        *reader, ref, deleted, movedRef, moved, ESM::Cell::GetNextRefMode::LoadOnlyNotMoved))
                    {
                        if (moved
                            || std::find(cell->mMovedRefs.begin(), cell->mMovedRefs.end(), ref.mRefNum)
                                != cell->mMovedRefs.end())
                            continue;
                        if (deleted)
                        {
                            refs.erase(ref.mRefNum);
                            continue;
                        }
                        if (isDistantObjectType(store.findStatic(ref.mRefID)))
                            refs.insert_or_assign(ref.mRefNum, ref);
                    }
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to collect references from cell \"" << cell->getDescription()
                                        << "\": " << e.what();
                }
            }
            for (const auto& [ref, deleted] : cell->mLeasedRefs)
            {
                if (deleted)
                    refs.erase(ref.mRefNum);
                else if (isDistantObjectType(store.findStatic(ref.mRefID)))
                    refs.insert_or_assign(ref.mRefNum, ref);
            }
            return refs;
        }

        // Builds one cell's chunk, not yet compiled; null when it has nothing to draw. Runs on an operation thread: it
        // reads the store, the content files and the models, and touches nothing the main thread writes.
        vsg::ref_ptr<vsg::Node> buildChunk(const MWAnim::Context& context, const MWWorld::ESMStore& store,
            const std::pair<int, int>& cell, const std::set<ESM::RefNum>& disabled, float minSize,
            const std::vector<Lod::ClusterRef>* clusterRefs, bool animate)
        {
            auto objects = vsg::Group::create();
            vsg::dbox bounds;
            vsg::ref_ptr<AnimatedChunk> animated;
            if (animate)
                animated = AnimatedChunk::create();
            std::map<const vsg::Node*, bool> particleModels;
            std::map<const vsg::Node*, vsg::ref_ptr<vsg::Node>> restModels;
            for (const auto& [refNum, ref] : collectReferences(cell, store))
            {
                if (disabled.contains(refNum) || Misc::ResourceHelpers::isHiddenMarker(ref.mRefID))
                    continue;
                const int type = store.findStatic(ref.mRefID);
                // A baked object cluster draws this static (task 0059): the cluster lists it for this cell.
                if (clusterRefs != nullptr && (type == ESM::REC_STAT || type == ESM::REC_ACTI)
                    && Lod::clusterDrawsReference(*clusterRefs,
                        Lod::ClusterRef{ refNum.mIndex, static_cast<std::uint32_t>(refNum.mContentFile) }))
                    continue;
                const std::string_view model = getModel(type, ref.mRefID, store);
                if (model.empty())
                    continue;

                vsg::ref_ptr<vsg::Node> prototype;
                try
                {
                    prototype = context.readNode(
                        std::string(Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)).value()));
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "Failed to load distant object model '" << model << "': " << e.what();
                    continue;
                }
                if (!prototype)
                    continue;
                HideParticles hideParticles;
                prototype->accept(hideParticles);

                const vsg::dsphere modelBound = vsgUtil::getBounds(prototype);
                const vsg::vec3 position(ref.mPos.pos[0], ref.mPos.pos[1], ref.mPos.pos[2]);
                const vsg::vec3 rotation(ref.mPos.rot[0], ref.mPos.rot[1], ref.mPos.rot[2]);
                const vsg::dmat4 matrix = distantObjectMatrix(position, rotation, ref.mScale);

                auto transform = vsg::MatrixTransform::create(matrix);
                auto lod = vsgUtil::DistanceLOD::create();
                lod->center = vsg::vec3(modelBound.center);
                // The range is in the transform's units, which the scale above it divides distance by.
                lod->ranges
                    = { { 0.f, distantObjectMaxDistance(static_cast<float>(modelBound.radius), 1.f, minSize) } };
                vsg::ref_ptr<vsg::Node> drawn = prototype;
                AnimatedChunk::Controllers controllers;
                AnimatedChunk::Controllers skins;
                if (animated && animated->objectCount() < sMaxAnimatedPerChunk)
                {
                    auto particles = particleModels.find(prototype.get());
                    if (particles == particleModels.end())
                        particles = particleModels.emplace(prototype.get(), hasParticles(*prototype)).first;
                    if (!particles->second)
                    {
                        AnimatedCopy copy = makeAnimatedCopy(context, prototype,
                            std::string(Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)).value()),
                            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(refNum.mContentFile)) << 32)
                                | refNum.mIndex);
                        drawn = copy.node;
                        controllers = std::move(copy.controllers);
                        skins = std::move(copy.skins);
                        if (farAnimationCensus())
                            Log(Debug::Info)
                                << "far animation census: " << model << " "
                                << (copy.reason != nullptr ? copy.reason : "animated") << " controllers "
                                << controllers.size() << " skins " << skins.size() << " kfbones " << copy.kfBones
                                << " at " << ref.mPos.pos[0] << " " << ref.mPos.pos[1] << " " << ref.mPos.pos[2];
                    }
                    else if (farAnimationCensus())
                        Log(Debug::Info) << "far animation census: " << model << " particles";
                }
                if (drawn == prototype)
                {
                    // Task 0096 stage g: a skinned model with a keyframe file would draw in its bind pose (a flag lies
                    // folded flat); give it the pose its idle has on the first frame, once per model in this chunk.
                    auto rest = restModels.find(prototype.get());
                    if (rest == restModels.end())
                    {
                        vsg::ref_ptr<vsg::Node> posed = prototype;
                        const std::string path(
                            Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)).value());
                        if (context.hasKeyframeFile && context.hasKeyframeFile(path) && !hasParticles(*prototype))
                        {
                            AnimatedCopy copy = makeAnimatedCopy(context, prototype, path, 0, true);
                            posed = copy.node;
                            if (farAnimationCensus() && copy.reason != nullptr)
                                Log(Debug::Info) << "far animation census: " << model << " rest: " << copy.reason;
                        }
                        rest = restModels.emplace(prototype.get(), posed).first;
                    }
                    drawn = rest->second;
                }
                lod->addChild(drawn);
                transform->addChild(lod);
                objects->addChild(transform);

                const vsg::dvec3 center = matrix * modelBound.center;
                if (!controllers.empty())
                    animated->addObject(center,
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(refNum.mContentFile)) << 32)
                            | refNum.mIndex,
                        controllers, skins, skins.empty() ? vsg::ref_ptr<vsg::Node>() : drawn);
                const double radius = modelBound.radius * ref.mScale;
                bounds.add(center - vsg::dvec3(radius, radius, radius));
                bounds.add(center + vsg::dvec3(radius, radius, radius));
            }
            if (objects->children.empty())
                return {};
            const vsg::dvec3 boundsCenter = (bounds.min + bounds.max) * 0.5;
            auto cull = vsg::CullGroup::create(vsg::dsphere(boundsCenter, vsg::length(bounds.max - boundsCenter)));
            cull->children = objects->children;
            if (animated && animated->objectCount() > 0)
            {
                animated->finish();
                cull->setObject(AnimatedChunk::sKey, animated);
            }
            return cull;
        }
    }

    std::map<ESM::RefNum, ESM::CellRef> collectDistantReferences(
        const std::pair<int, int>& cellIndex, const MWWorld::ESMStore& store)
    {
        return collectReferences(cellIndex, store);
    }

    std::string_view distantObjectModel(int type, const ESM::RefId& id, const MWWorld::ESMStore& store)
    {
        return getModel(type, id, store);
    }

    // One cell's chunk being built on an operation thread; the main thread reads chunk only once done is set.
    struct DistantObjects::ChunkBuild : public vsgUtil::Operation
    {
        const MWAnim::Context& context;
        const MWWorld::ESMStore& store;
        std::pair<int, int> cell;
        std::set<ESM::RefNum> disabled;
        float minSize;
        unsigned int generation;
        std::shared_ptr<const std::vector<Lod::ClusterRef>> clusterRefs;
        bool animate;
        vsg::ref_ptr<vsg::Node> chunk;

        ChunkBuild(const MWAnim::Context& in_context, const MWWorld::ESMStore& in_store,
            const std::pair<int, int>& in_cell, std::set<ESM::RefNum> in_disabled, float in_minSize,
            unsigned int in_generation, std::shared_ptr<const std::vector<Lod::ClusterRef>> in_clusterRefs,
            bool in_animate)
            : context(in_context)
            , store(in_store)
            , cell(in_cell)
            , disabled(std::move(in_disabled))
            , minSize(in_minSize)
            , generation(in_generation)
            , clusterRefs(std::move(in_clusterRefs))
            , animate(in_animate)
        {
        }

        void operate() override
        {
            try
            {
                chunk = buildChunk(context, store, cell, disabled, minSize, clusterRefs.get(), animate);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to build distant objects for cell " << cell.first << ", " << cell.second
                                  << ": " << e.what();
            }
        }
    };

    DistantObjects::DistantObjects(const MWAnim::Context& context, const MWWorld::ESMStore& store, float cellSize,
        float minSize, const AnimationOptions& animation)
        : mContext(context)
        , mAnimation(animation)
        , mStore(store)
        , mCellSize(cellSize)
        , mMinSize(minSize)
        , mThreads(vsg::OperationThreads::create(1))
        , mRoot(vsg::Group::create())
        , mTracker(sCachedChunks)
    {
        // Its own cache: HideParticles changes these models, which placed objects must not see.
        auto nodeOptions = vsg::Options::create(*mContext.nodeOptions);
        nodeOptions->sharedObjects = vsg::SharedObjects::create();
        mContext.nodeOptions = nodeOptions;
    }

    DistantObjects::~DistantObjects()
    {
        waitForBuilds();
        setShownChunks({});
    }

    void DistantObjects::waitForBuilds()
    {
        for (const auto& build : mBuilds)
            build->wait();
    }

    void DistantObjects::hide()
    {
        setShownChunks({});
        mHasUpdated = false;
    }

    void DistantObjects::setActiveGrid(const ActiveCellGrid& grid)
    {
        mActiveGrid = grid;
        mWantedChanged = true;
    }

    void DistantObjects::setCellUnbuilt(const std::pair<int, int>& cell, bool unbuilt)
    {
        const bool changed = unbuilt ? mUnbuilt.insert(cell).second : mUnbuilt.erase(cell) > 0;
        if (changed)
            mWantedChanged = true;
    }

    void DistantObjects::forget(const Cells& cells)
    {
        ++mGeneration;
        for (const auto& cell : cells)
        {
            mTracker.forget(cell);
            mCellGenerations[cell] = mGeneration;
            mRebuilding.insert(cell);
            mShownSkipsStatics.erase(cell);
        }
        mWantedChanged = true;
    }

    bool DistantObjects::setDisabled(ESM::RefNum ref, bool disabled, const Cells& cells)
    {
        const bool changed = disabled ? mDisabled.insert(ref).second : mDisabled.erase(ref) > 0;
        if (changed)
            forget(cells);
        return changed;
    }

    bool DistantObjects::staticsReady(const std::pair<int, int>& cell) const
    {
        if (!mHasUpdated)
            return true; // hidden or not started: the live objects draw nothing, there is nothing to wait for
        if (mUnbuilt.contains(cell))
            return false; // task 0060: the live objects of this cell are still being built
        std::optional<bool> shown;
        if (const auto it = mShownSkipsStatics.find(cell); it != mShownSkipsStatics.end())
            shown = it->second;
        return Lod::liveStaticsReady(mClustered.contains(cell), mRebuilding.contains(cell) || mTracker.isPending(cell),
            shown, std::binary_search(mWantedCells.begin(), mWantedCells.end(), cell));
    }

    std::shared_ptr<const std::vector<Lod::ClusterRef>> DistantObjects::clusterRefsOf(
        const std::pair<int, int>& cell) const
    {
        const auto it = mClustered.find(cell);
        return it == mClustered.end() ? nullptr : it->second;
    }

    void DistantObjects::setClusteredCells(const ClusteredCells& cells)
    {
        // A cell changed when it entered or left the set or its list of references differs.
        Cells changed;
        for (const auto& [cell, refs] : cells)
        {
            const auto old = mClustered.find(cell);
            if (old == mClustered.end() || !(*old->second == *refs))
                changed.push_back(cell);
        }
        for (const auto& [cell, refs] : mClustered)
            if (!cells.contains(cell))
                changed.push_back(cell);
        mClustered = cells;
        if (!changed.empty())
            forget(changed);
    }

    bool DistantObjects::blacklist(ESM::RefNum ref, const Cells& cells)
    {
        const bool changed = mBlacklist.insert(ref).second;
        if (changed)
            forget(cells);
        return changed;
    }

    void DistantObjects::clear()
    {
        mDisabled.clear();
        mBlacklist.clear();
        mCellGenerations.clear();
        mClustered.clear();
        mRebuilding.clear();
        mShownSkipsStatics.clear();
        mWantedCells.clear();
        mOldestValidBuild = ++mGeneration;
        mTracker.clear();
        setShownChunks({});
        mHasUpdated = false;
    }

    void DistantObjects::updateWanted()
    {
        // The ring next to the active grid is built too, and shown once the grid moves off it (update()).
        const ActiveCellGrid builtGrid{ mActiveGrid.centre, mActiveGrid.halfGrid - 1 };
        std::set<ESM::RefNum> leftOut = mDisabled;
        leftOut.insert(mBlacklist.begin(), mBlacklist.end());
        const auto wanted = distantObjectCells(mCameraPosition, mCellSize, mViewDistance, builtGrid);
        mWantedCells = wanted;
        // A forgotten cell that is no longer in range will never be built: it has nothing to wait for.
        std::erase_if(mRebuilding, [&wanted](const std::pair<int, int>& cell) {
            return !std::binary_search(wanted.begin(), wanted.end(), cell);
        });
        for (const auto& cell : mTracker.setWanted(wanted))
        {
            auto build = vsg::ref_ptr{ new ChunkBuild(
                mContext, mStore, cell, leftOut, mMinSize, mGeneration, clusterRefsOf(cell), mAnimation.mEnabled) };
            mBuilds.push_back(build);
            mThreads->add(build);
        }
    }

    void DistantObjects::update(const vsg::vec3& cameraPosition, float viewDistance)
    {
        bool changed = collectBuilds();
        if (!mHasUpdated || mWantedChanged || viewDistance != mViewDistance
            || vsg::length(vsg::vec2(cameraPosition.x - mCameraPosition.x, cameraPosition.y - mCameraPosition.y))
                >= mCellSize * sUpdateFraction)
        {
            changed = true;
            mHasUpdated = true;
            mWantedChanged = false;
            mCameraPosition = cameraPosition;
            mViewDistance = viewDistance;
            updateWanted();
        }
        if (!changed)
            return;

        vsg::Group::Children chunks;
        for (const auto& [cell, chunk] : mTracker.shown())
            if (chunk && (!mActiveGrid.contains(cell) || mUnbuilt.contains(cell)))
                chunks.push_back(chunk);
        setShownChunks(std::move(chunks));
    }

    bool DistantObjects::collectBuilds()
    {
        return std::erase_if(mBuilds, [this](const vsg::ref_ptr<ChunkBuild>& build) {
            if (!build->done)
                return false;
            // A reference changed or the game was cleared while this ran; the tracker waits for the newer build.
            const auto generation = mCellGenerations.find(build->cell);
            if (build->generation < mOldestValidBuild
                || (generation != mCellGenerations.end() && build->generation < generation->second))
                return true;
            vsg::ref_ptr<vsg::Node> chunk = build->chunk;
            if (chunk && !mContext.compileContext->compile(chunk))
                chunk = {};
            mTracker.finish(build->cell, chunk);
            mRebuilding.erase(build->cell);
            mShownSkipsStatics[build->cell] = build->clusterRefs != nullptr;
            return true;
        }) > 0;
    }

    void DistantObjects::setShownChunks(vsg::Group::Children chunks)
    {
        for (const auto& child : mRoot->children)
            if (std::find(chunks.begin(), chunks.end(), child) == chunks.end())
                mContext.compileContext->detach(child);
        mRoot->children = std::move(chunks);
        // The animated objects of the chunks now shown (a chunk's node holds its AnimatedChunk).
        mAnimated.clear();
        if (mAnimation.mEnabled)
        {
            static const std::string key = AnimatedChunk::sKey;
            for (const auto& child : mRoot->children)
                if (child)
                    if (auto* animated = child->getObject<AnimatedChunk>(key))
                        mAnimated.emplace_back(animated);
        }
    }

    void DistantObjects::updateAnimation(const vsg::vec3& cameraPosition, double dt)
    {
        if (!mAnimation.mEnabled)
            return;
        static const bool trace = std::getenv("OPENEDEN_FARANIM_TRACE") != nullptr;
        const auto started = trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        ++mAnimationFrame;
        mAnimationStats = {};
        const std::size_t count = mAnimated.size();
        if (count == 0)
            return;
        Lod::AnimBudget budget{ mAnimation.mBudget, mAnimation.mBudget };
        const vsg::dvec3 camera(cameraPosition.x, cameraPosition.y, cameraPosition.z);
        // The start moves with the frame, so a budget that cannot run every chunk leaves a different one out each time.
        const std::size_t start = mAnimationFrame % count;
        for (std::size_t n = 0; n < count; ++n)
        {
            AnimatedChunk& chunk = *mAnimated[(start + n) % count];
            mAnimationStats.mControllersRun
                += chunk.update(camera, dt, mAnimation.mStopDistance, mAnimationFrame, budget);
            mAnimationStats.mObjects += chunk.objectCount();
        }
        mAnimationStats.mChunks = count;
        // A developer trace (task 0096 stage d): one line every 60 frames, to see the rates and the cost in a run.
        if (trace)
        {
            mAnimationMicroseconds
                += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
            mAnimationControllers += mAnimationStats.mControllersRun;
            if (mAnimationFrame % 60 == 0)
            {
                Log(Debug::Info) << "far animation: frame " << mAnimationFrame << ", " << mAnimationStats.mChunks
                                 << " chunks, " << mAnimationStats.mObjects << " objects, "
                                 << mAnimationControllers / 60.0 << " controllers run a frame, "
                                 << mAnimationMicroseconds / 60.0 << " us a frame in updateAnimation";
                mAnimationMicroseconds = 0.0;
                mAnimationControllers = 0;
            }
        }
    }
}
