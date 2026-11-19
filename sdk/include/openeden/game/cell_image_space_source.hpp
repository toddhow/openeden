#pragma once

#include <openeden/game/weather_source.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// The image space of ONE CELL (task 0355 stage c): Skyrim SE's interiors name an image space record (CELL XCIM -> IMGS)
// that grades the picture while the player is inside (cool dungeons, warm inns). The grade has the same four numbers as
// the weather's (ImageSpaceGrade, weather_source.hpp) and the core feeds it to the same renderer path.
//
// ABI choice: like DisplayNameSource and WeatherSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen world model and source (1.x) are untouched. A pack that does not implement it gives
// no cell grade.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kCellImageSpaceSourceVersion{ 0, 1 };

    class CellImageSpaceSource
    {
    public:
        virtual ~CellImageSpaceSource() = default;

        /// The grade of the cell with the pack's id `cell` (WorldCell::id). False when the cell names no image space,
        /// the record is unknown or has no cinematic values; `out` is then left neutral.
        [[nodiscard]] virtual bool imageSpaceForCell(WorldId cell, ImageSpaceGrade& out) = 0;
    };
}
