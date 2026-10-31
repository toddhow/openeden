#ifndef CSV_RENDER_SELECTIONMARKERGEOMETRY_H
#define CSV_RENDER_SELECTIONMARKERGEOMETRY_H

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/misc/vsgmath.hpp>

/// The transform gizmo of ObjectMarker as plain data, with no window or GPU. It replaces files/opencs/selectionmarker.osgt:
/// the numbers below are that file's, after its transforms, and its triangle counts are kept (see testselectionmarker.cpp).
namespace CSVRender::SelectionMarkerGeometry
{
    /// Unindexed triangles of one colour. mAxis is 0, 1 or 2 for X, Y or Z.
    struct Part
    {
        std::string mName;
        int mAxis = 0;
        bool mAlpha = false;
        std::vector<vsg::vec3> mVertices;
    };

    inline constexpr int sAxisSegments = 16;
    inline constexpr float sShaftStart = 0.3803f;
    inline constexpr float sShaftRadius = 0.03296f;
    inline constexpr float sShaftEnd = 1.9978f;
    inline constexpr float sHeadRadius = 0.1318f;
    inline constexpr float sHeadTip = 2.5327f;
    inline constexpr float sCubeEnd = 2.2361f;
    inline constexpr float sCubeHalfSide = 0.1183f;
    inline constexpr float sWallMin = 1.1408f;
    inline constexpr float sWallMax = 1.3944f;
    inline constexpr float sPaneMin = 1.1662f;
    inline constexpr float sPaneMax = 1.3690f;
    inline constexpr float sRingRadius = 1.949f;
    inline constexpr float sRingTube = 0.049f;
    inline constexpr int sRingSegments = 48;
    inline constexpr int sRingTubeSegments = 12;
    inline constexpr float sPaneAlpha = 0.5f;
    /// The OSG marker dimmed each material to this share until highlighted (objectmarker.cpp).
    inline constexpr float sDimFactor = 0.25f;
    /// SceneWidget::update: a marker keeps its size on screen from this distance on.
    inline constexpr float sDistanceForUnitScale = 75.f;

    namespace Detail
    {
        /// A vertex given for the X axis, for this axis. A cyclic swap keeps triangles facing the same way.
        inline vsg::vec3 forAxis(const vsg::vec3& v, int axis)
        {
            switch (axis)
            {
                case 1:
                    return vsg::vec3(v.z, v.x, v.y);
                case 2:
                    return vsg::vec3(v.y, v.z, v.x);
                default:
                    return v;
            }
        }

        inline void triangle(std::vector<vsg::vec3>& out, const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c)
        {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }

        inline void quad(
            std::vector<vsg::vec3>& out, const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec3& d)
        {
            triangle(out, a, b, c);
            triangle(out, a, c, d);
        }

        inline vsg::vec3 onCircle(float x, float radius, int index, int segments)
        {
            const float angle = 2.f * Misc::VsgMath::PIf * static_cast<float>(index) / static_cast<float>(segments);
            return vsg::vec3(x, radius * std::cos(angle), radius * std::sin(angle));
        }

        /// The shaft's cap and sides, from the middle of the marker outwards along X.
        inline void addShaft(std::vector<vsg::vec3>& out, float end)
        {
            for (int i = 0; i < sAxisSegments; ++i)
            {
                const vsg::vec3 a0 = onCircle(sShaftStart, sShaftRadius, i, sAxisSegments);
                const vsg::vec3 a1 = onCircle(sShaftStart, sShaftRadius, i + 1, sAxisSegments);
                const vsg::vec3 b0 = onCircle(end, sShaftRadius, i, sAxisSegments);
                const vsg::vec3 b1 = onCircle(end, sShaftRadius, i + 1, sAxisSegments);
                triangle(out, vsg::vec3(sShaftStart, 0, 0), a1, a0);
                quad(out, a0, a1, b1, b0);
            }
        }

