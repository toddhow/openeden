#include "scene.hpp"

#include <atomic>
#include <chrono>
#include <limits>

#include <components/misc/loadtrace.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/box.h>

#include <vsg/io/Options.h>
#include <vsg/threading/OperationThreads.h>

#include <components/debug/debuglog.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/debug.hpp>
#include <components/detournavigator/heightfieldshape.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/detournavigator/updateguard.hpp>
#include <components/esmbridge/records.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/terrain/preload.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwstate/loading.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwrender/landmanager.hpp"
#include "../mwrender/rendermanager.hpp"

#include "../mwphysics/actor.hpp"
#include "../mwphysics/heightfield.hpp"
#include "../mwphysics/object.hpp"
#include "../mwphysics/physicssystem.hpp"

#include "../mwworld/actionteleport.hpp"

#include "cellstore.hpp"
#include "cellvisitors.hpp"
#include "class.hpp"
#include "esmstore.hpp"
#include "localscripts.hpp"
#include "player.hpp"
#include "preloadcell.hpp"
#include "worldimp.hpp"

namespace
{
    using MWWorld::RotationOrder;

    vsg::dquat makeActorOsgQuat(const ESM::Position& position)
    {
        return Misc::VsgMath::makeRotateFromAxis(position.rot[2], vsg::vec3(0, 0, -1));
    }

    vsg::dquat makeInversedOrderObjectOsgQuat(const ESM::Position& position)
    {
        const float xr = position.rot[0];
        const float yr = position.rot[1];
        const float zr = position.rot[2];

        return Misc::VsgMath::makeRotateFromAxis(xr, vsg::vec3(-1, 0, 0))
            * Misc::VsgMath::makeRotateFromAxis(yr, vsg::vec3(0, -1, 0))
            * Misc::VsgMath::makeRotateFromAxis(zr, vsg::vec3(0, 0, -1));
    }

    vsg::dquat makeInverseNodeRotation(const MWWorld::Ptr& ptr)
    {
        const auto& pos = ptr.getRefData().getPosition();
        return ptr.getClass().isActor() ? makeActorOsgQuat(pos) : makeInversedOrderObjectOsgQuat(pos);
    }

    vsg::dquat makeDirectNodeRotation(const MWWorld::Ptr& ptr)
    {
        const auto& pos = ptr.getRefData().getPosition();
        return ptr.getClass().isActor() ? makeActorOsgQuat(pos) : Misc::Convert::makeOsgQuat(pos);
    }

    vsg::dquat makeNodeRotation(const MWWorld::Ptr& ptr, RotationOrder order)
    {
        if (order == RotationOrder::inverse)
            return makeInverseNodeRotation(ptr);
        return makeDirectNodeRotation(ptr);
    }
    void setNodeRotation(const MWWorld::Ptr& ptr, MWRender::RenderManager& rendering, const vsg::dquat& rotation)
    {
        rendering.rotateObject(ptr, rotation);
    }

    VFS::Path::Normalized getModel(const MWWorld::Ptr& ptr)
    {
        if (Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId()))
            return {};
        return ptr.getClass().getCorrectedModel(ptr);
    }

    void addObject(const MWWorld::Ptr& ptr, const MWWorld::World& world, const std::vector<ESM::RefNum>& pagedRefs,
        MWPhysics::PhysicsSystem& physics, MWRender::RenderManager& rendering)
    {
        if (physics.getActor(ptr))
        {
            Log(Debug::Warning) << "Warning: Tried to add " << ptr.getCellRef().getRefId() << " to the scene twice";
            return;
        }

        const VFS::Path::Normalized model = getModel(ptr);
        const auto rotation = makeDirectNodeRotation(ptr);

        ESM::RefNum refnum = ptr.getCellRef().getRefNum();
        if (!refnum.hasContentFile() || !std::binary_search(pagedRefs.begin(), pagedRefs.end(), refnum))
        {
            Misc::LoadTrace::Scope trace("obj.render", static_cast<std::int64_t>(ptr.getType()));
            ptr.getClass().insertObjectRendering(ptr, model, rendering.getObjects());
        }
        setNodeRotation(ptr, rendering, rotation);

        if (ptr.getClass().useAnim())
        {
            Misc::LoadTrace::Scope trace("obj.mechanics");
            MWBase::Environment::get().getMechanicsManager()->add(ptr);
        }

        if (ptr.getClass().isActor())
            rendering.addWaterRippleEmitter(ptr);

        // Restore effect particles
        world.applyLoopingParticles(ptr);

        if (!model.empty())
        {
            Misc::LoadTrace::Scope trace("obj.physics");
            ptr.getClass().insertObject(ptr, model, rotation, physics);
        }

        Misc::LoadTrace::Scope traceLua("obj.lua");
        MWBase::Environment::get().getLuaManager()->objectAddedToScene(ptr);
        MWBase::Environment::get().getExtensionManager()->objectAddedToScene(ptr);
    }

    void addObject(const MWWorld::Ptr& ptr, const MWWorld::World& world, const MWPhysics::PhysicsSystem& physics,
        float& lowestPoint, bool isInterior, DetourNavigator::Navigator& navigator,
        const DetourNavigator::UpdateGuard* navigatorUpdateGuard = nullptr)
    {
        if (const auto object = physics.getObject(ptr))
        {
            // Find the lowest point of this collision object in world space from its AABB if interior
            // this point is used to determine the infinite fall cutoff from lowest point in the cell
            if (isInterior)
            {
                // The shape instance is unscaled, the object transform includes the scale
                const JPH::AABox localBounds = object->getShapeInstance()->mCollisionShape->GetLocalBounds();
                const vsg::dbox bounds(vsg::dvec3(Misc::Convert::toOsg(localBounds.mMin)),
                    vsg::dvec3(Misc::Convert::toOsg(localBounds.mMax)));
                const vsg::dmat4 transform = object->getTransform();
                for (unsigned int i = 0; i < 8; ++i)
                    lowestPoint
                        = std::min(lowestPoint, static_cast<float>((transform * Misc::VsgMath::corner(bounds, i)).z));
            }

            const DetourNavigator::ObjectTransform objectTransform{ ptr.getRefData().getPosition(),
                ptr.getCellRef().getScale() };

            if (ptr.getClass().isDoor() && !ptr.getCellRef().getTeleport())
            {
                const JPH::AABox localBounds = object->getShapeInstance()->mCollisionShape->GetLocalBounds();
                const float scale = ptr.getCellRef().getScale();
                const vsg::vec3 aabbMin = Misc::Convert::toOsg(localBounds.mMin) * scale;
                const vsg::vec3 aabbMax = Misc::Convert::toOsg(localBounds.mMax) * scale;

                const auto center = (aabbMax + aabbMin) * 0.5f;

                const auto distanceFromDoor = world.getMaxActivationDistance() * 0.5f;
                const auto toPoint = aabbMax.x - aabbMin.x < aabbMax.y - aabbMin.y ? vsg::vec3(distanceFromDoor, 0, 0)
                                                                                   : vsg::vec3(0, distanceFromDoor, 0);

                const vsg::dmat4 closedDoorTransform = vsg::translate(Misc::VsgMath::getTrans(object->getTransform()))
                    * Misc::VsgMath::toMatrix(Misc::Convert::makeQuaternion(ptr.getCellRef().getPosition()));

                const vsg::vec3 start(closedDoorTransform * vsg::dvec3(center + toPoint));
                const auto startPoint = physics.castRay(start, start - vsg::vec3(0, 0, 1000), { ptr }, {},
                    MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::WATER);
                const auto connectionStart = startPoint.mHit ? startPoint.mHitPos : start;

                const vsg::vec3 end(closedDoorTransform * vsg::dvec3(center - toPoint));
                const auto endPoint = physics.castRay(end, end - vsg::vec3(0, 0, 1000), { ptr }, {},
                    MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::WATER);
                const auto connectionEnd = endPoint.mHit ? endPoint.mHitPos : end;

                navigator.addObject(DetourNavigator::ObjectId(object),
                    DetourNavigator::DoorShapes(
                        object->getShapeInstance(), objectTransform, connectionStart, connectionEnd),
                    object->getTransform(), navigatorUpdateGuard);
            }
            else if (object->getShapeInstance()->mVisualCollisionType == Resource::VisualCollisionType::None)
            {
                navigator.addObject(DetourNavigator::ObjectId(object),
                    DetourNavigator::ObjectShapes(object->getShapeInstance(), objectTransform), object->getTransform(),
                    navigatorUpdateGuard);
            }
        }
        else if (physics.getActor(ptr))
        {
            const DetourNavigator::AgentBounds agentBounds = world.getPathfindingAgentBounds(ptr);
            if (!navigator.addAgent(agentBounds))
                Log(Debug::Warning) << "Agent bounds are not supported by navigator for " << ptr.toString() << ": "
                                    << agentBounds;
        }
    }

    struct InsertVisitor
    {
        MWWorld::CellStore& mCell;

        std::vector<MWWorld::Ptr> mToInsert;

        InsertVisitor(MWWorld::CellStore& cell);

        bool operator()(const MWWorld::Ptr& ptr);

        template <class AddObject>
        void insert(AddObject&& addObject);
    };

    InsertVisitor::InsertVisitor(MWWorld::CellStore& cell)
        : mCell(cell)
    {
    }

    bool InsertVisitor::operator()(const MWWorld::Ptr& ptr)
    {
        // do not insert directly as we can't modify the cell from within the visitation
        // CreatureLevList::insertObjectRendering may spawn a new creature
        mToInsert.push_back(ptr);
        return true;
    }

    template <class AddObject>
    void InsertVisitor::insert(AddObject&& addObject)
    {
        for (MWWorld::Ptr& ptr : mToInsert)
        {
            if (!ptr.mRef->isDeleted() && ptr.getRefData().isEnabled())
            {
                try
                {
                    addObject(ptr);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "failed to render '" << ptr.getCellRef().getRefId() << "': " << e.what();
                }
            }
        }
    }

    int getCellPositionDistanceToOrigin(const std::pair<int, int>& cellPosition)
    {
        return std::abs(cellPosition.first) + std::abs(cellPosition.second);
    }

    bool isCellInCollection(ESM::ExteriorCellLocation cellIndex, MWWorld::Scene::CellStoreCollection& collection)
    {
        for (auto* cell : collection)
        {
            assert(cell->getCell()->isExterior());
            if (cellIndex == cell->getCell()->getExteriorCellLocation())
                return true;
        }
        return false;
    }

    template <class Function>
    void iterateOverCellsAround(int cellX, int cellY, int range, Function&& f)
    {
        for (int x = cellX - range, lastX = cellX + range; x <= lastX; ++x)
            for (int y = cellY - range, lastY = cellY + range; y <= lastY; ++y)
                f(x, y);
    }

    void sortCellsToLoad(int centerX, int centerY, std::vector<std::pair<int, int>>& cells)
    {
        const auto getDistanceToPlayerCell = [&](const std::pair<int, int>& cellPosition) {
            return std::abs(cellPosition.first - centerX) + std::abs(cellPosition.second - centerY);
        };

        const auto getCellPositionPriority = [&](const std::pair<int, int>& cellPosition) {
            return std::make_pair(getDistanceToPlayerCell(cellPosition), getCellPositionDistanceToOrigin(cellPosition));
        };

        std::sort(cells.begin(), cells.end(), [&](const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) {
            return getCellPositionPriority(lhs) < getCellPositionPriority(rhs);
        });
    }

    int getHalfGridSize(ESM::RefId worldspace)
    {
        return ESM::isEsm4Ext(worldspace) ? Constants::ESM4CellGridRadius : Constants::CellGridRadius;
    }
}

