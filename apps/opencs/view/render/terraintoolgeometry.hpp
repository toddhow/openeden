#ifndef CSV_RENDER_TERRAINTOOLGEOMETRY_H
#define CSV_RENDER_TERRAINTOOLGEOMETRY_H

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <vsg/maths/vec3.h>

#include <components/esm3/loadland.hpp>
#include <components/misc/vsgmath.hpp>

#include "../../model/world/cellcoordinates.hpp"

/// The lines that the terrain tools draw and the sums they make, with no window or GPU, as the OSG editor's
/// brushdraw.cpp, terrainselection.cpp and TerrainShapeMode have them.
namespace CSVRender::TerrainToolGeometry
{
    inline constexpr float sOutlineHeight = 1.f;
    inline constexpr float sCrossHeadSize = 8.f;
    inline constexpr int sMaxSquareResolution = 128;
    inline constexpr int sCirclePoints = 128;

    inline float landSizeFactor(bool textureMode)
    {
        return textureMode ? static_cast<float>(ESM::Land::REAL_SIZE) / static_cast<float>(ESM::Land::LAND_TEXTURE_SIZE)
                           : static_cast<float>(ESM::Land::REAL_SIZE) / static_cast<float>(ESM::Land::LAND_SIZE - 1);
    }

    inline vsg::dvec3 snapPoint(const vsg::dvec3& point, bool textureMode)
    {
        if (textureMode)
        {
            const std::pair<int, int> xy = CSMWorld::CellCoordinates::toTextureCoords(point);
            const float offsetToMiddle = landSizeFactor(true) * 0.5f;
            return vsg::dvec3(CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(xy.first) + offsetToMiddle,
                CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(xy.second) + offsetToMiddle, point.z);
        }
        const std::pair<int, int> xy = CSMWorld::CellCoordinates::toVertexCoords(point);
        return vsg::dvec3(CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(xy.first),
            CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(xy.second), point.z);
    }

    template <class Height>
    vsg::vec3 onGround(double x, double y, Height&& height)
    {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        return vsg::vec3(fx, fy, height(fx, fy) + sOutlineHeight);
    }

    template <class Height>
    std::vector<vsg::vec3> pointLines(const vsg::dvec3& p, Height&& height)
    {
        const double s = sCrossHeadSize;
        return { onGround(p.x - s, p.y - s, height), onGround(p.x + s, p.y + s, height),
            onGround(p.x + s, p.y - s, height), onGround(p.x - s, p.y + s, height) };
    }

    template <class Height>
    std::vector<vsg::vec3> squareLines(float radius, const vsg::dvec3& p, float landSize, Height&& height)
    {
        const float diameter = radius * 2;
        int resolution = static_cast<int>(2.f * diameter / landSize); // half a vertex resolution
        float stepSize = landSize / 2;
        if (resolution > sMaxSquareResolution) // limit accuracy for performance
        {
            resolution = sMaxSquareResolution;
            stepSize = diameter / static_cast<float>(resolution);
        }

        std::vector<vsg::vec3> lines;
        lines.reserve(static_cast<std::size_t>(resolution) * 8);
        for (int i = 0; i < resolution; ++i)
        {
            const int step = static_cast<int>(static_cast<float>(i) * stepSize);
            const int step2 = static_cast<int>(static_cast<float>(i + 1) * stepSize);
            lines.push_back(onGround(p.x - radius + step, p.y - radius, height));
            lines.push_back(onGround(p.x - radius + step2, p.y - radius, height));
            lines.push_back(onGround(p.x - radius, p.y - radius + step, height));
            lines.push_back(onGround(p.x - radius, p.y - radius + step2, height));
            lines.push_back(onGround(p.x + radius - step, p.y + radius, height));
            lines.push_back(onGround(p.x + radius - step2, p.y + radius, height));
            lines.push_back(onGround(p.x + radius, p.y + radius - step, height));
            lines.push_back(onGround(p.x + radius, p.y + radius - step2, height));
        }
        return lines;
    }

    template <class Height>
    std::vector<vsg::vec3> circleLines(float radius, const vsg::dvec3& p, Height&& height)
    {
        const float step = (Misc::VsgMath::PIf * 2.0f) / static_cast<float>(sCirclePoints);
        std::vector<vsg::vec3> lines;
        lines.reserve(sCirclePoints * 2);
        for (int i = 0; i < sCirclePoints; ++i)
        {
            const float a0 = static_cast<float>(i) * step;
            const float a1 = static_cast<float>(i + 1) * step;
            lines.push_back(onGround(p.x + radius * std::cos(a0), p.y + radius * std::sin(a0), height));
            lines.push_back(onGround(p.x + radius * std::cos(a1), p.y + radius * std::sin(a1), height));
        }
        return lines;
    }

