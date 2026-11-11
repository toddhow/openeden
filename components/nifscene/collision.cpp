#include "collision.hpp"

#include <cmath>

namespace NifScene::Collision
{
    namespace
    {
        bool finite(const std::array<float, 3>& p)
        {
            return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
        }

        // Squared length of the cross product (twice the triangle's area, squared), in double so a sliver does not
        // vanish in rounding.
        double twiceAreaSquared(
            const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c)
        {
            const double ux = double(b[0]) - a[0], uy = double(b[1]) - a[1], uz = double(b[2]) - a[2];
            const double vx = double(c[0]) - a[0], vy = double(c[1]) - a[1], vz = double(c[2]) - a[2];
            const double x = uy * vz - uz * vy;
            const double y = uz * vx - ux * vz;
            const double z = ux * vy - uy * vx;
            return x * x + y * y + z * z;
        }
    }

    Mesh buildMesh(const Flat::FlatModel& model, const Limits& limits, Report* reportOut)
    {
        Report report;
        Mesh mesh;
        const double minTwiceAreaSquared = limits.mMinTwiceArea * limits.mMinTwiceArea;

        for (const Flat::Shape& shape : model.mShapes)
        {
            if (shape.mKey.mBlend && !shape.mKey.mTest)
            {
                ++report.mShapesBlended;
                continue;
            }
            const std::size_t vertexCount = shape.mPositions.size();
            const std::size_t triangles = shape.mIndices.size() / 3;
            std::vector<std::uint32_t> remap(vertexCount, 0xFFFFFFFFu); // shape vertex -> mesh vertex
            bool used = false;
            for (std::size_t t = 0; t < triangles; ++t)
            {
                ++report.mTrianglesIn;
                const std::uint32_t i0 = shape.mIndices[t * 3];
                const std::uint32_t i1 = shape.mIndices[t * 3 + 1];
                const std::uint32_t i2 = shape.mIndices[t * 3 + 2];
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                {
                    ++report.mTrianglesBadIndex;
                    continue;
                }
                if (i0 == i1 || i1 == i2 || i0 == i2)
                {
                    ++report.mTrianglesRepeatedCorner;
                    continue;
                }
                const auto& a = shape.mPositions[i0];
                const auto& b = shape.mPositions[i1];
                const auto& c = shape.mPositions[i2];
                if (!finite(a) || !finite(b) || !finite(c))
                {
                    ++report.mTrianglesBadVertex;
                    continue;
                }
                if (!(twiceAreaSquared(a, b, c) >= minTwiceAreaSquared))
                {
                    ++report.mTrianglesTooThin;
                    continue;
                }
                if (mesh.mTriangles.size() >= limits.mMaxTriangles)
                {
                    report.mOverCap = true;
                    continue;
                }
                std::array<std::uint32_t, 3> corners{ i0, i1, i2 };
                for (std::uint32_t& corner : corners)
                {
                    if (remap[corner] == 0xFFFFFFFFu)
                    {
                        remap[corner] = static_cast<std::uint32_t>(mesh.mVertices.size());
                        mesh.mVertices.push_back(shape.mPositions[corner]);
                    }
                    corner = remap[corner];
                }
                mesh.mTriangles.push_back(corners);
                used = true;
            }
            if (used)
                ++report.mShapesUsed;
        }

        if (report.mOverCap)
            mesh = Mesh{};
        report.mTrianglesKept = static_cast<std::uint32_t>(mesh.mTriangles.size());
        if (reportOut)
            *reportOut = report;
        return mesh;
    }
}
