#include "distantobjects.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>

#include <vsg/commands/Dispatch.h>
#include <vsg/core/Visitor.h>
#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>
#include <vsg/threading/OperationThreads.h>
#include <vsg/utils/SharedObjects.h>

#include <components/debug/debuglog.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/lod/clusterplan.hpp>
#include <components/misc/resourcehelpers.hpp>
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
            const std::vector<Lod::ClusterRef>* clusterRefs)
        {
            auto objects = vsg::Group::create();
            vsg::dbox bounds;
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
                lod->addChild(prototype);
                transform->addChild(lod);
                objects->addChild(transform);

                const vsg::dvec3 center = matrix * modelBound.center;
                const double radius = modelBound.radius * ref.mScale;
                bounds.add(center - vsg::dvec3(radius, radius, radius));
                bounds.add(center + vsg::dvec3(radius, radius, radius));
            }
            if (objects->children.empty())
                return {};
            const vsg::dvec3 boundsCenter = (bounds.min + bounds.max) * 0.5;
            auto cull = vsg::CullGroup::create(vsg::dsphere(boundsCenter, vsg::length(bounds.max - boundsCenter)));
            cull->children = objects->children;
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
        vsg::ref_ptr<vsg::Node> chunk;

        ChunkBuild(const MWAnim::Context& in_context, const MWWorld::ESMStore& in_store,
            const std::pair<int, int>& in_cell, std::set<ESM::RefNum> in_disabled, float in_minSize,
            unsigned int in_generation, std::shared_ptr<const std::vector<Lod::ClusterRef>> in_clusterRefs)
            : context(in_context)
            , store(in_store)
            , cell(in_cell)
            , disabled(std::move(in_disabled))
            , minSize(in_minSize)
            , generation(in_generation)
            , clusterRefs(std::move(in_clusterRefs))
        {
        }

        void operate() override
        {
            try
            {
                chunk = buildChunk(context, store, cell, disabled, minSize, clusterRefs.get());
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to build distant objects for cell " << cell.first << ", " << cell.second
                                  << ": " << e.what();
            }
        }
    };

    DistantObjects::DistantObjects(
        const MWAnim::Context& context, const MWWorld::ESMStore& store, float cellSize, float minSize)
        : mContext(context)
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
                mContext, mStore, cell, leftOut, mMinSize, mGeneration, clusterRefsOf(cell)) };
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
    }
}
