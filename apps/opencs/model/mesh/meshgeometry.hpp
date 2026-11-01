#ifndef CSM_MESH_MESHGEOMETRY_H
#define CSM_MESH_MESHGEOMETRY_H

#include <array>
#include <cstdint>
#include <vector>

namespace CSMMesh
{
    enum class Topology
    {
        TriangleList,
        TriangleStrip,
        LineList,
    };

    /// One draw's geometry copied out of a loaded model, so that the viewer can draw it in other ways (task 0043).
    /// Plain data. Normals and colours may be empty.
    struct IndexedMesh
    {
        std::vector<std::array<float, 3>> positions;
        std::vector<std::array<float, 3>> normals;
        std::vector<std::array<float, 4>> colours;
        std::vector<std::uint32_t> indices;
        Topology topology = Topology::TriangleList;
    };

    /// Triangles of the mesh. A strip's odd triangles swap their first two indices so that facing is kept; triangles
    /// with two equal indices and triangles with an index past the positions are dropped; a trailing partial triple
    /// is ignored; a line list has none.
    std::vector<std::array<std::uint32_t, 3>> triangles(const IndexedMesh& mesh);

    /// Each undirected edge of the triangles once, smaller index first, in order of first appearance (a triangle
    /// gives a-b, b-c, c-a). A line list returns its pairs.
    std::vector<std::array<std::uint32_t, 2>> uniqueEdges(const IndexedMesh& mesh);

    /// Unit normal of (b-a) x (c-a); zero for a degenerate triangle.
    std::array<float, 3> faceNormal(
        const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c);

    /// Grey: ambient + (1 - ambient) * max(0, dot(normal, towardsLight)); alpha 1. A zero normal gives the ambient.
    std::array<float, 4> flatShadeColour(
        const std::array<float, 3>& normal, const std::array<float, 3>& towardsLight, float ambient = 0.35f);

    /// The mean of the three vertex colours; opaque white if the mesh has no colour for one of them.
    std::array<float, 4> averageColour(const IndexedMesh& mesh, const std::array<std::uint32_t, 3>& triangle);
}

#endif
