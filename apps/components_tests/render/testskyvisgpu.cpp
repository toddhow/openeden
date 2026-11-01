// Tests for how the sky-visibility field is laid out for the GPU and read back, Render::Gi (task 0036, stage c).
// Written from the task spec before the implementation. The reader under test is the C++ twin of
// files/shaders/lib/view/skyvis.glsl; the field data here is made up.

#include <components/render/skyvisgpu.hpp>
#include <components/render/skyvismath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace Render::Gi;

    // A field of 1024 unit spacing (9 columns a side), 3 levels, every probe the same visibility.
    SkyVisField uniformField(int cx, int cy, const VisSh& vis, float base = 0.f)
    {
        SkyVisField f;
        f.cellX = cx;
        f.cellY = cy;
        f.rays = 1;
        f.seed = 1;
        f.spacing = 1024.f;
        f.levelSpacing = 100.f;
        f.levels = 3;
        const std::uint32_t side = skyVisColumnsPerSide(f.spacing);
        f.base.assign(static_cast<std::size_t>(side) * side, base);
        VisSh q = vis;
        for (float& v : q)
            v = skyVisQuantize(v);
        f.probes.assign(f.base.size() * f.levels, q);
        return f;
    }

    struct Gpu
    {
        std::vector<vsg::vec4> header = std::vector<vsg::vec4>(skyHeaderVec4s);
        std::vector<vsg::vec4> pool = std::vector<vsg::vec4>(2000000 / 4);
        SkyPackResult result;

        void pack(const std::vector<const SkyVisField*>& cells, const Sh9& sky, float strength = 1.f,
            bool enabled = true)
        {
            result = packSkyVisibility(cells, header.data(), pool.data(), pool.size());
            writeSkyHeader(header.data(), enabled, strength, sky);
        }

        vsg::vec3 at(const vsg::vec3& pos, const vsg::vec3& normal, const vsg::vec3& flat) const
        {
            return skyVisAmbient(header.data(), pool.data(), pos, normal, flat);
        }
    };

    const vsg::vec3 flat(0.2f, 0.3f, 0.4f);
    const vsg::vec3 up(0.f, 0.f, 1.f);

    Sh9 greySky()
    {
        const vsg::vec3 u(0.5f, 0.5f, 0.5f);
        return projectSky(u, u, u);
    }

    void expectNear(const vsg::vec3& a, const vsg::vec3& b, float tol)
    {
        EXPECT_NEAR(a.x, b.x, tol);
        EXPECT_NEAR(a.y, b.y, tol);
        EXPECT_NEAR(a.z, b.z, tol);
    }
}

TEST(SkyVisGpu, HeaderSizeFollowsTheWindow)
{
    EXPECT_EQ(skyWindow * skyWindow, static_cast<int>(skySlots));
    EXPECT_EQ(skyHeaderVec4s, 12u + skySlots);
}

TEST(SkyVisGpu, PackedCellCountsAndSkips)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, hemisphereVisibility());
    const SkyVisField b = uniformField(1, 0, hemisphereVisibility());
    const SkyVisField far = uniformField(100, 0, hemisphereVisibility()); // outside the window
    g.pack({ &a, &b, &far }, greySky());
    EXPECT_EQ(g.result.packedCells, 2u);
    EXPECT_EQ(g.result.skipped, 1u);
    // Nothing to pack leaves a disabled field.
    g.pack({}, greySky());
    EXPECT_EQ(g.result.packedCells, 0u);
    expectNear(g.at(vsg::vec3(100, 100, 50), up, flat), flat, 1e-6f);
}

TEST(SkyVisGpu, FieldsThatDoNotFitAreSkippedNotWrittenPast)
{
    Gpu g;
    g.pool.resize(100); // far too small for one cell
    const SkyVisField a = uniformField(0, 0, hemisphereVisibility());
    g.pack({ &a }, greySky());
    EXPECT_EQ(g.result.packedCells, 0u);
    EXPECT_EQ(g.result.skipped, 1u);
}

