#include "meshoverlay.hpp"

#include <algorithm>
#include <cmath>

namespace CSMMesh
{
    std::vector<Line> boxLines(const Aabb& box, const std::array<float, 4>& colour)
    {
        std::vector<Line> lines;
        if (!box.valid)
            return lines;
        auto corner = [&](int index) {
            return std::array<float, 3>{ static_cast<float>((index & 1) ? box.max[0] : box.min[0]),
                static_cast<float>((index & 2) ? box.max[1] : box.min[1]),
                static_cast<float>((index & 4) ? box.max[2] : box.min[2]) };
        };
        // Corners differing in one bit are joined.
        for (int index = 0; index < 8; ++index)
            for (int bit = 1; bit <= 4; bit <<= 1)
                if ((index & bit) == 0)
                    lines.push_back({ corner(index), corner(index | bit), colour });
        return lines;
    }

    double gridSpacing(double modelRadius)
    {
        if (!(modelRadius > 0.0))
            return 1.0;
        const double wanted = modelRadius / 4.0;
        for (int power = -3; power < 12; ++power)
            for (double step : { 1.0, 2.0, 5.0 })
            {
                const double value = step * std::pow(10.0, power);
                if (value >= wanted * (1.0 - 1e-12))
                    return value;
            }
        return 1e12;
    }

    std::vector<Line> gridLines(double spacing, int halfCount)
    {
        std::vector<Line> lines;
        if (halfCount <= 0 || !(spacing > 0.0))
            return lines;
        const float extent = static_cast<float>(halfCount * spacing);
        for (int i = -halfCount; i <= halfCount; ++i)
        {
            const float offset = static_cast<float>(i * spacing);
            // Parallel to X, at y = offset.
            std::array<float, 4> colourX = { 0.5f, 0.5f, 0.5f, 0.6f };
            if (i == 0)
                colourX = { 1.f, 0.3f, 0.3f, 1.f };
            lines.push_back({ { -extent, offset, 0.f }, { extent, offset, 0.f }, colourX });
            // Parallel to Y, at x = offset.
            std::array<float, 4> colourY = { 0.5f, 0.5f, 0.5f, 0.6f };
            if (i == 0)
                colourY = { 0.3f, 1.f, 0.3f, 1.f };
            lines.push_back({ { offset, -extent, 0.f }, { offset, extent, 0.f }, colourY });
        }
        return lines;
    }

    std::vector<Line> axesLines(const std::array<float, 3>& origin, float length)
    {
        return {
            { origin, { origin[0] + length, origin[1], origin[2] }, { 1.f, 0.f, 0.f, 1.f } },
            { origin, { origin[0], origin[1] + length, origin[2] }, { 0.f, 1.f, 0.f, 1.f } },
            { origin, { origin[0], origin[1], origin[2] + length }, { 0.f, 0.f, 1.f, 1.f } },
        };
    }

    std::vector<Line> normalLines(const IndexedMesh& mesh, float length, const std::array<float, 4>& colour)
    {
        std::vector<Line> lines;
        const std::size_t count = std::min(mesh.positions.size(), mesh.normals.size());
        lines.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& p = mesh.positions[i];
            const auto& n = mesh.normals[i];
            lines.push_back({ p, { p[0] + n[0] * length, p[1] + n[1] * length, p[2] + n[2] * length }, colour });
        }
        return lines;
    }
}
