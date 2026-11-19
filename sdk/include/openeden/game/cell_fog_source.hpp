#pragma once

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// The fog CURVE of one interior cell (task 0355, follow-up of stage d): Skyrim SE's XCLL carries, besides the fog near and
// far distances (CellLighting, world_model.hpp), a fog power and a fog maximum. The game's fog share at a distance d is
// pow(clamp((d - near) / (far - near), 0, 1), power) * max. Power 1 and max 1 are the plain linear fog.
//
// ABI choice: like CellImageSpaceSource, a NEW optional interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen world model (1.1) and source are untouched. A pack that does not implement it gives
// the linear fog.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kCellFogSourceVersion{ 0, 1 };

    class CellFogSource
    {
    public:
        virtual ~CellFogSource() = default;

        /// The fog power and maximum of the cell with the pack's id `cell` (WorldCell::id), as the data stores them
        /// (with the lighting template's values where the cell inherits them). False when the cell is unknown or has no
        /// lighting; `power` and `max` are then 1.
        [[nodiscard]] virtual bool fogCurveForCell(WorldId cell, double& power, double& max) = 0;
    };
}
