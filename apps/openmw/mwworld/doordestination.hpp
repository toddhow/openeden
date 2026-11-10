#ifndef GAME_MWWORLD_DOORDESTINATION_H
#define GAME_MWWORLD_DOORDESTINATION_H

#include <string>

#include <components/esm3/loaddoor.hpp>

#include "livecellref.hpp"

namespace MWWorld
{
    /// The destination of a teleport door as the game shows it: the name of the destination cell as a localisation
    /// token ("#{sCell=Balmora}"). Moved out of MWClass::Door (task 0098 move 8, stage 8a) because the world's door
    /// markers use it too and the door class now lives in the game pack; the text is unchanged.
    std::string getDoorDestination(const LiveCellRef<ESM::Door>& door);
}

#endif
