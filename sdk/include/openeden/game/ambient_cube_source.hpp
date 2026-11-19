#pragma once

#include <array>
#include <cstdint>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// The ambient cube of ONE CELL (task 0355 stage e): 98 percent of Skyrim SE's interiors light their surfaces with six
// ambient colours, one per axis direction (the cell's XCLL, or its lighting template's DALC when the cell inherits its
// ambient), instead of one flat colour. A surface takes the colours of the faces it looks at, weighed by its normal's
// squared components, so a floor, a wall and a ceiling are not lit alike.
//
// ABI choice: like CellImageSpaceSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen world model and source (1.x) are untouched. A pack that does not implement it gives
// no cube and the cell keeps its flat ambient.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kAmbientCubeSourceVersion{ 0, 1 };

    /// Six 8 bit colours in the order +X, -X, +Y, -Y, +Z, -Z of the world (Z up), as the game stores them.
    struct AmbientCube
    {
        std::array<std::array<std::uint8_t, 3>, 6> colour{};

        friend bool operator==(const AmbientCube&, const AmbientCube&) = default;
    };

    class AmbientCubeSource
    {
    public:
        virtual ~AmbientCubeSource() = default;

        /// The cube of the cell with the pack's id `cell` (WorldCell::id), once the cell has been loaded. False when
        /// the cell has none (no lighting record, or all six colours are black); `out` is then left as it was.
        [[nodiscard]] virtual bool ambientCubeForCell(WorldId cell, AmbientCube& out) = 0;
    };
}
