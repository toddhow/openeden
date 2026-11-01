#include "apps/opencs/model/mesh/meshgeometry.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        using Tri = std::array<std::uint32_t, 3>;
        using Edge = std::array<std::uint32_t, 2>;

        IndexedMesh makeMesh(std::size_t vertexCount, std::vector<std::uint32_t> indices, Topology topology)
        {
            IndexedMesh mesh;
            mesh.positions.assign(vertexCount, { 0.f, 0.f, 0.f });
            mesh.indices = std::move(indices);
            mesh.topology = topology;
            return mesh;
        }

        void expectColour(const std::array<float, 4>& actual, float r, float g, float b, float a)
        {
            EXPECT_NEAR(actual[0], r, 1e-6f);
            EXPECT_NEAR(actual[1], g, 1e-6f);
            EXPECT_NEAR(actual[2], b, 1e-6f);
            EXPECT_NEAR(actual[3], a, 1e-6f);
        }

        TEST(CSMMeshMeshGeometryTest, defaultMeshIsAnEmptyTriangleList)
        {
            const IndexedMesh mesh;
            EXPECT_EQ(mesh.topology, Topology::TriangleList);
            EXPECT_TRUE(triangles(mesh).empty());
            EXPECT_TRUE(uniqueEdges(mesh).empty());
        }

        TEST(CSMMeshMeshGeometryTest, triangleListGivesConsecutiveTriples)
        {
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 2, 1, 3 }, Topology::TriangleList);
            const std::vector<Tri> expected{ Tri{ 0, 1, 2 }, Tri{ 2, 1, 3 } };
            EXPECT_EQ(triangles(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, triangleListIgnoresATrailingIncompleteTriple)
        {
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 3 }, Topology::TriangleList);
            const std::vector<Tri> expected{ Tri{ 0, 1, 2 } };
            EXPECT_EQ(triangles(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, stripFlipsEveryOddTriangleToKeepFacing)
        {
            const IndexedMesh mesh = makeMesh(5, { 0, 1, 2, 3, 4 }, Topology::TriangleStrip);
            // triangle 0: (0,1,2); 1: (1,2,3) flipped -> (2,1,3); 2: (2,3,4)
            const std::vector<Tri> expected{ Tri{ 0, 1, 2 }, Tri{ 2, 1, 3 }, Tri{ 2, 3, 4 } };
            EXPECT_EQ(triangles(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, stripDropsDegenerateTrianglesButKeepsTheParityOfPositions)
        {
            // triangles by position: 0 (0,1,2) ok; 1 (1,2,2) degenerate; 2 (2,2,3) degenerate; 3 (2,3,4) odd, flipped
            const IndexedMesh mesh = makeMesh(5, { 0, 1, 2, 2, 3, 4 }, Topology::TriangleStrip);
            const std::vector<Tri> expected{ Tri{ 0, 1, 2 }, Tri{ 3, 2, 4 } };
            EXPECT_EQ(triangles(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, stripWithFewerThanThreeIndicesGivesNothing)
        {
            EXPECT_TRUE(triangles(makeMesh(3, { 0, 1 }, Topology::TriangleStrip)).empty());
            EXPECT_TRUE(triangles(makeMesh(3, {}, Topology::TriangleStrip)).empty());
        }

        TEST(CSMMeshMeshGeometryTest, outOfRangeIndicesSkipTheTriangle)
        {
            const IndexedMesh list = makeMesh(3, { 0, 1, 2, 0, 1, 7 }, Topology::TriangleList);
            const std::vector<Tri> expectedList{ Tri{ 0, 1, 2 } };
            EXPECT_EQ(triangles(list), expectedList);

            const IndexedMesh strip = makeMesh(4, { 0, 1, 2, 3, 99 }, Topology::TriangleStrip);
            // (0,1,2) ok, (1,2,3) flipped -> (2,1,3) ok, (2,3,99) skipped
            const std::vector<Tri> expectedStrip{ Tri{ 0, 1, 2 }, Tri{ 2, 1, 3 } };
            EXPECT_EQ(triangles(strip), expectedStrip);
        }

        TEST(CSMMeshMeshGeometryTest, listDegenerateTrianglesAreNotInvented)
        {
            // a list is taken as given, apart from out-of-range skipping
            const IndexedMesh mesh = makeMesh(3, { 0, 1, 2 }, Topology::TriangleList);
            EXPECT_EQ(triangles(mesh).size(), 1u);
        }

        TEST(CSMMeshMeshGeometryTest, lineListHasNoTriangles)
        {
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 3 }, Topology::LineList);
            EXPECT_TRUE(triangles(mesh).empty());
        }

        TEST(CSMMeshMeshGeometryTest, uniqueEdgesListsEachUndirectedEdgeOnceSmallerIndexFirst)
        {
            // two triangles sharing the edge 1-2
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 1, 2, 3 }, Topology::TriangleList);
            const std::vector<Edge> edges = uniqueEdges(mesh);
            ASSERT_EQ(edges.size(), 5u);
            for (const Edge& e : edges)
                EXPECT_LT(e[0], e[1]);

            // every expected edge appears exactly once
            const std::vector<Edge> all{ Edge{ 0, 1 }, Edge{ 1, 2 }, Edge{ 0, 2 }, Edge{ 2, 3 }, Edge{ 1, 3 } };
            for (const Edge& want : all)
            {
                int count = 0;
                for (const Edge& e : edges)
                    if (e == want)
                        ++count;
                EXPECT_EQ(count, 1) << want[0] << "-" << want[1];
            }
        }

        TEST(CSMMeshMeshGeometryTest, uniqueEdgesAreInOrderOfFirstAppearance)
        {
            // a triangle (a, b, c) contributes a-b, b-c, c-a
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 1, 2, 3 }, Topology::TriangleList);
            const std::vector<Edge> expected{ Edge{ 0, 1 }, Edge{ 1, 2 }, Edge{ 0, 2 }, Edge{ 2, 3 }, Edge{ 1, 3 } };
            EXPECT_EQ(uniqueEdges(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, uniqueEdgesTreatOppositeDirectionsAsTheSameEdge)
        {
            const IndexedMesh mesh = makeMesh(3, { 0, 1, 2, 2, 1, 0 }, Topology::TriangleList);
            EXPECT_EQ(uniqueEdges(mesh).size(), 3u);
        }

        TEST(CSMMeshMeshGeometryTest, uniqueEdgesOfALineListAreItsPairs)
        {
            const IndexedMesh mesh = makeMesh(4, { 0, 1, 2, 3 }, Topology::LineList);
            const std::vector<Edge> expected{ Edge{ 0, 1 }, Edge{ 2, 3 } };
            EXPECT_EQ(uniqueEdges(mesh), expected);
        }

        TEST(CSMMeshMeshGeometryTest, uniqueEdgesAreDeterministic)
        {
            const IndexedMesh mesh = makeMesh(5, { 0, 1, 2, 2, 3, 4, 0, 3, 4 }, Topology::TriangleList);
            EXPECT_EQ(uniqueEdges(mesh), uniqueEdges(mesh));
        }

        TEST(CSMMeshMeshGeometryTest, faceNormalIsTheNormalisedCrossProduct)
        {
            const auto n = faceNormal({ 0.f, 0.f, 0.f }, { 2.f, 0.f, 0.f }, { 0.f, 3.f, 0.f });
            EXPECT_NEAR(n[0], 0.f, 1e-6f);
            EXPECT_NEAR(n[1], 0.f, 1e-6f);
            EXPECT_NEAR(n[2], 1.f, 1e-6f);

            // reversing the winding reverses the normal
            const auto r = faceNormal({ 0.f, 0.f, 0.f }, { 0.f, 3.f, 0.f }, { 2.f, 0.f, 0.f });
            EXPECT_NEAR(r[2], -1.f, 1e-6f);
        }

        TEST(CSMMeshMeshGeometryTest, faceNormalHasUnitLength)
        {
            const auto n = faceNormal({ 1.f, 2.f, 3.f }, { 4.f, 1.f, 0.f }, { -2.f, 5.f, 7.f });
            EXPECT_NEAR(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.f, 1e-5f);
        }

        TEST(CSMMeshMeshGeometryTest, faceNormalOfADegenerateTriangleIsZero)
        {
            const auto collinear = faceNormal({ 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, { 2.f, 2.f, 2.f });
            EXPECT_EQ(collinear, (std::array<float, 3>{ 0.f, 0.f, 0.f }));
            const auto repeated = faceNormal({ 1.f, 2.f, 3.f }, { 1.f, 2.f, 3.f }, { 1.f, 2.f, 3.f });
            EXPECT_EQ(repeated, (std::array<float, 3>{ 0.f, 0.f, 0.f }));
        }

        TEST(CSMMeshMeshGeometryTest, flatShadeFacingTheLightIsWhiteAndAwayIsAmbient)
        {
            expectColour(flatShadeColour({ 0.f, 0.f, 1.f }, { 0.f, 0.f, 1.f }), 1.f, 1.f, 1.f, 1.f);
            expectColour(flatShadeColour({ 0.f, 0.f, 1.f }, { 0.f, 0.f, -1.f }), 0.35f, 0.35f, 0.35f, 1.f);
        }

        TEST(CSMMeshMeshGeometryTest, flatShadeUsesTheDotProductAndTheGivenAmbient)
        {
            // dot = 0.5, ambient 0.5 -> 0.5 + 0.5 * 0.5
            const std::array<float, 3> light{ 0.8660254f, 0.0f, 0.5f };
            expectColour(flatShadeColour({ 0.f, 0.f, 1.f }, light, 0.5f), 0.75f, 0.75f, 0.75f, 1.f);
            // perpendicular light gives just the ambient
            expectColour(flatShadeColour({ 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f }), 0.35f, 0.35f, 0.35f, 1.f);
        }

        TEST(CSMMeshMeshGeometryTest, flatShadeOfAZeroNormalIsTheAmbientGrey)
        {
            expectColour(flatShadeColour({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }), 0.35f, 0.35f, 0.35f, 1.f);
            expectColour(flatShadeColour({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, 0.2f), 0.2f, 0.2f, 0.2f, 1.f);
        }

        TEST(CSMMeshMeshGeometryTest, averageColourIsTheMeanOfTheThreeVertexColours)
        {
            IndexedMesh mesh = makeMesh(3, { 0, 1, 2 }, Topology::TriangleList);
            mesh.colours = { { 1.f, 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f, 0.5f }, { 0.f, 0.f, 1.f, 0.f } };
            expectColour(averageColour(mesh, Tri{ 0, 1, 2 }), 1.f / 3.f, 1.f / 3.f, 1.f / 3.f, 0.5f);
        }

        TEST(CSMMeshMeshGeometryTest, averageColourIsOpaqueWhiteWithoutColours)
        {
            const IndexedMesh mesh = makeMesh(3, { 0, 1, 2 }, Topology::TriangleList);
            expectColour(averageColour(mesh, Tri{ 0, 1, 2 }), 1.f, 1.f, 1.f, 1.f);
        }

        TEST(CSMMeshMeshGeometryTest, averageColourIsOpaqueWhiteWhenAnIndexHasNoColour)
        {
            IndexedMesh mesh = makeMesh(3, { 0, 1, 2 }, Topology::TriangleList);
            mesh.colours = { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f, 1.f } }; // vertex 2 has none
            expectColour(averageColour(mesh, Tri{ 0, 1, 2 }), 1.f, 1.f, 1.f, 1.f);
        }
    }
}