TEST(SkyVisGpu, CellsOfAnotherShapeAreSkipped)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, hemisphereVisibility());
    SkyVisField b = uniformField(1, 0, hemisphereVisibility());
    b.levels = 2;
    b.probes.resize(b.base.size() * 2);
    g.pack({ &a, &b }, greySky());
    EXPECT_EQ(g.result.packedCells, 1u);
    EXPECT_EQ(g.result.skipped, 1u);
}

TEST(SkyVisGpu, OpenGroundKeepsTheFlatAmbient)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, hemisphereVisibility());
    g.pack({ &a }, greySky());
    expectNear(g.at(vsg::vec3(3000, 3000, 50), up, flat), flat, 1e-4f);
    expectNear(g.at(vsg::vec3(3000, 3000, 50), vsg::vec3(1, 0, 0), flat), flat, 1e-4f);
}

TEST(SkyVisGpu, ABlockedPlaceGetsNoSkyLight)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, blockedVisibility());
    g.pack({ &a }, greySky());
    expectNear(g.at(vsg::vec3(3000, 3000, 50), up, flat), vsg::vec3(0, 0, 0), 1e-5f);
    // Strength scales the effect: half of the way to the flat ambient at strength 0.5, none at 0.
    g.pack({ &a }, greySky(), 0.5f);
    expectNear(g.at(vsg::vec3(3000, 3000, 50), up, flat), flat * 0.5f, 1e-5f);
    g.pack({ &a }, greySky(), 0.f);
    expectNear(g.at(vsg::vec3(3000, 3000, 50), up, flat), flat, 1e-5f);
}

TEST(SkyVisGpu, DisabledReturnsTheFlatAmbient)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, blockedVisibility());
    g.pack({ &a }, greySky(), 1.f, false);
    expectNear(g.at(vsg::vec3(3000, 3000, 50), up, flat), flat, 1e-6f);
}

TEST(SkyVisGpu, MatchesTheDirectMaths)
{
    // A half-blocked place under a coloured sky: the reader gives flat times skyOcclusion.
    VisSh half = hemisphereVisibility();
    for (float& v : half)
        v *= 0.4f;
    const SkyVisField a = uniformField(0, 0, half);
    Gpu g;
    const Sh9 sky = skyFromColours(vsg::vec3(0.3f, 0.5f, 0.9f), vsg::vec3(0.8f, 0.6f, 0.4f));
    g.pack({ &a }, sky);
    VisSh q = half;
    for (float& v : q)
        v = skyVisQuantize(v);
    const vsg::vec3 occ = skyOcclusion(sky, q);
    const vsg::vec3 got = g.at(vsg::vec3(4096, 4096, 40), up, flat);
    expectNear(got, vsg::vec3(flat.x * occ.x, flat.y * occ.y, flat.z * occ.z), 2e-4f);
}

TEST(SkyVisGpu, LevelsBlendAndFollowTheGround)
{
    // No normal, so the sample point is not moved off the surface.
    const vsg::vec3 up(0.f, 0.f, 0.f);
    // Level 0 open, level 1 blocked, level 2 open, in a field whose ground is 1000 up.
    SkyVisField f = uniformField(0, 0, hemisphereVisibility(), 1000.f);
    for (std::size_t i = 0; i < f.probes.size(); ++i)
        if (i % 3 == 1)
            f.probes[i] = VisSh{};
    Gpu g;
    g.pack({ &f }, greySky());
    // Level k is at base + (k + 0.5) * 100: 1050, 1150, 1250.
    expectNear(g.at(vsg::vec3(4096, 4096, 1050), up, flat), flat, 1e-4f);
    expectNear(g.at(vsg::vec3(4096, 4096, 1150), up, flat), vsg::vec3(0, 0, 0), 1e-4f);
    expectNear(g.at(vsg::vec3(4096, 4096, 1250), up, flat), flat, 1e-4f);
    // Halfway between level 0 and level 1 is halfway between the two (the ratio is linear in the visibility).
    expectNear(g.at(vsg::vec3(4096, 4096, 1100), up, flat), flat * 0.5f, 1e-3f);
    // Below the first level and above the last, the end level holds.
    expectNear(g.at(vsg::vec3(4096, 4096, -500), up, flat), flat, 1e-4f);
    expectNear(g.at(vsg::vec3(4096, 4096, 9000), up, flat), flat, 1e-4f);
}