namespace MWWorld
{
    // openeden-vsg: vsgopenmw's renderer has no object paging, so mPagedRefs stays empty and every object is
    // rendered on its own; the paging calls remain so object paging can return without changing callers.
    void Scene::removeFromPagedRefs(const Ptr& /*ptr*/) {}

    bool Scene::isPagedRef(const Ptr& ptr) const
    {
        const ESM::RefNum refnum = ptr.getCellRef().getRefNum();
        return refnum.hasContentFile() && std::binary_search(mPagedRefs.begin(), mPagedRefs.end(), refnum);
    }

    void Scene::updateObjectRotation(const Ptr& ptr, RotationOrder order)
    {
        const auto rot = makeNodeRotation(ptr, order);
        setNodeRotation(ptr, mRendering, rot);
        mPhysics->updateRotation(ptr, rot);
    }

    void Scene::updateObjectScale(const Ptr& ptr)
    {
        float scale = ptr.getCellRef().getScale();
        vsg::vec3 scaleVec(scale, scale, scale);
        ptr.getClass().adjustScale(ptr, scaleVec, true);
        mRendering.scaleObject(ptr, scaleVec);
        mPhysics->updateScale(ptr);
    }

    void Scene::update(float duration)
    {
        mPreloader->updateCache();
        preloadCells(duration);
    }

    void Scene::releaseTerrainPreload()
    {
        if (mTerrainPreloader)
            mTerrainPreloader->releaseViews();
    }

