// Tests for the sky-visibility baker, Render::Gi (task 0036, stage b). Written from the task spec before the
// implementation, against a made-up world: a flat ground at height 0, an optional bridge (a slab) and an optional
// solid block. Nothing here comes from a game.

#include <components/render/skyvisbake.hpp>
#include <components/render/skyvismath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace
{
    using namespace Render::Gi;

    struct Box
    {
        vsg::vec3 low, high;
    };

    // Ground plane at z = groundZ, plus boxes. A segment that starts inside a box (or below the ground) hits at once.
    class TestWorld final : public SkyRayWorld
    {
    public:
        float groundZ = 0.f;
        bool hasGround = true;
        std::vector<Box> boxes;
        mutable std::size_t casts = 0;

        bool groundHeight(float, float, float& z) const override
        {
            if (!hasGround)
                return false;
            z = groundZ;
            return true;
        }

        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            ++casts;
            float best = 2.f;
            vsg::vec3 normal(0, 0, 1);
            if (hasGround)
            {
                if (from.z <= groundZ)
                {
                    best = 0.f;
                }
                else if (to.z < groundZ)
                {
                    best = (from.z - groundZ) / (from.z - to.z);
                }
            }
            for (const Box& b : boxes)
            {
                float t0 = 0.f, t1 = 1.f;
                bool ok = true;
                const float f[3] = { from.x, from.y, from.z };
                const float d[3] = { to.x - from.x, to.y - from.y, to.z - from.z };
                const float lo[3] = { b.low.x, b.low.y, b.low.z };
                const float hi[3] = { b.high.x, b.high.y, b.high.z };
                for (int a = 0; a < 3 && ok; ++a)
                {
                    if (std::abs(d[a]) < 1e-9f)
                    {
                        if (f[a] < lo[a] || f[a] > hi[a])
                            ok = false;
                    }
                    else
                    {
                        float ta = (lo[a] - f[a]) / d[a];
                        float tb = (hi[a] - f[a]) / d[a];
                        if (ta > tb)
                            std::swap(ta, tb);
                        t0 = std::max(t0, ta);
                        t1 = std::min(t1, tb);
                        if (t0 > t1)
                            ok = false;
                    }
                }
                if (ok && t0 < best)
                    best = t0;
            }
            if (best > 1.f)
                return false;
            hitPos = from + (to - from) * best;
            hitNormal = normal;
            return true;
        }
    };

    SkyBakeSettings coarse()
    {
        SkyBakeSettings s;
        s.rays = 96;
        s.seed = 7;
        s.spacing = 1024.f; // 9 columns a side
        s.levelSpacing = 128.f;
        s.levels = 4;
        return s;
    }

    SkyVisField bakeAll(const SkyBakeSettings& s, const TestWorld& w, int cx, int cy, std::size_t slice = 1000000)
    {
        SkyVisBaker baker(s, w, cx, cy);
        while (!baker.done())
            baker.step(slice);
        return baker.finish();
    }

    std::size_t probeIndex(const SkyVisField& f, std::uint32_t ix, std::uint32_t iy, std::uint32_t level)
    {
        const std::uint32_t side = skyVisColumnsPerSide(f.spacing);
        return (static_cast<std::size_t>(ix) + side * iy) * f.levels + level;
    }

    vsg::vec3 occlusionOf(const SkyVisField& f, std::uint32_t ix, std::uint32_t iy, std::uint32_t level)
    {
        const vsg::vec3 u(0.5f, 0.5f, 0.5f);
        return skyOcclusion(projectSky(u, u, u), f.probes[probeIndex(f, ix, iy, level)]);
    }
}

TEST(SkyVisBake, TotalsFollowTheSettings)
{
    TestWorld w;
    SkyVisBaker baker(coarse(), w, 2, -1);
    EXPECT_EQ(baker.total(), 9u * 9u * 4u);
    EXPECT_FALSE(baker.done());
    EXPECT_EQ(baker.next(), 0u);
    baker.step(10);
    EXPECT_EQ(baker.next(), 10u);
    const SkyBakeSettings fine = [] {
        SkyBakeSettings s;
        return s;
    }();
    SkyVisBaker def(fine, w, 0, 0);
    EXPECT_EQ(def.total(), 33u * 33u * 8u);
}

TEST(SkyVisBake, FieldCarriesCellAndSettings)
{
    TestWorld w;
    const SkyVisField f = bakeAll(coarse(), w, 2, -1);
    EXPECT_EQ(f.cellX, 2);
    EXPECT_EQ(f.cellY, -1);
    EXPECT_EQ(f.rays, 96u);
    EXPECT_EQ(f.seed, 7u);
    EXPECT_EQ(f.levels, 4u);
    EXPECT_EQ(validate(f), "");
}