TEST(SkyVisGpu, ColumnsBlendHorizontally)
{
    // Column x = 4 (4096) blocked, its neighbours open: halfway to either side is halfway.
    SkyVisField f = uniformField(0, 0, hemisphereVisibility());
    const std::uint32_t side = skyVisColumnsPerSide(f.spacing);
    for (std::uint32_t l = 0; l < f.levels; ++l)
        f.probes[(4 + side * 4) * f.levels + l] = VisSh{};
    Gpu g;
    g.pack({ &f }, greySky());
    expectNear(g.at(vsg::vec3(4096, 4096, 50), up, flat), vsg::vec3(0, 0, 0), 1e-4f);
    expectNear(g.at(vsg::vec3(4096 + 512, 4096, 50), up, flat), flat * 0.5f, 5e-3f);
    expectNear(g.at(vsg::vec3(4096 + 1024, 4096, 50), up, flat), flat, 1e-4f);
}

TEST(SkyVisGpu, CellsAgreeAcrossTheirBorder)
{
    // The same visibility in two neighbours: no step at x = 8192 (the border columns are in both cells).
    VisSh half = hemisphereVisibility();
    for (float& v : half)
        v *= 0.5f;
    const SkyVisField a = uniformField(0, 0, half);
    const SkyVisField b = uniformField(1, 0, half);
    Gpu g;
    g.pack({ &a, &b }, greySky());
    const vsg::vec3 left = g.at(vsg::vec3(8192 - 1.f, 3000, 40), up, flat);
    const vsg::vec3 right = g.at(vsg::vec3(8192 + 1.f, 3000, 40), up, flat);
    expectNear(left, right, 1e-3f);
    EXPECT_LT(left.x, flat.x * 0.9f);
}

TEST(SkyVisGpu, ANeighbourWithoutDataFadesOutAcrossOneSpacing)
{
    // Only the cell to the west has data. Past its east border the effect fades to the flat ambient over one column.
    const SkyVisField a = uniformField(0, 0, blockedVisibility());
    Gpu g;
    g.pack({ &a }, greySky());
    // Inside the cell the place is dark; on the last column line (x = 8192) the border column is still valid.
    expectNear(g.at(vsg::vec3(8192.f, 3000, 40), up, flat), vsg::vec3(0, 0, 0), 1e-4f);
    // Halfway to the next column the share of valid columns is a half.
    expectNear(g.at(vsg::vec3(8192.f + 512.f, 3000, 40), up, flat), flat * 0.5f, 5e-3f);
    // A full column away there is nothing.
    expectNear(g.at(vsg::vec3(8192.f + 1024.f, 3000, 40), up, flat), flat, 1e-4f);
    expectNear(g.at(vsg::vec3(8192.f + 5000.f, 3000, 40), up, flat), flat, 1e-6f);
    // Nor west of the cell's own western border.
    expectNear(g.at(vsg::vec3(-1024.f, 3000, 40), up, flat), flat, 1e-4f);
}

TEST(SkyVisGpu, NegativeCellsWork)
{
    const SkyVisField a = uniformField(-2, -3, blockedVisibility());
    Gpu g;
    g.pack({ &a }, greySky());
    const float x = -2.f * 8192.f + 3000.f;
    const float y = -3.f * 8192.f + 5000.f;
    expectNear(g.at(vsg::vec3(x, y, 40), up, flat), vsg::vec3(0, 0, 0), 1e-4f);
    // The cell next to it, which has no data, is flat.
    expectNear(g.at(vsg::vec3(x + 8192.f * 3.f, y, 40), up, flat), flat, 1e-6f);
}

