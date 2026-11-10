#ifndef OPENEDEN_COMPONENTS_WORLDMODEL_VALIDATE_H
#define OPENEDEN_COMPONENTS_WORLDMODEL_VALIDATE_H

#include <cstddef>
#include <string>

#include <openeden/game/world_model.hpp>

// The core re-validates every snapshot a pack hands over (design document section 4, "Hostile input": the core must not
// trust a pack). Pure functions on the SDK value types; no engine type.

namespace NeutralWorld
{
    struct Limits
    {
        std::size_t mMaxObjectsPerCell = 1u << 21; // 2,097,152; Skyrim's largest cells hold a few thousand
        std::size_t mMaxStringBytes = 4096; // editor ids, names, model paths
        double mMaxCoordinate = 1.0e7; // game units, positions, bounds and teleport destinations
        double mMaxScale = 1.0e3;
        double mQuaternionTolerance = 1.0e-4; // |norm - 1|
        std::int32_t mMaxGrid = 1 << 20;
    };

    /// True when the cell is usable. False with the first problem found, located (`cell 12: object 3: scale 0`); the
    /// cell is never changed.
    [[nodiscard]] bool validateCell(
        const openeden::sdk::game::WorldCell& cell, const Limits& limits, std::string& problem);

    [[nodiscard]] bool validateObject(
        const openeden::sdk::game::WorldObject& object, const Limits& limits, std::string& problem);

    /// The object with this placement id in a cell that passed validateCell (objects are in ascending id order), or
    /// nullptr.
    [[nodiscard]] const openeden::sdk::game::WorldObject* findObject(
        const openeden::sdk::game::WorldCell& cell, openeden::sdk::game::WorldId id);
}

#endif
