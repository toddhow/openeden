// Tests for the pure part of the light probe baker, Render::Gi (task 0028, stage c).
//
// Written from the spec and the header alone, without reading the implementation.
//
// The baker asks a `RayWorld` what a ray hits. Here the worlds are made up in the test: an empty world, a closed box
// room, a solid box (an occluder) and a world that is solid everywhere.

#include <components/render/probebake.hpp>
#include <components/render/probes.hpp>
#include <components/render/probesamplermath.hpp>

#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace
{
    using namespace Render::Gi;

    // ---------------------------------------------------------------------------------------------
    // Small helpers
    // ---------------------------------------------------------------------------------------------

    float component(const vsg::vec3& v, int axis)
    {
        return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
    }

    float lengthOf(const vsg::vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    float dotOf(const vsg::vec3& a, const vsg::vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    vsg::vec3 normalized(const vsg::vec3& v)
    {
        const float l = lengthOf(v);
        return vsg::vec3(v.x / l, v.y / l, v.z / l);
    }

    // True when two probes hold exactly the same bits.
    bool bitEqual(const Sh9& a, const Sh9& b)
    {
        return std::memcmp(a.c.data(), b.c.data(), sizeof(vsg::vec3) * a.c.size()) == 0;
    }

    bool bitEqual(const Volume& a, const Volume& b)
    {
        if (a.probes.size() != b.probes.size())
            return false;
        for (std::size_t i = 0; i < a.probes.size(); ++i)
            if (!bitEqual(a.probes[i], b.probes[i]))
                return false;
        return true;
    }

    float meanChannel(const vsg::vec3& v)
    {
        return (v.x + v.y + v.z) / 3.f;
    }

    // The six axis directions and one slanted one.
    std::vector<vsg::vec3> testNormals()
    {
        return { vsg::vec3(1, 0, 0), vsg::vec3(-1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(0, -1, 0), vsg::vec3(0, 0, 1),
            vsg::vec3(0, 0, -1), normalized(vsg::vec3(1, 1, 1)) };
    }

    // ---------------------------------------------------------------------------------------------
    // Fake worlds
    // ---------------------------------------------------------------------------------------------

    class EmptyWorld : public RayWorld
    {
    public:
        bool cast(const vsg::vec3&, const vsg::vec3&, vsg::vec3&, vsg::vec3&) const override { return false; }
    };

    // Solid everywhere: every segment of length >= 0.5 hits at 0.5 units; a shorter one hits at its end.
    class SolidWorld : public RayWorld
    {
    public:
        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            const vsg::vec3 d(to.x - from.x, to.y - from.y, to.z - from.z);
            const float length = lengthOf(d);
            if (length <= 0.f)
                return false;
            const vsg::vec3 dir(d.x / length, d.y / length, d.z / length);
            const float t = std::min(0.5f, length);
            hitPos = vsg::vec3(from.x + dir.x * t, from.y + dir.y * t, from.z + dir.z * t);
            hitNormal = vsg::vec3(-dir.x, -dir.y, -dir.z);
            return true;
        }
    };

    // A closed room: the inside of an axis aligned box. The ray starts inside and leaves through a wall; the normal
    // faces into the room.
    class RoomWorld : public RayWorld
    {
    public:
        RoomWorld(const vsg::vec3& low, const vsg::vec3& high)
            : mLow(low)
            , mHigh(high)
        {
        }

        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            const vsg::vec3 d(to.x - from.x, to.y - from.y, to.z - from.z);
            float best = 2.f;
            int bestAxis = -1;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float delta = component(d, axis);
                if (delta == 0.f)
                    continue;
                const float wall = delta > 0.f ? component(mHigh, axis) : component(mLow, axis);
                const float t = (wall - component(from, axis)) / delta;
                if (t >= 0.f && t < best)
                {
                    best = t;
                    bestAxis = axis;
                }
            }
            if (bestAxis < 0 || best > 1.f)
                return false;
            hitPos = vsg::vec3(from.x + d.x * best, from.y + d.y * best, from.z + d.z * best);
            const float sign = component(d, bestAxis) > 0.f ? -1.f : 1.f;
            hitNormal = vsg::vec3(bestAxis == 0 ? sign : 0.f, bestAxis == 1 ? sign : 0.f, bestAxis == 2 ? sign : 0.f);
            return true;
        }

    private:
        vsg::vec3 mLow;
        vsg::vec3 mHigh;
    };

    // A solid axis aligned box, hit from outside. A segment that starts inside it hits nothing.
    class SolidBoxWorld : public RayWorld
    {
    public:
        SolidBoxWorld(const vsg::vec3& low, const vsg::vec3& high)
            : mLow(low)
            , mHigh(high)
        {
        }

        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            const vsg::vec3 d(to.x - from.x, to.y - from.y, to.z - from.z);
            float tEnter = -1e30f;
            float tLeave = 1e30f;
            int enterAxis = -1;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float delta = component(d, axis);
                const float start = component(from, axis);
                const float lo = component(mLow, axis);
                const float hi = component(mHigh, axis);
                if (delta == 0.f)
                {
                    if (start < lo || start > hi)
                        return false;
                    continue;
                }
                float t0 = (lo - start) / delta;
                float t1 = (hi - start) / delta;
                if (t0 > t1)
                    std::swap(t0, t1);
                if (t0 > tEnter)
                {
                    tEnter = t0;
                    enterAxis = axis;
                }
                tLeave = std::min(tLeave, t1);
            }
            if (enterAxis < 0 || tEnter > tLeave || tEnter < 0.f || tEnter > 1.f)
                return false;
            hitPos = vsg::vec3(from.x + d.x * tEnter, from.y + d.y * tEnter, from.z + d.z * tEnter);
            const float sign = component(d, enterAxis) > 0.f ? -1.f : 1.f;
            hitNormal = vsg::vec3(enterAxis == 0 ? sign : 0.f, enterAxis == 1 ? sign : 0.f, enterAxis == 2 ? sign : 0.f);
            return true;
        }

    private:
        vsg::vec3 mLow;
        vsg::vec3 mHigh;
    };

    // Several worlds in one: the nearest hit to `from` wins.
    class CombinedWorld : public RayWorld
    {
    public:
        CombinedWorld(const RayWorld& a, const RayWorld& b)
            : mA(a)
            , mB(b)
        {
        }

        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            vsg::vec3 posA, normalA, posB, normalB;
            const bool hitA = mA.cast(from, to, posA, normalA);
            const bool hitB = mB.cast(from, to, posB, normalB);
            if (!hitA && !hitB)
                return false;
            const auto distance = [&](const vsg::vec3& p) {
                return lengthOf(vsg::vec3(p.x - from.x, p.y - from.y, p.z - from.z));
            };
            if (hitA && (!hitB || distance(posA) <= distance(posB)))
            {
                hitPos = posA;
                hitNormal = normalA;
            }
            else
            {
                hitPos = posB;
                hitNormal = normalB;
            }
            return true;
        }

    private:
        const RayWorld& mA;
        const RayWorld& mB;
    };

    RoomWorld cubeRoom(float half)
    {
        return RoomWorld(vsg::vec3(-half, -half, -half), vsg::vec3(half, half, half));
    }

    BakeSettings plainSettings(std::uint32_t rays, std::uint32_t bounces)
    {
        BakeSettings s;
        s.rays = rays;
        s.bounces = bounces;
        s.ambient = vsg::vec3(0.4f, 0.3f, 0.2f);
        return s;
    }

    // ---------------------------------------------------------------------------------------------
    // lightRange and attenuate (equations.glsl)
    // ---------------------------------------------------------------------------------------------

    TEST(RenderGiProbeBakeFalloff, NonClassicRangeIsTwiceTheRadius)
    {
        LightFalloff f;
        f.classic = false;
        EXPECT_FLOAT_EQ(lightRange(f, 100.f), 200.f);
        EXPECT_FLOAT_EQ(lightRange(f, 37.5f), 75.f);
        f.linearValue = 9.f;
        f.linearRadiusMult = 3.f;
        EXPECT_FLOAT_EQ(lightRange(f, 100.f), 200.f);
    }

    TEST(RenderGiProbeBakeFalloff, ClassicRangeFollowsTheLinearTerm)
    {
        // range = 1 / (0.03 * (linearValue / (radius * mult)))
        LightFalloff f;
        f.classic = true;
        f.linearValue = 3.f;
        f.linearRadiusMult = 1.f;
        EXPECT_NEAR(lightRange(f, 100.f), 100.f / (0.03f * 3.f), 0.05f);

        f.linearValue = 2.f;
        f.linearRadiusMult = 2.f;
        EXPECT_NEAR(lightRange(f, 50.f), 100.f / (0.03f * 2.f), 0.05f);
    }

    TEST(RenderGiProbeBakeFalloff, NonClassicLinearMethodOneInsideTheRadius)
    {
        LightFalloff f; // linear, method 1, value 3, no fade inside the radius
        f.classic = false;
        // value = 3 / 100 * 50 = 1.5
        EXPECT_NEAR(attenuate(f, 100.f, 50.f), 1.f / 1.5f, 1e-5f);
        // value = 3 / 100 * 100 = 3, exactly at the radius the fade has not started
        EXPECT_NEAR(attenuate(f, 100.f, 100.f), 1.f / 3.f, 1e-5f);
    }

    TEST(RenderGiProbeBakeFalloff, NonClassicIsOneAtDistanceZeroAndZeroAtTwiceTheRadius)
    {
        LightFalloff f;
        f.classic = false;
        EXPECT_FLOAT_EQ(attenuate(f, 100.f, 0.f), 1.f);
        EXPECT_NEAR(attenuate(f, 100.f, 200.f), 0.f, 1e-6f);
        EXPECT_NEAR(attenuate(f, 100.f, 350.f), 0.f, 1e-6f);
    }

    TEST(RenderGiProbeBakeFalloff, NonClassicFadesBetweenRadiusAndTwiceTheRadius)
    {
        LightFalloff f;
        f.classic = false;
        const float atRadius = attenuate(f, 100.f, 100.f);
        const float between = attenuate(f, 100.f, 150.f);
        const float atEnd = attenuate(f, 100.f, 200.f);
        EXPECT_GT(atRadius, between);
        EXPECT_GT(between, atEnd);
        EXPECT_GE(between, 0.f);
    }

    TEST(RenderGiProbeBakeFalloff, MethodZeroUsesTheValueAsIs)
    {
        LightFalloff f;
        f.classic = true;
        f.linearMethod = 0;
        f.linearValue = 3.f;
        // value = 3 * 10
        EXPECT_NEAR(attenuate(f, 100.f, 10.f), 1.f / 30.f, 1e-6f);
    }

    TEST(RenderGiProbeBakeFalloff, MethodTwoDividesByTheRadiusSquared)
    {
        LightFalloff f;
        f.classic = true;
        f.linearMethod = 2;
        f.linearValue = 3.f;
        // value = 3 / 100^2 * 5000 = 1.5
        EXPECT_NEAR(attenuate(f, 100.f, 5000.f), 1.f / 1.5f, 1e-5f);
    }

    TEST(RenderGiProbeBakeFalloff, ClassicDoesNotFadeAtTwiceTheRadius)
    {
        LightFalloff f;
        f.classic = true;
        // value = 3 / 100 * 200 = 6
        EXPECT_NEAR(attenuate(f, 100.f, 200.f), 1.f / 6.f, 1e-5f);
        EXPECT_GT(attenuate(f, 100.f, 400.f), 0.f);
    }

    TEST(RenderGiProbeBakeFalloff, ConstantOnly)
    {
        LightFalloff f;
        f.classic = true;
        f.useConstant = true;
        f.constantValue = 2.f;
        f.useLinear = false;
        f.useQuadratic = false;
        EXPECT_NEAR(attenuate(f, 100.f, 0.f), 0.5f, 1e-6f);
        EXPECT_NEAR(attenuate(f, 100.f, 77.f), 0.5f, 1e-6f);
    }

    TEST(RenderGiProbeBakeFalloff, AllThreeTermsTogether)
    {
        LightFalloff f;
        f.classic = true;
        f.useConstant = true;
        f.constantValue = 1.f;
        f.useLinear = true;
        f.linearMethod = 1;
        f.linearValue = 2.f;
        f.linearRadiusMult = 2.f; // r = 10 * 2 = 20, linear = 2 / 20 * 10 = 1
        f.useQuadratic = true;
        f.quadraticMethod = 2;
        f.quadraticValue = 4.f;
        f.quadraticRadiusMult = 1.f; // r = 10, quadratic = 4 / 100 * 10^2 = 4
        // value = 1 + 1 + 4 = 6
        EXPECT_NEAR(attenuate(f, 10.f, 10.f), 1.f / 6.f, 1e-5f);
    }

    TEST(RenderGiProbeBakeFalloff, QuadraticMethodOneAndZero)
    {
        LightFalloff f;
        f.classic = true;
        f.useLinear = false;
        f.useQuadratic = true;
        f.quadraticMethod = 1;
        f.quadraticValue = 8.f;
        f.quadraticRadiusMult = 2.f; // r = 50 * 2 = 100, 8 / 100 * 30^2 = 72
        EXPECT_NEAR(attenuate(f, 50.f, 30.f), 1.f / 72.f, 1e-5f);

        f.quadraticMethod = 0; // 8 * 3^2 = 72
        EXPECT_NEAR(attenuate(f, 50.f, 3.f), 1.f / 72.f, 1e-5f);
    }

    TEST(RenderGiProbeBakeFalloff, ClassicAtDistanceZeroIsClampedToOne)
    {
        LightFalloff f;
        f.classic = true;
        EXPECT_FLOAT_EQ(attenuate(f, 100.f, 0.f), 1.f);
    }

    // ---------------------------------------------------------------------------------------------
    // Pcg32
    // ---------------------------------------------------------------------------------------------

    TEST(RenderGiProbeBakePcg32, MatchesTheReferenceVector)
    {
        Pcg32 rng(42, 54);
        const std::array<std::uint32_t, 6> expected
            = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
        for (std::uint32_t value : expected)
            EXPECT_EQ(rng.next(), value);
    }

    TEST(RenderGiProbeBakePcg32, SameSeedAndStreamGiveTheSameSequence)
    {
        Pcg32 a(7, 3);
        Pcg32 b(7, 3);
        for (int i = 0; i < 100; ++i)
            EXPECT_EQ(a.next(), b.next());
    }

    TEST(RenderGiProbeBakePcg32, DifferentStreamsDiffer)
    {
        Pcg32 a(7, 3);
        Pcg32 b(7, 4);
        bool differs = false;
        for (int i = 0; i < 10; ++i)
            differs = differs || a.next() != b.next();
        EXPECT_TRUE(differs);
    }

    TEST(RenderGiProbeBakePcg32, NextFloatIsInZeroToOneExclusive)
    {
        Pcg32 rng(1, 1);
        float sum = 0.f;
        for (int i = 0; i < 20000; ++i)
        {
            const float f = rng.nextFloat();
            ASSERT_GE(f, 0.f);
            ASSERT_LT(f, 1.f);
            sum += f;
        }
        EXPECT_NEAR(sum / 20000.f, 0.5f, 0.02f);
    }

    // ---------------------------------------------------------------------------------------------
    // Directions
    // ---------------------------------------------------------------------------------------------

    TEST(RenderGiProbeBakeDirections, SpiralDirectionsAreUnitLength)
    {
        for (std::uint32_t i = 0; i < 256; ++i)
            EXPECT_NEAR(lengthOf(spiralDirection(i, 256, 0.3f)), 1.f, 1e-4f);
    }

    TEST(RenderGiProbeBakeDirections, SpiralCoversTheSphereEvenly)
    {
        for (float offset : { 0.f, 0.37f, 0.9f })
        {
            vsg::vec3 sum(0.f, 0.f, 0.f);
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                const vsg::vec3 d = spiralDirection(i, 256, offset);
                sum += d;
            }
            EXPECT_LT(lengthOf(sum) / 256.f, 0.05f) << "offset " << offset;
        }
    }

    TEST(RenderGiProbeBakeDirections, SpiralReachesBothHemispheres)
    {
        int up = 0;
        for (std::uint32_t i = 0; i < 256; ++i)
            if (spiralDirection(i, 256, 0.f).z > 0.f)
                ++up;
        EXPECT_NEAR(static_cast<float>(up), 128.f, 16.f);
    }

    TEST(RenderGiProbeBakeDirections, CosineDirectionsAreUnitLengthAndInTheHemisphere)
    {
        Pcg32 rng(5, 9);
        for (const vsg::vec3& n : testNormals())
        {
            for (int i = 0; i < 500; ++i)
            {
                const vsg::vec3 d = cosineDirection(n, rng.nextFloat(), rng.nextFloat());
                ASSERT_NEAR(lengthOf(d), 1.f, 1e-4f);
                ASSERT_GE(dotOf(d, n), -1e-4f);
            }
        }
    }

    TEST(RenderGiProbeBakeDirections, CosineDirectionsHaveMeanCosineTwoThirds)
    {
        Pcg32 rng(11, 2);
        const vsg::vec3 n = normalized(vsg::vec3(0.3f, -0.5f, 0.8f));
        double sum = 0.0;
        constexpr int count = 40000;
        for (int i = 0; i < count; ++i)
            sum += dotOf(cosineDirection(n, rng.nextFloat(), rng.nextFloat()), n);
        EXPECT_NEAR(sum / count, 2.0 / 3.0, 0.02);
    }

    // ---------------------------------------------------------------------------------------------
    // bakeProbe
    // ---------------------------------------------------------------------------------------------

    TEST(RenderGiProbeBakeProbe, EmptyWorldGivesTheUniformAmbient)
    {
        const EmptyWorld world;
        const BakeSettings s = plainSettings(256, 2);
        const ProbeResult r = bakeProbe(s, world, {}, vsg::vec3(0, 0, 0), 0);
        EXPECT_FALSE(r.invalid);
        for (const vsg::vec3& n : testNormals())
        {
            const vsg::vec3 a = shAmbient(r.sh, n);
            EXPECT_NEAR(a.x, s.ambient.x, 0.02f);
            EXPECT_NEAR(a.y, s.ambient.y, 0.02f);
            EXPECT_NEAR(a.z, s.ambient.z, 0.02f);
        }
    }

    TEST(RenderGiProbeBakeProbe, ClosedRoomWithoutLightsIsDarkerThanTheOpenAmbient)
    {
        const RoomWorld room = cubeRoom(100.f);
        const BakeSettings s = plainSettings(1024, 0);
        const ProbeResult r = bakeProbe(s, room, {}, vsg::vec3(0, 0, 0), 0);
        EXPECT_FALSE(r.invalid);

        std::vector<vsg::vec3> axisValues;
        for (int i = 0; i < 6; ++i)
            axisValues.push_back(shAmbient(r.sh, testNormals()[static_cast<std::size_t>(i)]));

        for (const vsg::vec3& a : axisValues)
        {
            EXPECT_GT(a.x, 0.f);
            EXPECT_LT(a.x, s.ambient.x);
            EXPECT_GT(a.y, 0.f);
            EXPECT_LT(a.y, s.ambient.y);
            EXPECT_GT(a.z, 0.f);
            EXPECT_LT(a.z, s.ambient.z);
        }
        // The room is a cube and the probe is in its centre, so the six sides see about the same light.
        const float reference = meanChannel(axisValues[0]);
        for (const vsg::vec3& a : axisValues)
            EXPECT_NEAR(meanChannel(a), reference, 0.1f * reference);
    }

    TEST(RenderGiProbeBakeProbe, ClosedRoomLightIsOfTheOrderOfTheWallDistance)
    {
        // Every wall is between 100 and 173 units away; open distance 512 gives 0.2 to 0.34 of the ambient.
        const RoomWorld room = cubeRoom(100.f);
        BakeSettings s = plainSettings(1024, 0);
        s.ambient = vsg::vec3(1.f, 1.f, 1.f);
        const ProbeResult r = bakeProbe(s, room, {}, vsg::vec3(0, 0, 0), 0);
        const float v = shAmbient(r.sh, vsg::vec3(0, 0, 1)).x;
        EXPECT_GT(v, 0.15f);
        EXPECT_LT(v, 0.4f);
    }

    BakeLight roomLight()
    {
        return BakeLight{ vsg::vec3(60.f, 0.f, 0.f), vsg::vec3(3.f, 3.f, 3.f), 300.f };
    }

    TEST(RenderGiProbeBakeProbe, ABounceOfLightBrightensTheSideFacingTheLight)
    {
        const RoomWorld room = cubeRoom(100.f);
        BakeSettings s = plainSettings(512, 0);
        s.ambient = vsg::vec3(0.1f, 0.1f, 0.1f);
        const std::vector<BakeLight> lights = { roomLight() };

        const Sh9 dark = bakeProbe(s, room, lights, vsg::vec3(0, 0, 0), 0).sh;
        s.bounces = 1;
        const Sh9 lit = bakeProbe(s, room, lights, vsg::vec3(0, 0, 0), 0).sh;

        const vsg::vec3 towards(1, 0, 0);
        const vsg::vec3 away(-1, 0, 0);
        const float gainTowards = shAmbient(lit, towards).x - shAmbient(dark, towards).x;
        const float gainAway = shAmbient(lit, away).x - shAmbient(dark, away).x;
        EXPECT_GT(gainTowards, 0.01f);
        EXPECT_GT(gainTowards, gainAway);
    }

    TEST(RenderGiProbeBakeProbe, WithoutLightsBouncesAddNothing)
    {
        const RoomWorld room = cubeRoom(100.f);
        BakeSettings s = plainSettings(256, 0);
        const Sh9 zero = bakeProbe(s, room, {}, vsg::vec3(0, 0, 0), 0).sh;
        s.bounces = 1;
        const Sh9 one = bakeProbe(s, room, {}, vsg::vec3(0, 0, 0), 0).sh;
        for (std::size_t i = 0; i < zero.c.size(); ++i)
        {
            EXPECT_NEAR(zero.c[i].x, one.c[i].x, 1e-5f);
            EXPECT_NEAR(zero.c[i].y, one.c[i].y, 1e-5f);
            EXPECT_NEAR(zero.c[i].z, one.c[i].z, 1e-5f);
        }
    }

    TEST(RenderGiProbeBakeProbe, AnOccluderAroundTheLightBlocksItsDirectLight)
    {
        const RoomWorld room = cubeRoom(100.f);
        // A solid box all around the light (30 units from the light to each face, more than the shadow clearance).
        const SolidBoxWorld box(vsg::vec3(30.f, -40.f, -40.f), vsg::vec3(90.f, 40.f, 40.f));
        const CombinedWorld world(room, box);

        BakeSettings s = plainSettings(512, 1);
        s.ambient = vsg::vec3(0.1f, 0.1f, 0.1f);
        const std::vector<BakeLight> lights = { roomLight() };

        const Sh9 withLight = bakeProbe(s, world, lights, vsg::vec3(0, 0, 0), 0).sh;
        const Sh9 withoutLight = bakeProbe(s, world, {}, vsg::vec3(0, 0, 0), 0).sh;
        for (std::size_t i = 0; i < withLight.c.size(); ++i)
        {
            EXPECT_NEAR(withLight.c[i].x, withoutLight.c[i].x, 1e-4f);
            EXPECT_NEAR(withLight.c[i].y, withoutLight.c[i].y, 1e-4f);
            EXPECT_NEAR(withLight.c[i].z, withoutLight.c[i].z, 1e-4f);
        }

        // Control: without the box the same light does brighten the probe.
        const Sh9 open = bakeProbe(s, room, lights, vsg::vec3(0, 0, 0), 0).sh;
        const Sh9 openDark = bakeProbe(s, room, {}, vsg::vec3(0, 0, 0), 0).sh;
        EXPECT_GT(shAmbient(open, vsg::vec3(1, 0, 0)).x, shAmbient(openDark, vsg::vec3(1, 0, 0)).x + 0.01f);
    }

    TEST(RenderGiProbeBakeProbe, IsDeterministic)
    {
        const RoomWorld room = cubeRoom(100.f);
        const BakeSettings s = plainSettings(128, 2);
        const std::vector<BakeLight> lights = { roomLight() };
        const ProbeResult a = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 7);
        const ProbeResult b = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 7);
        EXPECT_TRUE(bitEqual(a.sh, b.sh));
        EXPECT_EQ(a.invalid, b.invalid);
    }

    TEST(RenderGiProbeBakeProbe, ADifferentSeedGivesADifferentResult)
    {
        const RoomWorld room = cubeRoom(100.f);
        BakeSettings s = plainSettings(128, 2);
        const std::vector<BakeLight> lights = { roomLight() };
        const ProbeResult a = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 7);
        s.seed = 99;
        const ProbeResult b = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 7);
        EXPECT_FALSE(bitEqual(a.sh, b.sh));
    }

    TEST(RenderGiProbeBakeProbe, ADifferentProbeIndexGivesADifferentResult)
    {
        const RoomWorld room = cubeRoom(100.f);
        const BakeSettings s = plainSettings(128, 2);
        const std::vector<BakeLight> lights = { roomLight() };
        const ProbeResult a = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 7);
        const ProbeResult b = bakeProbe(s, room, lights, vsg::vec3(10, 5, -3), 8);
        EXPECT_FALSE(bitEqual(a.sh, b.sh));
    }

    TEST(RenderGiProbeBakeProbe, SolidWorldIsInvalid)
    {
        const SolidWorld world;
        const ProbeResult r = bakeProbe(plainSettings(128, 1), world, {}, vsg::vec3(0, 0, 0), 0);
        EXPECT_TRUE(r.invalid);
    }

    TEST(RenderGiProbeBakeProbe, EmptyWorldAndRoomAreNotInvalid)
    {
        const EmptyWorld empty;
        const RoomWorld room = cubeRoom(100.f);
        EXPECT_FALSE(bakeProbe(plainSettings(128, 1), empty, {}, vsg::vec3(0, 0, 0), 0).invalid);
        EXPECT_FALSE(bakeProbe(plainSettings(128, 1), room, {}, vsg::vec3(0, 0, 0), 0).invalid);
    }

    // ---------------------------------------------------------------------------------------------
    // Layout and helpers
    // ---------------------------------------------------------------------------------------------

    void expectCovers(const Volume& v, const vsg::vec3& low, const vsg::vec3& high)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            EXPECT_LE(component(v.origin, axis), component(low, axis) + 1e-3f);
            const float last = component(v.origin, axis) + v.spacing * static_cast<float>(v.size[axis] - 1);
            EXPECT_GE(last, component(high, axis) - 1e-3f);
        }
    }

    std::size_t probeProduct(const Volume& v)
    {
        return static_cast<std::size_t>(v.size[0]) * v.size[1] * v.size[2];
    }

    TEST(RenderGiProbeBakeLayout, SmallBoxKeepsAtLeastTheRequestedSpacing)
    {
        const vsg::vec3 low(0, 0, 0), high(500, 300, 200);
        const Volume v = layoutVolume(low, high, 128.f, maxVolumeProbes);
        EXPECT_GE(v.spacing, 128.f);
        EXPECT_LE(probeProduct(v), maxVolumeProbes);
        EXPECT_EQ(v.probes.size(), probeProduct(v));
        expectCovers(v, low, high);
    }

    TEST(RenderGiProbeBakeLayout, ALargeBoxGrowsTheSpacing)
    {
        const vsg::vec3 low(-5000, -5000, -5000), high(5000, 5000, 5000);
        const Volume v = layoutVolume(low, high, 128.f, 1000);
        EXPECT_GT(v.spacing, 128.f);
        EXPECT_LE(probeProduct(v), 1000u);
        EXPECT_EQ(v.probes.size(), probeProduct(v));
        expectCovers(v, low, high);
    }

    TEST(RenderGiProbeBakeLayout, ADegenerateBoxStillHasAProbe)
    {
        const vsg::vec3 p(10, 20, 30);
        const Volume v = layoutVolume(p, p, 64.f, 100);
        EXPECT_GE(probeProduct(v), 1u);
        EXPECT_EQ(v.probes.size(), probeProduct(v));
        expectCovers(v, p, p);
    }

    TEST(RenderGiProbeBakeLayout, ProbePositionFollowsTheStorageOrder)
    {
        Volume v;
        v.origin = vsg::vec3(1, 2, 3);
        v.spacing = 10.f;
        v.size = { 3, 4, 5 };
        v.probes.resize(60);
        for (std::size_t z = 0; z < 5; ++z)
            for (std::size_t y = 0; y < 4; ++y)
                for (std::size_t x = 0; x < 3; ++x)
                {
                    const vsg::vec3 p = probePosition(v, x + 3 * (y + 4 * z));
                    EXPECT_NEAR(p.x, 1.f + 10.f * static_cast<float>(x), 1e-4f);
                    EXPECT_NEAR(p.y, 2.f + 10.f * static_cast<float>(y), 1e-4f);
                    EXPECT_NEAR(p.z, 3.f + 10.f * static_cast<float>(z), 1e-4f);
                }
    }

    TEST(RenderGiProbeBakeLayout, UniformProbeReadsBackAsItsColour)
    {
        const vsg::vec3 colour(0.7f, 0.2f, 0.45f);
        const Sh9 sh = uniformProbe(colour);
        for (const vsg::vec3& n : testNormals())
        {
            const vsg::vec3 a = shAmbient(sh, n);
            EXPECT_NEAR(a.x, colour.x, 1e-3f);
            EXPECT_NEAR(a.y, colour.y, 1e-3f);
            EXPECT_NEAR(a.z, colour.z, 1e-3f);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // fillInvalidProbes
    // ---------------------------------------------------------------------------------------------

    Volume lineVolume(std::uint32_t count)
    {
        Volume v;
        v.size = { count, 1, 1 };
        v.probes.resize(count);
        return v;
    }

    TEST(RenderGiProbeBakeFill, AnInvalidProbeTakesTheAverageOfItsValidNeighbours)
    {
        Volume v = lineVolume(3);
        v.probes[0] = uniformProbe(vsg::vec3(1, 0, 0));
        v.probes[2] = uniformProbe(vsg::vec3(0, 1, 0));
        fillInvalidProbes(v, { false, true, false }, vsg::vec3(0.5f, 0.5f, 0.5f));

        for (std::size_t i = 0; i < shCoefficients; ++i)
        {
            const vsg::vec3 expected = (uniformProbe(vsg::vec3(1, 0, 0)).c[i] + uniformProbe(vsg::vec3(0, 1, 0)).c[i]) * 0.5f;
            EXPECT_NEAR(v.probes[1].c[i].x, expected.x, 1e-5f);
            EXPECT_NEAR(v.probes[1].c[i].y, expected.y, 1e-5f);
            EXPECT_NEAR(v.probes[1].c[i].z, expected.z, 1e-5f);
        }
    }

    TEST(RenderGiProbeBakeFill, ValidProbesAreUntouched)
    {
        Volume v = lineVolume(3);
        v.probes[0] = uniformProbe(vsg::vec3(1, 0, 0));
        v.probes[2] = uniformProbe(vsg::vec3(0, 1, 0));
        const Sh9 first = v.probes[0];
        const Sh9 last = v.probes[2];
        fillInvalidProbes(v, { false, true, false }, vsg::vec3(0.5f, 0.5f, 0.5f));
        EXPECT_TRUE(bitEqual(v.probes[0], first));
        EXPECT_TRUE(bitEqual(v.probes[2], last));
    }

    TEST(RenderGiProbeBakeFill, AVolumeWithNoValidProbeBecomesUniformAmbient)
    {
        Volume v;
        v.size = { 2, 2, 2 };
        v.probes.resize(8);
        const vsg::vec3 ambient(0.2f, 0.4f, 0.6f);
        fillInvalidProbes(v, std::vector<bool>(8, true), ambient);
        for (const Sh9& p : v.probes)
        {
            const vsg::vec3 a = shAmbient(p, vsg::vec3(0, 0, 1));
            EXPECT_NEAR(a.x, ambient.x, 1e-3f);
            EXPECT_NEAR(a.y, ambient.y, 1e-3f);
            EXPECT_NEAR(a.z, ambient.z, 1e-3f);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // VolumeBaker
    // ---------------------------------------------------------------------------------------------

    struct BakerFixture
    {
        RoomWorld room = cubeRoom(120.f);
        BakeSettings settings = plainSettings(32, 2);
        std::vector<BakeLight> lights = { roomLight() };
        Volume layout = layoutVolume(vsg::vec3(-60, -60, -60), vsg::vec3(60, 60, 60), 60.f, 100);

        VolumeBaker make() const { return VolumeBaker(settings, room, lights, layout); }
    };

    TEST(RenderGiProbeBakeVolumeBaker, CountersFollowTheSteps)
    {
        BakerFixture f;
        VolumeBaker baker = f.make();
        EXPECT_EQ(baker.total(), f.layout.probes.size());
        EXPECT_EQ(baker.next(), 0u);
        EXPECT_FALSE(baker.done());

        EXPECT_FALSE(baker.step(3));
        EXPECT_EQ(baker.next(), 3u);
        EXPECT_FALSE(baker.done());

        EXPECT_TRUE(baker.step(baker.total()));
        EXPECT_TRUE(baker.done());
        EXPECT_EQ(baker.next(), baker.total());
    }

    TEST(RenderGiProbeBakeVolumeBaker, StepsInPiecesEqualOneBigStep)
    {
        BakerFixture f;
        VolumeBaker whole = f.make();
        whole.step(whole.total());

        VolumeBaker pieces = f.make();
        while (!pieces.done())
            pieces.step(4);

        EXPECT_TRUE(bitEqual(whole.finish(), pieces.finish()));
        EXPECT_EQ(whole.invalid(), pieces.invalid());
    }

    TEST(RenderGiProbeBakeVolumeBaker, ResumeReproducesTheFullResult)
    {
        BakerFixture f;
        VolumeBaker whole = f.make();
        whole.step(whole.total());

        VolumeBaker first = f.make();
        first.step(first.total() / 2);
        ASSERT_FALSE(first.done());

        VolumeBaker second = f.make();
        second.resume(first.volume(), first.next(), first.invalid());
        EXPECT_EQ(second.next(), first.next());
        while (!second.done())
            second.step(5);

        EXPECT_TRUE(bitEqual(whole.finish(), second.finish()));
    }

    TEST(RenderGiProbeBakeVolumeBaker, FinishFillsInvalidProbes)
    {
        const SolidWorld solid;
        BakeSettings s = plainSettings(32, 1);
        const Volume layout = layoutVolume(vsg::vec3(0, 0, 0), vsg::vec3(100, 100, 100), 100.f, 100);
        VolumeBaker baker(s, solid, {}, layout);
        baker.step(baker.total());
        ASSERT_TRUE(baker.done());
        for (bool invalid : baker.invalid())
            EXPECT_TRUE(invalid);

        const Volume result = baker.finish();
        for (const Sh9& p : result.probes)
        {
            const vsg::vec3 a = shAmbient(p, vsg::vec3(1, 0, 0));
            EXPECT_NEAR(a.x, s.ambient.x, 1e-3f);
            EXPECT_NEAR(a.y, s.ambient.y, 1e-3f);
            EXPECT_NEAR(a.z, s.ambient.z, 1e-3f);
        }
    }
}

namespace Render::Gi
{
    // Added after the review: a world may report a surface normal that points away from the ray.
    TEST(RenderGiProbeBakeFacing, a_normal_pointing_along_the_ray_is_turned_around)
    {
        const vsg::vec3 n = facingNormal(vsg::vec3(0.f, 0.f, 2.f), vsg::vec3(0.f, 0.f, 5.f));
        EXPECT_FLOAT_EQ(n.z, -1.f);
        EXPECT_FLOAT_EQ(n.x, 0.f);
    }

    TEST(RenderGiProbeBakeFacing, a_normal_facing_the_ray_source_is_only_made_a_unit_vector)
    {
        const vsg::vec3 n = facingNormal(vsg::vec3(3.f, 0.f, 0.f), vsg::vec3(-1.f, 0.f, 0.f));
        EXPECT_FLOAT_EQ(n.x, 1.f);
    }

    TEST(RenderGiProbeBakeFacing, a_zero_normal_becomes_the_reverse_of_the_ray)
    {
        const vsg::vec3 n = facingNormal(vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(0.f, 4.f, 0.f));
        EXPECT_FLOAT_EQ(n.y, -1.f);
    }

    TEST(RenderGiProbeBakeLayout, a_box_that_is_not_finite_gives_one_probe_and_returns)
    {
        const float inf = std::numeric_limits<float>::infinity();
        const Volume v = layoutVolume(vsg::vec3(inf, inf, inf), vsg::vec3(-inf, -inf, -inf), 128.f, 100);
        EXPECT_EQ(v.probes.size(), 1u);
    }
}
