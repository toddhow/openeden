// Tests for StudioWorld::cmWorld(): CreativeMotion's physics interface over Eden Studio's own Jolt world.
// Written from the spec alone, before the implementation exists.
//
// In plain words: the editor's world is measured in Morrowind units (about 70 per metre), but CreativeMotion thinks in
// metres. cmWorld() hands out an adapter that converts at the border: a body created at 5 m high sits 350 units high
// in the Jolt system, and reads back as 5 m. The adapter also joins the editor's fixed step (listeners run around the
// system's Update, observers around the listeners), and StudioWorld::clear() sweeps up whatever was made through it.

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/physics/studioworld.hpp"

#include <opas/physics/PhysicsWorld.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        using opas::physics::BodyDesc;
        using opas::physics::BodyId;
        using opas::physics::BodyState;
        using opas::physics::CreateResult;
        using opas::physics::StepListener;
        using opas::physics::Vec3;

        constexpr float kUnits = StudioWorld::sUnitsPerMeter;
        constexpr float kGravityMps2 = 8.96f; // Constants::GravityConst, Morrowind's value

        // A slab whose top face is at z = 0 (units).
        JPH::BodyID addSlab(StudioWorld& world)
        {
            return world.addStaticBox(
                JPH::RVec3(0, 0, -0.5 * kUnits), JPH::Vec3(20.0f * kUnits, 20.0f * kUnits, 0.5f * kUnits));
        }

        BodyDesc sphereAt(Vec3 positionM, float radiusM = 0.5f)
        {
            BodyDesc desc;
            desc.shape = opas::physics::SphereShape{ radiusM };
            desc.positionM = positionM;
            return desc;
        }

        std::vector<JPH::BodyID> allBodies(StudioWorld& world)
        {
            JPH::BodyIDVector ids;
            world.system().GetBodies(ids);
            return std::vector<JPH::BodyID>(ids.begin(), ids.end());
        }

        TEST(CSMPhysicsStudioWorldCm, cmWorldIsOneObjectPerStudioWorld)
        {
            StudioWorld world;
            opas::physics::PhysicsWorld& first = world.cmWorld();
            opas::physics::PhysicsWorld& second = world.cmWorld();
            EXPECT_EQ(&first, &second);
            EXPECT_EQ(first.fixedStepSeconds(), StudioWorld::sStepSeconds);
        }

        TEST(CSMPhysicsStudioWorldCm, askingForTheAdapterScalesTheSystemTolerances)
        {
            StudioWorld world;
            world.cmWorld();
            const JPH::PhysicsSettings settings = world.system().GetPhysicsSettings();
            EXPECT_NEAR(settings.mPointVelocitySleepThreshold, 0.03f * kUnits, 1e-4f);
            EXPECT_EQ(settings.mPenetrationSlop, 0.02f); // not scaled: how deep bodies may rest in each other
        }

        TEST(CSMPhysicsStudioWorldCm, aBodyCreatedInMetersSitsInUnitsInTheSystem)
        {
            StudioWorld world;
            const std::vector<JPH::BodyID> before = allBodies(world);
            const CreateResult<BodyId> created = world.cmWorld().createBody(sphereAt(Vec3{ 1.0f, 2.0f, 3.0f }));
            ASSERT_TRUE(created.ok()) << created.message;

            const std::vector<JPH::BodyID> after = allBodies(world);
            ASSERT_EQ(after.size(), before.size() + 1);
            JPH::BodyID joltId;
            for (const JPH::BodyID id : after)
            {
                if (std::find(before.begin(), before.end(), id) == before.end())
                    joltId = id;
            }
            ASSERT_FALSE(joltId.IsInvalid());

            const JPH::RVec3 position = world.system().GetBodyInterface().GetCenterOfMassPosition(joltId);
            EXPECT_NEAR(position.GetX(), 1.0 * kUnits, 1e-2);
            EXPECT_NEAR(position.GetY(), 2.0 * kUnits, 1e-2);
            EXPECT_NEAR(position.GetZ(), 3.0 * kUnits, 1e-2);

            BodyState state;
            ASSERT_TRUE(world.cmWorld().readBody(created.id, state));
            EXPECT_NEAR(state.positionM.x, 1.0f, 1e-4f);
            EXPECT_NEAR(state.positionM.y, 2.0f, 1e-4f);
            EXPECT_NEAR(state.positionM.z, 3.0f, 1e-4f);
        }

        TEST(CSMPhysicsStudioWorldCm, aSphereDroppedFromFiveMetersRestsOnTheStaticSlab)
        {
            StudioWorld world;
            addSlab(world);
            const CreateResult<BodyId> created = world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 5.0f }, 0.5f));
            ASSERT_TRUE(created.ok()) << created.message;

            for (int i = 0; i < 360; ++i)
                world.stepOnce();

            BodyState state;
            ASSERT_TRUE(world.cmWorld().readBody(created.id, state));
            // Radius 0.5 m on a surface at z = 0: the centre rests at 0.5 m (a little tolerance for contact softness).
            EXPECT_NEAR(state.positionM.z, 0.5f, 0.05f);
        }

        TEST(CSMPhysicsStudioWorldCm, aFreeBodyFallsWithMorrowindGravity)
        {
            StudioWorld world;
            const CreateResult<BodyId> created = world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 20.0f }, 0.3f));
            ASSERT_TRUE(created.ok()) << created.message;

            for (int i = 0; i < 60; ++i)
                world.stepOnce();

            BodyState state;
            ASSERT_TRUE(world.cmWorld().readBody(created.id, state));
            const double fallenM = 20.0 - state.positionM.z;
            const double expectedM = 0.5 * kGravityMps2;
            EXPECT_NEAR(fallenM, expectedM, 0.15 * expectedM);
        }

        TEST(CSMPhysicsStudioWorldCm, bodyCountCountsOnlyTheBodiesStudioWorldAdded)
        {
            StudioWorld world;
            addSlab(world);
            EXPECT_EQ(world.bodyCount(), 1u);
            ASSERT_TRUE(world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 3.0f })).ok());
            ASSERT_TRUE(world.cmWorld().createBody(sphereAt(Vec3{ 2.0f, 0.0f, 3.0f })).ok());
            EXPECT_EQ(world.bodyCount(), 1u);
            EXPECT_EQ(world.system().GetNumBodies(), 3u);
        }

        // Records the order of the calls around one step.
        class OrderRecorder final : public StudioWorld::StepObserver, public StepListener
        {
        public:
            explicit OrderRecorder(StudioWorld& world)
                : mWorld(world)
            {
            }

            void beforeStep(float dt) override
            {
                mLog.push_back("observer-before");
                mObserverDt.push_back(dt);
            }

            void afterStep(float dt) override
            {
                mLog.push_back("observer-after");
                mObserverDt.push_back(dt);
            }

            void onPreStep(opas::physics::PhysicsWorld& world, float dt) override
            {
                mLog.push_back("pre");
                mListenerDt.push_back(dt);
                BodyState state;
                if (world.readBody(mBody, state))
                    mPreZ.push_back(state.positionM.z);
            }

            void onPostStep(opas::physics::PhysicsWorld& world, float dt) override
            {
                mLog.push_back("post");
                mListenerDt.push_back(dt);
                BodyState state;
                if (world.readBody(mBody, state))
                    mPostZ.push_back(state.positionM.z);
            }

            StudioWorld& mWorld;
            BodyId mBody;
            std::vector<std::string> mLog;
            std::vector<float> mObserverDt, mListenerDt;
            std::vector<float> mPreZ, mPostZ;
        };

        TEST(CSMPhysicsStudioWorldCm, stepOnceCallsObserverListenersAndUpdateInOrder)
        {
            StudioWorld world;
            OrderRecorder recorder(world);
            const CreateResult<BodyId> created = world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 10.0f }));
            ASSERT_TRUE(created.ok()) << created.message;
            recorder.mBody = created.id;
            ASSERT_TRUE(world.cmWorld().addStepListener(recorder).isValid());
            world.setStepObserver(&recorder);

            world.stepOnce();
            world.stepOnce();

            const std::vector<std::string> expected{ "observer-before", "pre", "post", "observer-after",
                "observer-before", "pre", "post", "observer-after" };
            EXPECT_EQ(recorder.mLog, expected);
            for (const float dt : recorder.mListenerDt)
                EXPECT_EQ(dt, StudioWorld::sStepSeconds);
            for (const float dt : recorder.mObserverDt)
                EXPECT_EQ(dt, StudioWorld::sStepSeconds);

            // The system's Update ran between the pre and the post listener: the body fell in between.
            ASSERT_EQ(recorder.mPreZ.size(), 2u);
            ASSERT_EQ(recorder.mPostZ.size(), 2u);
            EXPECT_LT(recorder.mPostZ[0], recorder.mPreZ[0]);
            EXPECT_LT(recorder.mPostZ[1], recorder.mPreZ[1]);

            world.setStepObserver(nullptr);
        }

        TEST(CSMPhysicsStudioWorldCm, tickInvokesListenersOncePerStepAndOnlyWhenPlaying)
        {
            StudioWorld world;
            OrderRecorder recorder(world);
            ASSERT_TRUE(world.cmWorld().addStepListener(recorder).isValid());

            EXPECT_FALSE(world.tick()); // paused
            EXPECT_TRUE(recorder.mLog.empty());

            world.play();
            EXPECT_TRUE(world.tick());
            const std::vector<std::string> expected{ "pre", "post" };
            EXPECT_EQ(recorder.mLog, expected);
            ASSERT_EQ(recorder.mListenerDt.size(), 2u);
            EXPECT_EQ(recorder.mListenerDt[0], StudioWorld::sStepSeconds);
            EXPECT_EQ(recorder.mListenerDt[1], StudioWorld::sStepSeconds);
        }

        TEST(CSMPhysicsStudioWorldCm, clearDestroysEverythingMadeThroughCmWorld)
        {
            StudioWorld world;
            addSlab(world);
            opas::physics::PhysicsWorld& cm = world.cmWorld();
            const CreateResult<BodyId> a = cm.createBody(sphereAt(Vec3{ 0.0f, 0.0f, 3.0f }));
            const CreateResult<BodyId> b = cm.createBody(sphereAt(Vec3{ 2.0f, 0.0f, 3.0f }));
            ASSERT_TRUE(a.ok()) << a.message;
            ASSERT_TRUE(b.ok()) << b.message;
            world.stepOnce();
            world.stepOnce();
            ASSERT_EQ(world.stepIndex(), 2u);

            world.clear();

            EXPECT_EQ(world.system().GetNumBodies(), 0u);
            EXPECT_EQ(world.bodyCount(), 0u);
            EXPECT_EQ(world.stepIndex(), 0u);
            BodyState state;
            EXPECT_FALSE(cm.readBody(a.id, state));
            EXPECT_FALSE(cm.readBody(b.id, state));
            EXPECT_FALSE(cm.destroyBody(a.id));
        }

        TEST(CSMPhysicsStudioWorldCm, theAdapterWorksAgainAfterClear)
        {
            StudioWorld world;
            const BodyId oldBody = world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 3.0f })).id;
            world.clear();

            const CreateResult<BodyId> fresh = world.cmWorld().createBody(sphereAt(Vec3{ 0.0f, 0.0f, 4.0f }));
            ASSERT_TRUE(fresh.ok()) << fresh.message;
            BodyState state;
            EXPECT_TRUE(world.cmWorld().readBody(fresh.id, state));
            EXPECT_NEAR(state.positionM.z, 4.0f, 1e-4f);
            EXPECT_FALSE(world.cmWorld().readBody(oldBody, state)); // still stale
            EXPECT_EQ(world.system().GetNumBodies(), 1u);
        }
    }
}
