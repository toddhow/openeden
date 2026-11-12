// Task 0109 (neutral world, stage N3a-3): the start of a session whose game is not Morrowind. Morrowind's new game
// (newgame.hpp) runs its "main" script, plays its intro video and looks for its start cell by Morrowind's own lookups;
// none of that exists in another game's plugins, so a neutral session has a start path of its own: the player stub, the
// engine's world set-up for a new game, and the player placed in a neutral interior named on the command line.
#ifndef OPENMW_MWSTATE_NEUTRALNEWGAME_H
#define OPENMW_MWSTATE_NEUTRALNEWGAME_H

#include <stdexcept>
#include <string>

#include <components/debug/debuglog.hpp>
#include <components/esm/position.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/neutralcell.hpp"
#include "../mwworld/neutraldoor.hpp"
#include "../mwworld/neutralstart.hpp"
#include <components/esmbridge/util.hpp>
#include <components/worldmodel/store.hpp>
#include "../mwworld/worldmodel.hpp"

#include "error.hpp"
#include "operation.hpp"
#include "statemanagerimp.hpp"

namespace MWState
{
    class NeutralNewGame : public Operation
    {
        StateManager& mStateManager;
        StateManager::State& mState;
        std::string mStartCell;

    public:
        NeutralNewGame(StateManager& stateMgr, StateManager::State& out_state, std::string startCell)
            : mStateManager(stateMgr)
            , mState(out_state)
            , mStartCell(std::move(startCell))
        {
        }

        // Task 0142: a spot in a world's exterior. The world is found by its editor id, the spot must have land (the
        // pack's terrain), z comes from the land when not given, and the player is moved into the exterior cell of the
        // square the spot is in (the same path a door to an exterior takes).
        void startInExterior(MWBase::World& world, const MWWorld::NeutralStartTarget& target)
        {
            NeutralWorld::NeutralWorldStore* store = MWBase::Environment::get().getExtensionManager()->neutralWorldStore();
            if (store == nullptr)
                throw std::runtime_error("no game pack provides a neutral world");
            MWWorld::WorldModel& model = *MWBase::Environment::get().getWorldModel();
            const MWWorld::NeutralWorldLookup found = model.findNeutralWorld(*store, target.mWorld);
            if (found.mWorldspace.empty())
                throw std::runtime_error(found.mError);
            openeden::sdk::game::WorldCellSource* source = store->source(found.mGameId);
            if (source == nullptr)
                throw std::runtime_error("no world source for game '" + found.mGameId + "'");
            std::string error;
            const auto land = MWWorld::neutralLandHeight(*source, found.mWorld, target.mX, target.mY, error);
            if (!land)
                throw std::runtime_error("there is no land at (" + std::to_string(target.mX) + ", "
                    + std::to_string(target.mY) + ") in '" + target.mWorld + "': " + error);
            const auto square = MWWorld::neutralExteriorSquare(target.mX, target.mY, ESM::getCellSize(found.mWorldspace));
            if (!square)
                throw std::runtime_error("the spot is not a place the exterior grid can hold");
            const double z = target.mZ ? *target.mZ : *land + kDropAboveLand;
            MWWorld::CellStore& cell
                = model.getExterior(ESM::ExteriorCellLocation(square->first, square->second, found.mWorldspace));
            Log(Debug::Info) << "Neutral start: the player stands at (" << target.mX << ", " << target.mY << ", " << z
                             << ") in '" << target.mWorld << "' (square " << square->first << ", " << square->second
                             << ", land height " << *land << ")";
            ESM::Position pos;
            pos.pos[0] = static_cast<float>(target.mX);
            pos.pos[1] = static_cast<float>(target.mY);
            pos.pos[2] = static_cast<float>(z);
            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0.f;
            world.changeToCell(cell.getCell()->getId(), pos, true);

            mState = StateManager::State_Running;
            MWBase::Environment::get().getLuaManager()->gameLoaded();
            MWBase::Environment::get().getExtensionManager()->gameLoaded();
        }

        static constexpr double kDropAboveLand = 20.0; // units; the player settles on the ground from here

        bool run(float) override
        {
            Log(Debug::Info) << "Starting a new game (neutral world) in cell '" << mStartCell << "'";
            try
            {
                if (mStartCell.empty())
                    throw std::runtime_error("a neutral world session needs a start place: pass --start <the "
                                             "cell's editor id> or --start @<world>,<x>,<y>[,<z>]");
                const MWWorld::NeutralStartTarget target = MWWorld::parseNeutralStart(mStartCell);
                if (target.mKind == MWWorld::NeutralStartTarget::Kind::Invalid)
                    throw std::runtime_error(target.mError);
                auto world = MWBase::Environment::get().getWorld();
                world->startNewGame(true);
                MWBase::Environment::get().getWindowManager()->fadeScreenIn(1);

                if (target.mKind == MWWorld::NeutralStartTarget::Kind::Exterior)
                {
                    startInExterior(*world, target);
                    return false;
                }

                MWWorld::CellStore& cell = MWBase::Environment::get().getWorldModel()->getInterior(mStartCell);
                if (!cell.isNeutral())
                    throw std::runtime_error("'" + mStartCell + "' is not an interior cell of a game pack's neutral world");
                const auto start = MWWorld::neutralStartPosition(*cell.neutralCell());
                if (!start)
                    throw std::runtime_error("the neutral cell '" + mStartCell + "' has nothing that collides to stand on");
                Log(Debug::Info) << "Neutral start: the player stands at (" << start->x << ", " << start->y << ", "
                                 << start->z << ") in '" << mStartCell << "'";

                ESM::Position pos;
                pos.pos[0] = static_cast<float>(start->x);
                pos.pos[1] = static_cast<float>(start->y);
                pos.pos[2] = static_cast<float>(start->z);
                pos.rot[0] = pos.rot[1] = pos.rot[2] = 0.f;
                world->changeToInteriorCell(mStartCell, pos, true);

                mState = StateManager::State_Running;
                MWBase::Environment::get().getLuaManager()->gameLoaded();
                MWBase::Environment::get().getExtensionManager()->gameLoaded();
            }
            catch (std::exception& e)
            {
                mStateManager.error(std::string("Failed to start new game: ") + e.what());
            }
            return false;
        }
    };
}

#endif
