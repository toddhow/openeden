#ifndef OPENEDEN_RENDER_PROBEDEBUG_H
#define OPENEDEN_RENDER_PROBEDEBUG_H

// OpenEden (task 0028, stage d): the `gi debug` view. Every light probe is drawn as a small octahedron (eight
// triangles) coloured by the ambient light it gives an upward facing surface, and every reflection probe as a larger
// one coloured by what it sees straight up. The primitives go to a DebugPrimitiveSink, so the renderer draws them with
// its debug geometry and tests can count them. Colours are display-encoded here, because the debug draw shows its
// colours as they are written. No game types.

#include <algorithm>
#include <cstddef>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/sceneutil/detourdebugdraw.hpp>

#include "hdrmath.hpp"
#include "probes.hpp"
#include "probesamplermath.hpp"
#include "reflectionmath.hpp"
#include "reflectionprobes.hpp"

namespace Render::Gi
{
    // Triangles (and so vertices) one marker takes.
    constexpr std::size_t debugMarkerTriangles = 8;
    constexpr std::size_t debugMarkerVertices = 3 * debugMarkerTriangles;
    constexpr float debugMarkerAlpha = 0.9f;
    // A light probe marker's radius as a share of the spacing, and a reflection probe marker's radius in game units.
    constexpr float debugProbeRadiusFraction = 0.1f;
    constexpr float debugReflectionRadius = 16.f;

    // A colour in linear light, clamped to [0, 1] and encoded for display.
    inline vsg::vec4 debugColor(const vsg::vec3& linear)
    {
        const vsg::vec3 clamped(
            std::clamp(linear.x, 0.f, 1.f), std::clamp(linear.y, 0.f, 1.f), std::clamp(linear.z, 0.f, 1.f));
        const vsg::vec3 encoded = Hdr::toEncoded(clamped);
        return vsg::vec4(encoded.x, encoded.y, encoded.z, debugMarkerAlpha);
    }

    // An octahedron: six points a radius from the centre along the axes, eight faces.
    inline void drawDebugMarker(
        SceneUtil::DebugPrimitiveSink& sink, const vsg::vec3& center, float radius, const vsg::vec4& color)
    {
        const vsg::vec3 px = center + vsg::vec3(radius, 0.f, 0.f);
        const vsg::vec3 nx = center - vsg::vec3(radius, 0.f, 0.f);
        const vsg::vec3 py = center + vsg::vec3(0.f, radius, 0.f);
        const vsg::vec3 ny = center - vsg::vec3(0.f, radius, 0.f);
        const vsg::vec3 pz = center + vsg::vec3(0.f, 0.f, radius);
        const vsg::vec3 nz = center - vsg::vec3(0.f, 0.f, radius);
        sink.addTriangle(px, py, pz, color);
        sink.addTriangle(py, nx, pz, color);
        sink.addTriangle(nx, ny, pz, color);
        sink.addTriangle(ny, px, pz, color);
        sink.addTriangle(py, px, nz, color);
        sink.addTriangle(nx, py, nz, color);
        sink.addTriangle(ny, nx, nz, color);
        sink.addTriangle(px, ny, nz, color);
    }

    // One marker per light probe of the volume.
    inline void drawProbes(SceneUtil::DebugPrimitiveSink& sink, const Volume& volume)
    {
        const float radius = volume.spacing * debugProbeRadiusFraction;
        const std::size_t sx = volume.size[0];
        const std::size_t sy = volume.size[1];
        for (std::size_t i = 0; i < volume.probes.size(); ++i)
        {
            const vsg::vec3 position = volume.origin
                + vsg::vec3(
                      static_cast<float>(i % sx), static_cast<float>((i / sx) % sy), static_cast<float>(i / (sx * sy)))
                    * volume.spacing;
            drawDebugMarker(sink, position, radius, debugColor(shAmbient(volume.probes[i], vsg::vec3(0.f, 0.f, 1.f))));
        }
    }

    // One marker per reflection probe, coloured by the sharp map's texel straight up (+Z, the middle of the map).
    inline void drawReflectionProbes(SceneUtil::DebugPrimitiveSink& sink, const ReflectionSet& set)
    {
        const std::size_t middle = static_cast<std::size_t>(set.resolution / 2) * set.resolution + set.resolution / 2;
        for (const ReflectionProbe& probe : set.probes)
        {
            const vsg::vec3 up = middle < probe.texels.size() ? probe.texels[middle] : vsg::vec3(0.f, 0.f, 0.f);
            drawDebugMarker(sink, probe.position, debugReflectionRadius, debugColor(up));
        }
    }
}

#endif
