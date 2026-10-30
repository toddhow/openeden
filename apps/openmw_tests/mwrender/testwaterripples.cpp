// Tests for MWRender::RippleParticles and MWRender::updateRippleEmitter, OpenMW's RippleSimulation without a renderer
// (task 0012, V4). Written from the comments and declarations in apps/openmw/mwrender/waterripples.hpp only.
//
// Conventions these tests rely on:
// - The 20-unit height window is tested away from its edges (19.5 inside, 20.5 outside): whether exactly 20 counts
//   as inside is not specified.
// - A freshly emitted ripple has age 0; the ages used in exact comparisons are sums of exactly representable floats.
// - Angles are compared modulo 2 pi, since the spec does not say whether step() wraps them into [-pi, pi).
// - RippleEmitterTimer::timer counts down: 0 (the default) means it has run out, which is why the first call in the
//   water makes a ripple, and "resets its timer" sets it back to 0.

#include "apps/openmw/mwrender/waterripples.hpp"

#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
    using MWRender::RippleEmitterTimer;
    using MWRender::RippleParticles;
    using MWRender::RippleSettings;

    constexpr float sEpsilon = 1e-4f;
    constexpr float sPi = 3.14159265358979323846f;

    RippleSettings makeSettings(int maxNumberRipples = 10, float lifetime = 100.f, float rotationSpeed = 0.f)
    {
        RippleSettings settings;
        settings.maxNumberRipples = maxNumberRipples;
        settings.lifetime = lifetime;
        settings.rotationSpeed = rotationSpeed;
        return settings;
    }

    // The difference a - b brought into [-pi, pi].
    float angleDifference(float a, float b)
    {
        const double twoPi = 2.0 * 3.14159265358979323846;
        double d = std::fmod(static_cast<double>(a) - static_cast<double>(b), twoPi);
        if (d > twoPi / 2)
            d -= twoPi;
        if (d < -twoPi / 2)
            d += twoPi;
        return static_cast<float>(d);
    }

    std::vector<float> sortedAges(const RippleParticles& particles)
    {
        std::vector<float> ages;
        for (std::size_t i = 0; i < particles.size(); ++i)
            ages.push_back(particles[i].age);
        std::sort(ages.begin(), ages.end());
        return ages;
    }

    std::vector<float> xs(const RippleParticles& particles)
    {
        std::vector<float> result;
        for (std::size_t i = 0; i < particles.size(); ++i)
            result.push_back(particles[i].position.x);
        return result;
    }

    // RippleParticles: emission and the height window

    TEST(MWRenderRippleParticlesTest, startsEmpty)
    {
        RippleParticles particles(makeSettings());
        EXPECT_EQ(particles.size(), 0u);
        EXPECT_EQ(particles.getWaterHeight(), 0.f);
    }

    TEST(MWRenderRippleParticlesTest, setWaterHeightIsReturned)
    {
        RippleParticles particles(makeSettings());
        particles.setWaterHeight(-37.5f);
        EXPECT_EQ(particles.getWaterHeight(), -37.5f);
    }

    TEST(MWRenderRippleParticlesTest, emitsWithinTwentyUnitsOfTheWaterHeight)
    {
        RippleParticles particles(makeSettings(100));
        particles.setWaterHeight(100.f);
        const float heights[] = { 100.f, 110.f, 90.f, 119.5f, 80.5f };
        std::size_t expected = 0;
        for (float z : heights)
        {
            particles.emit(vsg::vec3(1.f, 2.f, z));
            ++expected;
            EXPECT_EQ(particles.size(), expected) << "z " << z;
        }
    }

    TEST(MWRenderRippleParticlesTest, doesNotEmitAboveTheWindow)
    {
        RippleParticles particles(makeSettings(100));
        particles.setWaterHeight(100.f);
        particles.emit(vsg::vec3(1.f, 2.f, 120.5f));
        particles.emit(vsg::vec3(1.f, 2.f, 500.f));
        EXPECT_EQ(particles.size(), 0u);
    }

    TEST(MWRenderRippleParticlesTest, doesNotEmitBelowTheWindow)
    {
        RippleParticles particles(makeSettings(100));
        particles.setWaterHeight(100.f);
        particles.emit(vsg::vec3(1.f, 2.f, 79.5f));
        particles.emit(vsg::vec3(1.f, 2.f, 0.f)); // inside the window of the default height 0, not of 100
        particles.emit(vsg::vec3(1.f, 2.f, -300.f));
        EXPECT_EQ(particles.size(), 0u);
    }

    TEST(MWRenderRippleParticlesTest, windowFollowsANegativeWaterHeight)
    {
        RippleParticles particles(makeSettings(100));
        particles.setWaterHeight(-50.f);
        particles.emit(vsg::vec3(0.f, 0.f, -60.f));
        particles.emit(vsg::vec3(0.f, 0.f, -35.f));
        EXPECT_EQ(particles.size(), 2u);
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.emit(vsg::vec3(0.f, 0.f, -75.f));
        EXPECT_EQ(particles.size(), 2u);
    }

    TEST(MWRenderRippleParticlesTest, emittedPositionIsTheInputXY)
    {
        RippleParticles particles(makeSettings());
        particles.setWaterHeight(10.f);
        particles.emit(vsg::vec3(3.5f, -4.25f, 15.f));
        ASSERT_EQ(particles.size(), 1u);
        EXPECT_EQ(particles[0].position.x, 3.5f);
        EXPECT_EQ(particles[0].position.y, -4.25f);
        EXPECT_EQ(particles[0].age, 0.f);
    }

    TEST(MWRenderRippleParticlesTest, zeroMaxNumberRipplesEmitsNothing)
    {
        RippleParticles particles(makeSettings(0));
        for (int i = 0; i < 5; ++i)
            particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.step(0.5f);
        EXPECT_EQ(particles.size(), 0u);
    }

    TEST(MWRenderRippleParticlesTest, negativeMaxNumberRipplesEmitsNothing)
    {
        RippleParticles particles(makeSettings(-3));
        for (int i = 0; i < 5; ++i)
            particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.step(0.5f);
        EXPECT_EQ(particles.size(), 0u);
    }

    // RippleParticles: the cap

    TEST(MWRenderRippleParticlesTest, atMostMaxPlusOneAlive)
    {
        RippleParticles particles(makeSettings(3));
        for (int i = 0; i < 20; ++i)
        {
            particles.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
            ASSERT_LE(particles.size(), 4u) << "emit " << i;
            ASSERT_LE(particles.size(), particles.capacity());
        }
        EXPECT_EQ(particles.size(), 4u);
    }

    TEST(MWRenderRippleParticlesTest, maxOfOneKeepsAtMostTwo)
    {
        RippleParticles particles(makeSettings(1));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_EQ(particles.size(), 1u);
        particles.emit(vsg::vec3(1.f, 0.f, 0.f));
        EXPECT_EQ(particles.size(), 2u);
        particles.emit(vsg::vec3(2.f, 0.f, 0.f));
        EXPECT_EQ(particles.size(), 2u);
    }

    TEST(MWRenderRippleParticlesTest, theOldestIsReplacedFirst)
    {
        RippleParticles particles(makeSettings(3, 100.f));
        // Ripples at x = 1, 2, 3, 4 emitted 0.25 s apart: ages 1, 0.75, 0.5, 0.25.
        for (int i = 1; i <= 4; ++i)
        {
            particles.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
            particles.step(0.25f);
        }
        ASSERT_EQ(particles.size(), 4u);
        EXPECT_EQ(sortedAges(particles), (std::vector<float>{ 0.25f, 0.5f, 0.75f, 1.f }));

        // The fifth replaces the ripple at x = 1, the oldest.
        particles.emit(vsg::vec3(5.f, 0.f, 0.f));
        ASSERT_EQ(particles.size(), 4u);
        EXPECT_EQ(sortedAges(particles), (std::vector<float>{ 0.f, 0.25f, 0.5f, 0.75f }));
        std::vector<float> x = xs(particles);
        std::sort(x.begin(), x.end());
        EXPECT_EQ(x, (std::vector<float>{ 2.f, 3.f, 4.f, 5.f }));

        // Then the one at x = 2.
        particles.step(0.25f);
        particles.emit(vsg::vec3(6.f, 0.f, 0.f));
        ASSERT_EQ(particles.size(), 4u);
        EXPECT_EQ(sortedAges(particles), (std::vector<float>{ 0.f, 0.25f, 0.5f, 0.75f }));
        x = xs(particles);
        std::sort(x.begin(), x.end());
        EXPECT_EQ(x, (std::vector<float>{ 3.f, 4.f, 5.f, 6.f }));
    }

    // RippleParticles: stepping

    TEST(MWRenderRippleParticlesTest, stepAgesByDt)
    {
        RippleParticles particles(makeSettings(10, 100.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.step(0.25f);
        particles.emit(vsg::vec3(1.f, 0.f, 0.f));
        particles.step(0.5f);
        EXPECT_EQ(sortedAges(particles), (std::vector<float>{ 0.5f, 0.75f }));
    }

    TEST(MWRenderRippleParticlesTest, stepTurnsByRotationSpeedTimesDt)
    {
        RippleParticles particles(makeSettings(10, 100.f, 0.5f));
        for (int i = 0; i < 5; ++i)
            particles.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
        ASSERT_EQ(particles.size(), 5u);
        std::vector<float> before;
        for (std::size_t i = 0; i < particles.size(); ++i)
            before.push_back(particles[i].angle);

        particles.step(0.25f);
        ASSERT_EQ(particles.size(), 5u);
        for (std::size_t i = 0; i < particles.size(); ++i)
            EXPECT_NEAR(angleDifference(particles[i].angle, before[i]), 0.125f, sEpsilon) << "ripple " << i;

        particles.step(1.f);
        for (std::size_t i = 0; i < particles.size(); ++i)
            EXPECT_NEAR(angleDifference(particles[i].angle, before[i]), 0.625f, sEpsilon) << "ripple " << i;
    }

    TEST(MWRenderRippleParticlesTest, negativeRotationSpeedTurnsTheOtherWay)
    {
        RippleParticles particles(makeSettings(10, 100.f, -2.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        const float before = particles[0].angle;
        particles.step(0.25f);
        EXPECT_NEAR(angleDifference(particles[0].angle, before), -0.5f, sEpsilon);
    }

    TEST(MWRenderRippleParticlesTest, zeroRotationSpeedKeepsTheAngle)
    {
        RippleParticles particles(makeSettings(10, 100.f, 0.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        const float before = particles[0].angle;
        particles.step(1.5f);
        EXPECT_NEAR(angleDifference(particles[0].angle, before), 0.f, sEpsilon);
    }

    TEST(MWRenderRippleParticlesTest, removedOnceAgeReachesLifetime)
    {
        RippleParticles particles(makeSettings(10, 1.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.step(0.5f);
        EXPECT_EQ(particles.size(), 1u);
        particles.step(0.25f);
        EXPECT_EQ(particles.size(), 1u);
        particles.step(0.25f); // age 1, the lifetime
        EXPECT_EQ(particles.size(), 0u);
    }

    TEST(MWRenderRippleParticlesTest, removedWhenAStepPassesTheLifetime)
    {
        RippleParticles particles(makeSettings(10, 1.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.step(3.f);
        EXPECT_EQ(particles.size(), 0u);
    }

    TEST(MWRenderRippleParticlesTest, survivorsKeepTheirOrder)
    {
        RippleParticles particles(makeSettings(10, 1.f));
        // x = 1, 2, 3, 4, 5 emitted 0.25 s apart. x = 1 reached the lifetime in the step before x = 5 was emitted,
        // so x = 2, 3, 4, 5 remain with ages 0.75, 0.5, 0.25, 0.
        for (int i = 1; i <= 5; ++i)
        {
            particles.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
            if (i < 5)
                particles.step(0.25f);
        }
        ASSERT_EQ(particles.size(), 4u);
        const std::vector<float> before = xs(particles);

        particles.step(0.25f); // removes x = 2
        ASSERT_EQ(particles.size(), 3u);
        std::vector<float> expected;
        for (float x : before)
            if (x != 2.f)
                expected.push_back(x);
        EXPECT_EQ(xs(particles), expected);

        particles.step(0.25f); // removes x = 3
        ASSERT_EQ(particles.size(), 2u);
        std::vector<float> expectedAfter;
        for (float x : expected)
            if (x != 3.f)
                expectedAfter.push_back(x);
        EXPECT_EQ(xs(particles), expectedAfter);
        EXPECT_EQ(sortedAges(particles), (std::vector<float>{ 0.5f, 0.75f }));
    }

    TEST(MWRenderRippleParticlesTest, zeroLifetimeNeverEnds)
    {
        RippleParticles particles(makeSettings(10, 0.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        for (int i = 0; i < 10; ++i)
            particles.step(100.f);
        ASSERT_EQ(particles.size(), 1u);
        EXPECT_NEAR(particles.sizeOf(particles[0]), 15.f, sEpsilon);
        EXPECT_NEAR(particles.alphaOf(particles[0]), 0.7f, sEpsilon);
    }

    TEST(MWRenderRippleParticlesTest, negativeLifetimeNeverEnds)
    {
        RippleParticles particles(makeSettings(10, -1.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        particles.emit(vsg::vec3(1.f, 0.f, 0.f));
        for (int i = 0; i < 10; ++i)
            particles.step(50.f);
        ASSERT_EQ(particles.size(), 2u);
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            EXPECT_TRUE(std::isfinite(particles.sizeOf(particles[i])));
            EXPECT_NEAR(particles.sizeOf(particles[i]), 15.f, sEpsilon) << "ripple " << i;
            EXPECT_NEAR(particles.alphaOf(particles[i]), 0.7f, sEpsilon) << "ripple " << i;
        }
    }

    // RippleParticles: size and alpha

    TEST(MWRenderRippleParticlesTest, sizeAndAlphaAtEmission)
    {
        RippleParticles particles(makeSettings(10, 2.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        ASSERT_EQ(particles.size(), 1u);
        EXPECT_NEAR(particles.sizeOf(particles[0]), 15.f, sEpsilon);
        EXPECT_NEAR(particles.alphaOf(particles[0]), 0.7f, sEpsilon);
    }

    TEST(MWRenderRippleParticlesTest, sizeAndAlphaAreLinearInAgeOverLifetime)
    {
        RippleParticles particles(makeSettings(10, 2.f));
        particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        const float steps[] = { 0.5f, 0.5f, 0.5f, 0.25f };
        float age = 0.f;
        for (float dt : steps)
        {
            particles.step(dt);
            age += dt;
            ASSERT_EQ(particles.size(), 1u) << "age " << age;
            const float fraction = age / 2.f;
            EXPECT_NEAR(particles.sizeOf(particles[0]), 15.f + 165.f * fraction, 1e-3f) << "age " << age;
            EXPECT_NEAR(particles.alphaOf(particles[0]), 0.7f * (1.f - fraction), sEpsilon) << "age " << age;
        }
        // At age 1.75 of 2: size 159.375, alpha 0.0875.
        EXPECT_NEAR(particles.sizeOf(particles[0]), 159.375f, 1e-3f);
        EXPECT_NEAR(particles.alphaOf(particles[0]), 0.0875f, sEpsilon);
    }

    TEST(MWRenderRippleParticlesTest, sizeAndAlphaOfAnyRipple)
    {
        // sizeOf and alphaOf depend only on the ripple's age and the lifetime.
        RippleParticles particles(makeSettings(10, 4.f));
        RippleParticles::Ripple ripple;
        ripple.age = 1.f;
        EXPECT_NEAR(particles.sizeOf(ripple), 56.25f, 1e-3f);
        EXPECT_NEAR(particles.alphaOf(ripple), 0.525f, sEpsilon);
        ripple.age = 2.f;
        EXPECT_NEAR(particles.sizeOf(ripple), 97.5f, 1e-3f);
        EXPECT_NEAR(particles.alphaOf(ripple), 0.35f, sEpsilon);
        ripple.age = 4.f;
        EXPECT_NEAR(particles.sizeOf(ripple), 180.f, 1e-3f);
        EXPECT_NEAR(particles.alphaOf(ripple), 0.f, sEpsilon);
    }

    // RippleParticles: angles and seeds

    TEST(MWRenderRippleParticlesTest, initialAngleIsInMinusPiToPi)
    {
        RippleParticles particles(makeSettings(1000, 100.f), 12345);
        for (int i = 0; i < 500; ++i)
            particles.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
        ASSERT_EQ(particles.size(), 500u);
        bool negative = false;
        bool positive = false;
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            const float angle = particles[i].angle;
            ASSERT_TRUE(std::isfinite(angle)) << "ripple " << i;
            EXPECT_GE(angle, -sPi - sEpsilon) << "ripple " << i;
            EXPECT_LT(angle, sPi + sEpsilon) << "ripple " << i;
            negative = negative || angle < 0.f;
            positive = positive || angle > 0.f;
        }
        // Spread over the range, not one value.
        EXPECT_TRUE(negative);
        EXPECT_TRUE(positive);
    }

    TEST(MWRenderRippleParticlesTest, sameSeedGivesTheSameAngles)
    {
        RippleParticles first(makeSettings(100, 100.f, 0.75f), 7);
        RippleParticles second(makeSettings(100, 100.f, 0.75f), 7);
        for (int i = 0; i < 30; ++i)
        {
            first.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
            second.emit(vsg::vec3(static_cast<float>(i), 0.f, 0.f));
            first.step(0.125f);
            second.step(0.125f);
        }
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            EXPECT_EQ(first[i].angle, second[i].angle) << "ripple " << i;
            EXPECT_EQ(first[i].age, second[i].age) << "ripple " << i;
            EXPECT_EQ(first[i].position, second[i].position) << "ripple " << i;
        }
    }

    TEST(MWRenderRippleParticlesTest, defaultSeedIsOne)
    {
        RippleParticles first(makeSettings(100));
        RippleParticles second(makeSettings(100), 1);
        for (int i = 0; i < 10; ++i)
        {
            first.emit(vsg::vec3(0.f, 0.f, 0.f));
            second.emit(vsg::vec3(0.f, 0.f, 0.f));
        }
        for (std::size_t i = 0; i < first.size(); ++i)
            EXPECT_EQ(first[i].angle, second[i].angle) << "ripple " << i;
    }

    TEST(MWRenderRippleParticlesTest, differentSeedsGiveDifferentAngles)
    {
        RippleParticles first(makeSettings(100), 1);
        RippleParticles second(makeSettings(100), 2);
        for (int i = 0; i < 20; ++i)
        {
            first.emit(vsg::vec3(0.f, 0.f, 0.f));
            second.emit(vsg::vec3(0.f, 0.f, 0.f));
        }
        ASSERT_EQ(first.size(), second.size());
        bool differs = false;
        for (std::size_t i = 0; i < first.size() && !differs; ++i)
            differs = first[i].angle != second[i].angle;
        EXPECT_TRUE(differs);
    }

    // RippleParticles: clear

    TEST(MWRenderRippleParticlesTest, clearRemovesEveryRipple)
    {
        RippleParticles particles(makeSettings(10, 100.f));
        for (int i = 0; i < 5; ++i)
            particles.emit(vsg::vec3(0.f, 0.f, 0.f));
        const std::size_t capacity = particles.capacity();
        ASSERT_EQ(particles.size(), 5u);
        particles.clear();
        EXPECT_EQ(particles.size(), 0u);
        EXPECT_EQ(particles.capacity(), capacity);
        particles.step(0.5f);
        EXPECT_EQ(particles.size(), 0u);

        // Emitting works again afterwards.
        particles.emit(vsg::vec3(2.f, 3.f, 0.f));
        ASSERT_EQ(particles.size(), 1u);
        EXPECT_EQ(particles[0].position.x, 2.f);
        EXPECT_EQ(particles[0].position.y, 3.f);
        EXPECT_EQ(particles[0].age, 0.f);
    }

    // updateRippleEmitter

    TEST(MWRenderRippleEmitterTest, notInWaterReturnsFalseAndResetsTheTimer)
    {
        RippleEmitterTimer timer;
        timer.timer = 1.25f;
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, 0.f, 0.f), false));
        EXPECT_EQ(timer.timer, 0.f);
    }

    TEST(MWRenderRippleEmitterTest, notInWaterNeverEmitsEvenWhenMoving)
    {
        RippleEmitterTimer timer;
        for (int i = 0; i < 10; ++i)
            EXPECT_FALSE(
                MWRender::updateRippleEmitter(timer, 2.f, vsg::vec3(100.f * static_cast<float>(i), 0.f, 0.f), false))
                << "call " << i;
    }

    TEST(MWRenderRippleEmitterTest, firstCallInWaterEmitsAndRecordsThePosition)
    {
        RippleEmitterTimer timer;
        const vsg::vec3 position(12.5f, -3.f, 7.f);
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, position, true));
        EXPECT_EQ(timer.lastEmitPosition, position);
    }

    TEST(MWRenderRippleEmitterTest, waitsOneAndAHalfSecondsAfterARipple)
    {
        RippleEmitterTimer timer;
        const vsg::vec3 position(5.f, 5.f, 0.f);
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, position, true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.5f, position, true)); // 0.5 s
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.5f, position, true)); // 1 s
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.25f, position, true)); // 1.25 s
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.5f, position, true)); // 1.75 s

        // And the wait starts again.
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 1.25f, position, true));
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.5f, position, true));
    }

    TEST(MWRenderRippleEmitterTest, movingExactlyTenUnitsDoesNotEmit)
    {
        RippleEmitterTimer timer;
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, 0.f, 0.f), true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(10.f, 0.f, 0.f), true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(6.f, 8.f, 0.f), true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, -10.f, 0.f), true));
        EXPECT_EQ(timer.lastEmitPosition, vsg::vec3(0.f, 0.f, 0.f));
    }

    TEST(MWRenderRippleEmitterTest, movingMoreThanTenUnitsEmitsBeforeTheTimerRunsOut)
    {
        RippleEmitterTimer timer;
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, 0.f, 0.f), true));
        const vsg::vec3 moved(10.5f, 0.f, 0.f);
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, moved, true));
        EXPECT_EQ(timer.lastEmitPosition, moved);
    }

    TEST(MWRenderRippleEmitterTest, distanceIsMeasuredFromTheLastRipple)
    {
        RippleEmitterTimer timer;
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, 0.f, 0.f), true));
        // Small moves adding up to more than 10 units from the last ripple.
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(4.f, 0.f, 0.f), true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(8.f, 0.f, 0.f), true));
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(12.f, 0.f, 0.f), true));
        EXPECT_EQ(timer.lastEmitPosition, vsg::vec3(12.f, 0.f, 0.f));
        // The next move is measured from 12.
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(20.f, 0.f, 0.f), true));
    }

    TEST(MWRenderRippleEmitterTest, aRippleFromMovingRestartsTheWait)
    {
        RippleEmitterTimer timer;
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, vsg::vec3(0.f, 0.f, 0.f), true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 1.f, vsg::vec3(0.f, 0.f, 0.f), true)); // 1 s
        const vsg::vec3 moved(0.f, 20.f, 0.f);
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.25f, moved, true)); // 1.25 s, moved 20

        // Without the restart the old wait would run out after another 0.25 s.
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.5f, moved, true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.75f, moved, true)); // 1.25 s since the move
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.5f, moved, true)); // 1.75 s
    }

    TEST(MWRenderRippleEmitterTest, leavingTheWaterLetsTheNextCallInWaterEmit)
    {
        RippleEmitterTimer timer;
        const vsg::vec3 position(1.f, 2.f, 3.f);
        ASSERT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, position, true));
        ASSERT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, position, true));
        EXPECT_FALSE(MWRender::updateRippleEmitter(timer, 0.0625f, position, false));
        EXPECT_TRUE(MWRender::updateRippleEmitter(timer, 0.0625f, position, true));
    }
}
