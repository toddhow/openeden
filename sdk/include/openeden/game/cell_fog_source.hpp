#pragma once

#include <array>
#include <cstdint>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// The fog CURVE of one interior cell (task 0355, follow-up of stage d): Skyrim SE's XCLL carries, besides the fog near
// and far distances (CellLighting, world_model.hpp), a fog power and a fog maximum. The game's fog share at a distance
// d is pow(clamp((d - near) / (far - near), 0, 1), power) * max. Power 1 and max 1 are the plain linear fog.
//
// ABI choice: like CellImageSpaceSource, a NEW optional interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen world model (1.1) and source are untouched. A pack that does not implement it gives
// the linear fog.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kCellFogSourceVersion{ 0, 2 };

    class CellFogSource
    {
    public:
        virtual ~CellFogSource() = default;

        /// The fog power and maximum of the cell with the pack's id `cell` (WorldCell::id), as the data stores them
        /// (with the lighting template's values where the cell inherits them). False when the cell is unknown or has no
        /// lighting; `power` and `max` are then 1.
        [[nodiscard]] virtual bool fogCurveForCell(WorldId cell, double& power, double& max) = 0;

        /// Added in 0.2 (task 0362 item 1): the fog's FAR colour of the cell (XCLL's, with the lighting template's when
        /// the cell inherits its fog colour), 8 bit red, green, blue. CellLighting::fogColor is the near colour; the
        /// fog's colour goes from near to far with its linear share. False (the default) when the cell has none.
        [[nodiscard]] virtual bool fogFarColorForCell(WorldId cell, std::array<std::uint8_t, 3>& colour)
        {
            (void)cell;
            colour = {};
            return false;
        }
    };
}
