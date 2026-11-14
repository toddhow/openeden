#ifndef OPENEDEN_MWSTATE_AUTOFRAMES_H
#define OPENEDEN_MWSTATE_AUTOFRAMES_H

#include "../mwbase/statemanager.hpp"

namespace MWState
{
    // Task 0165: whether a frame counts toward --auto-screenshot and --auto-quit. Loading screens, operations and
    // videos do not (no scene, no Lua). A game whose player died (State_Ended: the main menu draws over it) does, or a
    // test run whose player is killed before the quit frame (the new game's player is attacked within about 10 s) never
    // quits.
    inline bool countsAsAutoFrame(MWBase::StateManager::State state, bool requiresScene, bool updateLua)
    {
        const bool inGame = state == MWBase::StateManager::State_Running || state == MWBase::StateManager::State_Ended;
        return inGame && requiresScene && updateLua;
    }
}

#endif
