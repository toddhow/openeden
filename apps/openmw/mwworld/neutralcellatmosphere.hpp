#ifndef OPENMW_MWWORLD_NEUTRALCELLATMOSPHERE_H
#define OPENMW_MWWORLD_NEUTRALCELLATMOSPHERE_H

// Task 0355 stages c and d: what a game pack's INTERIOR cell says about the air and the picture.
//
//  * stage c, the cell's image space: the pack's CellImageSpaceSource gives the grade (saturation, brightness,
//    contrast, tint) of the image space the cell names. It goes to the SAME renderer path as the weather's image space
//    (RenderManager::setImageSpaceStops / setImageSpaceGrade, the tonemap's GRADE define), so there is one mechanism.
//    A neutral interior that names none gets no grade (the picture is not graded); it does not inherit the grade the
//    last exterior weather left behind. Exteriors keep the weather's image space.
//  * stage d, the cell's fog: the cell's fog near and far distances (CellLighting::fogNear / fogFar, taken from the
//    lighting template when the cell inherits them) become the interior's linear fog start and end.
//
// Everything here is read only for cells a game pack made (neutral cells); Morrowind cells never reach it.

#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include <components/esm3/loadcell.hpp>

#include <openeden/game/weather_source.hpp>
#include "neutralfogcurve.hpp"
#include <openeden/game/world_model.hpp>

namespace MWRender
{
    class RenderManager;
}

namespace MWWorld
{
    class Cell;
    class CellStore;

    /// The arguments of RenderManager::setImageSpaceStops / setImageSpaceGrade for one cell grade.
    struct NeutralCellGrade
    {
        bool on = false;
        float stops = 0.f; ///< log2 of the brightness multiplier (floor 0.25: the weather manager's floor)
        float saturation = 1.f;
        float contrast = 1.f;
        float tintAmount = 0.f;
        std::array<float, 3> tint{ 1.f, 1.f, 1.f };

        friend bool operator==(const NeutralCellGrade&, const NeutralCellGrade&) = default;
    };

    /// How much of an interior image space's CONTRAST is applied (0 none, 1 all). The game's contrast pivots on the
    /// scene's average brightness; ours pivots on mid grey (Render::ImageSpace), so in a dark dungeon a contrast of 1.2
    /// crushes the picture to black (seen in Bleak Falls Barrow, task 0355 stage c, and the same reason task 0257 left
    /// Night Eye's contrast neutral). Until the pivot is the scene's, contrast is left out of the cell grade.
    inline constexpr float kCellGradeContrastShare = 0.f;

    /// Task 0361b follow-up: the same rule for a pack WEATHER's image space (the exterior): with a contrast above 1 the
    /// ground in a cliff's shadow (display value 0.08) went to 0 under the tonemap's GRADE; objects in the same light
    /// stayed lit. 0 = contrast not applied.
    inline constexpr float kWeatherGradeContrastShare = 0.f;

    /// The contrast handed to the renderer for a weather grade: 1 + (contrast - 1) * share; a value that is not a number
    /// is the neutral 1.
    [[nodiscard]] inline float neutralWeatherGradeContrast(
        float contrast, float share = kWeatherGradeContrastShare)
    {
        if (!std::isfinite(contrast) || !std::isfinite(share))
            return 1.f;
        return 1.f + (contrast - 1.f) * share;
    }

    /// The conversion, with no engine type. A null grade gives the "off" grade (nothing graded, no exposure change).
    /// A value that is not a number is the neutral one (as in the weather path). `contrastShare` scales the grade's
    /// distance from the neutral contrast 1.
    [[nodiscard]] NeutralCellGrade neutralCellGrade(
        const openeden::sdk::game::ImageSpaceGrade* grade, float contrastShare = kCellGradeContrastShare);

    /// The linear fog of an interior, in the game's units.
    struct NeutralInteriorFog
    {
        float start = 0.f;
        float end = 0.f;

        friend bool operator==(const NeutralInteriorFog&, const NeutralInteriorFog&) = default;
    };

    /// The fog a cell's lighting asks for, or nothing: the setting off, no lighting, distances that are not numbers,
    /// a negative near, or a far that is not past the near (Skyrim writes 0 and 0 for "no fog"). Both are in units.
    [[nodiscard]] std::optional<NeutralInteriorFog> neutralInteriorFog(
        const openeden::sdk::game::CellLighting* lighting, bool enabled);

    /// Task 0355 (follow-up of stage d): the fog power and maximum of a cell, made safe. The data is mostly sane (power
    /// 0.4 to 2.5, max 0.2 to 1) and has a few wild numbers (max 6000, 8, 13000, and 22 zeros that stand for "not set"):
    /// a power that is not a number or outside the encodable range [1/64, 255/64] is 1, and a max that is not a number,
    /// zero or below, or above 1 is 1. Both 1 (or `enabled` false, or no values) gives the plain linear fog: nothing.
    [[nodiscard]] std::optional<NeutralFogCurve> neutralFogCurve(
        std::optional<std::pair<double, double>> powerAndMax, bool enabled);

    /// The engine's cell object of a neutral interior record: `Cell(record)`, plus the fog the pack's lighting asks for
    /// when [Game] neutral interior fog is on (task 0355 stage d), with its power and maximum when `curve` is set.
    [[nodiscard]] Cell makeNeutralInteriorCell(const ESM::Cell& record, const openeden::sdk::game::WorldCell& cell,
        std::optional<NeutralFogCurve> curve = std::nullopt);

    /// Task 0355 stage c: whether the tonemap carries the image space grade (the GRADE shader define). The older two
    /// settings ask for it as before; the cell setting counts only in a neutral world, because it is on by default and
    /// a Morrowind session must keep its tonemap pipeline.
    [[nodiscard]] bool neutralGradeShaderWanted(
        bool neutralWorld, bool weatherImageSpace, bool imageSpaceEffects, bool cellImageSpace);

    /// Called when the player enters a cell, and again every frame while it returns true. Only for a neutral INTERIOR
    /// with [Game] neutral world and [Game] neutral cell image space: asks the pack for the cell's grade and tells the
    /// renderer (the "off" grade when it has none). Never waits: returns true while the pack is still reading its data
    /// ("not yet"; the renderer is told nothing then), false once it has answered or when it does not apply.
    [[nodiscard]] bool applyNeutralCellImageSpace(const CellStore& cell, MWRender::RenderManager& rendering);

    /// Task 0355 stage e: called when a cell is loaded, after the flat ambient is configured. For a neutral interior
    /// with [Game] neutral ambient cube asks the pack for the cell's ambient cube and hands it to the renderer; for any
    /// other cell (an exterior, a Morrowind interior) it tells the renderer there is none. Does nothing when the
    /// renderer has no room for a cube (the setting or [Game] neutral world off).
    void applyNeutralAmbientCube(const CellStore& cell, MWRender::RenderManager& rendering);

    /// Called when the player enters an exterior of a neutral world: puts the renderer's grade back to "off" (the
    /// weather sets its own every frame when [Game] neutral weather image space is on), so an interior's grade does
    /// not stay on the street.
    void resetNeutralCellImageSpace(const CellStore& cell, MWRender::RenderManager& rendering);
}

#endif
