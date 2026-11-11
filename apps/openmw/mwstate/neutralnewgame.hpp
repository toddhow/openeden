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

        bool run(float) override
        {
            Log(Debug::Info) << "Starting a new game (neutral world) in cell '" << mStartCell << "'";
            try
            {
                if (mStartCell.empty())
                    throw std::runtime_error(
                        "a neutral world session needs a start cell: pass --start <the cell's editor id>");
                auto world = MWBase::Environment::get().getWorld();
                world->startNewGame(true);
                MWBase::Environment::get().getWindowManager()->fadeScreenIn(1);

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
