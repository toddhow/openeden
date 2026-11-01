#ifndef OPENEDEN_RENDER_SKYVISDEBUG_H
#define OPENEDEN_RENDER_SKYVISDEBUG_H

// OpenEden (task 0036, stage e): the `gi debug` view of the exterior sky-visibility field. Every probe near a point is
// drawn as a small octahedron, grey, brighter where the probe sees more sky (its share of the sky that open flat ground
// sees, so open ground is white and a place under a bridge is dark). The primitives go to a DebugPrimitiveSink, as the
// light probe markers do (probedebug.hpp). No game types.

#include <cmath>
#include <cstddef>

#include "probedebug.hpp"
#include "skyvismath.hpp"

namespace Render::Gi
{
    // A marker's radius as a share of the spacing between columns.
    constexpr float skyVisDebugRadiusFraction = 0.08f;
    // How far from the camera the markers are drawn (game units): about 80 columns of 8 levels with the default spacing.
    constexpr float skyVisDebugRadius = 1024.f;

    // The most markers drawSkyVisibility can draw for columns `spacing` apart, `levels` levels and this radius.
    inline std::size_t skyVisDebugMaxMarkers(float spacing, std::uint32_t levels, float radius)
    {
        const std::size_t across = static_cast<std::size_t>(2.f * radius / spacing) + 1;
        return across * across * levels;
    }

    // One marker per probe of `field` whose column is within `radius` (horizontally) of `centre`. Returns how many.
    inline std::size_t drawSkyVisibility(
        SceneUtil::DebugPrimitiveSink& sink, const SkyVisField& field, const vsg::vec3& centre, float radius)
    {
        const std::uint32_t side = skyVisColumnsPerSide(field.spacing);
        const float originX = static_cast<float>(field.cellX) * exteriorCellSize;
        const float originY = static_cast<float>(field.cellY) * exteriorCellSize;
        const float markerRadius = field.spacing * skyVisDebugRadiusFraction;
        const float openGround = hemisphereVisibility()[0];
        std::size_t drawn = 0;
        for (std::uint32_t iy = 0; iy < side; ++iy)
            for (std::uint32_t ix = 0; ix < side; ++ix)
            {
                const float x = originX + static_cast<float>(ix) * field.spacing;
                const float y = originY + static_cast<float>(iy) * field.spacing;
                if (std::hypot(x - centre.x, y - centre.y) > radius)
                    continue;
                const std::size_t column = static_cast<std::size_t>(ix) + static_cast<std::size_t>(side) * iy;
                for (std::uint32_t level = 0; level < field.levels; ++level)
                {
                    const VisSh& v = field.probes[column * field.levels + level];
                    const float share = v[0] / openGround;
                    const vsg::vec3 position(
                        x, y, skyVisLevelHeight(field.base[column], level, field.levelSpacing));
                    drawDebugMarker(sink, position, markerRadius, debugColor(vsg::vec3(share, share, share)));
                    ++drawn;
                }
            }
        return drawn;
    }
}

#endif