    void Scene::unloadCell(CellStore* cell, const DetourNavigator::UpdateGuard* navigatorUpdateGuard)
    {
        if (mActiveCells.find(cell) == mActiveCells.end())
            return;
        Log(Debug::Info) << "Unloading cell " << cell->getCell()->getDescription();
        // Task 0060: objects of this cell that were still waiting to be built are dropped with it. Nothing else is
        // needed for them: unloading already copes with objects that were never added (disabled ones).
        mBuildQueue.cancelGroup(cell);
        if (mBuildingCells.erase(cell) > 0 && cell->isExterior())
            mRendering.setCellObjectsPending(cell->getCell()->getGridX(), cell->getCell()->getGridY(), false);
        mBuildStartUs.erase(cell);
        if (cell->isExterior())
            mRendering.exteriorCellUnloaded(cell->getCell()->getGridX(), cell->getCell()->getGridY());
        MWBase::Environment::get().getExtensionManager()->cellUnloaded(cell->getCell()->getDescription());

        ListAndResetObjectsVisitor visitor;

        cell->forEach(visitor, true); // Include objects being teleported by Lua
        for (const auto& ptr : visitor.mObjects)
        {
            if (const auto object = mPhysics->getObject(ptr))
            {
                if (object->getShapeInstance()->mVisualCollisionType == Resource::VisualCollisionType::None)
                    mNavigator.removeObject(DetourNavigator::ObjectId(object), navigatorUpdateGuard);
                mPhysics->remove(ptr);
            }
            else if (mPhysics->getActor(ptr))
            {
                mNavigator.removeAgent(mWorld.getPathfindingAgentBounds(ptr));
                mRendering.removeActorPath(ptr);
                mPhysics->remove(ptr);
            }
            else
                ptr.mRef->mData.mPhysicsPostponed = false;
            MWBase::Environment::get().getLuaManager()->objectRemovedFromScene(ptr);
            MWBase::Environment::get().getExtensionManager()->objectRemovedFromScene(ptr);
        }

        const auto cellX = cell->getCell()->getGridX();
        const auto cellY = cell->getCell()->getGridY();

        if (cell->getCell()->isExterior())
        {
            mNavigator.removeHeightfield(vsg::ivec2(cellX, cellY), navigatorUpdateGuard);
            mPhysics->removeHeightField(cellX, cellY);
        }

        if (cell->getCell()->hasWater())
            mNavigator.removeWater(vsg::ivec2(cellX, cellY), navigatorUpdateGuard);

        ESM::visit(ESM::VisitOverload{
                       [&](const ESM::Cell& c) {
                           if (const auto pathgrid = mWorld.getStore().get<ESM::Pathgrid>().search(c))
                               mNavigator.removePathgrid(*pathgrid);
                       },
                       [&](const ESM4::Cell& /*c*/) {},
                   },
            *cell->getCell());

        MWBase::Environment::get().getMechanicsManager()->drop(cell);

        Misc::LoadTrace::Scope traceRemove("unload.rendering.removecell");
        mRendering.removeCell(cell);
        traceRemove.end();
        MWBase::Environment::get().getWindowManager()->removeCell(cell);

        mWorld.getLocalScripts().clearCell(cell);

        MWBase::Environment::get().getSoundManager()->stopSound(cell);
        mActiveCells.erase(cell);
        // Clean up any effects that may have been spawned while unloading all cells
        if (mActiveCells.empty())
            mRendering.notifyWorldSpaceChanged();
    }

    void Scene::loadCell(CellStore& cell, bool respawn, const DetourNavigator::UpdateGuard* navigatorUpdateGuard,
        const IncrementalLoad* incremental)
    {
        using DetourNavigator::HeightfieldShape;

        assert(mActiveCells.find(&cell) == mActiveCells.end());
        mActiveCells.insert(&cell);

        Misc::LoadTrace::Scope traceCell(
            "cell.load", static_cast<std::int64_t>(cell.getCell()->getGridX()) * 100000 + cell.getCell()->getGridY());
        Log(Debug::Info) << "Loading cell " << cell.getCell()->getDescription();
        MWBase::Environment::get().getExtensionManager()->cellLoaded(cell.getCell()->getDescription());
        Misc::LoadTrace::Scope traceLand("load.exterior.total");

        const int cellX = cell.getCell()->getGridX();
        const int cellY = cell.getCell()->getGridY();
        const MWWorld::Cell& cellVariant = *cell.getCell();
        ESM::RefId worldspace = cellVariant.getWorldSpace();
        ESM::ExteriorCellLocation cellIndex(cellX, cellY, worldspace);

        if (cellVariant.isExterior())
        {
            // openeden-vsg: vsgopenmw's land manager reads ESM3 land only, so ESM4 worldspaces get no terrain
            // heightfield until ESM4 terrain is ported (task 0012, Track V stage V4).
            Misc::LoadTrace::Scope traceLandRead("load.land.read");
            auto land = mRendering.getLandManager()->getLand(cellIndex);
            traceLandRead.end();
            Misc::LoadTrace::Scope traceHeight("load.physics.heightfield");
            const auto* data = land ? land->getData(ESM::Land::DATA_VHGT) : nullptr;
            const int verts = ESM::Land::LAND_SIZE;
            const int worldsize = ESM::Land::REAL_SIZE;
            if (data)
            {
                mPhysics->addHeightField(data->mHeights.data(), cellX, cellY, worldsize, verts, data->mMinHeight,
                    data->mMaxHeight, land.get());
            }
            else if (!ESM::isEsm4Ext(worldspace))
            {
                static const std::vector<float> defaultHeight(verts * verts, ESM::Land::DEFAULT_HEIGHT);
                mPhysics->addHeightField(defaultHeight.data(), cellX, cellY, worldsize, verts,
                    ESM::Land::DEFAULT_HEIGHT, ESM::Land::DEFAULT_HEIGHT, land.get());
            }
            traceHeight.end();
            if (mPhysics->getHeightField(cellX, cellY))
            {
                const vsg::ivec2 cellPosition(cellX, cellY);
                const HeightfieldShape shape = [&]() -> HeightfieldShape {
                    if (data == nullptr)
                    {
                        return DetourNavigator::HeightfieldPlane{ static_cast<float>(ESM::Land::DEFAULT_HEIGHT) };
                    }
                    else
                    {
                        DetourNavigator::HeightfieldSurface heights;
                        heights.mHeights = data->mHeights.data();
                        heights.mSize = static_cast<std::size_t>(verts);
                        heights.mMinHeight = data->mMinHeight;
                        heights.mMaxHeight = data->mMaxHeight;
                        return heights;
                    }
                }();
                Misc::LoadTrace::Scope traceNavHeight("load.navigator.heightfield");
                mNavigator.addHeightfield(cellPosition, worldsize, shape, navigatorUpdateGuard);
            }
        }

        traceLand.end();
        Misc::LoadTrace::Scope tracePathgrid("load.pathgrid");
        ESM::visit(ESM::VisitOverload{
                       [&](const ESM::Cell& c) {
                           if (const auto pathgrid = mWorld.getStore().get<ESM::Pathgrid>().search(c))
                               mNavigator.addPathgrid(c, *pathgrid);
                       },
                       [&](const ESM4::Cell& /*c*/) {},
                   },
            *cell.getCell());

        tracePathgrid.end();
        Misc::LoadTrace::Scope traceScripts("load.scripts.respawn");
        // register local scripts
        // do this before insertCell, to make sure we don't add scripts from levelled creature spawning twice
        mWorld.getLocalScripts().addCell(&cell);

        if (respawn)
            cell.respawn();

        traceScripts.end();
        {
            Misc::LoadTrace::Scope trace("load.insertcell");
            insertCell(cell, navigatorUpdateGuard, incremental);
        }
        Misc::LoadTrace::Scope traceAdd("load.rendering.addcell");
        mRendering.addCell(&cell);
        traceAdd.end();
        Misc::LoadTrace::Scope traceWindow("load.window.addcell");
        MWBase::Environment::get().getWindowManager()->addCell(&cell);
        traceWindow.end();
        Misc::LoadTrace::Scope traceWater("load.water.ambient.sky");
        bool waterEnabled = cellVariant.hasWater() || cell.isExterior();
        float waterLevel = cell.getWaterLevel();
        mRendering.setWaterEnabled(waterEnabled);
        if (waterEnabled)
        {
            mPhysics->enableWater(waterLevel);
            mRendering.setWaterHeight(waterLevel);

            if (cellVariant.isExterior())
            {
                if (mPhysics->getHeightField(cellX, cellY))
                    mNavigator.addWater(
                        vsg::ivec2(cellX, cellY), ESM::Land::REAL_SIZE, waterLevel, navigatorUpdateGuard);
            }
            else
            {
                mNavigator.addWater(
                    vsg::ivec2(cellX, cellY), std::numeric_limits<int>::max(), waterLevel, navigatorUpdateGuard);
            }
        }
        else
            mPhysics->disableWater();

        if (!cell.isExterior() && !cellVariant.isQuasiExterior())
            mRendering.configureAmbient(cellVariant);
        else
            mRendering.disableProbes(); // task 0028: exteriors keep the flat ambient
        if (cell.isExterior())
            mRendering.exteriorCellLoaded(cellX, cellY); // task 0036: the sky-visibility field of the exterior
    }

