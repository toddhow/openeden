#include <components/extension/debug_draw_service.hpp>
#include <components/extension/host_impl.hpp>
#include <openeden/debug_draw.hpp>
#include <openeden/host.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::extension::DebugDrawServiceImpl;
    using openeden::extension::HostImpl;
    using openeden::sdk::Color4f;
    using openeden::sdk::DebugBox;
    using openeden::sdk::DebugDrawService;
    using openeden::sdk::DebugLine;
    using openeden::sdk::DebugPoint;
    using openeden::sdk::DebugSnapshot;
    using openeden::sdk::DebugSphere;
    using openeden::sdk::DebugText;
    using openeden::sdk::Vector3;

    TEST(DebugDrawTest, PrimitiveSubmissionAllTypes)
    {
        DebugDrawServiceImpl service;

        const Vector3 posA{ 1.0f, 2.0f, 3.0f };
        const Vector3 posB{ 4.0f, 5.0f, 6.0f };
        const Color4f red{ 1.0f, 0.0f, 0.0f, 1.0f };
        const Color4f green{ 0.0f, 1.0f, 0.0f, 1.0f };
        const Color4f blue{ 0.0f, 0.0f, 1.0f, 1.0f };
        const Color4f white{ 1.0f, 1.0f, 1.0f, 1.0f };

        // 1. Point
        service.drawPoint("creativemotion", posA, red, 4.0f, true, 0.0f);

        // 2. Line
        service.drawLine("creativemotion", posA, posB, green, 2.0f, true, 0.0f);

        // 3. Box
        service.drawBox("creativemotion", posA, posB, blue, true, 0.0f);

        // 4. Sphere
        service.drawSphere("creativemotion", posA, 5.0f, white, true, 0.0f);

        // 5. Text
        service.drawText("creativemotion", posA, "Debug Joint", white, 1.2f, false, 0.0f);

        const DebugSnapshot snapshot = service.extractSnapshot();

        EXPECT_FALSE(snapshot.empty());
        EXPECT_EQ(snapshot.totalCount(), 5u);

        ASSERT_EQ(snapshot.points.size(), 1u);
        EXPECT_FLOAT_EQ(snapshot.points[0].position.x, 1.0f);
        EXPECT_FLOAT_EQ(snapshot.points[0].color.r, 1.0f);
        EXPECT_FLOAT_EQ(snapshot.points[0].size, 4.0f);
        EXPECT_TRUE(snapshot.points[0].depthTest);
        EXPECT_EQ(snapshot.points[0].extensionId, "creativemotion");

        ASSERT_EQ(snapshot.lines.size(), 1u);
        EXPECT_FLOAT_EQ(snapshot.lines[0].start.x, 1.0f);
        EXPECT_FLOAT_EQ(snapshot.lines[0].end.x, 4.0f);
        EXPECT_FLOAT_EQ(snapshot.lines[0].lineWidth, 2.0f);
        EXPECT_EQ(snapshot.lines[0].extensionId, "creativemotion");

        ASSERT_EQ(snapshot.boxes.size(), 1u);
        EXPECT_FLOAT_EQ(snapshot.boxes[0].min.y, 2.0f);
        EXPECT_FLOAT_EQ(snapshot.boxes[0].max.y, 5.0f);

        ASSERT_EQ(snapshot.spheres.size(), 1u);
        EXPECT_FLOAT_EQ(snapshot.spheres[0].center.z, 3.0f);
        EXPECT_FLOAT_EQ(snapshot.spheres[0].radius, 5.0f);

        ASSERT_EQ(snapshot.texts.size(), 1u);
        EXPECT_EQ(snapshot.texts[0].text, "Debug Joint");
        EXPECT_FLOAT_EQ(snapshot.texts[0].scale, 1.2f);
        EXPECT_FALSE(snapshot.texts[0].depthTest);
    }

    TEST(DebugDrawTest, ExtensionAttribution)
    {
        DebugDrawServiceImpl service;

        const Vector3 origin{ 0.0f, 0.0f, 0.0f };
        const Color4f color{ 1.0f, 1.0f, 1.0f, 1.0f };

        service.drawPoint("extension_ragdoll", origin, color);
        service.drawPoint("extension_navmesh", origin, color);

        const DebugSnapshot snapshot = service.extractSnapshot();
        ASSERT_EQ(snapshot.points.size(), 2u);

        EXPECT_EQ(snapshot.points[0].extensionId, "extension_ragdoll");
        EXPECT_EQ(snapshot.points[1].extensionId, "extension_navmesh");
    }

    TEST(DebugDrawTest, DepthTestAndDurationToggles)
    {
        DebugDrawServiceImpl service;

        const Vector3 p1{ 0.0f, 0.0f, 0.0f };
        const Vector3 p2{ 1.0f, 1.0f, 1.0f };
        const Color4f col{ 1.0f, 1.0f, 0.0f, 1.0f };

        service.drawLine("extension_test", p1, p2, col, 1.0f, false, 3.5f);

        const DebugSnapshot snapshot = service.extractSnapshot();
        ASSERT_EQ(snapshot.lines.size(), 1u);
        EXPECT_FALSE(snapshot.lines[0].depthTest);
        EXPECT_FLOAT_EQ(snapshot.lines[0].duration, 3.5f);
    }

    TEST(DebugDrawTest, FrameClearingTransientPrimitives)
    {
        DebugDrawServiceImpl service;

        const Vector3 origin{ 0.0f, 0.0f, 0.0f };
        const Color4f col{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Submit transient primitives (duration = 0.0f)
        service.drawPoint("extension_test", origin, col, 1.0f, true, 0.0f);
        service.drawLine("extension_test", origin, Vector3{ 1.0f, 0.0f, 0.0f }, col, 1.0f, true, 0.0f);

        EXPECT_EQ(service.extractSnapshot().totalCount(), 2u);

        // Frame boundary advances
        service.endFrame();

        // Transient primitives must be completely cleared
        const DebugSnapshot postFrameSnapshot = service.extractSnapshot();
        EXPECT_TRUE(postFrameSnapshot.empty());
        EXPECT_EQ(postFrameSnapshot.totalCount(), 0u);
    }

    TEST(DebugDrawTest, PersistentPrimitivesDecayOverTime)
    {
        DebugDrawServiceImpl service;

        const Vector3 origin{ 0.0f, 0.0f, 0.0f };
        const Color4f col{ 1.0f, 0.0f, 0.0f, 1.0f };

        // Persistent line with duration 1.0s
        service.drawLine("extension_test", origin, Vector3{ 0.0f, 1.0f, 0.0f }, col, 1.0f, true, 1.0f);

        // Transient point
        service.drawPoint("extension_test", origin, col, 1.0f, true, 0.0f);

        EXPECT_EQ(service.extractSnapshot().totalCount(), 2u);

        // Advance 0.4 seconds
        service.endFrame(0.4f);

        // Transient point is gone, persistent line remains
        DebugSnapshot snap1 = service.extractSnapshot();
        EXPECT_EQ(snap1.points.size(), 0u);
        ASSERT_EQ(snap1.lines.size(), 1u);

        // Advance another 0.7 seconds (total 1.1s > 1.0s duration)
        service.endFrame(0.7f);

        // Persistent line should now have expired
        DebugSnapshot snap2 = service.extractSnapshot();
        EXPECT_TRUE(snap2.empty());
    }

    TEST(DebugDrawTest, ClearAllSemantics)
    {
        DebugDrawServiceImpl service;

        const Vector3 pos{ 0.0f, 0.0f, 0.0f };
        const Color4f col{ 1.0f, 1.0f, 1.0f, 1.0f };

        service.drawPoint("extension_a", pos, col);
        service.drawBox("extension_b", pos, pos, col);
        service.drawSphere("extension_c", pos, 1.0f, col);

        EXPECT_EQ(service.extractSnapshot().totalCount(), 3u);

        service.clear();

        EXPECT_TRUE(service.extractSnapshot().empty());
    }

    TEST(DebugDrawTest, ClearExtensionSemantics)
    {
        DebugDrawServiceImpl service;

        const Vector3 pos{ 0.0f, 0.0f, 0.0f };
        const Color4f col{ 1.0f, 1.0f, 1.0f, 1.0f };

        service.drawPoint("extension_target", pos, col);
        service.drawLine("extension_target", pos, pos, col);
        service.drawSphere("extension_keeper", pos, 2.0f, col);

        EXPECT_EQ(service.extractSnapshot().totalCount(), 3u);

        service.clearExtension("extension_target");

        const DebugSnapshot snapshot = service.extractSnapshot();
        EXPECT_EQ(snapshot.totalCount(), 1u);
        ASSERT_EQ(snapshot.spheres.size(), 1u);
        EXPECT_EQ(snapshot.spheres[0].extensionId, "extension_keeper");
    }

    TEST(DebugDrawTest, HostImplWiresDebugDrawService)
    {
        HostImpl host("TestHost");
        DebugDrawService* debugDraw = host.getDebugDrawService();

        ASSERT_NE(debugDraw, nullptr);

        debugDraw->drawPoint("host_extension", Vector3{ 0.0f, 0.0f, 0.0f }, Color4f{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
}
