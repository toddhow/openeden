#include <gtest/gtest.h>

#include <components/esm3/loadpgrd.hpp>

#include <apps/opencs/view/render/pathgridgeometry.hpp>

// Written from the OSG build's components/sceneutil/pathgridutil.cpp.
namespace
{
    namespace Geometry = CSVRender::PathgridGeometry;

    ESM::Pathgrid threePoints()
    {
        ESM::Pathgrid pathgrid;
        pathgrid.mPoints = { ESM::Pathgrid::Point(100, 200, 300), ESM::Pathgrid::Point(1100, 200, 300),
            ESM::Pathgrid::Point(100, 1200, 300) };
        ESM::Pathgrid::Edge edge;
        edge.mV0 = 0;
        edge.mV1 = 1;
        pathgrid.mEdges.push_back(edge);
        edge.mV1 = 2;
        pathgrid.mEdges.push_back(edge);
        // Skipped: to itself, and to no point.
        edge.mV1 = 0;
        pathgrid.mEdges.push_back(edge);
        edge.mV1 = 9;
        pathgrid.mEdges.push_back(edge);
        return pathgrid;
    }

    TEST(PathgridGeometryTest, eachPointIsADiamondWithConnectors)
    {
        const Geometry::Grid grid = Geometry::createGrid(threePoints());
        EXPECT_EQ(grid.mVertices.size(), 30u);
        EXPECT_EQ(grid.mColours.size(), 30u);
        EXPECT_EQ(grid.mTriangles.size(), 72u);
        // Top, at twice the half height.
        EXPECT_FLOAT_EQ(grid.mVertices[0].x, 100.f);
        EXPECT_FLOAT_EQ(grid.mVertices[0].z, 300.f + 2 * Geometry::sDiamondHalfHeight);
        // Bottom.
        EXPECT_FLOAT_EQ(grid.mVertices[5].z, 300.f);
        // A connector is at the same place as the corner it stands for, in the edge colour.
        EXPECT_FLOAT_EQ(grid.mVertices[6].x, grid.mVertices[1].x);
        EXPECT_FLOAT_EQ(grid.mColours[6].r, 0.5f);
    }

    TEST(PathgridGeometryTest, edgesSkipLoopsAndMissingPoints)
    {
        EXPECT_EQ(Geometry::createGrid(threePoints()).mLines.size(), 4u);
    }

    TEST(PathgridGeometryTest, anEdgeStartsAtTheConnectorFacingItsSide)
    {
        const Geometry::Grid grid = Geometry::createGrid(threePoints());
        // Along +x: the direction turned by -90 degrees is -y, the "x >= 0, y < 0" case, connector 2.
        EXPECT_EQ(grid.mLines[0], 0u * 10u + 6u + 2u);
        EXPECT_EQ(grid.mLines[1], 1u * 10u + 6u + 2u);
        // Along +y: turned to +x, the "y >= 0, x > 0" case, connector 3.
        EXPECT_EQ(grid.mLines[2], 0u * 10u + 6u + 3u);
        EXPECT_EQ(grid.mLines[3], 2u * 10u + 6u + 3u);
    }

    TEST(PathgridGeometryTest, aVertexKnowsItsNode)
    {
        const Geometry::Grid grid = Geometry::createGrid(threePoints());
        EXPECT_EQ(Geometry::nodeOfVertex(grid.mTriangles[0]), 0);
        EXPECT_EQ(Geometry::nodeOfVertex(grid.mTriangles[24]), 1);
        EXPECT_EQ(Geometry::nodeOfVertex(grid.mTriangles[71]), 2);
    }

    TEST(PathgridGeometryTest, trianglesFaceOutwards)
    {
        // The picking rule counts a triangle only from its front.
        const Geometry::Grid grid = Geometry::createGrid(threePoints());
        const vsg::vec3 centre(100.f, 200.f, 300.f + Geometry::sDiamondHalfHeight);
        for (std::size_t i = 0; i < 24; i += 3)
        {
            const vsg::vec3& a = grid.mVertices[grid.mTriangles[i]];
            const vsg::vec3& b = grid.mVertices[grid.mTriangles[i + 1]];
            const vsg::vec3& c = grid.mVertices[grid.mTriangles[i + 2]];
            EXPECT_GT(vsg::dot(vsg::cross(b - a, c - a), (a + b + c) / 3.f - centre), 0.f) << i;
        }
    }

    TEST(PathgridGeometryTest, wireframeIsLargerAndTheLastSelectedHasTheFocusColour)
    {
        const auto wire = Geometry::createSelectedWireframe(threePoints(), { 0, 2 });
        EXPECT_EQ(wire.mVertices.size(), 12u);
        EXPECT_EQ(wire.mColours.size(), 12u);
        EXPECT_EQ(wire.mLines.size(), 48u);
        EXPECT_FLOAT_EQ(wire.mColours[0].r, 0.72f);
        EXPECT_FLOAT_EQ(wire.mColours[6].r, 0.91f);
        // Scaled by 1.1 about the point, then moved down by a tenth of the half height.
        const float offset = (1.f - 1.1f) * Geometry::sDiamondHalfHeight;
        EXPECT_NEAR(wire.mVertices[0].z, 300.f + offset + 2 * Geometry::sDiamondHalfHeight * 1.1f, 1e-3f);
    }

    TEST(PathgridGeometryTest, emptyGridAndNoSelectionGiveNothing)
    {
        const Geometry::Grid grid = Geometry::createGrid(ESM::Pathgrid());
        EXPECT_TRUE(grid.mVertices.empty());
        EXPECT_TRUE(grid.mTriangles.empty());
        EXPECT_TRUE(Geometry::createSelectedWireframe(threePoints(), {}).mLines.empty());
    }
}
