#ifndef OPENMW_MWWORLD_NEUTRALKEEPCELL_H
#define OPENMW_MWWORLD_NEUTRALKEEPCELL_H

// Task 0274 (the Helgen cart ride): which cells a change of the exterior grid unloads. An actor a game pack's script
// created (a relocated horse, a rider) lives in the cell it was made in and cannot move to another (the engine moves
// a neutral object inside its cell), so a grid change that unloaded that cell would take the actor away mid-walk.
// A neutral exterior cell that holds a living, enabled, script-created actor therefore stays loaded after the player
// has left its grid, until the actor is disabled or deleted (or the player changes cell by teleport or door).
// Pure: no engine type.

namespace MWWorld
{
    /// `outsideGrid`: the cell is farther from the player's cell than the grid's half size. `neutral`: the cell comes
    /// from a game pack. `holdsLiveCreatedActor`: it holds an enabled actor a script created.
    [[nodiscard]] constexpr bool unloadAfterGridChange(bool outsideGrid, bool neutral, bool holdsLiveCreatedActor)
    {
        return outsideGrid && !(neutral && holdsLiveCreatedActor);
    }
}

#endif
