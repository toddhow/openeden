#ifndef GAME_MWWORLD_SCENE_H
#define GAME_MWWORLD_SCENE_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec4.h>

#include "positioncellgrid.hpp"
#include "ptr.hpp"

#include <memory>
#include <set>
#include <vector>

#include <components/esm/exteriorcelllocation.hpp>
#include <components/misc/constants.hpp>

namespace Terrain
{
    class Preload;
}
namespace ESM
{
    struct Position;
}

namespace Files
{
    class Collections;
}

namespace DetourNavigator
{
    struct Navigator;
    class UpdateGuard;
}

namespace MWState
{
    class Loading;
}
namespace MWRender
{
    class RenderManager;
}

namespace MWPhysics
{
    class PhysicsSystem;
}

namespace MWWorld
{
    class Player;
    class CellStore;
    class CellPreloader;
    class World;

    enum class RotationOrder
    {
        direct,
        inverse
    };

    class Scene
    {
    public:
        using CellStoreCollection = std::set<CellStore*, std::less<>>;

    private:
        CellStore* mCurrentCell; // the cell the player is in
        CellStoreCollection mActiveCells;
        bool mCellChanged;
        bool mCellLoaded = false;
        MWWorld::World& mWorld;
        MWPhysics::PhysicsSystem* mPhysics;
        MWRender::RenderManager& mRendering;
        DetourNavigator::Navigator& mNavigator;
        std::unique_ptr<CellPreloader> mPreloader;
        std::unique_ptr<Terrain::Preload> mTerrainPreloader;
        float mCellLoadingThreshold;
        float mPreloadDistance;
        bool mPreloadEnabled;

        bool mPreloadExteriorGrid;
        bool mPreloadDoors;
        bool mPreloadFastTravel;
        float mPredictionTime;
        float mLowestPoint;

        int mHalfGridSize = Constants::CellGridRadius;

        vsg::vec3 mLastPlayerPos;

        std::vector<ESM::RefNum> mPagedRefs;

        void insertCell(CellStore& cell,
            const DetourNavigator::UpdateGuard* navigatorUpdateGuard);

        vsg::ivec2 mCurrentGridCenter;

        // Load and unload cells as necessary to create a cell grid with "X" and "Y" in the center
        void changeCellGrid(const vsg::vec3& pos, ESM::ExteriorCellLocation playerCellIndex, bool changeEvent = true);

        void preloadCells(float dt);
        void preloadTeleportDoorDestinations(const vsg::vec3& playerPos, const vsg::vec3& predictedPos);
        void preloadExteriorGrid(const vsg::vec3& playerPos, const vsg::vec3& predictedPos);
        void preloadFastTravelDestinations(
            const vsg::vec3& playerPos, std::vector<PositionCellGrid>& exteriorPositions);

        vsg::ivec4 gridCenterToBounds(const vsg::ivec2& centerCell) const;
        vsg::ivec2 getNewGridCenter(const vsg::vec3& pos, const vsg::ivec2* currentGridCenter = nullptr) const;

        void unloadCell(CellStore* cell, const DetourNavigator::UpdateGuard* navigatorUpdateGuard);
        void loadCell(CellStore& cell, bool respawn,
            const DetourNavigator::UpdateGuard* navigatorUpdateGuard);

    public:
        Scene(MWWorld::World& world, MWRender::RenderManager& rendering, MWPhysics::PhysicsSystem* physics,
            DetourNavigator::Navigator& navigator);

        ~Scene();

        /// @return how much of the preload has completed, from 0 to 1
        float preloadCell(MWWorld::CellStore& cell, bool preloadSurrounding = false);
        void preloadTerrain(const vsg::vec3& pos, ESM::RefId worldspace, bool sync = false);
        void reloadTerrain();

        bool needToChangeCellGrid(const vsg::vec3& playerPos);

        /// Resets the player to the interior's marker after falling into the void (#1415).
        void playerMoved(const vsg::vec3& pos);

        void changePlayerCell(CellStore& newCell);

        CellStore* getCurrentCell();

        const CellStoreCollection& getActiveCells() const;

        bool hasCellChanged() const;
        ///< Has the set of active cells changed, since the last frame?

        bool hasCellLoaded() const { return mCellLoaded; }

        void resetCellLoaded() { mCellLoaded = false; }

        void changeToInteriorCell(std::string_view cellName, const ESM::Position& position, bool changeEvent = true);
        ///< Move to interior cell.
        /// @param changeEvent Set cellChanged flag?

        void changeToExteriorCell(
            const ESM::RefId& extCellId, const ESM::Position& position, bool changeEvent = true);
        ///< Move to exterior cell.
        /// @param changeEvent Set cellChanged flag?

        void clear();
        ///< Change into a void

        void markCellAsUnchanged();

        void update(float duration);

        void addObjectToScene(const Ptr& ptr);
        ///< Add an object that already exists in the world model to the scene.

        void removeObjectFromScene(const Ptr& ptr, bool keepActive = false);
        ///< Remove an object from the scene, but not from the world model.

        void addPostponedPhysicsObjects();

        void removeFromPagedRefs(const Ptr& ptr);

        bool isPagedRef(const Ptr& ptr) const;

        void updateObjectRotation(const Ptr& ptr, RotationOrder order);
        void updateObjectScale(const Ptr& ptr);

        bool isCellActive(const CellStore& cell);

        void preload(const std::string& mesh, bool useAnim = false);

        void testExteriorCells(MWState::Loading& state);
        void testInteriorCells(MWState::Loading& state);
    };
}

#endif
