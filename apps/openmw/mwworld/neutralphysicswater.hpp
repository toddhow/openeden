#ifndef OPENMW_MWWORLD_NEUTRALPHYSICSWATER_H
#define OPENMW_MWWORLD_NEUTRALPHYSICSWATER_H

#include <optional>

// Task 0129 follow-up (swimming): the physics water level of a neutral session.
//
// Morrowind has ONE water height for the whole exterior and the physics world gets one water plane at it. A neutral
// exterior (Skyrim SE) has a height per grid square, so a single global plane cannot be right. toddhow's decision of
// 2026-11-12: the physics water plane follows the square the PLAYER stands in. The swim state of an actor does not
// depend on this plane (it reads its own cell's water level through the physics frame data and World::isUnderwater);
// the plane is the water layer that water walking and the water-surface rays hit.

namespace MWWorld
{
    class CellStore;

    /// The height of the physics water plane while the player is in `cell`, or nothing for "no plane". Only a neutral
    /// exterior square that has water gives a height; every other cell (Morrowind's, interiors, dry squares) gives
    /// nothing. The caller applies it only when `cell` is neutral, so Morrowind's own water handling stays untouched.
    std::optional<float> neutralPhysicsWaterLevel(const CellStore& cell);
}

#endif
