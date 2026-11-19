#pragma once

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// The rest of one interior cell's lighting (task 0355 stages f and j): Skyrim SE's XCLL carries, besides the colours
// and fog distances in CellLighting (world_model.hpp), the rotation of the directional ("key") light, its fade, the
// cell's clip distance and the begin and end distances over which the cell's lights fade out.
//
// ABI choice: like CellImageSpaceSource and CellFogSource, a NEW optional interface found by dynamic_cast on the game's
// registered WorldCellSource, so the frozen world model (1.1) and source are untouched. A pack that does not implement
// it gives the engine's fixed defaults.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kCellLightSourceVersion{ 0, 1 };

    /// All numbers as the data stores them, after the lighting template (Skyrim SE: LGTM) where the cell inherits them.
    struct CellLightSettings
    {
        /// The directional light's rotation in degrees (Skyrim SE: XCLL "directional rotation XY" and "Z").
        double rotationXY = 0.0;
        double rotationZ = 0.0;
        /// The directional light's fade (Skyrim SE: XCLL "directional fade"; the data holds 0 to 1 and a few 5s).
        double directionalFade = 0.0;
        /// The cell's far clip distance in units; 0 means none.
        double clipDistance = 0.0;
        /// Lights fade out between these distances from the camera, in units; 0 and 0 mean no fade.
        double lightFadeBegin = 0.0;
        double lightFadeEnd = 0.0;
    };

    class CellLightSource
    {
    public:
        virtual ~CellLightSource() = default;

        /// The settings of the cell with the pack's id `cell` (WorldCell::id). False when the cell is unknown, is not
        /// an interior or has no lighting; `out` is then left at its defaults.
        [[nodiscard]] virtual bool lightSettingsForCell(WorldId cell, CellLightSettings& out) = 0;
    };
}