TEST(SkyVisGpu, TheNormalMovesTheSamplePoint)
{
    // The column at x = 4096 is blocked, its east neighbour open. A surface just east of 4096 facing east samples
    // further east than one facing west.
    SkyVisField f = uniformField(0, 0, hemisphereVisibility());
    const std::uint32_t side = skyVisColumnsPerSide(f.spacing);
    for (std::uint32_t l = 0; l < f.levels; ++l)
        f.probes[(4 + side * 4) * f.levels + l] = VisSh{};
    Gpu g;
    g.pack({ &f }, greySky());
    const vsg::vec3 pos(4096 + 100.f, 4096, 40);
    const vsg::vec3 east = g.at(pos, vsg::vec3(1, 0, 0), flat);
    const vsg::vec3 west = g.at(pos, vsg::vec3(-1, 0, 0), flat);
    EXPECT_GT(east.x, west.x);
}

TEST(SkyVisGpu, ASkyWithNoLightAndAZeroNormalDoNotBreakIt)
{
    const SkyVisField a = uniformField(0, 0, blockedVisibility());
    Gpu g;
    g.pack({ &a }, projectSky(vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0)));
    const vsg::vec3 r = g.at(vsg::vec3(3000, 3000, 40), vsg::vec3(0, 0, 0), flat);
    EXPECT_TRUE(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z));
    // A blocked place is dark whatever the sky (the shade is geometry), and nothing is NaN.
    expectNear(r, vsg::vec3(0, 0, 0), 1e-5f);
}

TEST(SkyVisGpu, TheHeaderHoldsTheSkyAndTheReference)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, hemisphereVisibility());
    const Sh9 sky = skyFromColours(vsg::vec3(0.3f, 0.5f, 0.9f), vsg::vec3(0.8f, 0.6f, 0.4f));
    g.pack({ &a }, sky, 0.75f);
    EXPECT_EQ(g.header[0].x, 1.f);
    EXPECT_EQ(g.header[0].y, 0.75f);
    EXPECT_EQ(g.header[0].z, 0.f); // the window's west cell
    EXPECT_EQ(g.header[0].w, 0.f);
    EXPECT_EQ(g.header[1].x, 1024.f);
    EXPECT_EQ(g.header[1].y, 100.f);
    EXPECT_EQ(g.header[1].z, 3.f);
    EXPECT_EQ(g.header[1].w, 9.f);
    for (int i = 0; i < 9; ++i)
    {
        EXPECT_FLOAT_EQ(g.header[2 + i].x, sky.c[i].x);
        EXPECT_FLOAT_EQ(g.header[2 + i].y, sky.c[i].y);
        EXPECT_FLOAT_EQ(g.header[2 + i].z, sky.c[i].z);
    }
    const vsg::vec3 ref = skyDot(sky, hemisphereVisibility());
    EXPECT_FLOAT_EQ(g.header[11].x, ref.x);
    EXPECT_FLOAT_EQ(g.header[11].z, ref.z);
}

TEST(SkyVisGpu, UpdatingTheSkyLeavesTheCellsAlone)
{
    Gpu g;
    const SkyVisField a = uniformField(0, 0, blockedVisibility());
    g.pack({ &a }, greySky());
    const auto slotBefore = g.header[12];
    const vsg::vec3 other(0.9f, 0.1f, 0.1f);
    writeSkyHeader(g.header.data(), true, 1.f, projectSky(other, other, other));
    EXPECT_EQ(g.header[12].x, slotBefore.x);
    EXPECT_EQ(g.header[12].y, slotBefore.y);
    expectNear(g.at(vsg::vec3(3000, 3000, 40), up, flat), vsg::vec3(0, 0, 0), 1e-5f);
}
