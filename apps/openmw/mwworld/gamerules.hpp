#ifndef OPENMW_MWWORLD_GAMERULES_H
#define OPENMW_MWWORLD_GAMERULES_H

#include <openeden/game/game_rules.hpp>

namespace MWWorld
{
    /// Installs the rules of a game pack into the core (task 0084 leftovers, stage I1): today the crafting rules,
    /// replacing whatever was installed before, including with "none" when the pack has no crafting (a game without
    /// crafting must not keep another game's). The core owns what the pack makes; `rules` itself stays the pack's.
    /// Main thread, before the windows are made (the crafting windows begin their sessions when they are constructed).
    ///
    /// openmw-lib never names a pack: a program (apps/openmw/main.cpp today) obtains the GameRules of the pack it
    /// plays and passes it here.
    void installGameRules(openeden::sdk::game::GameRules& rules);
}

#endif