    void Scene::clear()
    {
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        for (auto iter = mActiveCells.begin(); iter != mActiveCells.end();)
        {
            auto* cell = *iter++;
            unloadCell(cell, navigatorUpdateGuard.get());
        }
        navigatorUpdateGuard.reset();
        assert(mActiveCells.empty());
        mBuildQueue.clear();
        mBuildFrames = 0;
        mBuildingCells.clear();
        mBuildStartUs.clear();
        mCurrentCell = nullptr;
        mLowestPoint = std::numeric_limits<float>::max();

        mPreloader->clear();
    }

    vsg::ivec4 Scene::gridCenterToBounds(const vsg::ivec2& centerCell) const
    {
        return vsg::ivec4(centerCell.x - mHalfGridSize, centerCell.y - mHalfGridSize, centerCell.x + mHalfGridSize + 1,
            centerCell.y + mHalfGridSize + 1);
    }

    vsg::ivec2 Scene::getNewGridCenter(const vsg::vec3& pos, const vsg::ivec2* currentGridCenter) const
    {
        ESM::RefId worldspace
            = mCurrentCell ? mCurrentCell->getCell()->getWorldSpace() : ESM::Cell::sDefaultWorldspaceId;
        if (currentGridCenter)
        {
            const vsg::vec2 center = ESM::indexToPosition(
                ESM::ExteriorCellLocation(currentGridCenter->x, currentGridCenter->y, worldspace), true);
            float distance = std::max(std::abs(center.x - pos.x), std::abs(center.y - pos.y));
            int cellSize = ESM::getCellSize(worldspace);
            const float maxDistance = cellSize / 2 + mCellLoadingThreshold; // 1/2 cell size + threshold
            if (distance <= maxDistance)
                return *currentGridCenter;
        }
        ESM::ExteriorCellLocation cellPos = ESM::positionToExteriorCellLocation(pos.x, pos.y, worldspace);
        return { cellPos.mX, cellPos.mY };
    }

    bool Scene::needToChangeCellGrid(const vsg::vec3& pos)
    {
        if (!mCurrentCell || !mCurrentCell->isExterior())
            return false;

        vsg::ivec2 newCell = getNewGridCenter(pos, &mCurrentGridCenter);
        return newCell != mCurrentGridCenter;
    }

    void Scene::playerMoved(const vsg::vec3& pos)
    {
        if (!mCurrentCell || mCurrentCell->isExterior())
            return;

        // The player is reset when z is 90 units below the lowest reference bound z.
        constexpr float lowestPointAdjustment = -90.0f;
        if (pos.z >= mLowestPoint + lowestPointAdjustment)
            return;

        // Player has fallen into the void, reset to interior marker/coc (#1415)
        const std::string_view cellNameId = mCurrentCell->getCell()->getNameId();
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr playerPtr = world->getPlayerPtr();

        // Check that collision is enabled, which is opposite to Vanilla
        // this change was decided in MR #4100 as the behaviour is preferable
        if (world->isActorCollisionEnabled(playerPtr))
        {
            ESM::Position newPos;
            const ESM::RefId refId = world->findInteriorPosition(cellNameId, newPos);

            // Only teleport if that teleport point is > the lowest point, rare edge case
            if (!refId.empty() && newPos.pos[2] >= mLowestPoint - lowestPointAdjustment)
            {
                MWWorld::ActionTeleport(refId, newPos, false).execute(playerPtr);
                Log(Debug::Warning) << "Player position has been reset due to falling into the void";
            }
        }
    }

    void Scene::changeCellGrid(
        const vsg::vec3& pos, ESM::ExteriorCellLocation playerCellIndex, bool changeEvent, bool incremental)
    {
        Misc::LoadTrace::Scope traceGrid("grid.total");
        const int halfGridSize = getHalfGridSize(playerCellIndex.mWorldspace);
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        const int playerCellX = playerCellIndex.mX;
        const int playerCellY = playerCellIndex.mY;

        Misc::LoadTrace::Scope traceUnload("grid.unload");
        for (auto iter = mActiveCells.begin(); iter != mActiveCells.end();)
        {
            auto* cell = *iter++;
            if (cell->getCell()->isExterior() && cell->getCell()->getWorldSpace() == playerCellIndex.mWorldspace)
            {
                const auto dx = std::abs(playerCellX - cell->getCell()->getGridX());
                const auto dy = std::abs(playerCellY - cell->getCell()->getGridY());
                if (dx > halfGridSize || dy > halfGridSize)
                    unloadCell(cell, navigatorUpdateGuard.get());
            }
            else
                unloadCell(cell, navigatorUpdateGuard.get());
        }

        traceUnload.end();
        const DetourNavigator::CellGridBounds cellGridBounds{
            .mCenter = vsg::ivec2(playerCellX, playerCellY),
            .mHalfSize = halfGridSize,
        };

        Misc::LoadTrace::Scope traceBounds("grid.navigator.updatebounds");
        mNavigator.updateBounds(playerCellIndex.mWorldspace, cellGridBounds, pos, navigatorUpdateGuard.get());
        traceBounds.end();

        mHalfGridSize = halfGridSize;
        mCurrentGridCenter = vsg::ivec2(playerCellX, playerCellY);
        mRendering.setActiveGrid(playerCellX, playerCellY, halfGridSize);
        mRendering.setRenderModeActive(MWRender::Render_Terrain, true);
        {
            Misc::LoadTrace::Scope trace("grid.rendering.changecellgrid");
            mRendering.changeCellGrid(pos);
        }

        {
            Misc::LoadTrace::Scope trace("grid.postponed.physics");
            addPostponedPhysicsObjects();
        }

        std::vector<std::pair<int, int>> cellsPositionsToLoad;
        iterateOverCellsAround(playerCellX, playerCellY, mHalfGridSize, [&](int x, int y) {
            if (!isCellInCollection(ESM::ExteriorCellLocation(x, y, playerCellIndex.mWorldspace), mActiveCells))
                cellsPositionsToLoad.emplace_back(x, y);
        });

        sortCellsToLoad(playerCellX, playerCellY, cellsPositionsToLoad);

        for (const auto& [x, y] : cellsPositionsToLoad)
        {
            ESM::ExteriorCellLocation indexToLoad = { x, y, playerCellIndex.mWorldspace };
            if (!isCellInCollection(indexToLoad, mActiveCells))
            {
                Misc::LoadTrace::Scope traceGet("grid.getexterior");
                CellStore& cell = mWorld.getWorldModel().getExterior(indexToLoad);
                traceGet.end();
                // Task 0060: only a border crossing, in a game that already has a player cell, builds objects
                // later; the cell the player stands in is always built whole.
                const bool buildLater = incremental && mIncrementalCellBuild && mCurrentCell != nullptr
                    && (x != playerCellX || y != playerCellY);
                const IncrementalLoad incrementalLoad{ pos, std::abs(x - playerCellX) + std::abs(y - playerCellY) };
                loadCell(cell, changeEvent, navigatorUpdateGuard.get(), buildLater ? &incrementalLoad : nullptr);
            }
        }

        Misc::LoadTrace::Scope traceNavUpdate("grid.navigator.update.release");
        mNavigator.update(pos, navigatorUpdateGuard.get());

        navigatorUpdateGuard.reset();
        traceNavUpdate.end();

        Misc::LoadTrace::Scope traceWindow("grid.window.changecell");
        CellStore& current = mWorld.getWorldModel().getExterior(playerCellIndex);
        // The player's cell may still have objects waiting from an earlier crossing (a very fast flight): the player
        // must never stand in a cell that is not built. A load that is not a border crossing (a teleport) builds
        // every waiting cell now, as it always has.
        if (incremental)
        {
            finishCell(current);
        }
        else
        {
            const std::vector<CellStore*> waiting(mBuildingCells.begin(), mBuildingCells.end());
            for (CellStore* cell : waiting)
                finishCell(*cell);
        }
        MWBase::Environment::get().getWindowManager()->changeCell(&current);

        if (changeEvent)
            mCellChanged = true;

        mCellLoaded = true;
    }

