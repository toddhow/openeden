#include <gtest/gtest.h>

#include <cmath>

#include <vsg/nodes/VertexIndexDraw.h>

#include <components/vsgutil/id.hpp>
#include <components/vsgutil/intersectnormal.hpp>

// OSG's getWorldIntersectNormal took the hit triangle's normal, also for a mesh with vertex normals, so a smooth mesh's
// interpolated normals must not reach canPlaceObject or Lua's castRenderingRay.
namespace
{
    constexpr int sNormalArrayId = 7;

    TEST(VsgUtilHitNormalMesh, aMeshWithVertexNormalsGivesTheTrianglesNormal)
    {
        // A triangle facing straight up, whose vertex normals all lean over.
        auto vertices = vsg::vec3Array::create({ { -100, -100, 0 }, { 100, -100, 0 }, { 0, 100, 0 } });
        auto normals = vsg::vec3Array::create({ { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } });
        vsgUtil::setID(*normals, sNormalArrayId);
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices, normals });
        draw->assignIndices(vsg::ushortArray::create({ 0, 1, 2 }));
        draw->indexCount = 3;
        draw->instanceCount = 1;

        vsg::LineSegmentIntersector intersector(vsg::dvec3(0, 0, 1000), vsg::dvec3(0, 0, -1000));
        draw->accept(intersector);
        ASSERT_FALSE(intersector.intersections.empty());

        const vsg::vec3 normal = vsgUtil::worldNormal(*intersector.intersections.front(), sNormalArrayId);
        EXPECT_NEAR(normal.x, 0.f, 1e-5f);
        EXPECT_NEAR(normal.y, 0.f, 1e-5f);
        EXPECT_NEAR(normal.z, 1.f, 1e-5f);
    }

    // OSG's intersector never hit a triangle with no area, so there is no normal: a zero vector, which canPlaceObject
    // refuses (changed with toddhow's approval, 2026-10-31; it was straight up).
    TEST(VsgUtilHitNormalMesh, aDegenerateTriangleGivesNoNormalAndNotNaN)
    {
        vsg::LineSegmentIntersector::Intersection hit;
        hit.arrays = { vsg::vec3Array::create({ { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2, 3 } }) };
        hit.indexRatios = { { 0, 0.25 }, { 1, 0.25 }, { 2, 0.5 } };

        const vsg::vec3 normal = vsgUtil::worldNormal(hit);
        EXPECT_FALSE(std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z));
        EXPECT_EQ(normal.x, 0.f);
        EXPECT_EQ(normal.y, 0.f);
        EXPECT_EQ(normal.z, 0.f);
    }

    TEST(VsgUtilHitNormalMesh, aHitWithNoVerticesGivesStraightUp)
    {
        vsg::LineSegmentIntersector::Intersection hit;
        const vsg::vec3 normal = vsgUtil::worldNormal(hit);
        EXPECT_EQ(normal.z, 1.f);
    }
}
