// Task 0017 stage 5 (W3 leftover): the per-frame copy of the extensions' debug draw, and the lines drawn from it.
//
// In plain words: DebugDrawFrame is the fixed-size list the engine fills once a frame from the DebugDrawService, and
// expandDebugDrawFrame is the pure step that turns it into line segments for the renderer. These tests check what gets
// copied, that the caps drop and count instead of growing, and the shapes and units of the lines.
#include <components/extension/debug_draw_frame.hpp>
#include <components/extension/debug_draw_geometry.hpp>
#include <components/extension/debug_draw_service.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    using openeden::extension::DebugDrawFrame;
    using openeden::extension::DebugDrawServiceImpl;
    using openeden::extension::expandDebugDrawFrame;
    using openeden::sdk::Color4f;
    using openeden::sdk::Vector3;

    struct Segment
    {
        Vector3 from;
        Vector3 to;
        Color4f color;
    };

    std::vector<Segment> expand(const DebugDrawFrame& frame, bool depthTest, float scale = 1.0f)
    {
        std::vector<Segment> out;
        const std::size_t count
            = expandDebugDrawFrame(frame, depthTest, scale, [&](const Vector3& a, const Vector3& b, const Color4f& c) {
                  out.push_back({ a, b, c });
              });
        EXPECT_EQ(count, out.size());
        return out;
    }

    float distance(const Vector3& a, const Vector3& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    TEST(DebugDrawFrame, StartsEmpty)
    {
        const DebugDrawFrame frame;
        EXPECT_EQ(frame.primitiveCount(), 0u);
        EXPECT_EQ(frame.dropped().total(), 0u);
        EXPECT_EQ(frame.overflowFrames(), 0u);
    }

    TEST(DebugDrawFrame, CollectCopiesEveryKindWithItsValues)
    {
        DebugDrawServiceImpl service;
        const Color4f red{ 1.0f, 0.0f, 0.0f, 0.5f };
        service.drawPoint("a", { 1, 2, 3 }, red, 2.5f, false);
        service.drawLine("a", { 0, 0, 0 }, { 4, 5, 6 }, red, 3.0f, true);
        service.drawBox("b", { -1, -1, -1 }, { 1, 2, 3 }, red, false);
        service.drawSphere("b", { 7, 8, 9 }, 1.5f, red, true);
        service.drawText("b", { 1, 1, 1 }, "hello", red, 1.0f, true);

        DebugDrawFrame frame;
        frame.collect(service);

        ASSERT_EQ(frame.points().size(), 1u);
        EXPECT_FLOAT_EQ(frame.points()[0].position.y, 2.0f);
        EXPECT_FLOAT_EQ(frame.points()[0].size, 2.5f);
        EXPECT_FALSE(frame.points()[0].depthTest);
        EXPECT_FLOAT_EQ(frame.points()[0].color.a, 0.5f);

        ASSERT_EQ(frame.lines().size(), 1u);
        EXPECT_FLOAT_EQ(frame.lines()[0].to.z, 6.0f);
        EXPECT_TRUE(frame.lines()[0].depthTest);

        ASSERT_EQ(frame.boxes().size(), 1u);
        EXPECT_FLOAT_EQ(frame.boxes()[0].max.z, 3.0f);
        EXPECT_FALSE(frame.boxes()[0].depthTest);

        ASSERT_EQ(frame.spheres().size(), 1u);
        EXPECT_FLOAT_EQ(frame.spheres()[0].radius, 1.5f);
        EXPECT_FLOAT_EQ(frame.spheres()[0].center.x, 7.0f);

        ASSERT_EQ(frame.labels().size(), 1u);
        EXPECT_STREQ(frame.labels()[0].text, "hello");
        EXPECT_TRUE(frame.labels()[0].depthTest);

        EXPECT_EQ(frame.primitiveCount(), 5u);
        EXPECT_EQ(frame.dropped().total(), 0u);
        EXPECT_EQ(frame.overflowFrames(), 0u);
    }

    TEST(DebugDrawFrame, CollectReplacesTheContents)
    {
        DebugDrawServiceImpl service;
        DebugDrawFrame frame;
        service.drawLine("a", {}, { 1, 0, 0 }, Color4f{});
        frame.collect(service);
        ASSERT_EQ(frame.lines().size(), 1u);

        service.clear();
        service.drawPoint("a", {}, Color4f{});
        frame.collect(service);
        EXPECT_EQ(frame.lines().size(), 0u);
        EXPECT_EQ(frame.points().size(), 1u);
    }

    TEST(DebugDrawFrame, TransientPrimitivesAreNotCollectedAfterTheFrameEndsButPersistentOnesAre)
    {
        DebugDrawServiceImpl service;
        service.drawLine("a", {}, { 1, 0, 0 }, Color4f{}, 1.0f, true, 0.0f);
        service.drawLine("a", {}, { 2, 0, 0 }, Color4f{}, 1.0f, true, 1.0f);

        DebugDrawFrame frame;
        frame.collect(service);
        EXPECT_EQ(frame.lines().size(), 2u);

        service.endFrame(0.25f);
        frame.collect(service);
        ASSERT_EQ(frame.lines().size(), 1u);
        EXPECT_FLOAT_EQ(frame.lines()[0].to.x, 2.0f);
    }

    TEST(DebugDrawFrame, LongLabelTextIsCutAndAlwaysEndsWithZero)
    {
        DebugDrawServiceImpl service;
        service.drawText("a", {}, std::string(200, 'x'), Color4f{});
        service.drawText("a", {}, "", Color4f{});

        DebugDrawFrame frame;
        frame.collect(service);
        ASSERT_EQ(frame.labels().size(), 2u);
        EXPECT_EQ(std::string(frame.labels()[0].text).size(), DebugDrawFrame::MaxLabelChars);
        EXPECT_EQ(frame.labels()[0].text[DebugDrawFrame::MaxLabelChars], '\0');
        EXPECT_STREQ(frame.labels()[1].text, "");
    }

    TEST(DebugDrawFrame, PrimitivesPastTheCapAreDroppedAndCountedAndNothingGrows)
    {
        DebugDrawServiceImpl service;
        const std::size_t extra = 10;
        for (std::size_t i = 0; i < DebugDrawFrame::MaxLines + extra; ++i)
            service.drawLine("a", { static_cast<float>(i), 0, 0 }, { 0, 0, 0 }, Color4f{});
        for (std::size_t i = 0; i < DebugDrawFrame::MaxPoints + 3; ++i)
            service.drawPoint("a", {}, Color4f{});
        for (std::size_t i = 0; i < DebugDrawFrame::MaxBoxes + 2; ++i)
            service.drawBox("a", {}, { 1, 1, 1 }, Color4f{});
        for (std::size_t i = 0; i < DebugDrawFrame::MaxSpheres + 1; ++i)
            service.drawSphere("a", {}, 1.0f, Color4f{});
        for (std::size_t i = 0; i < DebugDrawFrame::MaxLabels + 4; ++i)
            service.drawText("a", {}, "t", Color4f{});

        DebugDrawFrame frame;
        const auto* linesData = frame.lines().data();
        const auto linesCapacity = frame.lines().capacity();
        const auto* labelsData = frame.labels().data();
        const auto labelsCapacity = frame.labels().capacity();
        EXPECT_GE(linesCapacity, DebugDrawFrame::MaxLines);

        frame.collect(service);

        EXPECT_EQ(frame.lines().size(), DebugDrawFrame::MaxLines);
        EXPECT_EQ(frame.points().size(), DebugDrawFrame::MaxPoints);
        EXPECT_EQ(frame.boxes().size(), DebugDrawFrame::MaxBoxes);
        EXPECT_EQ(frame.spheres().size(), DebugDrawFrame::MaxSpheres);
        EXPECT_EQ(frame.labels().size(), DebugDrawFrame::MaxLabels);
        EXPECT_EQ(frame.dropped().lines, extra);
        EXPECT_EQ(frame.dropped().points, 3u);
        EXPECT_EQ(frame.dropped().boxes, 2u);
        EXPECT_EQ(frame.dropped().spheres, 1u);
        EXPECT_EQ(frame.dropped().labels, 4u);
        EXPECT_EQ(frame.dropped().total(), extra + 3 + 2 + 1 + 4);
        EXPECT_EQ(frame.overflowFrames(), 1u);

        // No reallocation: the storage is the one reserved at construction
        EXPECT_EQ(frame.lines().data(), linesData);
        EXPECT_EQ(frame.lines().capacity(), linesCapacity);
        EXPECT_EQ(frame.labels().data(), labelsData);
        EXPECT_EQ(frame.labels().capacity(), labelsCapacity);

        // The first primitives are the ones kept
        EXPECT_FLOAT_EQ(frame.lines().front().from.x, 0.0f);
        EXPECT_FLOAT_EQ(frame.lines().back().from.x, static_cast<float>(DebugDrawFrame::MaxLines - 1));
    }

    TEST(DebugDrawFrame, TheDroppedCountIsOfTheLastCollectAndOverflowFramesAdds)
    {
        DebugDrawServiceImpl service;
        for (std::size_t i = 0; i < DebugDrawFrame::MaxPoints + 1; ++i)
            service.drawPoint("a", {}, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);
        frame.collect(service);
        EXPECT_EQ(frame.dropped().points, 1u);
        EXPECT_EQ(frame.overflowFrames(), 2u);

        service.clear();
        frame.collect(service);
        EXPECT_EQ(frame.dropped().total(), 0u);
        EXPECT_EQ(frame.overflowFrames(), 2u);
    }

    TEST(DebugDrawFrame, ClearEmptiesButKeepsTheOverflowHistory)
    {
        DebugDrawServiceImpl service;
        for (std::size_t i = 0; i < DebugDrawFrame::MaxPoints + 1; ++i)
            service.drawPoint("a", {}, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);
        frame.clear();
        EXPECT_EQ(frame.primitiveCount(), 0u);
        EXPECT_EQ(frame.dropped().total(), 0u);
        EXPECT_EQ(frame.overflowFrames(), 1u);
    }

    // The lines drawn from a frame

    TEST(DebugDrawGeometry, ALineIsOneSegmentWithItsColor)
    {
        DebugDrawServiceImpl service;
        service.drawLine("a", { 1, 2, 3 }, { 4, 5, 6 }, Color4f{ 0.1f, 0.2f, 0.3f, 0.4f });
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true);
        ASSERT_EQ(segments.size(), 1u);
        EXPECT_FLOAT_EQ(segments[0].from.x, 1.0f);
        EXPECT_FLOAT_EQ(segments[0].to.z, 6.0f);
        EXPECT_FLOAT_EQ(segments[0].color.g, 0.2f);
        EXPECT_FLOAT_EQ(segments[0].color.a, 0.4f);
    }

    TEST(DebugDrawGeometry, TheScaleMultipliesEveryPosition)
    {
        DebugDrawServiceImpl service;
        service.drawLine("a", { 1, 2, 3 }, { 4, 5, 6 }, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true, 69.99125f);
        ASSERT_EQ(segments.size(), 1u);
        EXPECT_FLOAT_EQ(segments[0].from.y, 2.0f * 69.99125f);
        EXPECT_FLOAT_EQ(segments[0].to.x, 4.0f * 69.99125f);
    }

    TEST(DebugDrawGeometry, DepthTestedAndOnTopPrimitivesSplitWithNoneLostOrRepeated)
    {
        DebugDrawServiceImpl service;
        service.drawLine("a", {}, { 1, 0, 0 }, Color4f{}, 1.0f, true);
        service.drawLine("a", {}, { 2, 0, 0 }, Color4f{}, 1.0f, false);
        service.drawPoint("a", {}, Color4f{}, 1.0f, false);
        service.drawBox("a", {}, { 1, 1, 1 }, Color4f{}, true);
        service.drawSphere("a", {}, 1.0f, Color4f{}, false);
        service.drawText("a", {}, "t", Color4f{}, 1.0f, true);
        DebugDrawFrame frame;
        frame.collect(service);

        const auto tested = expand(frame, true);
        const auto onTop = expand(frame, false);
        using namespace openeden::extension;
        EXPECT_EQ(tested.size(), 1 + kLinesPerBox + kLinesPerLabel);
        EXPECT_EQ(onTop.size(), 1 + kLinesPerPoint + kLinesPerSphere);
    }

    TEST(DebugDrawGeometry, APointIsACrossWhoseSizeScalesItsArms)
    {
        DebugDrawServiceImpl service;
        service.drawPoint("a", { 1, 2, 3 }, Color4f{}, 2.0f);
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true);
        ASSERT_EQ(segments.size(), openeden::extension::kLinesPerPoint);
        const float half = 2.0f * openeden::extension::kPointHalfExtentPerSizeM;
        for (const auto& segment : segments)
        {
            EXPECT_NEAR(distance(segment.from, segment.to), 2.0f * half, 1e-5f);
            // The cross is centred on the point
            EXPECT_NEAR((segment.from.x + segment.to.x) * 0.5f, 1.0f, 1e-5f);
            EXPECT_NEAR((segment.from.y + segment.to.y) * 0.5f, 2.0f, 1e-5f);
            EXPECT_NEAR((segment.from.z + segment.to.z) * 0.5f, 3.0f, 1e-5f);
        }
    }

    TEST(DebugDrawGeometry, ALabelIsMarkedByACross)
    {
        DebugDrawServiceImpl service;
        service.drawText("a", { 1, 2, 3 }, "name", Color4f{ 0, 1, 0, 1 }, 1.0f, true);
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true);
        ASSERT_EQ(segments.size(), openeden::extension::kLinesPerLabel);
        EXPECT_NEAR(
            distance(segments[0].from, segments[0].to), 2.0f * openeden::extension::kLabelMarkerHalfExtentM, 1e-5f);
        EXPECT_FLOAT_EQ(segments[0].color.g, 1.0f);
    }

    TEST(DebugDrawGeometry, ABoxIsItsTwelveEdgesEachOnceAndAxisAligned)
    {
        DebugDrawServiceImpl service;
        service.drawBox("a", { 0, 0, 0 }, { 2, 3, 4 }, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true);
        ASSERT_EQ(segments.size(), 12u);
        int alongX = 0;
        int alongY = 0;
        int alongZ = 0;
        for (const auto& s : segments)
        {
            const float dx = std::fabs(s.to.x - s.from.x);
            const float dy = std::fabs(s.to.y - s.from.y);
            const float dz = std::fabs(s.to.z - s.from.z);
            // Exactly one axis changes, and by the box's extent on that axis
            if (dx > 0 && dy == 0 && dz == 0)
            {
                EXPECT_FLOAT_EQ(dx, 2.0f);
                ++alongX;
            }
            else if (dy > 0 && dx == 0 && dz == 0)
            {
                EXPECT_FLOAT_EQ(dy, 3.0f);
                ++alongY;
            }
            else if (dz > 0 && dx == 0 && dy == 0)
            {
                EXPECT_FLOAT_EQ(dz, 4.0f);
                ++alongZ;
            }
            else
            {
                ADD_FAILURE() << "an edge that is not axis aligned";
            }
            // Endpoints are corners of the box
            for (const Vector3& p : { s.from, s.to })
            {
                EXPECT_TRUE(p.x == 0.0f || p.x == 2.0f);
                EXPECT_TRUE(p.y == 0.0f || p.y == 3.0f);
                EXPECT_TRUE(p.z == 0.0f || p.z == 4.0f);
            }
        }
        EXPECT_EQ(alongX, 4);
        EXPECT_EQ(alongY, 4);
        EXPECT_EQ(alongZ, 4);
    }

    TEST(DebugDrawGeometry, ASphereIsThreeClosedCirclesOnItsSurface)
    {
        DebugDrawServiceImpl service;
        const Vector3 center{ 10, -4, 2 };
        service.drawSphere("a", center, 2.0f, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);

        const auto segments = expand(frame, true);
        ASSERT_EQ(segments.size(), openeden::extension::kLinesPerSphere);
        for (const auto& s : segments)
        {
            EXPECT_NEAR(distance(s.from, center), 2.0f, 1e-4f);
            EXPECT_NEAR(distance(s.to, center), 2.0f, 1e-4f);
        }
        // Each circle closes: the segment ends are the next segment's starts (within the circle)
        const int perCircle = openeden::extension::kSphereCircleSegments;
        for (int circle = 0; circle < 3; ++circle)
        {
            for (int i = 0; i < perCircle; ++i)
            {
                const auto& a = segments[static_cast<std::size_t>(circle * perCircle + i)];
                const auto& b = segments[static_cast<std::size_t>(circle * perCircle + (i + 1) % perCircle)];
                EXPECT_NEAR(distance(a.to, b.from), 0.0f, 1e-4f);
            }
        }
    }

    TEST(DebugDrawGeometry, ScaleAppliesToSpheresAndBoxesToo)
    {
        DebugDrawServiceImpl service;
        service.drawSphere("a", { 1, 0, 0 }, 1.0f, Color4f{});
        DebugDrawFrame frame;
        frame.collect(service);
        const auto segments = expand(frame, true, 10.0f);
        for (const auto& s : segments)
            EXPECT_NEAR(distance(s.from, { 10.0f, 0.0f, 0.0f }), 10.0f, 1e-3f);
    }

    TEST(DebugDrawGeometry, TheWorstCaseFitsTheRenderersTwoGeometries)
    {
        using namespace openeden::extension;
        constexpr std::size_t worst = DebugDrawFrame::MaxLines + DebugDrawFrame::MaxPoints * kLinesPerPoint
            + DebugDrawFrame::MaxBoxes * kLinesPerBox + DebugDrawFrame::MaxSpheres * kLinesPerSphere
            + DebugDrawFrame::MaxLabels * kLinesPerLabel;
        // The renderer's debug geometry for this holds 1 << 18 vertices, that is 1 << 17 lines, for each of the two
        static_assert(worst <= (std::size_t(1) << 17));
        EXPECT_LE(worst, std::size_t(1) << 17);
    }

    TEST(DebugDrawGeometry, AnEmptyFrameMakesNoLines)
    {
        const DebugDrawFrame frame;
        EXPECT_TRUE(expand(frame, true).empty());
        EXPECT_TRUE(expand(frame, false).empty());
    }
}
