#ifndef OPENMW_MWWORLD_NEUTRALCELLLIGHT_H
#define OPENMW_MWWORLD_NEUTRALCELLLIGHT_H

// Task 0355 stages f and j: what a game pack's INTERIOR cell says about its directional light and its distances.
//
//  * stage f, the interior sun: the rotation of the cell's directional ("key") light and its fade replace the engine's
//    fixed direction from above (RenderManager::configureAmbient: (-0.15, 0.15, 1) for every interior) and the colour's
//    full strength.
//  * stage j, light fade and clip: the lights of the cell fade out with the distance from the camera between the cell's
//    two distances (View::CollectLights::pack, which before had no distance rule, only the radius), and the interior's
//    far clip follows the cell's clip distance (RenderManager::updateProjectionMatrix).
//
// Everything here is read only for cells a game pack made (neutral cells); Morrowind cells never reach it.

#include <array>
#include <optional>

#include <openeden/game/cell_light_source.hpp>

namespace MWRender
{
    class RenderManager;
}

namespace MWWorld
{
    class CellStore;

    /// The directional light of an interior: the unit vector FROM a surface TOWARD the light and a linear intensity
    /// multiplier in [0, 1].
    struct NeutralInteriorSun
    {
        std::array<float, 3> towardLight{ 0.f, 0.f, 1.f };
        float intensity = 1.f;

        friend bool operator==(const NeutralInteriorSun&, const NeutralInteriorSun&) = default;
    };

    /// The sun a cell's settings ask for, or nothing (the engine's own direction and full colour stay): the setting
    /// off, no settings, a rotation or fade that is not a number.
    ///
    /// Rotation (xEdit: "Directional Rotation XY" and "Z", degrees; the convention is RECALLED-VERIFY and was checked
    /// against the data, see the task doc): Z is the elevation of the light above the horizon (90: straight above, the
    /// lighting templates' own default; 270 and 240: from below, the lava and glow caves), XY its heading, clockwise
    /// from north (+Y): toward-light = (cos Z sin XY, cos Z cos XY, sin Z); exactly (0, 0) is "not set" and keeps the
    /// old fixed direction from above (toddhow, 2026-11-19). Fade (toddhow, 2026-11-19): 0 is the full light; between 0
    /// and 1 it is the intensity; the rest clamps to [0, 1] (the data holds 0, 0.1 to 1, 5).
    [[nodiscard]] std::optional<NeutralInteriorSun> neutralInteriorSun(
        const openeden::sdk::game::CellLightSettings* settings, bool enabled);

    /// The cell's clip distance in units, or 0 for none: the setting off, no settings, not a number, not positive, or
    /// below `kMinNeutralClipDistance` (the smallest the five masters give is 1,800).
    inline constexpr float kMinNeutralClipDistance = 256.f;
    [[nodiscard]] float neutralClipDistance(const openeden::sdk::game::CellLightSettings* settings, bool enabled);

    /// The cell's light fade distances; begin = end = 0 for none: the setting off, no settings, not numbers, an end
    /// that is not positive, or a begin that is negative (then 0).
    struct NeutralLightFade
    {
        float begin = 0.f;
        float end = 0.f;

        friend bool operator==(const NeutralLightFade&, const NeutralLightFade&) = default;
    };
    [[nodiscard]] NeutralLightFade neutralLightFade(
        const openeden::sdk::game::CellLightSettings* settings, bool enabled);

    /// Called after the engine has configured a cell's ambient and fog (Scene::changeToInteriorCell, and for an
    /// exterior Scene::changeToExteriorCell): for a neutral interior asks the pack for the cell's settings and tells
    /// the renderer (sun, light fade, clip); for any other cell gives the renderer the "none" values. Morrowind cells
    /// therefore get exactly the values the renderer starts with.
    void applyNeutralCellLight(const CellStore& cell, MWRender::RenderManager& rendering);
}

#endif