TEST(SkyVisBake, GroundHeightsComeFromTheWorld)
{
    TestWorld w;
    w.groundZ = 250.f;
    const SkyVisField f = bakeAll(coarse(), w, 0, 0);
    for (const float b : f.base)
        EXPECT_FLOAT_EQ(b, 250.f);
    // No ground (open water, an empty cell) takes the named fallback.
    TestWorld none;
    none.hasGround = false;
    const SkyVisField g = bakeAll(coarse(), none, 0, 0);
    for (const float b : g.base)
        EXPECT_FLOAT_EQ(b, coarse().fallbackHeight);
}

TEST(SkyVisBake, OpenGroundKeepsTheFlatAmbient)
{
    TestWorld w;
    const SkyVisField f = bakeAll(coarse(), w, 0, 0);
    for (std::uint32_t l = 0; l < f.levels; ++l)
    {
        const vsg::vec3 o = occlusionOf(f, 4, 4, l);
        EXPECT_NEAR(o.x, 1.f, 0.06f) << "level " << l;
    }
}

TEST(SkyVisBake, UnderABridgeIsDarkerThanOpenGround)
{
    TestWorld w;
    // A slab over x in [1500, 6500], y everywhere, 200 to 260 units above the ground.
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    const SkyVisField f = bakeAll(coarse(), w, 0, 0);
    // Column x = 4 * 1024 = 4096 is under the slab, column 0 is out in the open.
    const vsg::vec3 under = occlusionOf(f, 4, 4, 0);
    const vsg::vec3 open = occlusionOf(f, 0, 4, 0);
    EXPECT_LT(under.x, 0.35f);
    EXPECT_GT(open.x, 0.9f);
    // Above the slab (level 3 is 448 up) the sky is open again.
    EXPECT_GT(occlusionOf(f, 4, 4, 3).x, 0.9f);
}

TEST(SkyVisBake, TwoBakesAreIdentical)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    const SkyVisField a = bakeAll(coarse(), w, 3, 4);
    const SkyVisField b = bakeAll(coarse(), w, 3, 4);
    EXPECT_EQ(writeCanonical(a), writeCanonical(b));
}

TEST(SkyVisBake, SliceSizeDoesNotChangeTheResult)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    const SkyVisField a = bakeAll(coarse(), w, 0, 0, 1);
    const SkyVisField b = bakeAll(coarse(), w, 0, 0, 37);
    EXPECT_EQ(writeCanonical(a), writeCanonical(b));
}

TEST(SkyVisBake, SeedChangesTheRayDirections)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    SkyBakeSettings other = coarse();
    other.seed = 8;
    EXPECT_NE(writeCanonical(bakeAll(coarse(), w, 0, 0)), writeCanonical(bakeAll(other, w, 0, 0)));
}

TEST(SkyVisBake, AStoppedAndResumedBakeEqualsAnUninterruptedOne)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    w.boxes.push_back({ vsg::vec3(0.f, 0.f, -10.f), vsg::vec3(300.f, 300.f, 500.f) }); // invalid probes inside
    const SkyVisField whole = bakeAll(coarse(), w, 0, 0);

    SkyVisBaker first(coarse(), w, 0, 0);
    first.step(150);
    const std::size_t stoppedAt = first.next();
    const SkyVisField partial = first.field();
    const std::vector<bool> invalid = first.invalid();

    SkyVisBaker second(coarse(), w, 0, 0);
    ASSERT_TRUE(second.resume(partial, stoppedAt, invalid));
    EXPECT_EQ(second.next(), stoppedAt);
    while (!second.done())
        second.step(11);
    EXPECT_EQ(writeCanonical(second.finish()), writeCanonical(whole));
}

TEST(SkyVisBake, ResumeRefusesAFieldOfAnotherShape)
{
    TestWorld w;
    SkyVisBaker a(coarse(), w, 0, 0);
    a.step(20);
    SkyBakeSettings other = coarse();
    other.levels = 5;
    SkyVisBaker b(other, w, 0, 0);
    EXPECT_FALSE(b.resume(a.field(), a.next(), a.invalid()));
    EXPECT_EQ(b.next(), 0u);
    SkyVisBaker c(coarse(), w, 1, 0);
    EXPECT_FALSE(c.resume(a.field(), a.next(), a.invalid()));
    SkyVisBaker d(coarse(), w, 0, 0);
    EXPECT_FALSE(d.resume(a.field(), a.field().probes.size() + 1, a.invalid()));
}

TEST(SkyVisBake, NeighbouringCellsAgreeOnTheirSharedBorder)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(7000.f, -100000.f, 150.f), vsg::vec3(9500.f, 100000.f, 200.f) });
    const SkyVisField a = bakeAll(coarse(), w, 0, 0);
    const SkyVisField b = bakeAll(coarse(), w, 1, 0);
    const std::uint32_t side = skyVisColumnsPerSide(a.spacing);
    for (std::uint32_t iy = 0; iy < side; ++iy)
    {
        EXPECT_EQ(a.base[(side - 1) + side * iy], b.base[0 + side * iy]);
        for (std::uint32_t l = 0; l < a.levels; ++l)
            EXPECT_EQ(a.probes[probeIndex(a, side - 1, iy, l)], b.probes[probeIndex(b, 0, iy, l)]);
    }
}

