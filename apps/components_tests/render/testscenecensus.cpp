#include <gtest/gtest.h>

#include <components/render/scenecensus.hpp>
#include <components/vsgutil/distancelod.hpp>

#include <vsg/all.h>

#include <limits>

// Task 0062 stage a: the scene census applies DistanceLOD ranges for an eye.
namespace
{
    // A draw of `triangles` triangles (VertexIndexDraw with that many indices / 3).
    vsg::ref_ptr<vsg::VertexIndexDraw> makeDraw(std::uint32_t triangles)
    {
        auto draw = vsg::VertexIndexDraw::create();
        draw->indexCount = triangles * 3;
        draw->instanceCount = 1;
        return draw;
    }

    // One object with three levels: 1000, 500 and 250 triangles in [0, 100), [100, 300) and [300, inf).
    vsg::ref_ptr<vsgUtil::DistanceLOD> makeThreeLevels()
    {
        auto lod = vsgUtil::DistanceLOD::create();
        lod->addChild(makeDraw(1000));
        lod->addChild(makeDraw(500));
        lod->addChild(makeDraw(250));
        const float inf = std::numeric_limits<float>::max();
        lod->ranges = { { 0.f, 100.f }, { 100.f, 300.f }, { 300.f, inf } };
        return lod;
    }

    Render::SceneCensus run(const vsg::Node& root, const vsg::dvec3* eye)
    {
        Render::SceneCensus census;
        if (eye)
            census.setEye(*eye);
        root.accept(census);
        return census;
    }
}

TEST(RenderSceneCensus, WithoutAnEyeEveryLevelOfADistanceLodIsCounted)
{
    auto lod = makeThreeLevels();
    const auto census = run(*lod, nullptr);
    EXPECT_EQ(census.draws(), 3u);
    EXPECT_EQ(census.triangles(), 1750u);
    EXPECT_EQ(census.lodChildrenSkipped(), 0u);
}

TEST(RenderSceneCensus, WithAnEyeOnlyTheLevelInRangeIsCounted)
{
    auto lod = makeThreeLevels();
    const vsg::dvec3 near(50.0, 0.0, 0.0), middle(200.0, 0.0, 0.0), far(1000.0, 0.0, 0.0);
    EXPECT_EQ(run(*lod, &near).triangles(), 1000u);
    EXPECT_EQ(run(*lod, &middle).triangles(), 500u);
    const auto farCensus = run(*lod, &far);
    EXPECT_EQ(farCensus.triangles(), 250u);
    EXPECT_EQ(farCensus.draws(), 1u);
    EXPECT_EQ(farCensus.lodNodes(), 1u);
    EXPECT_EQ(farCensus.lodChildrenSkipped(), 2u);
}

TEST(RenderSceneCensus, TheDistanceIsTakenFromTheCentreAndInTheNodesOwnUnits)
{
    auto lod = makeThreeLevels();
    lod->center = vsg::vec3(1000.f, 0.f, 0.f);
    // A transform that moves the node by 1000 and scales it by 2: the centre is 2000 away from the origin, so
    // an eye at 1900 on the x axis is 100 away in world units, 50 in the node's own units: level 0.
    auto transform = vsg::MatrixTransform::create(vsg::translate(1000.0, 0.0, 0.0) * vsg::scale(2.0, 2.0, 2.0));
    transform->addChild(lod);
    const vsg::dvec3 eye(3000.0, 0.0, 0.0); // centre is at 1000 + 2 * 1000 = 3000 -> distance 0 -> level 0
    EXPECT_EQ(run(*transform, &eye).triangles(), 1000u);
    const vsg::dvec3 farEye(3300.0, 0.0, 0.0); // 300 world units = 150 own units -> level 1
    EXPECT_EQ(run(*transform, &farEye).triangles(), 500u);
}

TEST(RenderSceneCensus, ResetClearsTheLodCounters)
{
    auto lod = makeThreeLevels();
    const vsg::dvec3 far(1000.0, 0.0, 0.0);
    auto census = run(*lod, &far);
    census.reset();
    EXPECT_EQ(census.lodNodes(), 0u);
    EXPECT_EQ(census.lodChildrenSkipped(), 0u);
    EXPECT_EQ(census.triangles(), 0u);
}