    void Scene::addPostponedPhysicsObjects()
    {
        for (const auto& cell : mActiveCells)
        {
            cell->forEach([&](const MWWorld::Ptr& ptr) {
                // Task 0060: an object waiting to be built gets its physics when it is built.
                if (ptr.mRef->mData.mPhysicsPostponed && !mBuildQueue.contains(ptr.mRef))
                {
                    ptr.mRef->mData.mPhysicsPostponed = false;
                    if (ptr.mRef->mData.isEnabled() && ptr.mRef->mRef.getCount() > 0)
                    {
                        const VFS::Path::Normalized model = getModel(ptr);
                        if (!model.empty())
                        {
                            const auto rotation = makeNodeRotation(ptr, RotationOrder::direct);
                            ptr.getClass().insertObjectPhysics(ptr, model, rotation, *mPhysics);
                        }
                    }
                }
                return true;
            });
        }
    }

    void Scene::testExteriorCells(MWState::Loading& state)
    {
        const MWWorld::Store<ESM::Cell>& cells = mWorld.getStore().get<ESM::Cell>();

        state.progressStep = 1.f / cells.getExtSize();

        MWWorld::Store<ESM::Cell>::iterator it = cells.extBegin();
        int i = 1;
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        for (; !state.abort && it != cells.extEnd(); ++it)
        {
            state.setDescription("#{OMWEngine:TestingExteriorCells} (" + std::to_string(i) + "/"
                + std::to_string(cells.getExtSize()) + ")...");

            CellStore& cell = mWorld.getWorldModel().getExterior(
                ESM::ExteriorCellLocation(it->mData.mX, it->mData.mY, ESM::Cell::sDefaultWorldspaceId));
            const vsg::vec3 position
                = vsg::vec3(it->mData.mX + 0.5f, it->mData.mY + 0.5f, 0) * Constants::CellSizeInUnits;

            const DetourNavigator::CellGridBounds cellGridBounds{
                .mCenter = vsg::ivec2(it->mData.mX, it->mData.mY),
                .mHalfSize = Constants::CellGridRadius,
            };

            mNavigator.updateBounds(
                ESM::Cell::sDefaultWorldspaceId, cellGridBounds, position, navigatorUpdateGuard.get());
            loadCell(cell, false, navigatorUpdateGuard.get());

            mNavigator.update(position, navigatorUpdateGuard.get());
            navigatorUpdateGuard.reset();
            mNavigator.wait(DetourNavigator::WaitConditionType::requiredTilesPresent, nullptr);
            navigatorUpdateGuard = mNavigator.makeUpdateGuard();

            auto iter = mActiveCells.begin();
            while (iter != mActiveCells.end())
            {
                if (it->isExterior() && it->mData.mX == (*iter)->getCell()->getGridX()
                    && it->mData.mY == (*iter)->getCell()->getGridY())
                {
                    unloadCell(*iter, navigatorUpdateGuard.get());
                    break;
                }

                ++iter;
            }

            mRendering.pruneCache();
            mPhysics->pruneCache();

            state.advance();
            i++;
        }
    }

    void Scene::testInteriorCells(MWState::Loading& state)
    {
        const MWWorld::Store<ESM::Cell>& cells = mWorld.getStore().get<ESM::Cell>();

        state.progressStep = 1.f / cells.getIntSize();

        int i = 1;
        MWWorld::Store<ESM::Cell>::iterator it = cells.intBegin();
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        for (; !state.abort && it != cells.intEnd(); ++it)
        {
            state.setDescription("#{OMWEngine:TestingInteriorCells} (" + std::to_string(i) + "/"
                + std::to_string(cells.getIntSize()) + ")...");

            CellStore& cell = mWorld.getWorldModel().getInterior(it->mName);
            ESM::Position position;
            mWorld.findInteriorPosition(it->mName, position);
            mNavigator.updateBounds(
                cell.getCell()->getWorldSpace(), std::nullopt, position.asVec3(), navigatorUpdateGuard.get());
            loadCell(cell, false, navigatorUpdateGuard.get());

            mNavigator.update(position.asVec3(), navigatorUpdateGuard.get());
            navigatorUpdateGuard.reset();
            mNavigator.wait(DetourNavigator::WaitConditionType::requiredTilesPresent, nullptr);
            navigatorUpdateGuard = mNavigator.makeUpdateGuard();

            auto iter = mActiveCells.begin();
            while (iter != mActiveCells.end())
            {
                assert(!(*iter)->getCell()->isExterior());

                if (it->mName == (*iter)->getCell()->getNameId())
                {
                    unloadCell(*iter, navigatorUpdateGuard.get());
                    break;
                }

                ++iter;
            }

            mRendering.pruneCache();
            mPhysics->pruneCache();

            state.advance();
            i++;
        }
    }

    void Scene::changePlayerCell(CellStore& cell)
    {
        finishCell(cell);
        mHalfGridSize = cell.getCell()->isEsm4() ? Constants::ESM4CellGridRadius : Constants::CellGridRadius;
        mCurrentCell = &cell;
        MWBase::Environment::get().getExtensionManager()->cellChanged(cell.getCell()->getDescription());

        MWWorld::Ptr old = mWorld.getPlayerPtr();
        mWorld.removeContainerScripts(old);
        mWorld.getPlayer().setCell(&cell);

        MWWorld::Ptr player = mWorld.getPlayerPtr();
        mRendering.updatePlayerPtr(player);
        mWorld.addContainerScripts(player, &cell);

        // Optimize the physics broadphase after rapidly changing the dataset (such as an interior load)
        // NOTE: for exteriors we call optimize in changeToExteriorCell
        if (!cell.isExterior())
            mPhysics->optimize();

        // The player is loaded before the scene and by default it is grounded, with the scene fully loaded,
        // we validate and correct this. Only run once, during initial cell load.
        if (old.mCell == &cell)
            mPhysics->traceDown(player, player.getRefData().getPosition().asVec3(), 10.f);

        MWBase::Environment::get().getMechanicsManager()->updateCell(old, player);
        MWBase::Environment::get().getWindowManager()->watchActor(player);

        mPhysics->updatePtr(old, player);

        mWorld.adjustSky();

        mLastPlayerPos = player.getRefData().getPosition().asVec3();

        mRendering.pruneCache();
        mPhysics->pruneCache();
    }