TEST(SkyVisBake, ProbesInsideSolidGeometryAreFilledFromNeighbours)
{
    TestWorld w;
    // A block that holds the probe of column (4, 4) level 1 (x = y = 4096, z = 192 + 64).
    w.boxes.push_back({ vsg::vec3(3900.f, 3900.f, 100.f), vsg::vec3(4300.f, 4300.f, 330.f) });
    SkyVisBaker baker(coarse(), w, 0, 0);
    while (!baker.done())
        baker.step(100);
    const std::size_t inside = probeIndex(baker.field(), 4, 4, 1);
    ASSERT_LT(inside, baker.invalid().size());
    EXPECT_TRUE(baker.invalid()[inside]);
    EXPECT_FALSE(baker.invalid()[probeIndex(baker.field(), 0, 0, 0)]);
    const SkyVisField done = baker.finish();
    // It now holds what its valid neighbours see, not zero.
    EXPECT_GT(done.probes[inside][0], 0.5f);
    EXPECT_EQ(validate(done), "");
}

TEST(SkyVisBake, AWorldWithNothingInvalidatesNothing)
{
    TestWorld w;
    SkyVisBaker baker(coarse(), w, 0, 0);
    while (!baker.done())
        baker.step(500);
    for (const bool b : baker.invalid())
        EXPECT_FALSE(b);
}

TEST(SkyVisBake, TheBakeIsQuantisedForTheFile)
{
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(1500.f, -100000.f, 200.f), vsg::vec3(6500.f, 100000.f, 260.f) });
    const SkyVisField f = bakeAll(coarse(), w, 0, 0);
    ASSERT_EQ(validate(f), "");
    for (const VisSh& sh : f.probes)
        for (const float v : sh)
            EXPECT_EQ(skyVisQuantize(v), v);
}

TEST(SkyVisBake, TheRayLengthLimitsWhatBlocks)
{
    // A roof far above the sky distance does not count as sky blocking.
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(-1e6f, -1e6f, 20000.f), vsg::vec3(1e6f, 1e6f, 20100.f) });
    const SkyVisField f = bakeAll(coarse(), w, 0, 0);
    EXPECT_GT(occlusionOf(f, 4, 4, 0).x, 0.9f);
}

TEST(SkyVisBake, AProbeInAClosedRoomIsInvalidAndFilledFromOutside)
{
    TestWorld w;
    // A closed room around column (4, 4): inside x and y from 3896 to 4296 and z from 0 to 350, walls 100 thick.
    const vsg::vec3 lo(3796.f, 3796.f, -100.f), hi(4396.f, 4396.f, 450.f);
    w.boxes.push_back({ vsg::vec3(lo.x, lo.y, 350.f), vsg::vec3(hi.x, hi.y, hi.z) }); // roof
    w.boxes.push_back({ vsg::vec3(lo.x, lo.y, 0.f), vsg::vec3(3896.f, hi.y, 350.f) }); // west
    w.boxes.push_back({ vsg::vec3(4296.f, lo.y, 0.f), vsg::vec3(hi.x, hi.y, 350.f) }); // east
    w.boxes.push_back({ vsg::vec3(lo.x, lo.y, 0.f), vsg::vec3(hi.x, 3896.f, 350.f) }); // south
    w.boxes.push_back({ vsg::vec3(lo.x, 4296.f, 0.f), vsg::vec3(hi.x, hi.y, 350.f) }); // north
    SkyVisBaker baker(coarse(), w, 0, 0);
    while (!baker.done())
        baker.step(200);
    for (std::uint32_t level = 0; level < 3; ++level)
    {
        const std::size_t room = probeIndex(baker.field(), 4, 4, level);
        EXPECT_TRUE(baker.invalid()[room]) << "level " << level;
    }
    // The open ground next to it is not touched by the rule.
    EXPECT_FALSE(baker.invalid()[probeIndex(baker.field(), 1, 1, 0)]);
    const SkyVisField done = baker.finish();
    // Filled from outside: it reads as open ground, not as a hole of darkness.
    EXPECT_GT(occlusionOf(done, 4, 4, 0).x, 0.5f);
}

TEST(SkyVisBake, ACoveredButNotClosedPlaceKeepsItsShade)
{
    // A big flat roof, 300 units up, 20000 wide: nothing escapes, but the ground far off is beyond the enclosed
    // distance.
    TestWorld w;
    w.boxes.push_back({ vsg::vec3(-30000.f, -30000.f, 300.f), vsg::vec3(30000.f, 30000.f, 360.f) });
    SkyVisBaker baker(coarse(), w, 0, 0);
    while (!baker.done())
        baker.step(500);
    // Below the roof the sky is hidden everywhere, yet the probe is valid (a hall, a canopy, a deck).
    EXPECT_FALSE(baker.invalid()[probeIndex(baker.field(), 4, 4, 0)]);
    const SkyVisField done = baker.finish();
    EXPECT_LT(occlusionOf(done, 4, 4, 0).x, 0.1f);
}
