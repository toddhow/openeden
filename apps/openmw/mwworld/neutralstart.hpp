#ifndef OPENMW_MWWORLD_NEUTRALSTART_H
#define OPENMW_MWWORLD_NEUTRALSTART_H

#include <optional>
#include <string>
#include <string_view>

#include <openeden/game/world_source.hpp>
#include <openeden/game/world_terrain.hpp>

// Task 0140 (neutral world): where a neutral session starts. `--start <editor id>` names an interior (task 0109);
// `--start @<world editor id>,<x>,<y>[,<z>]` names a spot in a world's exterior, x and y in the game's units. The text
// is parsed here (pure), and the ground height under the spot is read from the pack's terrain (a source, no engine
// state). Main thread; no state.

namespace MWWorld
{
    struct NeutralStartTarget
    {
        enum class Kind
        {
            Interior, ///< `mCell` is the editor id, exactly as given
            Exterior, ///< `mWorld`, `mX`, `mY` and maybe `mZ`
            Invalid, ///< `mError` says why (a located message for the user)
        };

        Kind mKind = Kind::Invalid;
        std::string mCell;
        std::string mWorld;
        double mX = 0.0;
        double mY = 0.0;
        std::optional<double> mZ;
        std::string mError;
    };

    /// Anything that does not start with '@' is an interior name (also an empty text: the caller decides). The '@' form
    /// needs a world name, two finite numbers and optionally a third, separated by commas, spaces around the fields
    /// ignored; coordinates beyond +-10,000,000 units are refused.
    [[nodiscard]] NeutralStartTarget parseNeutralStart(std::string_view text);

    /// The land height under (x, y) in `world`, by bilinear interpolation of its vertices. A square the world has no
    /// land in borrows its parent's when WorldSpaceInfo says so; the world's default land height applies to a cell
    /// with land but no heights. `error` is set and nullopt returned when there is no land at the spot (the world is
    /// unknown, the source has no terrain, or no world on the parent chain has land there).
    [[nodiscard]] std::optional<double> neutralLandHeight(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId world, double x, double y, std::string& error);

    /// Bilinear height of one cell's land at the world position (x, y) (clamped to the cell). Heights are
    /// `heights[row * vertsPerSide + col]`, col east, row north. nullopt when the cell holds no usable heights.
    [[nodiscard]] std::optional<double> neutralCellHeightAt(
        const openeden::sdk::game::TerrainCell& cell, double cellSizeUnits, double x, double y);
}

#endif
