#pragma once

#include "debug_draw_frame.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>

namespace openeden::extension
{
    // Shapes drawn from the frame's primitives (task 0017). These are the choices the SDK leaves open:
    /// A point is a three-axis cross; its half length is `size` times this, in meters (size 1 is a 10 cm cross).
    inline constexpr float kPointHalfExtentPerSizeM = 0.05f;
    /// A label is not drawn as text (the engine has no in-world text path yet); a cross of this half length marks where
    /// it is, in meters.
    inline constexpr float kLabelMarkerHalfExtentM = 0.1f;
    /// A sphere is three great circles of this many segments.
    inline constexpr int kSphereCircleSegments = 16;
    inline constexpr std::size_t kLinesPerPoint = 3;
    inline constexpr std::size_t kLinesPerBox = 12;
    inline constexpr std::size_t kLinesPerSphere = 3 * static_cast<std::size_t>(kSphereCircleSegments);
    inline constexpr std::size_t kLinesPerLabel = 3;

    /**
     * @brief Turns the primitives of `frame` whose depthTest equals `depthTest` into line segments, one call of
     * `sink(from, to, color)` each, and returns how many it made. Positions are multiplied by `scale` (the game's
     * units per meter, applied here once); the SDK's line width is not used (the renderer draws one-pixel lines).
     * Pure and allocation free; `sink` may be any callable.
     */
    template <typename LineSink>
    std::size_t expandDebugDrawFrame(const DebugDrawFrame& frame, bool depthTest, float scale, LineSink&& sink)
    {
        using openeden::sdk::Color4f;
        using openeden::sdk::Vector3;
        std::size_t count = 0;
        auto emit = [&](const Vector3& a, const Vector3& b, const Color4f& color) {
            sink(Vector3{ a.x * scale, a.y * scale, a.z * scale }, Vector3{ b.x * scale, b.y * scale, b.z * scale },
                color);
            ++count;
        };
        auto cross = [&](const Vector3& c, float half, const Color4f& color) {
            emit({ c.x - half, c.y, c.z }, { c.x + half, c.y, c.z }, color);
            emit({ c.x, c.y - half, c.z }, { c.x, c.y + half, c.z }, color);
            emit({ c.x, c.y, c.z - half }, { c.x, c.y, c.z + half }, color);
        };

        for (const auto& line : frame.lines())
            if (line.depthTest == depthTest)
                emit(line.from, line.to, line.color);
        for (const auto& point : frame.points())
            if (point.depthTest == depthTest)
                cross(point.position, point.size * kPointHalfExtentPerSizeM, point.color);
        for (const auto& label : frame.labels())
            if (label.depthTest == depthTest)
                cross(label.position, kLabelMarkerHalfExtentM, label.color);
        for (const auto& box : frame.boxes())
        {
            if (box.depthTest != depthTest)
                continue;
            const Vector3 c[8] = {
                { box.min.x, box.min.y, box.min.z },
                { box.max.x, box.min.y, box.min.z },
                { box.max.x, box.max.y, box.min.z },
                { box.min.x, box.max.y, box.min.z },
                { box.min.x, box.min.y, box.max.z },
                { box.max.x, box.min.y, box.max.z },
                { box.max.x, box.max.y, box.max.z },
                { box.min.x, box.max.y, box.max.z },
            };
            for (int i = 0; i < 4; ++i)
            {
                emit(c[i], c[(i + 1) % 4], box.color); // bottom ring
                emit(c[4 + i], c[4 + (i + 1) % 4], box.color); // top ring
                emit(c[i], c[4 + i], box.color); // verticals
            }
        }
        for (const auto& sphere : frame.spheres())
        {
            if (sphere.depthTest != depthTest)
                continue;
            constexpr float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSphereCircleSegments);
            for (int axis = 0; axis < 3; ++axis)
            {
                auto pointOn = [&](int i) {
                    const float a = step * static_cast<float>(i);
                    const float u = sphere.radius * std::cos(a);
                    const float v = sphere.radius * std::sin(a);
                    Vector3 p = sphere.center;
                    if (axis == 0)
                    {
                        p.x += u;
                        p.y += v;
                    }
                    else if (axis == 1)
                    {
                        p.x += u;
                        p.z += v;
                    }
                    else
                    {
                        p.y += u;
                        p.z += v;
                    }
                    return p;
                };
                for (int i = 0; i < kSphereCircleSegments; ++i)
                    emit(pointOn(i), pointOn(i + 1), sphere.color);
            }
        }
        return count;
    }
}