    Scene::Scene(MWWorld::World& world, MWRender::RenderManager& rendering, MWPhysics::PhysicsSystem* physics,
        DetourNavigator::Navigator& navigator)
        : mCurrentCell(nullptr)
        , mCellChanged(false)
        , mWorld(world)
        , mPhysics(physics)
        , mRendering(rendering)
        , mNavigator(navigator)
        , mCellLoadingThreshold(1024.f)
        , mPreloadDistance(Settings::cells().mPreloadDistance)
        , mPreloadEnabled(Settings::cells().mPreloadEnabled)
        , mPreloadExteriorGrid(Settings::cells().mPreloadExteriorGrid)
        , mPreloadDoors(Settings::cells().mPreloadDoors)
        , mPreloadFastTravel(Settings::cells().mPreloadFastTravel)
        , mPredictionTime(Settings::cells().mPredictionTime)
        , mLowestPoint(std::numeric_limits<float>::max())
        , mIncrementalCellBuild(Settings::cells().mIncrementalCellBuild)
        , mIncrementalBudgetMs(Settings::cells().mIncrementalBuildBudgetMs)
    {
        mPreloader = std::make_unique<CellPreloader>(rendering.getAnimContext(), mPhysics->getShapeManager(),
            rendering.getLandManager(), rendering.getOperationThreads());
        mTerrainPreloader = std::make_unique<Terrain::Preload>(
            rendering.getTerrain(), rendering.getOperationThreads(), mPreloader->getCompileThreads());

        mPreloader->setExpiryDelay(Settings::cells().mPreloadCellExpiryDelay);
        mPreloader->setMinCacheSize(Settings::cells().mPreloadCellCacheMin);
        mPreloader->setMaxCacheSize(Settings::cells().mPreloadCellCacheMax);
        mPreloader->setPreloadInstances(Settings::cells().mPreloadInstances);
    }

    Scene::~Scene() = default;

    bool Scene::hasCellChanged() const
    {
        return mCellChanged;
    }

    const Scene::CellStoreCollection& Scene::getActiveCells() const
    {
        return mActiveCells;
    }

    void Scene::changeToInteriorCell(std::string_view cellName, const ESM::Position& position, bool changeEvent)
    {
        CellStore& cell = mWorld.getWorldModel().getInterior(cellName);

        if (mCurrentCell == &cell)
        {
            mWorld.moveObject(mWorld.getPlayerPtr(), position.asVec3());
            mWorld.rotateObject(mWorld.getPlayerPtr(), position.asRotationVec3());
            return;
        }

        Log(Debug::Info) << "Changing to interior";

        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();

        // unload
        for (auto iter = mActiveCells.begin(); iter != mActiveCells.end();)
        {
            auto* cellToUnload = *iter++;
            unloadCell(cellToUnload, navigatorUpdateGuard.get());
        }
        assert(mActiveCells.empty());

        mNavigator.updateBounds(
            cell.getCell()->getWorldSpace(), std::nullopt, position.asVec3(), navigatorUpdateGuard.get());

        // Load cell.
        mPagedRefs.clear();
        loadCell(cell, changeEvent, navigatorUpdateGuard.get());

        navigatorUpdateGuard.reset();

        changePlayerCell(cell);

        // adjust fog
        mRendering.configureFog(*mCurrentCell->getCell());

        mRendering.setRenderModeActive(MWRender::Render_Terrain, false);

        // Sky system
        mWorld.adjustSky();

        if (changeEvent)
            mCellChanged = true;

        mCellLoaded = true;

        MWBase::Environment::get().getWindowManager()->changeCell(mCurrentCell);
    }

    void Scene::changeToExteriorCell(
        const ESM::RefId& extCellId, const ESM::Position& position, bool changeEvent, bool incremental)
    {
        CellStore& current = mWorld.getWorldModel().getCell(extCellId);
        const vsg::ivec2 cellIndex(current.getCell()->getGridX(), current.getCell()->getGridY());
        changeCellGrid(position.asVec3(),
            ESM::ExteriorCellLocation(cellIndex.x, cellIndex.y, current.getCell()->getWorldSpace()), changeEvent,
            incremental);

        if (&current != mCurrentCell)
            changePlayerCell(current);

        mPhysics->optimize();
    }

    CellStore* Scene::getCurrentCell()
    {
        return mCurrentCell;
    }

    void Scene::markCellAsUnchanged()
    {
        mCellChanged = false;
    }

    void Scene::insertCell(
        CellStore& cell, const DetourNavigator::UpdateGuard* navigatorUpdateGuard, const IncrementalLoad* incremental)
    {
        const bool isInterior = !cell.isExterior();
        InsertVisitor insertVisitor(cell);
        {
            Misc::LoadTrace::Scope trace("insert.collect");
            cell.forEach(insertVisitor);
        }
        if (incremental != nullptr)
        {
            // Task 0060: the cell is loaded (records, scripts, land) but its objects are built later, a few
            // milliseconds a frame. Each object is queued twice: to be added (plain objects first, actors after
            // them) and, after every object of the cell is added, for the navigator.
            Misc::LoadTrace::Scope trace("insert.queue", static_cast<std::int64_t>(insertVisitor.mToInsert.size()));
            for (const MWWorld::Ptr& ptr : insertVisitor.mToInsert)
            {
                if (ptr.mRef->isDeleted() || !ptr.getRefData().isEnabled())
                    continue;
                const vsg::vec3 offset = ptr.getRefData().getPosition().asVec3() - incremental->mPlayerPos;
                const float distance = vsg::dot(offset, offset);
                const Misc::BuildPhase phase
                    = ptr.getClass().isActor() ? Misc::BuildPhase::Actor : Misc::BuildPhase::Object;
                mBuildQueue.add({ &cell, ptr.mRef, incremental->mRank, phase, distance, ptr });
                mBuildQueue.add({ &cell, ptr.mRef, incremental->mRank, Misc::BuildPhase::Navigator, distance, ptr });
            }
            if (mBuildQueue.pendingInGroup(&cell) > 0)
            {
                mBuildingCells.insert(&cell);
                mRendering.setCellObjectsPending(cell.getCell()->getGridX(), cell.getCell()->getGridY(), true);
                if (Misc::LoadTrace::enabled())
                    mBuildStartUs[&cell] = Misc::LoadTrace::nowMicroseconds();
            }
            return;
        }
        {
            Misc::LoadTrace::Scope trace("insert.objects", static_cast<std::int64_t>(insertVisitor.mToInsert.size()));
            insertVisitor.insert(
                [&](const MWWorld::Ptr& ptr) { addObject(ptr, mWorld, mPagedRefs, *mPhysics, mRendering); });
        }
        Misc::LoadTrace::Scope trace("insert.navigator");
        insertVisitor.insert([&](const MWWorld::Ptr& ptr) {
            addObject(ptr, mWorld, *mPhysics, mLowestPoint, isInterior, mNavigator, navigatorUpdateGuard);
        });
    }

