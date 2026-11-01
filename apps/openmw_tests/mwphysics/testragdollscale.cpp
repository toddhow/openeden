// Scale measurement for the debug ragdoll, stage b of task 0035 ("Scale (answer 19): this stage measures it").
//
// In plain words: Jolt's tolerances are tuned for a world measured in metres, and the game's world is about 70 times
// bigger per metre. This test drops a limp ragdoll 5 cm above the floor at game scale and writes down what happens
// every half second for 10 seconds: how low and high it is, how fast the fastest body moves, how many bodies are
// awake, and at the end how much the bodies still jitter from step to step. The numbers go to the test output for the
// implementer to read; the test asserts only loose sanity bounds.

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;

    constexpr double kUnits = Constants::UnitsPerMeter;

    TEST(RagdollScale, LimpRagdollSettlesAtGameScale)
    {
        GameLikeWorld world;
        std::string error;
        std::unique_ptr<MWPhysics::RagdollHost> host
            = MWPhysics::RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        ASSERT_NE(host, nullptr) << error;

        const std::vector<JPH::BodyID> before = world.allBodies();
        // Feet 0.05 m above the floor.
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.05 * kUnits), 0.0f, &error)) << error;
        const std::vector<JPH::BodyID> ragdollBodies = world.bodiesAddedSince(before);
        ASSERT_FALSE(ragdollBodies.empty());

        std::vector<JPH::RVec3> previous(ragdollBodies.size());
        for (std::size_t i = 0; i < ragdollBodies.size(); ++i)
            previous[i] = world.bodies().GetCenterOfMassPosition(ragdollBodies[i]);

        double lowestEver = 1.0e30;
        double jitterMetres = 0.0; // largest single-step movement of any body, over the last 60 steps
        bool anyNotFinite = false;
        constexpr int kSteps = 600;

        for (int step = 1; step <= kSteps; ++step)
        {
            host->beginStep();
            world.update();
            host->endStep();

            double stepMax = 0.0;
            for (std::size_t i = 0; i < ragdollBodies.size(); ++i)
            {
                const JPH::RVec3 now = world.bodies().GetCenterOfMassPosition(ragdollBodies[i]);
                stepMax = std::max(stepMax, double((now - previous[i]).Length()) / kUnits);
                previous[i] = now;
            }
            if (step > kSteps - 60)
                jitterMetres = std::max(jitterMetres, stepMax);

            const MWPhysics::RagdollStatus status = host->status();
            if (!std::isfinite(status.lowestZ) || !std::isfinite(status.highestZ)
                || !std::isfinite(status.maxSpeedMetresPerSecond))
                anyNotFinite = true;
            lowestEver = std::min(lowestEver, status.lowestZ / kUnits);

            if (step % 30 == 0)
            {
                // Awake bodies in the whole system (the floor is static and never counts).
                const unsigned awake = world.system().GetNumActiveBodies(JPH::EBodyType::RigidBody);
                std::cout << "[ragdoll scale] t=" << step / 60.0 << " s lowestZ=" << status.lowestZ / kUnits
                          << " m highestZ=" << status.highestZ / kUnits
                          << " m maxSpeed=" << status.maxSpeedMetresPerSecond << " m/s awakeBodies=" << awake << "/"
                          << status.bodies << " allAsleep=" << status.allAsleep << "\n";
            }
        }

        const MWPhysics::RagdollStatus last = host->status();
        std::cout << "[ragdoll scale] lowest centre ever=" << lowestEver
                  << " m; max movement of any body in one step over the last 60 steps=" << jitterMetres << " m ("
                  << jitterMetres * 60.0 << " m/s equivalent)\n";

        EXPECT_FALSE(anyNotFinite);
        ASSERT_TRUE(last.present);
        EXPECT_GT(lowestEver, -0.25); // never well below the floor (floor top is z = 0)
        EXPECT_LT(last.maxSpeedMetresPerSecond, 0.5f);
    }
}