    /// \param height the land's height at global vertex coordinates.
    template <class Height>
    std::vector<vsg::vec3> shapeSelectionLines(const std::vector<std::pair<int, int>>& selection, Height&& height)
    {
        using Coordinates = CSMWorld::CellCoordinates;
        std::vector<vsg::vec3> lines;
        lines.reserve(selection.size() * 8);
        for (const auto& [x, y] : selection)
        {
            const float worldX = Coordinates::vertexGlobalToWorldCoords(x);
            const float worldY = Coordinates::vertexGlobalToWorldCoords(y);
            const vsg::vec3 point(worldX, worldY, static_cast<float>(height(x, y)));
            const auto add = [&](int nx, int ny) {
                lines.push_back(point);
                lines.emplace_back(Coordinates::vertexGlobalToWorldCoords(nx),
                    Coordinates::vertexGlobalToWorldCoords(ny), static_cast<float>(height(nx, ny)));
            };
            add(x, y - 1);
            add(x - 1, y);
            add(x, y + 1);
            add(x + 1, y);
        }
        return lines;
    }

    /// Lines along the outside of a set of texels. \param height the land's height at global vertex coordinates.
    template <class Height>
    std::vector<vsg::vec3> textureSelectionLines(const std::vector<std::pair<int, int>>& selection, Height&& height)
    {
        using Coordinates = CSMWorld::CellCoordinates;
        std::vector<vsg::vec3> lines;
        // Does this work with all land size configurations?
        const int landHeightsNudge = (ESM::Land::REAL_SIZE / ESM::Land::LAND_SIZE) / (ESM::Land::LAND_SIZE - 1);
        const int modifier = (ESM::Land::LAND_SIZE - 1) / ESM::Land::LAND_TEXTURE_SIZE;
        const int vertexStep = ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1);
        const auto selected = [&](int x, int y) {
            return std::find(selection.begin(), selection.end(), std::make_pair(x, y)) != selection.end();
        };

        for (const auto& [x, y] : selection)
        {
            // convert texture selection to global vertex coordinates at selection box corners
            const int x1 = x * modifier + landHeightsNudge;
            const int x2 = x * modifier + modifier + landHeightsNudge;
            const int y1 = y * modifier - landHeightsNudge;
            const int y2 = y * modifier + modifier - landHeightsNudge;

            const float worldX = Coordinates::textureGlobalXToWorldCoords(x);
            const float worldY = Coordinates::textureGlobalYToWorldCoords(y);
            const float worldX2 = Coordinates::textureGlobalXToWorldCoords(x + 1);
            const float worldY2 = Coordinates::textureGlobalYToWorldCoords(y + 1);

            // Draw edges (check all sides, draw lines between vertices)
            // Check adjancent selections, draw lines only to edges of the selection
            for (int i = 1; i < modifier + 1; ++i)
            {
                const float previous = static_cast<float>((i - 1) * vertexStep);
                const float current = static_cast<float>(i * vertexStep);
                if (!selected(x, y + 1))
                {
                    lines.emplace_back(worldX + previous, worldY2, static_cast<float>(height(x1 + (i - 1), y2)));
                    lines.emplace_back(worldX + current, worldY2, static_cast<float>(height(x1 + i, y2)));
                }
                if (!selected(x, y - 1))
                {
                    lines.emplace_back(worldX + previous, worldY, static_cast<float>(height(x1 + (i - 1), y1)));
                    lines.emplace_back(worldX + current, worldY, static_cast<float>(height(x1 + i, y1)));
                }
                if (!selected(x + 1, y))
                {
                    lines.emplace_back(worldX2, worldY + previous, static_cast<float>(height(x2, y1 + (i - 1))));
                    lines.emplace_back(worldX2, worldY + current, static_cast<float>(height(x2, y1 + i)));
                }
                if (!selected(x - 1, y))
                {
                    lines.emplace_back(worldX, worldY + previous, static_cast<float>(height(x1, y1 + (i - 1))));
                    lines.emplace_back(worldX, worldY + current, static_cast<float>(height(x1, y1 + i)));
                }
            }
        }
        return lines;
    }

    inline float bilinear(float fx, float fy, float h00, float h10, float h01, float h11)
    {
        return (h00 * (1.f - fx) + h10 * fx) * (1.f - fy) + (h01 * (1.f - fx) + h11 * fx) * fy;
    }

    /// A land normal, as bytes of a record, from the rises to the next vertices in x and y.
    inline vsg::vec3 landNormal(float riseX, float riseY)
    {
        const vsg::vec3 v1(128, 0, riseX);
        const vsg::vec3 v2(0, 128, riseY);
        const vsg::vec3 normal = vsg::cross(v1, v2);
        return normal / (vsg::length(normal) / 127.0f);
    }
}

#endif
