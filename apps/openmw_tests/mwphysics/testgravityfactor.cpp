// Tests for HostedWorldConfig::gravityFactor, stage b of task 0035 ("Gravity", in "Stage b as decided").
//
// In plain words: the game sets Jolt's gravity to point UP (+Z) because its actors fall by their own code. A ragdoll
// must still fall DOWN, so every dynamic body the adapter makes gets Jolt's per-body "gravity factor", and the game
// passes -1. A factor of 1 (the default, used by the editor) means ordinary gravity. A factor that is not a finite
// number is refused when the adapter is created.

#include "ragdolltestworld.hpp"

#include <components/cmphysics/hostedjoltworld.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

namespace
{
    using namespace MWPhysicsTest;

    // The game-like setup the spec describes: metres-to-units 69.99..., ragdoll layer, no static layer, game layers.
    CMPhysics::HostedWorldConfig makeGameConfig(float gravityFactor)
    {
        CMPhysics::HostedWorldConfig config;
        config.unitsPerMeter = Constants::UnitsPerMeter;
        config.stepSeconds = kStepSeconds;
        config.movingLayer = MWPhysics::Layers::RAGDOLL;
        config.staticLayer.reset();
        config.gravityFactor = gravityFactor;
        return config;
    }

    // Drops a 0.5 m sphere from 100 m up, runs 60 steps of 1/60 s (one second) and returns how many metres it fell
    // (positive = down), as reported by readBody (metres).
    double metresFallenInOneSecond(GameLikeWorld& world, float gravityFactor)
    {
        std::string error;
        std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
            = CMPhysics::HostedJoltWorld::create(world.system(), makeGameConfig(gravityFactor), &error);
        EXPECT_NE(adapter, nullptr) << error;
        if (adapter == nullptr)
            return 0.0;

        opas::physics::BodyDesc desc;
        desc.shape = opas::physics::SphereShape{ 0.5f };
        desc.positionM = opas::physics::Vec3{ 0.0f, 0.0f, 100.0f };
        const auto created = adapter->createBody(desc);
        EXPECT_TRUE(created.ok()) << created.message;
        if (!created.ok())
            return 0.0;

        opas::physics::BodyState before;
        EXPECT_TRUE(adapter->readBody(created.id, before));
        for (int i = 0; i < 60; ++i)
        {
            adapter->beginStep();
            world.update();
            adapter->endStep();
        }
        opas::physics::BodyState after;
        EXPECT_TRUE(adapter->readBody(created.id, after));
        return static_cast<double>(before.positionM.z) - static_cast<double>(after.positionM.z);
    }

    // In one second from rest a body falls 0.5 * g * 1^2 = 4.48 metres (the same as 0.5 * 8.96 * 70 units), within
    // 15 percent (damping and Jolt's integration take a little).
    constexpr double kExpectedMetres = 0.5 * Constants::GravityConst;

    TEST(GravityFactor, DefaultsToOne)
    {
        const CMPhysics::HostedWorldConfig config;
        EXPECT_EQ(config.gravityFactor, 1.0f);
    }

    TEST(GravityFactor, MinusOneMakesABodyFallDownInTheGamesUpwardGravity)
    {
        // The game's world: gravity points +Z. With factor -1 the sphere still falls down.
        GameLikeWorld world(false);
        const double fallen = metresFallenInOneSecond(world, -1.0f);
        EXPECT_NEAR(fallen, kExpectedMetres, kExpectedMetres * 0.15);
    }

    TEST(GravityFactor, OneMakesABodyFallDownInAWorldWithDownwardGravity)
    {
        // The editor's world: gravity points -Z, factor 1: the same fall.
        GameLikeWorld world(false, JPH::Vec3(0.0f, 0.0f, -Constants::GravityConst * Constants::UnitsPerMeter));
        const double fallen = metresFallenInOneSecond(world, 1.0f);
        EXPECT_NEAR(fallen, kExpectedMetres, kExpectedMetres * 0.15);
    }

    TEST(GravityFactor, NonFiniteFactorsAreRefusedWithAMessage)
    {
        GameLikeWorld world(false);
        const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity() };
        for (const float factor : bad)
        {
            std::string error;
            const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
                = CMPhysics::HostedJoltWorld::create(world.system(), makeGameConfig(factor), &error);
            EXPECT_EQ(adapter, nullptr) << factor;
            EXPECT_FALSE(error.empty()) << factor;
        }
    }
}
