// Tests for the `gi debug` view of the exterior sky-visibility field, Render::Gi (task 0036, stage e). Written from the
// task spec before the implementation: a marker per probe near a point, brighter where more sky is seen.

#include <components/render/skyvisdebug.hpp>
#include <components/render/skyvismath.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    using namespace Render::Gi;

    class RecordingSink : public SceneUtil::DebugPrimitiveSink
    {
    public:
        struct Triangle
        {
            vsg::vec3 a, b, c;
            vsg::vec4 color;
        };
        std::vector<Triangle> triangles;
        std::size_t lines = 0;
        void addLine(const vsg::vec3&, const vsg::vec3&, const vsg::vec4&) override { ++lines; }
        void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color) override
        {
            triangles.push_back({ a, b, c, color });
        }
    };

    SkyVisField field(float level0Visibility)
    {
        SkyVisField f;
        f.cellX = 0;
        f.cellY = 0;
        f.rays = 1;
        f.spacing = 1024.f;
        f.levelSpacing = 100.f;
        f.levels = 2;
        const std::uint32_t side = skyVisColumnsPerSide(f.spacing);
        f.base.assign(static_cast<std::size_t>(side) * side, 50.f);
        VisSh v = openVisibility();
        for (float& x : v)
            x = skyVisQuantize(x * level0Visibility);
        f.probes.assign(f.base.size() * f.levels, v);
        return f;
    }
}

TEST(SkyVisDebug, OneMarkerPerProbeWithinTheRadius)
{
    const SkyVisField f = field(1.f);
    RecordingSink sink;
    // Around column (4, 4) at (4096, 4096): a radius of 1500 holds the 3 by 3 columns at 1024 spacing, 2 levels each.
    const std::size_t drawn = drawSkyVisibility(sink, f, vsg::vec3(4096.f, 4096.f, 0.f), 1500.f);
    EXPECT_EQ(drawn, 3u * 3u * 2u);
    EXPECT_EQ(sink.triangles.size(), drawn * debugMarkerTriangles);
    EXPECT_EQ(sink.lines, 0u);
    // A tiny radius holds the one column under the point.
    RecordingSink small;
    EXPECT_EQ(drawSkyVisibility(small, f, vsg::vec3(4096.f, 4096.f, 0.f), 10.f), 2u);
    // Far from the cell: nothing.
    RecordingSink none;
    EXPECT_EQ(drawSkyVisibility(none, f, vsg::vec3(50000.f, 0.f, 0.f), 1500.f), 0u);
    EXPECT_TRUE(none.triangles.empty());
}

TEST(SkyVisDebug, MarkersSitAtTheProbePositions)
{
    const SkyVisField f = field(1.f);
    RecordingSink sink;
    drawSkyVisibility(sink, f, vsg::vec3(4096.f, 4096.f, 0.f), 10.f);
    // Level 0 is at base + 0.5 * level spacing = 100, level 1 at 200, both over (4096, 4096).
    bool seen0 = false, seen1 = false;
    for (const auto& t : sink.triangles)
    {
        EXPECT_NEAR(t.a.x, 4096.f, 200.f);
        EXPECT_NEAR(t.a.y, 4096.f, 200.f);
        if (std::abs(t.a.z - 100.f) < 40.f)
            seen0 = true;
        if (std::abs(t.a.z - 200.f) < 40.f)
            seen1 = true;
    }
    EXPECT_TRUE(seen0);
    EXPECT_TRUE(seen1);
}

TEST(SkyVisDebug, BrightnessFollowsHowMuchSkyIsSeen)
{
    RecordingSink open, shaded, blocked;
    drawSkyVisibility(open, field(1.f), vsg::vec3(4096.f, 4096.f, 0.f), 10.f);
    drawSkyVisibility(shaded, field(0.4f), vsg::vec3(4096.f, 4096.f, 0.f), 10.f);
    drawSkyVisibility(blocked, field(0.f), vsg::vec3(4096.f, 4096.f, 0.f), 10.f);
    ASSERT_FALSE(open.triangles.empty());
    EXPECT_GT(open.triangles[0].color.x, shaded.triangles[0].color.x);
    EXPECT_GT(shaded.triangles[0].color.x, blocked.triangles[0].color.x);
    EXPECT_FLOAT_EQ(blocked.triangles[0].color.x, 0.f);
    // Grey: all channels alike; the alpha is the debug marker's.
    EXPECT_FLOAT_EQ(open.triangles[0].color.x, open.triangles[0].color.y);
    EXPECT_FLOAT_EQ(open.triangles[0].color.w, debugMarkerAlpha);
}

TEST(SkyVisDebug, ThePrimitiveCountIsBoundedByTheRadius)
{
    EXPECT_LE(skyVisDebugMaxMarkers(256.f, 8, 1024.f), 81u * 8u);
    EXPECT_GE(skyVisDebugMaxMarkers(256.f, 8, 1024.f), 64u * 8u);
}
