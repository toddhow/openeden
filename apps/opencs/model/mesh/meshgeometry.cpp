#include "meshgeometry.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace CSMMesh
{
    std::vector<std::array<std::uint32_t, 3>> triangles(const IndexedMesh& mesh)
    {
        std::vector<std::array<std::uint32_t, 3>> result;
        const std::size_t count = mesh.indices.size();
        const std::size_t vertices = mesh.positions.size();
        auto inRange = [vertices](const std::array<std::uint32_t, 3>& t) {
            return t[0] < vertices && t[1] < vertices && t[2] < vertices;
        };
        if (mesh.topology == Topology::TriangleList)
        {
            for (std::size_t i = 0; i + 2 < count; i += 3)
            {
                const std::array<std::uint32_t, 3> t{ mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
                if (inRange(t))
                    result.push_back(t);
            }
        }
        else if (mesh.topology == Topology::TriangleStrip)
        {
            for (std::size_t i = 0; i + 2 < count; ++i)
            {
                std::array<std::uint32_t, 3> t{ mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
                if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2])
                    continue;
                if (i % 2 == 1)
                    std::swap(t[0], t[1]);
                if (inRange(t))
                    result.push_back(t);
            }
        }
        return result;
    }

    std::vector<std::array<std::uint32_t, 2>> uniqueEdges(const IndexedMesh& mesh)
    {
        std::vector<std::array<std::uint32_t, 2>> result;
        if (mesh.topology == Topology::LineList)
        {
            for (std::size_t i = 0; i + 1 < mesh.indices.size(); i += 2)
                result.push_back({ mesh.indices[i], mesh.indices[i + 1] });
            return result;
        }
        std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        auto add = [&](std::uint32_t a, std::uint32_t b) {
            const std::uint32_t lo = std::min(a, b);
            const std::uint32_t hi = std::max(a, b);
            if (seen.insert({ lo, hi }).second)
                result.push_back({ lo, hi });
        };
        for (const auto& t : triangles(mesh))
        {
            add(t[0], t[1]);
            add(t[1], t[2]);
            add(t[2], t[0]);
        }
        return result;
    }

    std::array<float, 3> faceNormal(
        const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c)
    {
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const float x = uy * vz - uz * vy;
        const float y = uz * vx - ux * vz;
        const float z = ux * vy - uy * vx;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (!(length > 0.f))
            return { 0.f, 0.f, 0.f };
        return { x / length, y / length, z / length };
    }

    std::array<float, 4> flatShadeColour(
        const std::array<float, 3>& normal, const std::array<float, 3>& towardsLight, float ambient)
    {
        const float dot = normal[0] * towardsLight[0] + normal[1] * towardsLight[1] + normal[2] * towardsLight[2];
        const float value = ambient + (1.f - ambient) * std::max(0.f, dot);
        return { value, value, value, 1.f };
    }

    std::array<float, 4> averageColour(const IndexedMesh& mesh, const std::array<std::uint32_t, 3>& triangle)
    {
        std::array<float, 4> sum{};
        for (std::uint32_t index : triangle)
        {
            if (index >= mesh.colours.size())
                return { 1.f, 1.f, 1.f, 1.f };
            for (int i = 0; i < 4; ++i)
                sum[i] += mesh.colours[index][i];
        }
        for (float& value : sum)
            value /= 3.f;
        return sum;
    }
}
