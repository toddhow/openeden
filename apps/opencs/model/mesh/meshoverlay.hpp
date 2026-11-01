#ifndef CSM_MESH_MESHOVERLAY_H
#define CSM_MESH_MESHOVERLAY_H

#include <array>
#include <vector>

#include "framemath.hpp"
#include "meshgeometry.hpp"

namespace CSMMesh
{
    /// A coloured line for the viewer's overlay (task 0043). Plain data.
    struct Line
    {
        std::array<float, 3> a;
        std::array<float, 3> b;
        std::array<float, 4> colour;
    };

    /// The 12 edges of the box; none if it is invalid.
    std::vector<Line> boxLines(const Aabb& box, const std::array<float, 4>& colour);

    /// The smallest of 1, 2, 5 times a power of ten (down to 0.001) that is at least modelRadius / 4; 1 if the radius
    /// is not positive.
    double gridSpacing(double modelRadius);

    /// Lines in the plane z = 0, 2 * halfCount + 1 along X and as many along Y. The X axis is red, the Y axis green,
    /// the rest grey. None if halfCount or spacing is not positive.
    std::vector<Line> gridLines(double spacing, int halfCount);

    /// Three lines from the origin along +X (red), +Y (green) and +Z (blue).
    std::vector<Line> axesLines(const std::array<float, 3>& origin, float length);

    /// One line per vertex that has a normal, from the position to position + normal * length.
    std::vector<Line> normalLines(const IndexedMesh& mesh, float length, const std::array<float, 4>& colour);
}

#endif
