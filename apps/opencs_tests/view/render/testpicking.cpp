#include <gtest/gtest.h>

#include <cmath>

#include <vsg/app/ProjectionMatrix.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>

#include <apps/opencs/view/render/mask.hpp>
#include <apps/opencs/view/render/picking.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

// Written from the OSG editor's WorldspaceWidget::mousePick (worldspacewidget.cpp at 124715f58d^), with no GPU: a scene of
// generated quads in known places, each under a Switch for its element of mask.hpp, as in an editor cell. The old code's
// rules: the ray goes from the near plane to the far plane through the pixel; only what the mask lets through counts; a hit
// on the back of a triangle does not count; with no hit the result is `start + direction * distance`, not hit; the first
// hit's nearest-the-root tag is the result, and with no tag it is still a hit (terrain).
namespace
{
    using namespace CSVRender;

    constexpr double sTolerance = 1e-6;
    constexpr double sPlacementDistance = 400;
    constexpr double sHalfSize = 100;

    class TestTag : public TagBase
    {
    public:
        explicit TestTag(Mask mask)
            : TagBase(mask)
        {
        }
    };

    /// A square facing up (counter-clockwise from above) at a height, as an indexed triangle list.
    vsg::ref_ptr<vsg::Node> quad(double x, double y, double z)
    {
        const float h = static_cast<float>(sHalfSize);
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        auto vertices = vsg::vec3Array::create({ { fx - h, fy - h, fz }, { fx + h, fy - h, fz }, { fx + h, fy + h, fz },
            { fx - h, fy + h, fz } });
        auto indices = vsg::ushortArray::create({ 0, 1, 2, 0, 2, 3 });
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices });
        draw->assignIndices(indices);
        draw->indexCount = 6;
        draw->instanceCount = 1;
        return draw;
    }

    /// The same square as a triangle strip without indices, as water and cell markers are.
    vsg::ref_ptr<vsg::Node> strip(double x, double y, double z)
    {
        const float h = static_cast<float>(sHalfSize);
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        auto vertices = vsg::vec3Array::create({ { fx - h, fy - h, fz }, { fx + h, fy - h, fz }, { fx - h, fy + h, fz },
            { fx + h, fy + h, fz } });
        auto draw = vsg::VertexDraw::create();
        draw->assignArrays({ vertices });
        draw->vertexCount = 4;
        draw->instanceCount = 1;

        // An intersector takes the topology from the pipeline, as it does for what the editor draws.
        auto pipeline = vsg::GraphicsPipeline::create(vsg::PipelineLayout::create(), vsg::ShaderStages{},
            vsg::GraphicsPipelineStates{ vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) });
        auto state = vsg::StateGroup::create();
        state->stateCommands = { vsg::BindGraphicsPipeline::create(pipeline) };
        state->addChild(draw);
        return state;
    }

    vsg::ref_ptr<vsg::Switch> shownFor(unsigned int mask, vsg::ref_ptr<vsg::Node> child)
    {
        auto node = vsg::Switch::create();
        node->addChild(vsg::Mask(mask), child);
        return node;
    }

    struct Scene
    {
        vsg::ref_ptr<vsg::Group> mRoot = vsg::Group::create();
        vsg::ref_ptr<TagBase> mObjectTag{ new TestTag(Mask_Reference) };
        vsg::ref_ptr<TagBase> mMarkerTag{ new TestTag(Mask_CellMarker) };

        Scene()
        {
            // An object at height 100 over (0, 0): a tag on the group above a transform, as on Object's root node.
            auto object = vsg::MatrixTransform::create(vsg::translate(vsg::dvec3(0, 0, 100)));
            object->addChild(quad(0, 0, 0));
            auto objectRoot = vsg::Group::create();
            objectRoot->addChild(object);
            Picking::setTag(*objectRoot, mObjectTag);
            mRoot->addChild(shownFor(Mask_Reference, objectRoot));

            // Water at 50, and terrain at 0, which has no tag, each reaching further than the object.
            auto wideWater = vsg::Group::create();
            wideWater->addChild(strip(0, 0, 50));
            wideWater->addChild(strip(1000, 0, 50));
            mRoot->addChild(shownFor(Mask_Water, wideWater));

            auto terrain = vsg::Group::create();
            terrain->addChild(quad(0, 0, 0));
            terrain->addChild(quad(1000, 0, 0));
            terrain->addChild(quad(2000, 0, 0));
            mRoot->addChild(shownFor(Mask_Terrain, terrain));

            // A marker over (3000, 0), alone.
            auto marker = vsg::Group::create();
            marker->addChild(strip(3000, 0, 10));
            Picking::setTag(*marker, mMarkerTag);
            mRoot->addChild(shownFor(Mask_CellMarker, marker));
        }
    };

    Picking::Ray down(double x, double y = 0)
    {
        return { vsg::dvec3(x, y, 1000), vsg::dvec3(x, y, -1000) };
    }

    constexpr unsigned int sEverything = Mask_Reference | Mask_Water | Mask_Terrain | Mask_CellMarker;

    TEST(EditorPicking, theNearestHitIsTheObjectAndGivesItsTag)
    {
        const Scene scene;
        const auto hit = Picking::pick(*scene.mRoot, down(10, 20), sEverything, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mTag.get(), scene.mObjectTag.get());
        EXPECT_NEAR(hit.mWorldPos.x, 10, sTolerance);
        EXPECT_NEAR(hit.mWorldPos.y, 20, sTolerance);
        EXPECT_NEAR(hit.mWorldPos.z, 100, sTolerance);
    }

    TEST(EditorPicking, theMaskLeavesOutWhatItDoesNotName)
    {
        const Scene scene;
        const auto hit = Picking::pick(*scene.mRoot, down(10, 20), Mask_Terrain, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mTag.get(), nullptr);
        EXPECT_NEAR(hit.mWorldPos.z, 0, sTolerance);
    }

    TEST(EditorPicking, terrainHasNoTagAndIsStillAHit)
    {
        const Scene scene;
        const auto hit = Picking::pick(*scene.mRoot, down(2000), sEverything, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mTag.get(), nullptr);
        EXPECT_NEAR(hit.mWorldPos.z, 0, sTolerance);
    }

    TEST(EditorPicking, waterDrawnAsAStripIsHit)
    {
        const Scene scene;
        for (const double x : { 950.0, 1050.0 }) // Both triangles of the strip.
        {
            const auto hit = Picking::pick(*scene.mRoot, down(x, 30), Mask_Water | Mask_Terrain, sPlacementDistance);
            ASSERT_TRUE(hit.mHit);
            EXPECT_NEAR(hit.mWorldPos.z, 50, sTolerance);
        }
    }

    TEST(EditorPicking, aStripGivesItsTagToo)
    {
        const Scene scene;
        const auto hit = Picking::pick(*scene.mRoot, down(3000), Mask_CellMarker, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mTag.get(), scene.mMarkerTag.get());
    }

    TEST(EditorPicking, emptySkyIsNoHitAtThePlacementDistanceAlongTheRay)
    {
        const Scene scene;
        const auto hit = Picking::pick(*scene.mRoot, down(5000), sEverything, sPlacementDistance);
        EXPECT_FALSE(hit.mHit);
        EXPECT_EQ(hit.mTag.get(), nullptr);
        EXPECT_NEAR(hit.mWorldPos.x, 5000, sTolerance);
        EXPECT_NEAR(hit.mWorldPos.z, 1000 - sPlacementDistance, sTolerance);
    }

    TEST(EditorPicking, theBackOfATriangleDoesNotCount)
    {
        const Scene scene;
        const Picking::Ray up{ vsg::dvec3(10, 20, -1000), vsg::dvec3(10, 20, 1000) };
        EXPECT_FALSE(Picking::pick(*scene.mRoot, up, Mask_Terrain | Mask_Reference, sPlacementDistance).mHit);
    }

    TEST(EditorPicking, theBackOfAStripTriangleDoesNotCount)
    {
        const Scene scene;
        for (const double x : { 950.0, 1050.0 })
        {
            const Picking::Ray up{ vsg::dvec3(x, 30, -1000), vsg::dvec3(x, 30, 1000) };
            EXPECT_FALSE(Picking::pick(*scene.mRoot, up, Mask_Water, sPlacementDistance).mHit);
        }
    }

    TEST(EditorPicking, aHitGivesTheVerticesOfItsTriangle)
    {
        const Scene scene;
        // In the triangle 0, 1, 2 of the terrain's quad at (1000, 0).
        const auto hit = Picking::pick(*scene.mRoot, down(1050, -50), Mask_Terrain, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mIndex0, 0u);
        EXPECT_EQ(hit.mIndex1, 1u);
        EXPECT_EQ(hit.mIndex2, 2u);
    }

    TEST(EditorPicking, theTagNearestTheRootWins)
    {
        vsg::ref_ptr<TagBase> outer(new TestTag(Mask_Reference));
        vsg::ref_ptr<TagBase> inner(new TestTag(Mask_Reference));
        auto innerGroup = vsg::Group::create();
        innerGroup->addChild(quad(0, 0, 0));
        Picking::setTag(*innerGroup, inner);
        auto outerGroup = vsg::Group::create();
        outerGroup->addChild(innerGroup);
        Picking::setTag(*outerGroup, outer);
        EXPECT_EQ(Picking::pick(*outerGroup, down(0), vsg::MASK_ALL, sPlacementDistance).mTag.get(), outer.get());
    }

    TEST(EditorPicking, aTransformedObjectIsHitWhereItIs)
    {
        auto transform = vsg::MatrixTransform::create(
            vsg::translate(vsg::dvec3(500, 0, 7)) * vsg::rotate(std::acos(-1.0) / 2, vsg::dvec3(0, 0, 1)));
        transform->addChild(quad(0, 0, 0));
        auto root = vsg::Group::create();
        root->addChild(transform);

        const auto hit = Picking::pick(*root, down(500), vsg::MASK_ALL, sPlacementDistance);
        ASSERT_TRUE(hit.mHit);
        EXPECT_NEAR(hit.mWorldPos.z, 7, sTolerance);
        EXPECT_FALSE(Picking::pick(*root, down(0), vsg::MASK_ALL, sPlacementDistance).mHit);
    }

    // The ray: VSG's projections flip Y and reverse depth, which the OSG editor's did not.
    const vsg::dvec3 sEye(10, 20, 30);

    vsg::dmat4 viewAlongY()
    {
        return vsg::lookAt(sEye, sEye + vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, 1));
    }

    TEST(EditorPickingRay, theMiddleOfThePictureLooksStraightAheadFromTheNearPlane)
    {
        const auto projection = vsg::Perspective::create(60.0, 16.0 / 9.0, 1.0, 10000.0)->transform();
        const auto ray = Picking::viewRay(projection, viewAlongY(), 0.5, 0.5);
        EXPECT_NEAR(ray.mStart.x, sEye.x, 1e-6);
        EXPECT_NEAR(ray.mStart.y, sEye.y + 1, 1e-6);
        EXPECT_NEAR(ray.mStart.z, sEye.z, 1e-6);
        EXPECT_NEAR(ray.mEnd.y, sEye.y + 10000, 1e-3);
        EXPECT_NEAR(ray.mEnd.x, sEye.x, 1e-3);
    }

    TEST(EditorPickingRay, theTopOfThePictureLooksUpAndTheRightLooksRight)
    {
        const auto projection = vsg::Perspective::create(60.0, 2.0, 1.0, 10000.0)->transform();
        const double halfHeight = std::tan(vsg::radians(30.0));

        const auto top = Picking::viewRay(projection, viewAlongY(), 0.5, 0.0);
        EXPECT_NEAR(top.mStart.z - sEye.z, halfHeight, 1e-6);

        const auto bottom = Picking::viewRay(projection, viewAlongY(), 0.5, 1.0);
        EXPECT_NEAR(bottom.mStart.z - sEye.z, -halfHeight, 1e-6);

        const auto right = Picking::viewRay(projection, viewAlongY(), 1.0, 0.5);
        EXPECT_NEAR(right.mStart.x - sEye.x, halfHeight * 2.0, 1e-6);
    }

    TEST(EditorPickingRay, anOrthographicRayIsParallelToTheView)
    {
        const auto projection = vsg::Orthographic::create(-100.0, 100.0, -50.0, 50.0, 1.0, 1000.0)->transform();
        const auto ray = Picking::viewRay(projection, viewAlongY(), 0.75, 0.25);
        EXPECT_NEAR(ray.mStart.x, sEye.x + 50, 1e-6);
        EXPECT_NEAR(ray.mEnd.x, sEye.x + 50, 1e-6);
        EXPECT_NEAR(ray.mStart.z, sEye.z + 25, 1e-6);
        EXPECT_NEAR(ray.mEnd.z, sEye.z + 25, 1e-6);
        EXPECT_LT(ray.mStart.y, ray.mEnd.y);
    }
}
