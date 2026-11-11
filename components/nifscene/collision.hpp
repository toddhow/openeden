#ifndef OPENEDEN_COMPONENTS_NIFSCENE_COLLISION_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_COLLISION_HPP

// Task 0110 (neutral world stage N3a-4b): the pure half of collision from a Skyrim SE model's visual triangles. It
// turns the FlatModel that drawing uses (components/nifscene/flatten.hpp: every node's transform baked into the
// vertices, hidden nodes, editor markers, skinned and effect shapes already left out) into one indexed triangle list
// that a physics shape can be built from. It has no Jolt, VSG or engine type; the other half
// (apps/openmw/mwphysics/skyrimshape.hpp, host layer) makes the Jolt shape.
//
// Policy, each line a named test (apps/components_tests/nifscene/testcollision.cpp):
//  - a shape that is alpha blended and not alpha tested (glass, cobwebs, decals, fog planes) has no collision, as in
//    the game; a cutout (alpha test) still collides;
//  - triangles that cannot make a solid face are dropped and counted: an index outside the shape, a repeated corner, a
//    corner that is not a finite number, and a face whose area is below Limits::mMinTwiceArea;
//  - only the vertices the kept triangles use are copied, in order of first use, so the same model always gives the
//    same mesh;
//  - a model with more than Limits::mMaxTriangles triangles gets NO mesh (and the report says so): cutting a model at a
//    count would leave holes the player falls through, and the caller falls back to the object's bounds;
//  - nothing is ever indexed outside an array, whatever the model holds.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "flatten.hpp"

namespace NifScene::Collision
{
    struct Limits
    {
        /// Above this many kept triangles the model gets no mesh. The Whiterun inn is about 285,000 triangles in all
        /// and most of them are decoration; Skyrim's own largest interior statics are far below this.
        std::size_t mMaxTriangles = 400000;
        /// Twice the area of the smallest face kept, in square game units (a Skyrim unit is about 1.4 cm). Anything
        /// thinner than a hair cannot hold a character up and only gives Jolt degenerate edges.
        double mMinTwiceArea = 1.0e-4;
    };

    struct Mesh
    {
        std::vector<std::array<float, 3>> mVertices;
        std::vector<std::array<std::uint32_t, 3>> mTriangles; // indices into mVertices, all in range
        bool empty() const { return mTriangles.empty(); }
    };

    struct Report
    {
        std::uint32_t mShapesUsed = 0; // shapes that gave at least one triangle
        std::uint32_t mShapesBlended = 0; // left out: blended and not tested
        std::uint32_t mTrianglesIn = 0; // triangles of the shapes that were looked at (not the blended ones)
        std::uint32_t mTrianglesKept = 0;
        std::uint32_t mTrianglesBadIndex = 0;
        std::uint32_t mTrianglesRepeatedCorner = 0;
        std::uint32_t mTrianglesBadVertex = 0; // a corner that is not finite
        std::uint32_t mTrianglesTooThin = 0;
        bool mOverCap = false; // more kept triangles than Limits::mMaxTriangles: no mesh
    };

    /// Never throws; the mesh obeys the limits. An empty mesh means "no collision from this model".
    Mesh buildMesh(const Flat::FlatModel& model, const Limits& limits = {}, Report* report = nullptr);
}

#endif
