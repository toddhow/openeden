#include <gtest/gtest.h>

#include <cmath>

#include <vsg/app/ProjectionMatrix.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/VertexDraw.h>

#include <apps/opencs/view/render/mask.hpp>
#include <apps/opencs/view/render/picking.hpp>
#include <apps/opencs/view/render/selectionmarkergeometry.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

// toddhow's report: a drag that starts on one of the object marker's arrows does a box selection instead of an axis
// move. The marker is drawn at a size that follows its distance (SceneWidget::update's divide by 75, times the
// preference object-marker-scale, 5), so that an arrow's shaft, 0.033 across in the marker's own units, is a pixel or
// two wide on the screen. A ray through the pixel next to it misses, and a mouse press never lands exactly. These build
// the marker's triangles as ObjectMarker does, put them where the capture of Seyda Neen had them, and aim.
namespace
{
    using namespace CSVRender;
    namespace Marker = CSVRender::SelectionMarkerGeometry;

    constexpr double sWidth = 1280;
    constexpr double sHeight = 720;
    constexpr double sDistance = 1400;
    constexpr double sMarkerScale = sDistance / Marker::sDistanceForUnitScale * 5.0;
    constexpr double sRadiusPixels = 4;

    class TestMarkerTag : public TagBase
    {
    public:
        explicit TestMarkerTag(int axis)
            : TagBase(Mask_Reference)
            , mAxis(axis)
        {
        }
        int mAxis;
    };

    class TestObjectTag : public TagBase
    {
    public:
        TestObjectTag()
            : TagBase(Mask_Reference)
        {
        }
    };

    struct Scene
    {
        vsg::ref_ptr<vsg::Group> mRoot = vsg::Group::create();
        vsg::ref_ptr<vsg::MatrixTransform> mMarker = vsg::MatrixTransform::create();
        vsg::dmat4 mProjection = vsg::Perspective::create(60.0, sWidth / sHeight, 1.0, 20000.0)->transform();
        // The eye looks along +Y at the marker, which is at the origin.
        vsg::dmat4 mView = vsg::lookAt(vsg::dvec3(0, -sDistance, 0), vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, 1));

        Scene()
        {
            mMarker->matrix = vsg::scale(sMarkerScale, sMarkerScale, sMarkerScale);
            for (const auto& source : Marker::moveParts())
            {
                auto vertices = vsg::vec3Array::create(static_cast<uint32_t>(source.mVertices.size()));
                std::copy(source.mVertices.begin(), source.mVertices.end(), vertices->begin());
                auto draw = vsg::VertexDraw::create();
                draw->assignArrays({ vertices });
                draw->vertexCount = static_cast<uint32_t>(vertices->size());
                draw->instanceCount = 1;
                Picking::setTag(*draw, vsg::ref_ptr<TagBase>(new TestMarkerTag(source.mAxis)));
                mMarker->addChild(draw);
            }
            mRoot->addChild(mMarker);

            // A big object behind the marker, as the house is: the marker must win over it.
            const float h = 5000.f;
            auto vertices = vsg::vec3Array::create({ { -h, 300.f, -h }, { h, 300.f, -h }, { h, 300.f, h },
                { -h, 300.f, -h }, { h, 300.f, h }, { -h, 300.f, h } });
            auto wall = vsg::VertexDraw::create();
            wall->assignArrays({ vertices });
            wall->vertexCount = 6;
            wall->instanceCount = 1;
            Picking::setTag(*wall, vsg::ref_ptr<TagBase>(new TestObjectTag));
            mRoot->addChild(wall);
        }

        /// The pixel where a point of the world is.
        std::pair<double, double> pixelOf(const vsg::dvec3& world) const
        {
            const vsg::dvec4 clip = mProjection * (mView * vsg::dvec4(world.x, world.y, world.z, 1.0));
            return { (clip.x / clip.w + 1.0) / 2.0 * sWidth, (clip.y / clip.w + 1.0) / 2.0 * sHeight };
        }

        std::optional<Picking::Result> aim(double px, double py, double radius) const
        {
            return Picking::pickNear(*mRoot, mProjection, mView, px / sWidth, py / sHeight, sWidth, sHeight, radius,
                vsg::Mask(Mask_Reference), [](const Picking::Result& result) {
                    return dynamic_cast<TestMarkerTag*>(result.mTag.get()) != nullptr;
                });
        }

        int axisOf(const std::optional<Picking::Result>& result) const
        {
            if (!result)
                return -1;
            return dynamic_cast<TestMarkerTag&>(*result->mTag).mAxis;
        }
    };

    /// A point on the Z arrow's shaft, in the world.
    vsg::dvec3 shaftPoint()
    {
        return vsg::dvec3(0, 0, 1.2 * sMarkerScale);
    }
}

TEST(CSVRenderMarkerPicking, aimingAtTheShaftsMiddleHits)
{
    Scene scene;
    const auto [px, py] = scene.pixelOf(shaftPoint());
    EXPECT_EQ(scene.axisOf(scene.aim(px, py, 0)), 2);
}

TEST(CSVRenderMarkerPicking, theShaftIsOnlyAPixelOrTwoWide)
{
    // The cause of the report: what the ray through the next pixel but two sees is the object behind.
    Scene scene;
    const auto [px, py] = scene.pixelOf(shaftPoint());
    EXPECT_EQ(scene.axisOf(scene.aim(px + 3, py, 0)), -1);
}

TEST(CSVRenderMarkerPicking, aNearMissOfAFewPixelsHitsTheShaft)
{
    Scene scene;
    const auto [px, py] = scene.pixelOf(shaftPoint());
    EXPECT_EQ(scene.axisOf(scene.aim(px + 3, py, sRadiusPixels)), 2);
    EXPECT_EQ(scene.axisOf(scene.aim(px - 3, py + 2, sRadiusPixels)), 2);
}

TEST(CSVRenderMarkerPicking, aFarMissStillMisses)
{
    Scene scene;
    const auto [px, py] = scene.pixelOf(shaftPoint());
    EXPECT_EQ(scene.axisOf(scene.aim(px + 12, py, sRadiusPixels)), -1);
}

TEST(CSVRenderMarkerPicking, theNearerPartWinsAmongThoseAroundThePoint)
{
    // On the shaft itself, with the radius on: the shaft, not something a few pixels away.
    Scene scene;
    const auto [px, py] = scene.pixelOf(shaftPoint());
    const auto result = scene.aim(px, py, sRadiusPixels);
    ASSERT_TRUE(result);
    EXPECT_NEAR(result->mWorldPos.x, 0.0, Marker::sShaftRadius * sMarkerScale * 1.01);
}
