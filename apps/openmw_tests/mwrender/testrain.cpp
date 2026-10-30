// Tests for MWRender::RainSimulation, OpenMW's procedural rain without a renderer (task 0012, V4). Written from
// apps/openmw/mwrender/rain.hpp and OpenMW's sky.cpp / skyutil.cpp at cf3dbd3aa7 only.
//
// Conventions these tests rely on:
// - Emission is floor-based with the fraction carried over (osgParticle::ConstantRateCounter, used by RainCounter).
//   Emission time is capped at 0.2 s per step, as OpenMW's RainCounter; steps whose emitted count matters stay at or
//   below 0.2 s, except the test of the cap itself.
// - Drops [0, size()) are the live drops; when a step removes nothing and emits nothing, the drops keep their index.
// - Every RainParameters here has enabled = true; the simulation's use of `enabled` is not specified.

#include "apps/openmw/mwrender/rain.hpp"

#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
    using MWRender::RainParameters;
    using MWRender::RainSimulation;

    constexpr float sEpsilon = 1e-3f;

    RainParameters makeParameters(int maxRaindrops = 10, float entranceSpeed = 1.f)
    {
        RainParameters parameters;
        parameters.enabled = true;
        parameters.speed = 500.f;
        parameters.diameter = 1000.f;
        parameters.minHeight = 200.f;
        parameters.maxHeight = 600.f;
        parameters.entranceSpeed = entranceSpeed;
        parameters.maxRaindrops = maxRaindrops;
        parameters.windSpeed = 10.f;
        parameters.alpha = 1.f;
        return parameters;
    }

    // Half extents of the wrap box: diameter x diameter x (minHeight + maxHeight) / 2, centred on the camera.
    vsg::vec3 halfExtents(const RainParameters& parameters)
    {
        return vsg::vec3(
            parameters.diameter / 2.f, parameters.diameter / 2.f, (parameters.minHeight + parameters.maxHeight) / 4.f);
    }

    void expectInsideBox(const RainSimulation& simulation, const RainParameters& parameters)
    {
        const vsg::vec3 half = halfExtents(parameters);
        for (std::size_t i = 0; i < simulation.size(); ++i)
        {
            const vsg::vec3& p = simulation[i].position;
            for (int j = 0; j < 3; ++j)
            {
                ASSERT_TRUE(std::isfinite(p[j])) << "drop " << i << " axis " << j;
                EXPECT_LE(std::abs(p[j]), half[j] + sEpsilon * std::max(1.f, half[j])) << "drop " << i << " axis " << j;
            }
        }
    }

    // 10 / 1 * 20 = 200 drops per second.
    TEST(MWRenderRainSimulationTest, emitsAtTheRateOfMaxRaindropsOverEntranceSpeedTimesTwenty)
    {
        RainSimulation simulation(4096);
        simulation.step(0.2f, vsg::vec3(), makeParameters(10, 1.f));
        EXPECT_NEAR(static_cast<double>(simulation.size()), 40.0, 1.0);
    }

    TEST(MWRenderRainSimulationTest, emissionTimeIsCappedAtPointTwoSecondsPerStep)
    {
        // A 1 s step emits only 0.2 s worth: 200 per second gives 40, not 200.
        RainSimulation simulation(4096);
        simulation.step(1.f, vsg::vec3(), makeParameters(10, 1.f));
        EXPECT_NEAR(static_cast<double>(simulation.size()), 40.0, 1.0);
    }

    TEST(MWRenderRainSimulationTest, entranceSpeedDividesTheRate)
    {
        // 10 / 2 * 20 = 100 drops per second.
        RainSimulation simulation(4096);
        simulation.step(0.2f, vsg::vec3(), makeParameters(10, 2.f));
        EXPECT_NEAR(static_cast<double>(simulation.size()), 20.0, 1.0);
    }

    TEST(MWRenderRainSimulationTest, emitsOverSeveralStepsWithoutLosingDrops)
    {
        RainSimulation simulation(4096);
        for (int i = 0; i < 3; ++i)
            simulation.step(0.15f, vsg::vec3(), makeParameters(10, 1.f));
        // 0.45 s at 200 per second, all still alive.
        EXPECT_NEAR(static_cast<double>(simulation.size()), 90.0, 1.0);
    }

    TEST(MWRenderRainSimulationTest, manySmallStepsEmitAsManyAsOneBigStep)
    {
        const RainParameters parameters = makeParameters(10, 1.f);

        RainSimulation big(4096);
        big.step(0.2f, vsg::vec3(), parameters);

        // 0.001 s at 200 per second is 0.2 of a drop per step: only the carried fraction makes any drop appear.
        RainSimulation small(4096);
        for (int i = 0; i < 200; ++i)
            small.step(0.001f, vsg::vec3(), parameters);

        EXPECT_GT(small.size(), 0u);
        EXPECT_NEAR(static_cast<double>(small.size()), static_cast<double>(big.size()), 1.0);
    }

    TEST(MWRenderRainSimulationTest, constructorSetsCapacityAndStartsEmpty)
    {
        RainSimulation simulation(123);
        EXPECT_EQ(simulation.capacity(), 123u);
        EXPECT_EQ(simulation.size(), 0u);
    }

    TEST(MWRenderRainSimulationTest, neverExceedsCapacity)
    {
        // 100 / 1 * 20 = 2000 per second, far beyond a capacity of 50.
        RainSimulation simulation(50);
        const RainParameters parameters = makeParameters(100, 1.f);
        for (int i = 0; i < 20; ++i)
        {
            simulation.step(0.05f, vsg::vec3(), parameters);
            ASSERT_LE(simulation.size(), 50u);
        }
        EXPECT_EQ(simulation.size(), 50u);
        EXPECT_EQ(simulation.capacity(), 50u);
    }

    TEST(MWRenderRainSimulationTest, dropsAreRemovedOnceTheyReachOneSecond)
    {
        RainSimulation simulation(4096);
        simulation.step(0.1f, vsg::vec3(), makeParameters(10, 1.f));
        const std::size_t emitted = simulation.size();
        ASSERT_GT(emitted, 0u);

        // No further emission.
        const RainParameters none = makeParameters(0, 1.f);

        // Ages are now about 0.8 or 0.9 s (depending on whether a drop ages in the step that emits it): all alive.
        simulation.step(0.8f, vsg::vec3(), none);
        EXPECT_EQ(simulation.size(), emitted);
        for (std::size_t i = 0; i < simulation.size(); ++i)
        {
            EXPECT_GE(simulation[i].age, 0.f);
            EXPECT_LT(simulation[i].age, 1.f);
        }

        // Ages now at least 1.1 s: all gone.
        simulation.step(0.3f, vsg::vec3(), none);
        EXPECT_EQ(simulation.size(), 0u);
    }

    TEST(MWRenderRainSimulationTest, dropsAgeByDt)
    {
        RainSimulation simulation(4096);
        simulation.step(0.1f, vsg::vec3(), makeParameters(10, 1.f));
        ASSERT_GT(simulation.size(), 0u);
        std::vector<float> ages;
        for (std::size_t i = 0; i < simulation.size(); ++i)
            ages.push_back(simulation[i].age);

        simulation.step(0.25f, vsg::vec3(), makeParameters(0, 1.f));
        ASSERT_EQ(simulation.size(), ages.size());
        for (std::size_t i = 0; i < ages.size(); ++i)
            EXPECT_NEAR(simulation[i].age, ages[i] + 0.25f, 1e-4f) << "drop " << i;
    }

    TEST(MWRenderRainSimulationTest, newDropsStartInsideTheBox)
    {
        const RainParameters parameters = makeParameters(100, 1.f);
        RainSimulation simulation(4096);
        simulation.step(0.2f, vsg::vec3(), parameters);
        ASSERT_GT(simulation.size(), 100u);
        expectInsideBox(simulation, parameters);
    }

    TEST(MWRenderRainSimulationTest, newDropsFillTheBox)
    {
        // Uniform placement: with hundreds of drops, some lie on each side of the camera on every axis.
        const RainParameters parameters = makeParameters(100, 1.f);
        RainSimulation simulation(4096);
        simulation.step(0.2f, vsg::vec3(), parameters);
        ASSERT_GT(simulation.size(), 100u);
        for (int j = 0; j < 3; ++j)
        {
            bool negative = false;
            bool positive = false;
            for (std::size_t i = 0; i < simulation.size(); ++i)
            {
                negative = negative || simulation[i].position[j] < 0.f;
                positive = positive || simulation[i].position[j] > 0.f;
            }
            EXPECT_TRUE(negative) << "axis " << j;
            EXPECT_TRUE(positive) << "axis " << j;
        }
    }

    TEST(MWRenderRainSimulationTest, dropsWrapAroundToStayInsideTheBox)
    {
        RainParameters parameters = makeParameters(100, 1.f);
        parameters.speed = 3000.f;
        parameters.windSpeed = 80.f;
        RainSimulation simulation(4096);

        // Camera moves of more than one box per step on every axis, in both directions.
        const vsg::vec3 moves[] = {
            vsg::vec3(1700.f, -2300.f, 900.f),
            vsg::vec3(-4100.f, 600.f, -1250.f),
            vsg::vec3(350.f, 5200.f, 30.f),
        };
        for (int i = 0; i < 15; ++i)
        {
            simulation.step(0.05f, moves[i % 3], parameters);
            expectInsideBox(simulation, parameters);
        }
        EXPECT_GT(simulation.size(), 0u);
    }

    TEST(MWRenderRainSimulationTest, dropsMoveByVelocityAndAgainstTheCamera)
    {
        RainParameters parameters = makeParameters(100, 1.f);
        parameters.diameter = 100000.f;
        parameters.minHeight = 100000.f;
        parameters.maxHeight = 100000.f;
        parameters.speed = 500.f;
        parameters.windSpeed = 50.f;

        RainSimulation simulation(4096);
        simulation.step(0.1f, vsg::vec3(), parameters);
        ASSERT_GT(simulation.size(), 0u);

        std::vector<vsg::vec3> before;
        for (std::size_t i = 0; i < simulation.size(); ++i)
            before.push_back(simulation[i].position);

        const float dt = 0.01f;
        const vsg::vec3 cameraMove(1.f, 2.f, 3.f);
        const float a = -std::atan(parameters.windSpeed / 50.f);
        const vsg::vec3 velocity(0.f, parameters.speed * std::sin(a), -parameters.speed / std::cos(a));
        const vsg::vec3 expectedDelta = velocity * dt - cameraMove;

        parameters.maxRaindrops = 0; // no new drops, so indices stay put
        simulation.step(dt, cameraMove, parameters);
        ASSERT_EQ(simulation.size(), before.size());

        const vsg::vec3 half = halfExtents(parameters);
        const float margin = 100.f; // far more than one step's movement, so these drops cannot wrap
        std::size_t checked = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            bool nearEdge = false;
            for (int j = 0; j < 3; ++j)
                nearEdge = nearEdge || std::abs(before[i][j]) > half[j] - margin;
            if (nearEdge)
                continue;
            ++checked;
            for (int j = 0; j < 3; ++j)
                EXPECT_NEAR(simulation[i].position[j], before[i][j] + expectedDelta[j], 0.05f)
                    << "drop " << i << " axis " << j;
        }
        EXPECT_GT(checked, 0u);
    }

    // Size grows linearly with age over the one-second life: 5 at emission, 15 at the end (OSG 3.6.5
    // Particle::update interpolating the template's size range 5..15).
    TEST(MWRenderRainSimulationTest, freshDropsHaveSizeFive)
    {
        // A step so short that no drop can have aged noticeably, whether or not a drop ages in the step that emits it.
        // 1000 / 1 * 20 = 20000 per second for 0.001 s is 20 drops.
        RainSimulation simulation(4096);
        simulation.step(0.001f, vsg::vec3(), makeParameters(1000, 1.f));
        ASSERT_GT(simulation.size(), 0u);
        for (std::size_t i = 0; i < simulation.size(); ++i)
        {
            EXPECT_NEAR(simulation[i].size, 5.f + 10.f * simulation[i].age, 1e-4f) << "drop " << i;
            EXPECT_NEAR(simulation[i].size, 5.f, 0.01f + 1e-4f) << "drop " << i;
        }
    }

    TEST(MWRenderRainSimulationTest, sizeGrowsByTenPerSecondOfAge)
    {
        RainSimulation simulation(4096);
        simulation.step(0.05f, vsg::vec3(), makeParameters(100, 1.f));
        ASSERT_GT(simulation.size(), 0u);
        std::vector<float> sizes;
        for (std::size_t i = 0; i < simulation.size(); ++i)
            sizes.push_back(simulation[i].size);

        // No emission, so the same drops at the same indices, each aged by t.
        const RainParameters none = makeParameters(0, 1.f);
        const float t = 0.375f;
        simulation.step(t, vsg::vec3(), none);
        ASSERT_EQ(simulation.size(), sizes.size());
        for (std::size_t i = 0; i < sizes.size(); ++i)
        {
            EXPECT_NEAR(simulation[i].size, sizes[i] + 10.f * t, 1e-4f) << "drop " << i;
            EXPECT_NEAR(simulation[i].size, 5.f + 10.f * simulation[i].age, 1e-4f) << "drop " << i;
        }

        simulation.step(0.5f, vsg::vec3(), none);
        ASSERT_EQ(simulation.size(), sizes.size());
        for (std::size_t i = 0; i < sizes.size(); ++i)
        {
            EXPECT_NEAR(simulation[i].size, sizes[i] + 10.f * (t + 0.5f), 1e-4f) << "drop " << i;
            EXPECT_LT(simulation[i].size, 15.f);
        }
    }

    TEST(MWRenderRainSimulationTest, sizeFollowsAgeInALongRun)
    {
        RainParameters parameters = makeParameters(50, 1.5f);
        RainSimulation simulation(4096);
        for (int i = 0; i < 60; ++i)
        {
            simulation.step(0.033f, vsg::vec3(1.f, -2.f, 0.5f), parameters);
            for (std::size_t d = 0; d < simulation.size(); ++d)
            {
                ASSERT_NEAR(simulation[d].size, 5.f + 10.f * simulation[d].age, 1e-4f) << "drop " << d;
                ASSERT_GE(simulation[d].size, 5.f);
                ASSERT_LT(simulation[d].size, 15.f);
            }
        }
        EXPECT_GT(simulation.size(), 0u);
    }

    void runScenario(RainSimulation& simulation)
    {
        RainParameters parameters = makeParameters(50, 1.5f);
        parameters.windSpeed = 30.f;
        const vsg::vec3 moves[]
            = { vsg::vec3(10.f, 0.f, 0.f), vsg::vec3(0.f, -700.f, 5.f), vsg::vec3(3.f, 4.f, -900.f) };
        for (int i = 0; i < 40; ++i)
            simulation.step(0.033f, moves[i % 3], parameters);
    }

    TEST(MWRenderRainSimulationTest, sameSeedAndInputsGiveIdenticalDrops)
    {
        RainSimulation first(4096, 7);
        RainSimulation second(4096, 7);
        runScenario(first);
        runScenario(second);
        ASSERT_GT(first.size(), 0u);
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            EXPECT_EQ(first[i].position, second[i].position) << "drop " << i;
            EXPECT_EQ(first[i].age, second[i].age) << "drop " << i;
            EXPECT_EQ(first[i].size, second[i].size) << "drop " << i;
        }
    }

    TEST(MWRenderRainSimulationTest, differentSeedsGiveDifferentDrops)
    {
        RainSimulation first(4096, 1);
        RainSimulation second(4096, 2);
        runScenario(first);
        runScenario(second);
        ASSERT_GT(first.size(), 0u);
        ASSERT_EQ(first.size(), second.size()); // the count does not depend on the seed
        // Sizes follow age, which does not depend on the seed, so only positions can differ.
        bool differs = false;
        for (std::size_t i = 0; i < first.size() && !differs; ++i)
            differs = first[i].position != second[i].position;
        EXPECT_TRUE(differs);
    }

    TEST(MWRenderRainSimulationTest, clearRemovesEveryDrop)
    {
        RainSimulation simulation(4096);
        simulation.step(0.2f, vsg::vec3(), makeParameters(10, 1.f));
        ASSERT_GT(simulation.size(), 0u);
        simulation.clear();
        EXPECT_EQ(simulation.size(), 0u);
        EXPECT_EQ(simulation.capacity(), 4096u);

        simulation.step(0.1f, vsg::vec3(), makeParameters(0, 1.f));
        EXPECT_EQ(simulation.size(), 0u);
    }

    TEST(MWRenderRainSimulationTest, clearRemovesTheCarriedOverEmission)
    {
        // 2 / 10 * 20 = 4 per second (exact in float and double); 0.125 s is exactly half a drop each step, and
        // under the 0.2 s emission cap.
        const RainParameters parameters = makeParameters(2, 10.f);

        RainSimulation simulation(4096);
        simulation.step(0.125f, vsg::vec3(), parameters);
        ASSERT_EQ(simulation.size(), 0u);
        simulation.clear();
        simulation.step(0.125f, vsg::vec3(), parameters);
        EXPECT_EQ(simulation.size(), 0u);

        // Without the clear, the two halves make a drop.
        RainSimulation control(4096);
        control.step(0.125f, vsg::vec3(), parameters);
        control.step(0.125f, vsg::vec3(), parameters);
        EXPECT_EQ(control.size(), 1u);
    }

    TEST(MWRenderRainSimulationTest, zeroEntranceSpeedEmitsNothing)
    {
        RainSimulation simulation(4096);
        const RainParameters parameters = makeParameters(10, 0.f);
        for (int i = 0; i < 10; ++i)
            simulation.step(0.1f, vsg::vec3(5.f, 5.f, 5.f), parameters);
        EXPECT_EQ(simulation.size(), 0u);

        // And leaves nothing behind that emits later.
        simulation.step(0.01f, vsg::vec3(), makeParameters(0, 1.f));
        EXPECT_EQ(simulation.size(), 0u);
    }

    TEST(MWRenderRainSimulationTest, zeroEntranceSpeedKeepsExistingDropsFinite)
    {
        RainSimulation simulation(4096);
        simulation.step(0.1f, vsg::vec3(), makeParameters(10, 1.f));
        const std::size_t emitted = simulation.size();
        ASSERT_GT(emitted, 0u);
        simulation.step(0.1f, vsg::vec3(1.f, 1.f, 1.f), makeParameters(10, 0.f));
        EXPECT_EQ(simulation.size(), emitted);
        for (std::size_t i = 0; i < simulation.size(); ++i)
            for (int j = 0; j < 3; ++j)
                EXPECT_TRUE(std::isfinite(simulation[i].position[j])) << "drop " << i << " axis " << j;
    }

    TEST(MWRenderRainSimulationTest, zeroSizedBoxGivesFinitePositions)
    {
        RainParameters parameters = makeParameters(10, 1.f);
        parameters.diameter = 0.f;
        parameters.minHeight = 0.f;
        parameters.maxHeight = 0.f;

        RainSimulation simulation(4096);
        for (int i = 0; i < 5; ++i)
        {
            simulation.step(0.05f, vsg::vec3(3.f, -4.f, 5.f), parameters);
            for (std::size_t d = 0; d < simulation.size(); ++d)
            {
                for (int j = 0; j < 3; ++j)
                    ASSERT_TRUE(std::isfinite(simulation[d].position[j])) << "drop " << d << " axis " << j;
                ASSERT_TRUE(std::isfinite(simulation[d].size));
                ASSERT_TRUE(std::isfinite(simulation[d].age));
            }
        }
    }
}