        inline std::vector<vsg::vec3> arrow()
        {
            std::vector<vsg::vec3> out;
            addShaft(out, sShaftEnd);
            for (int i = 0; i < sAxisSegments; ++i)
            {
                const vsg::vec3 inner0 = onCircle(sShaftEnd, sShaftRadius, i, sAxisSegments);
                const vsg::vec3 inner1 = onCircle(sShaftEnd, sShaftRadius, i + 1, sAxisSegments);
                const vsg::vec3 outer0 = onCircle(sShaftEnd, sHeadRadius, i, sAxisSegments);
                const vsg::vec3 outer1 = onCircle(sShaftEnd, sHeadRadius, i + 1, sAxisSegments);
                quad(out, inner0, inner1, outer1, outer0);
                triangle(out, outer0, outer1, vsg::vec3(sHeadTip, 0, 0));
            }
            return out;
        }

        inline std::vector<vsg::vec3> scaleArrow()
        {
            std::vector<vsg::vec3> out;
            addShaft(out, sShaftEnd);
            const float x0 = sShaftEnd;
            const float x1 = sCubeEnd;
            const float s = sCubeHalfSide;
            const vsg::vec3 c000(x0, -s, -s), c001(x0, -s, s), c010(x0, s, -s), c011(x0, s, s);
            const vsg::vec3 c100(x1, -s, -s), c101(x1, -s, s), c110(x1, s, -s), c111(x1, s, s);
            quad(out, c000, c001, c011, c010); // -x
            quad(out, c100, c110, c111, c101); // +x
            quad(out, c000, c100, c101, c001); // -y
            quad(out, c010, c011, c111, c110); // +y
            quad(out, c000, c010, c110, c100); // -z
            quad(out, c001, c101, c111, c011); // +z
            return out;
        }

        /// A frame around a pane in the plane x = 0, seen from both sides.
        inline std::vector<vsg::vec3> wallFrame()
        {
            std::vector<vsg::vec3> out;
            const auto bar = [&](float y0, float z0, float y1, float z1) {
                const vsg::vec3 a(0, y0, z0), b(0, y1, z0), c(0, y1, z1), d(0, y0, z1);
                quad(out, a, b, c, d);
                quad(out, a, d, c, b);
            };
            bar(sWallMin, sWallMin, sWallMax, sPaneMin);
            bar(sWallMin, sPaneMax, sWallMax, sWallMax);
            bar(sWallMin, sPaneMin, sPaneMin, sPaneMax);
            bar(sPaneMax, sPaneMin, sWallMax, sPaneMax);
            return out;
        }

        inline std::vector<vsg::vec3> wallPane()
        {
            std::vector<vsg::vec3> out;
            const vsg::vec3 a(0, sPaneMin, sPaneMin), b(0, sPaneMax, sPaneMin), c(0, sPaneMax, sPaneMax),
                d(0, sPaneMin, sPaneMax);
            quad(out, a, b, c, d);
            quad(out, a, d, c, b);
            return out;
        }

        inline std::vector<vsg::vec3> ring()
        {
            std::vector<vsg::vec3> out;
            const auto point = [](int major, int minor) {
                const float a = 2.f * Misc::VsgMath::PIf * static_cast<float>(major) / static_cast<float>(sRingSegments);
                const float b
                    = 2.f * Misc::VsgMath::PIf * static_cast<float>(minor) / static_cast<float>(sRingTubeSegments);
                const float radius = sRingRadius + sRingTube * std::cos(b);
                return vsg::vec3(sRingTube * std::sin(b), radius * std::cos(a), radius * std::sin(a));
            };
            for (int major = 0; major < sRingSegments; ++major)
                for (int minor = 0; minor < sRingTubeSegments; ++minor)
                    quad(out, point(major, minor), point(major + 1, minor), point(major + 1, minor + 1),
                        point(major, minor + 1));
            return out;
        }

        inline Part makePart(const std::string& suffix, int axis, bool alpha, const std::vector<vsg::vec3>& vertices)
        {
            Part part{ std::string(1, static_cast<char>('X' + axis)) + suffix, axis, alpha, {} };
            part.mVertices.reserve(vertices.size());
            for (const auto& v : vertices)
                part.mVertices.push_back(forAxis(v, axis));
            return part;
        }

        inline std::vector<Part> axesAndWalls(
            const std::vector<vsg::vec3>& arrowShape, const std::string& axisSuffix, const std::string& wallSuffix)
        {
            std::vector<Part> parts;
            for (int axis = 0; axis < 3; ++axis)
            {
                parts.push_back(makePart(axisSuffix, axis, false, arrowShape));
                parts.push_back(makePart(wallSuffix, axis, false, wallFrame()));
                parts.push_back(makePart(wallSuffix, axis, true, wallPane()));
            }
            return parts;
        }
    }