    void Scene::buildQueuedObject(const Misc::BuildQueue<Ptr>::Item& item, const DetourNavigator::UpdateGuard* guard)
    {
        const Ptr& ptr = item.mPayload;
        // The same checks as InsertVisitor::insert: the object may have been disabled or deleted while it waited
        // (those paths also cancel it, this is the second line of defence).
        if (ptr.mRef->isDeleted() || !ptr.getRefData().isEnabled())
            return;
        try
        {
            if (item.mPhase == Misc::BuildPhase::Navigator)
            {
                addObject(ptr, mWorld, *mPhysics, mLowestPoint, false, mNavigator, guard);
            }
            else
            {
                // A script moved it while it waited and asked for its physics to be added later: it is added now,
                // at the position it has now.
                ptr.mRef->mData.mPhysicsPostponed = false;
                addObject(ptr, mWorld, mPagedRefs, *mPhysics, mRendering);
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "failed to render '" << ptr.getCellRef().getRefId() << "': " << e.what();
        }
    }

    void Scene::reportBuiltCells()
    {
        for (auto iter = mBuildingCells.begin(); iter != mBuildingCells.end();)
        {
            CellStore* cell = *iter;
            if (mBuildQueue.pendingInGroup(cell) > 0)
            {
                ++iter;
                continue;
            }
            iter = mBuildingCells.erase(iter);
            const int gridX = cell->getCell()->getGridX();
            const int gridY = cell->getCell()->getGridY();
            mRendering.setCellObjectsPending(gridX, gridY, false);
            if (const auto started = mBuildStartUs.find(cell); started != mBuildStartUs.end())
            {
                Misc::LoadTrace::record("cell.built", started->second, Misc::LoadTrace::nowMicroseconds(),
                    static_cast<std::int64_t>(gridX) * 100000 + gridY);
                mBuildStartUs.erase(started);
            }
        }
    }

    void Scene::buildPendingObjects()
    {
        if (mBuildQueue.empty())
            return;
        Misc::LoadTrace::Scope trace("incremental.build");
        const vsg::vec3 playerPos = mWorld.getPlayerPtr().getRefData().getPosition().asVec3();
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        // Wall-clock time only decides how many objects fit in this frame. The order they are built in does not
        // depend on it (Misc::BuildQueue).
        const auto nowMs = [] {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        // One object is always built. If the queue has not emptied after sFramesBeforeCatchUp calls (a very low frame
        // rate), a share of it is built whatever the budget, so that the objects cannot stay unbuilt for long.
        ++mBuildFrames;
        const std::size_t minItems = mBuildFrames > sFramesBeforeCatchUp ? (mBuildQueue.size() + 9) / 10 : 1;
        const std::size_t built = mBuildQueue.run(mIncrementalBudgetMs, minItems, nowMs,
            [&](const Misc::BuildQueue<Ptr>::Item& item) { buildQueuedObject(item, navigatorUpdateGuard.get()); });
        mNavigator.update(playerPos, navigatorUpdateGuard.get());
        navigatorUpdateGuard.reset();
        trace.setValue(static_cast<std::int64_t>(built));
        if (mBuildQueue.empty())
            mBuildFrames = 0;
        reportBuiltCells();
    }

    void Scene::finishCell(CellStore& cell)
    {
        if (mBuildQueue.pendingInGroup(&cell) == 0)
            return;
        Misc::LoadTrace::Scope trace("incremental.finish");
        const vsg::vec3 playerPos = mWorld.getPlayerPtr().getRefData().getPosition().asVec3();
        auto navigatorUpdateGuard = mNavigator.makeUpdateGuard();
        const std::size_t built = mBuildQueue.runGroup(&cell,
            [&](const Misc::BuildQueue<Ptr>::Item& item) { buildQueuedObject(item, navigatorUpdateGuard.get()); });
        mNavigator.update(playerPos, navigatorUpdateGuard.get());
        navigatorUpdateGuard.reset();
        trace.setValue(static_cast<std::int64_t>(built));
        reportBuiltCells();
    }

    bool Scene::isPendingBuild(const MWWorld::ConstPtr& ptr) const
    {
        return !mBuildQueue.empty() && mBuildQueue.contains(ptr.mRef);
    }

    void Scene::updatePendingPtr(const Ptr& old, const Ptr& cur)
    {
        if (!mBuildQueue.empty())
            mBuildQueue.replaceKeyAndGroup(old.mRef, cur.mRef, cur.getCell(), cur);
    }

    void Scene::addObjectToScene(const Ptr& ptr)
    {
        const bool isInterior = mCurrentCell && !mCurrentCell->isExterior();
        // Task 0060: an object that still waits in the build queue is added now, once.
        mBuildQueue.cancelKey(ptr.mRef);
        try
        {
            addObject(ptr, mWorld, mPagedRefs, *mPhysics, mRendering);
            addObject(ptr, mWorld, *mPhysics, mLowestPoint, isInterior, mNavigator);
            mWorld.scaleObject(ptr, ptr.getCellRef().getScale());
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "failed to render '" << ptr.getCellRef().getRefId() << "': " << e.what();
        }
    }

    void Scene::removeObjectFromScene(const Ptr& ptr, bool keepActive)
    {
        // Task 0060: an object that was never built is just forgotten; the removal below copes with that, as it does
        // for objects that were disabled.
        mBuildQueue.cancelKey(ptr.mRef);
        MWBase::Environment::get().getMechanicsManager()->remove(ptr, keepActive);
        // You'd expect the sounds attached to the object to be stopped here
        // because the object is nowhere to be heard, but in Morrowind, they're not.
        // They're still stopped when the cell is unloaded
        // or if the player moves away far from the object's position.
        // Todd Howard, Who art in Bethesda, hallowed be Thy name.
        MWBase::Environment::get().getLuaManager()->objectRemovedFromScene(ptr);
        MWBase::Environment::get().getExtensionManager()->objectRemovedFromScene(ptr);
        if (const auto object = mPhysics->getObject(ptr))
        {
            if (object->getShapeInstance()->mVisualCollisionType == Resource::VisualCollisionType::None)
                mNavigator.removeObject(DetourNavigator::ObjectId(object), nullptr);
        }
        else if (mPhysics->getActor(ptr))
        {
            mNavigator.removeAgent(mWorld.getPathfindingAgentBounds(ptr));
        }
        mPhysics->remove(ptr);
        mRendering.removeObject(ptr);
        if (ptr.getClass().isActor())
            mRendering.removeWaterRippleEmitter(ptr);
    }

    bool Scene::isCellActive(const CellStore& cell)
    {
        return mActiveCells.contains(&cell);
    }

    void Scene::preload(const std::string& /*mesh*/, bool /*useAnim*/)
    {
        // openeden-vsg: the OSG scene manager that warmed this mesh cache is gone; vsgopenmw preloads cell
        // contents through CellPreloader instead. Single-mesh preloading returns with task 0010 (stage V2).
    }

    void Scene::preloadCells(float dt)
    {
        if (dt <= 1e-06)
            return;
        std::vector<PositionCellGrid> exteriorPositions;

        const MWWorld::ConstPtr player = mWorld.getPlayerPtr();
        vsg::vec3 playerPos = player.getRefData().getPosition().asVec3();
        vsg::vec3 moved = playerPos - mLastPlayerPos;
        vsg::vec3 predictedPos = playerPos + Misc::VsgMath::divide(moved, dt) * mPredictionTime;

        if (mCurrentCell->isExterior())
            exteriorPositions.push_back(PositionCellGrid{
                predictedPos, gridCenterToBounds(getNewGridCenter(predictedPos, &mCurrentGridCenter)) });

        mLastPlayerPos = playerPos;

        if (mPreloadEnabled)
        {
            if (mPreloadDoors)
                preloadTeleportDoorDestinations(playerPos, predictedPos);
            if (mPreloadExteriorGrid)
                preloadExteriorGrid(playerPos, predictedPos);
            if (mPreloadFastTravel)
                preloadFastTravelDestinations(playerPos, exteriorPositions);
        }

        std::vector<Terrain::Preload::Position> terrainPositions;
        for (const PositionCellGrid& position : exteriorPositions)
            terrainPositions.emplace_back(vsg::dvec3(position.mPosition), mRendering.getViewDistance());
        mTerrainPreloader->setPositions(terrainPositions);
    }

    void Scene::preloadTeleportDoorDestinations(const vsg::vec3& playerPos, const vsg::vec3& predictedPos)
    {
        std::vector<MWWorld::ConstPtr> teleportDoors;
        for (const MWWorld::CellStore* cellStore : mActiveCells)
        {
            typedef MWWorld::CellRefList<ESM::Door>::List DoorList;
            const DoorList& doors = cellStore->getReadOnlyDoors().mList;
            for (auto& door : doors)
            {
                if (!door.mRef.getTeleport())
                {
                    continue;
                }
                teleportDoors.emplace_back(&door, cellStore);
            }
        }

        for (const MWWorld::ConstPtr& door : teleportDoors)
        {
            float sqrDistToPlayer = vsg::length2(playerPos - door.getRefData().getPosition().asVec3());
            sqrDistToPlayer
                = std::min(sqrDistToPlayer, vsg::length2(predictedPos - door.getRefData().getPosition().asVec3()));

            if (sqrDistToPlayer < mPreloadDistance * mPreloadDistance)
            {
                try
                {
                    preloadCell(mWorld.getWorldModel().getCell(door.getCellRef().getDestCell()), true);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to schedule preload for door " << door.toString() << ": "
                                        << e.what();
                }
            }
        }
    }

    void Scene::preloadExteriorGrid(const vsg::vec3& playerPos, const vsg::vec3& predictedPos)
    {
        if (!mWorld.isCellExterior())
            return;

        int halfGridSizePlusOne = mHalfGridSize + 1;

        int cellX, cellY;
        cellX = mCurrentGridCenter.x;
        cellY = mCurrentGridCenter.y;
        ESM::RefId extWorldspace = mWorld.getCurrentWorldspace();

        int cellSize = ESM::getCellSize(extWorldspace);

        for (int dx = -halfGridSizePlusOne; dx <= halfGridSizePlusOne; ++dx)
        {
            for (int dy = -halfGridSizePlusOne; dy <= halfGridSizePlusOne; ++dy)
            {
                if (dy != halfGridSizePlusOne && dy != -halfGridSizePlusOne && dx != halfGridSizePlusOne
                    && dx != -halfGridSizePlusOne)
                    continue; // only care about the outer (not yet loaded) part of the grid
                ESM::ExteriorCellLocation cellIndex(cellX + dx, cellY + dy, extWorldspace);
                const vsg::vec2 thisCellCenter = ESM::indexToPosition(cellIndex, true);

                float dist
                    = std::max(std::abs(thisCellCenter.x - playerPos.x), std::abs(thisCellCenter.y - playerPos.y));
                dist = std::min(dist,
                    std::max(std::abs(thisCellCenter.x - predictedPos.x), std::abs(thisCellCenter.y - predictedPos.y)));
                float loadDist = cellSize / 2 + cellSize - mCellLoadingThreshold + mPreloadDistance;

                if (dist < loadDist)
                    preloadCell(mWorld.getWorldModel().getExterior(cellIndex));
            }
        }
    }

    float Scene::preloadCell(CellStore& cell, bool preloadSurrounding)
    {
        if (!preloadSurrounding || !cell.isExterior())
            return mPreloader->preload(cell);

        const int cellX = cell.getCell()->getGridX();
        const int cellY = cell.getCell()->getGridY();
        const int halfGridSize = getHalfGridSize(cell.getCell()->getWorldSpace());

        std::vector<std::pair<int, int>> cells;
        const std::size_t gridSize = static_cast<std::size_t>(2 * halfGridSize + 1);
        cells.reserve(gridSize * gridSize);

        iterateOverCellsAround(cellX, cellY, halfGridSize, [&](int x, int y) { cells.emplace_back(x, y); });

        sortCellsToLoad(cellX, cellY, cells);

        const std::size_t capacity = mPreloader->getMaxCacheSize();
        if (cells.size() > capacity)
        {
            [[maybe_unused]] static const bool logged = [&] {
                Log(Debug::Warning) << "Not enough cell preloader cache capacity to preload exterior cells, consider "
                                       "increasing \"preload cell cache max\" up to "
                                    << cells.size();
                return true;
            }();
            cells.resize(capacity);
        }

        const ESM::RefId worldspace = cell.getCell()->getWorldSpace();
        float progress = 0;
        unsigned int numPreloaded = 0;
        for (const auto& [x, y] : cells)
        {
            CellStore& surrounding = mWorld.getWorldModel().getExterior(ESM::ExteriorCellLocation(x, y, worldspace));
            if (mActiveCells.contains(&surrounding))
                continue;
            progress += mPreloader->preload(surrounding);
            ++numPreloaded;
        }
        return numPreloaded == 0 ? 1.f : progress / numPreloaded;
    }

    void Scene::preloadTerrain(const vsg::vec3& pos, ESM::RefId /*worldspace*/, bool /*sync*/)
    {
        std::vector<Terrain::Preload::Position> positions;
        positions.emplace_back(vsg::dvec3(pos), mRendering.getViewDistance());
        mTerrainPreloader->setPositions(positions);
    }

    void Scene::reloadTerrain()
    {
        mTerrainPreloader->setPositions({});
    }

    struct ListFastTravelDestinationsVisitor
    {
        ListFastTravelDestinationsVisitor(float preloadDist, const vsg::vec3& playerPos)
            : mPreloadDist(preloadDist)
            , mPlayerPos(playerPos)
        {
        }

        bool operator()(const MWWorld::Ptr& ptr)
        {
            if (vsg::length2(vsg::vec3(ptr.getRefData().getPosition().asVec3()) - mPlayerPos)
                > mPreloadDist * mPreloadDist)
                return true;

            if (ptr.getClass().isNpc())
            {
                const std::vector<ESM::Transport::Dest>& transport = ptr.get<ESM::NPC>()->mBase->mTransport.mList;
                mList.insert(mList.begin(), transport.begin(), transport.end());
            }
            else
            {
                const std::vector<ESM::Transport::Dest>& transport = ptr.get<ESM::Creature>()->mBase->mTransport.mList;
                mList.insert(mList.begin(), transport.begin(), transport.end());
            }
            return true;
        }
        float mPreloadDist;
        vsg::vec3 mPlayerPos;
        std::vector<ESM::Transport::Dest> mList;
    };

    void Scene::preloadFastTravelDestinations(
        const vsg::vec3& playerPos, std::vector<PositionCellGrid>& exteriorPositions)
    {
        ListFastTravelDestinationsVisitor listVisitor(mPreloadDistance, playerPos);
        ESM::RefId extWorldspace = mWorld.getCurrentWorldspace();
        for (MWWorld::CellStore* cellStore : mActiveCells)
        {
            cellStore->forEachType<ESM::NPC>(listVisitor);
            cellStore->forEachType<ESM::Creature>(listVisitor);
        }

        for (ESM::Transport::Dest& dest : listVisitor.mList)
        {
            if (!dest.mCellName.empty())
                preloadCell(mWorld.getWorldModel().getInterior(dest.mCellName));
            else
            {
                vsg::vec3 pos = dest.mPos.asVec3();
                const ESM::ExteriorCellLocation cellIndex
                    = ESM::positionToExteriorCellLocation(pos.x, pos.y, extWorldspace);
                preloadCell(mWorld.getWorldModel().getExterior(cellIndex), true);
                exteriorPositions.push_back(PositionCellGrid{ pos, gridCenterToBounds(getNewGridCenter(pos)) });
            }
        }
    }
}
