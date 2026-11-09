#ifndef OPENMW_MWMECHANICS_REPAIR_FORWARD_H
#define OPENMW_MWMECHANICS_REPAIR_FORWARD_H

// Moved to game_packs/morrowind (task 0085 move 2): the Morrowind game pack owns the crafting rules. This header only
// keeps the old include path alive for the characterization tests in apps/openmw_tests/mwmechanics, which pin the rules
// and are not edited by the move. The game's own code reaches the rules through the crafting contract
// (sdk/include/openeden/game/crafting.hpp, MWWorld::craftingRules()), never through this header.
#include "game_packs/morrowind/include/game_morrowind/repair.hpp"

#endif