    inline std::vector<Part> moveParts()
    {
        return Detail::axesAndWalls(Detail::arrow(), "_Axis", "_Wall");
    }

    inline std::vector<Part> scaleParts()
    {
        return Detail::axesAndWalls(Detail::scaleArrow(), "_Axis_Scale", "_Wall_Scale");
    }

    inline std::vector<Part> rotateParts()
    {
        std::vector<Part> parts;
        for (int axis = 0; axis < 3; ++axis)
            parts.push_back(Detail::makePart("_Axis_Rot", axis, false, Detail::ring()));
        return parts;
    }

    /// The old materials: emission red, green and blue, dimmed until highlighted.
    inline vsg::vec4 axisColour(int axis, bool highlighted, bool alpha)
    {
        static constexpr std::array<std::array<float, 3>, 3> colours
            = { { { 0.8f, 0.123281f, 0.11568f }, { 0.190456f, 0.8f, 0.229407f }, { 0.0512699f, 0.38982f, 0.8f } } };
        const float factor = highlighted ? 1.f : sDimFactor;
        const auto& c = colours[static_cast<std::size_t>(axis)];
        return vsg::vec4(c[0] * factor, c[1] * factor, c[2] * factor, alpha ? sPaneAlpha : 1.f);
    }

    /// What the old clip plane through the marker's centre left of a ring: triangles on the far side from the eye collapse
    /// to a point, so that they neither draw nor get picked. \p eye is the eye's position relative to the marker's centre.
    /// There is no clip plane in the pipelines. \p out must be as long as \p full, and is written without allocating.
    inline void clipRing(const std::vector<vsg::vec3>& full, const vsg::vec3& eye, std::vector<vsg::vec3>& out)
    {
        for (std::size_t i = 0; i + 2 < full.size(); i += 3)
        {
            const float side = vsg::dot(full[i] + full[i + 1] + full[i + 2], eye);
            if (side >= 0.f)
            {
                out[i] = full[i];
                out[i + 1] = full[i + 1];
                out[i + 2] = full[i + 2];
            }
            else
            {
                out[i] = vsg::vec3();
                out[i + 1] = vsg::vec3();
                out[i + 2] = vsg::vec3();
            }
        }
    }

    /// SceneWidget::update: should the marker turn by half a turn about z, so that its walls stay near the camera?
    /// \param cameraLocal the eye in the marker's own axes.
    inline bool cameraWantsFlip(const vsg::vec3& cameraLocal)
    {
        const bool inFrontRightQuadrant = cameraLocal.x > 0.1f && cameraLocal.y > 0.1f;
        const bool significantlyBehind = cameraLocal.x < 1.f && cameraLocal.y < 1.f;
        return !inFrontRightQuadrant && significantlyBehind;
    }

    inline float distanceScale(float distance)
    {
        return std::max(distance / sDistanceForUnitScale, 1.f);
    }

    /// ObjectMarker::hitBehindMarker, for the half of the rotate rings that the clip hid.
    inline bool hitBehind(const vsg::dvec3& hit, const vsg::dvec3& markerPosition, const vsg::dvec3& forward)
    {
        return vsg::dot(hit - markerPosition, forward) > 0;
    }

    /// A box's twelve edges as a list of lines: how a selected object is marked (the OSG editor's osgFX::Scribe has no
    /// counterpart, task 0014's C7 entry).
    inline std::vector<vsg::vec3> boxLines(const vsg::vec3& min, const vsg::vec3& max)
    {
        std::vector<vsg::vec3> lines;
        lines.reserve(24);
        const auto corner = [&](int x, int y, int z) {
            return vsg::vec3(x ? max.x : min.x, y ? max.y : min.y, z ? max.z : min.z);
        };
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
            {
                lines.push_back(corner(0, a, b));
                lines.push_back(corner(1, a, b));
                lines.push_back(corner(a, 0, b));
                lines.push_back(corner(a, 1, b));
                lines.push_back(corner(a, b, 0));
                lines.push_back(corner(a, b, 1));
            }
        return lines;
    }
}

#endif
