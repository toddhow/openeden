#include <gtest/gtest.h>

#include <cmath>

#include <vsg/commands/Commands.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/Sampler.h>

#include <components/animation/skin.hpp>
#include <components/animation/softwareskin.hpp>
#include <components/terrain/heightarraystate.hpp>
#include <components/terrain/indexbuffer.hpp>
#include <components/vsgutil/intersectnormal.hpp>

// What the game's canPlaceObject and Lua's castRenderingRay read, and the editor's facing test: OSG's
// getWorldIntersectNormal took the hit triangle's normal. Terrain and software skins compute their vertices in a shared
// proxy, so these run a real intersector over a graph with no GPU.
namespace
{
    constexpr uint32_t sHeightBinding = 2;

    vsg::ref_ptr<vsg::StateGroup> terrainGroup(vsg::ref_ptr<vsg::floatArray2D> heights, float scale)
    {
        auto image = vsg::DescriptorImage::create(vsg::Sampler::create(), heights, sHeightBinding);
        auto set = vsg::DescriptorSet::create(vsg::DescriptorSetLayout::create(), vsg::Descriptors{ image });
        auto bind = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, vsg::ref_ptr<vsg::PipelineLayout>(), 0, set);
        auto group = vsg::StateGroup::create();
        group->stateCommands = { bind };
        auto state = Terrain::HeightArrayState::create();
        state->vertexScale = scale;
        state->heightBinding = sHeightBinding;
        state->parent = group.get();
        group->prototypeArrayState = state;
        return group;
    }

    vsg::ref_ptr<vsg::Node> terrainDraw(uint32_t verts, uint32_t instance)
    {
        auto bindIndices = Terrain::CreateIndexBuffer().create(std::make_pair(verts, 0u));
        auto commands = vsg::Commands::create();
        const uint32_t count = bindIndices->indices->data->valueCount();
        commands->children = { bindIndices, vsg::DrawIndexed::create(count, 1, 0, 0, instance) };
        return commands;
    }

    vsg::ref_ptr<vsg::LineSegmentIntersector::Intersection> rayDown(const vsg::Node& root, double x, double y)
    {
        vsg::LineSegmentIntersector intersector(vsg::dvec3(x, y, 1000), vsg::dvec3(x, y, -1000));
        root.accept(intersector);
        if (intersector.intersections.empty())
            return {};
        return intersector.intersections.front();
    }

    // z = slope * x, on a grid of three by three vertices, 100 units apart.
    vsg::ref_ptr<vsg::floatArray2D> slopeHeights(uint32_t size, float step, float slope)
    {
        auto heights = vsg::floatArray2D::create(size, size);
        for (uint32_t x = 0; x < size; ++x)
            for (uint32_t y = 0; y < size; ++y)
                heights->at(x, y) = static_cast<float>(x) * step * slope;
        return heights;
    }

    TEST(VsgUtilHitNormal, terrainGivesTheTrianglesNormalNotStraightUp)
    {
        auto group = terrainGroup(slopeHeights(3, 100.f, 1.f), 200.f);
        group->addChild(terrainDraw(3, 0));

        auto hit = rayDown(*group, 55, 45);
        ASSERT_TRUE(hit);
        const vsg::vec3 normal = vsgUtil::worldNormal(*hit, -1);
        EXPECT_NEAR(normal.x, -0.7071f, 1e-3f);
        EXPECT_NEAR(normal.y, 0.f, 1e-3f);
        EXPECT_NEAR(normal.z, 0.7071f, 1e-3f);
    }

    TEST(VsgUtilHitNormal, flatTerrainFacesUp)
    {
        auto group = terrainGroup(slopeHeights(3, 100.f, 0.f), 200.f);
        group->addChild(terrainDraw(3, 0));
        auto hit = rayDown(*group, 150, 20);
        ASSERT_TRUE(hit);
        const vsg::vec3 normal = vsgUtil::worldNormal(*hit, -1);
        EXPECT_NEAR(normal.z, 1.f, 1e-5f);
    }

    TEST(VsgUtilHitNormal, aLaterDrawOverwritingTheProxyDoesNotChangeTheHit)
    {
        // Five by five, flat in x up to the middle and steep after it, drawn as two chunks of a half each in x: the hit
        // is in the first and the second draw fills the proxy last.
        auto heights = vsg::floatArray2D::create(5, 5);
        for (uint32_t x = 0; x < 5; ++x)
            for (uint32_t y = 0; y < 5; ++y)
                heights->at(x, y) = x <= 2 ? 0.f : (static_cast<float>(x) - 2.f) * 100.f;
        auto group = terrainGroup(heights, 400.f);
        const uint32_t shift = 1u << 8;
        group->addChild(terrainDraw(3, shift));
        group->addChild(terrainDraw(3, shift | (1u << 16)));

        auto hit = rayDown(*group, 55, 45);
        ASSERT_TRUE(hit);
        const vsg::vec3 normal = vsgUtil::worldNormal(*hit, -1);
        EXPECT_NEAR(normal.z, 1.f, 1e-5f);
    }

    TEST(VsgUtilHitNormal, terrainHitVerticesAreTheChunks)
    {
        auto group = terrainGroup(slopeHeights(3, 100.f, 1.f), 200.f);
        group->addChild(terrainDraw(3, 0));
        auto hit = rayDown(*group, 55, 45);
        ASSERT_TRUE(hit);
        auto vertices = vsgUtil::hitVertices(*hit);
        ASSERT_TRUE(vertices);
        EXPECT_EQ(vertices->size(), 9u);
        EXPECT_FLOAT_EQ(vertices->at(8).z, 200.f);
    }

    TEST(VsgUtilHitNormal, softwareSkinGivesThePosedTrianglesNormal)
    {
        // A flat triangle in the bind pose, its bone turned by a quarter about y so that it stands on edge.
        auto vertices = vsg::vec3Array::create({ { -100, -100, 0 }, { 100, -100, 0 }, { 0, 100, 0 } });
        auto indices = vsg::ushortArray::create({ 0, 1, 2 });
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices });
        draw->assignIndices(indices);
        draw->indexCount = 3;
        draw->instanceCount = 1;

        auto skin = vsg::ref_ptr{ new Anim::Skin };
        skin->boneIndices = vsg::vec4Array::create(3, vsg::vec4(0, 0, 0, 0));
        skin->boneWeights = vsg::vec4Array::create(3, vsg::vec4(1, 0, 0, 0));
        // x stays, z goes to -x: a tilt of 45 degrees, so that a ray from above still meets the triangle.
        const float c = std::sqrt(0.5f);
        auto bones = vsg::mat4Array::create(1);
        bones->at(0) = vsg::mat4(c, 0, -c, 0, 0, 1, 0, 0, c, 0, c, 0, 0, 0, 0, 1);

        auto group = vsg::StateGroup::create();
        group->prototypeArrayState = Anim::SoftwareSkin::create(vsg::ref_ptr<const vsg::mat4Array>(bones), skin);
        group->addChild(draw);

        auto hit = rayDown(*group, 0, 0);
        ASSERT_TRUE(hit);
        const vsg::vec3 normal = vsgUtil::worldNormal(*hit, -1);
        EXPECT_NEAR(normal.x, c, 1e-3f);
        EXPECT_NEAR(normal.y, 0.f, 1e-3f);
        EXPECT_NEAR(normal.z, c, 1e-3f);

        auto posed = vsgUtil::hitVertices(*hit);
        ASSERT_TRUE(posed);
        EXPECT_NEAR(posed->at(1).z, -100.f * c, 1e-2f);
    }

    TEST(VsgUtilHitNormal, plainGeometryWithNoNormalArrayGivesTheTriangles)
    {
        auto vertices = vsg::vec3Array::create({ { -100, -100, 100 }, { 100, -100, -100 }, { 0, 100, 0 } });
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices });
        draw->assignIndices(vsg::ushortArray::create({ 0, 1, 2 }));
        draw->indexCount = 3;
        draw->instanceCount = 1;
        auto hit = rayDown(*draw, 0, 0);
        ASSERT_TRUE(hit);
        const vsg::vec3 normal = vsgUtil::worldNormal(*hit, -1);
        EXPECT_GT(normal.z, 0.f);
        EXPECT_GT(normal.x, 0.f);
    }
}
