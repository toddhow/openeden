// Tests for CSMPhysics::PushScene: the push sandbox's model (task 0065, stage A). Written from the spec alone, before
// the implementation exists (the stub answers "nothing", so every test starts red).
//
// In plain words: a PushScene builds a ragdoll from a rig and a profile in the editor's one Jolt world (through
// StudioWorld::cmWorld()), keeps a name for each body's bone, and lets the user queue an impulse on a bone. The impulse
// is applied once, just before the next step (never inside a step), so it works while Paused (the next Step) and while
// Playing (the next tick). Reset puts the ragdoll back as it was built. Everything is main thread and fixed size.
//
// The open questions of task 0065 are answered as the stage log says: the scene is rig-agnostic (it takes any rig and
// profile), a bone is chosen by name (a list or a handle click supplies the name), and a push is also allowed while
// Playing.

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/physics/pushscene.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include "apps/opencs_tests/model/module/creativemotion/cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        namespace rig = opas::rig;
        namespace math = opas::math;
        using opas::physics::BodyState;

        constexpr float kUnits = StudioWorld::sUnitsPerMeter;

        // A slab whose top face is at z = 0 (units).
        JPH::BodyID addSlab(StudioWorld& world)
        {
            return world.addStaticBox(
                JPH::RVec3(0, 0, -0.5 * kUnits), JPH::Vec3(20.0f * kUnits, 20.0f * kUnits, 0.5f * kUnits));
        }

        bool bitIdentical(double a, double b)
        {
            return std::memcmp(&a, &b, sizeof(double)) == 0;
        }

        // One free sphere on a bone "Pelvis", 10 kg, no joints: the impulse arithmetic has no joint to pass it on to.
        rig::Rig makeSingleBodyRig()
        {
            rig::Rig result;
            result.name = "single";
            result.skeleton.bones = { CMTest::makeBone("Pelvis", -1, { 0.0, 0.0, 0.0 }, math::Quat{}) };
            result.bodies = { CMTest::makeBody("Pelvis", rig::SphereShape{ 0.2 }, math::Transform{}, 10.0) };
            return result;
        }

        rig::Profile makeSingleProfile()
        {
            rig::Profile result;
            result.rig = "single";
            result.defaultMotor = rig::MotorGains{ 300.0, 30.0, 500.0 };
            result.defaultMaterial = rig::Material{ 0.8, 0.0, 0.05, 0.05 };
            return result;
        }

        // Far above the slab: nothing touches anything.
        PushScene::Placement highUp()
        {
            return PushScene::Placement{ math::Vec3{ 0.0, 0.0, 50.0 }, 0.0 };
        }

        // The four-body rig of the module tests, standing a little above the slab.
        PushScene::Placement aboveTheSlab()
        {
            return PushScene::Placement{ math::Vec3{ 0.0, 0.0, 0.6 }, 0.0 };
        }

        std::unique_ptr<PushScene> makeScene(
            StudioWorld& world, const rig::Rig& r, const rig::Profile& p, const PushScene::Placement& placement)
        {
            std::string error;
            std::unique_ptr<PushScene> scene = PushScene::create(world, r, p, placement, &error);
            EXPECT_NE(scene, nullptr) << error;
            return scene;
        }

        BodyState stateOf(const PushScene& scene, const std::string& bone)
        {
            BodyState state;
            EXPECT_TRUE(scene.boneState(bone, state)) << bone;
            return state;
        }

        // Applies pending pushes the way a host that steps by hand would, then steps.
        void step(StudioWorld& world, int count = 1)
        {
            for (int i = 0; i < count; ++i)
                world.stepOnce();
        }

        TEST(CSMPhysicsPushScene, buildsOneBodyPerRigBodyInRigOrderAndNamesTheirBones)
        {
            StudioWorld world;
            addSlab(world);
            const std::uint32_t before = world.system().GetNumBodies();
            const rig::Rig r = CMTest::makeLimitsRig();
            const std::unique_ptr<PushScene> scene = makeScene(world, r, CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);

            const std::vector<std::string> expected{ "Pelvis", "Spine", "LeftThigh", "LeftShin" };
            EXPECT_EQ(scene->boneNames(), expected);
            EXPECT_EQ(world.system().GetNumBodies(), before + 4u);
            EXPECT_TRUE(scene->present());
            EXPECT_TRUE(scene->hasBone("LeftShin"));
            EXPECT_FALSE(scene->hasBone("Head"));
            EXPECT_DOUBLE_EQ(scene->boneMassKg("Spine"), 4.0);
            EXPECT_DOUBLE_EQ(scene->boneMassKg("Head"), 0.0);
            // bodyCount() counts only the world's own bodies, as for every body made through cmWorld().
            EXPECT_EQ(world.bodyCount(), 1u);
        }

        TEST(CSMPhysicsPushScene, anInvalidRigIsRefusedWithAMessageAndLeavesNothingInTheWorld)
        {
            StudioWorld world;
            rig::Rig broken = CMTest::makeLimitsRig();
            broken.bodies[1].massKg = -1.0;
            std::string error;
            const std::unique_ptr<PushScene> scene
                = PushScene::create(world, broken, CMTest::makeProfile(), aboveTheSlab(), &error);
            EXPECT_EQ(scene, nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(world.system().GetNumBodies(), 0u);
        }

        TEST(CSMPhysicsPushScene, aProfileForAnotherRigIsRefused)
        {
            StudioWorld world;
            rig::Profile other = CMTest::makeProfile();
            other.rig = "some-other-rig";
            std::string error;
            const std::unique_ptr<PushScene> scene
                = PushScene::create(world, CMTest::makeLimitsRig(), other, aboveTheSlab(), &error);
            EXPECT_EQ(scene, nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(world.system().GetNumBodies(), 0u);
        }

        TEST(CSMPhysicsPushScene, theRagdollFallsAndRestsOnTheStaticSlab)
        {
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            const double startZ = stateOf(*scene, "Pelvis").positionM.z;

            step(world, 600);

            for (const std::string& bone : scene->boneNames())
            {
                const BodyState state = stateOf(*scene, bone);
                EXPECT_GT(state.positionM.z, 0.0) << bone << " went through the slab";
                EXPECT_LT(state.positionM.z, startZ + 0.05) << bone << " rose";
            }
            EXPECT_LT(stateOf(*scene, "Pelvis").positionM.z, startZ - 0.2) << "the pelvis did not fall";
        }

        // The impulse arithmetic, on a free body far from everything.
        TEST(CSMPhysicsPushScene, aPushChangesTheBodysVelocityByImpulseOverMassAfterTheNextStepAndNotBefore)
        {
            const rig::Rig r = makeSingleBodyRig();
            const rig::Profile p = makeSingleProfile();

            StudioWorld control;
            const std::unique_ptr<PushScene> controlScene = makeScene(control, r, p, highUp());
            StudioWorld pushed;
            const std::unique_ptr<PushScene> scene = makeScene(pushed, r, p, highUp());
            ASSERT_NE(controlScene, nullptr);
            ASSERT_NE(scene, nullptr);
            step(control, 5);
            step(pushed, 5);

            const BodyState beforePush = stateOf(*scene, "Pelvis");
            const math::Vec3 point{ beforePush.positionM.x, beforePush.positionM.y, beforePush.positionM.z };
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 30.0, 0.0, 0.0 }, point));
            EXPECT_EQ(scene->pending(), 1u);

            // Not before the step: the velocity is the control's.
            EXPECT_DOUBLE_EQ(stateOf(*scene, "Pelvis").linearVelocityMps.x, beforePush.linearVelocityMps.x);

            step(control, 1);
            step(pushed, 1);
            EXPECT_EQ(scene->pending(), 0u);

            const BodyState a = stateOf(*scene, "Pelvis");
            const BodyState b = stateOf(*controlScene, "Pelvis");
            // 30 N s on 10 kg is 3 m/s along x; gravity and damping act on both bodies alike.
            EXPECT_NEAR(a.linearVelocityMps.x - b.linearVelocityMps.x, 3.0, 0.01);
            EXPECT_NEAR(a.linearVelocityMps.y - b.linearVelocityMps.y, 0.0, 1e-4);
            EXPECT_NEAR(a.linearVelocityMps.z - b.linearVelocityMps.z, 0.0, 1e-4);
        }

        TEST(CSMPhysicsPushScene, aPushIsAppliedOnceNotOnEveryStep)
        {
            const rig::Rig r = makeSingleBodyRig();
            const rig::Profile p = makeSingleProfile();
            StudioWorld control;
            const std::unique_ptr<PushScene> controlScene = makeScene(control, r, p, highUp());
            StudioWorld pushed;
            const std::unique_ptr<PushScene> scene = makeScene(pushed, r, p, highUp());
            ASSERT_NE(scene, nullptr);
            ASSERT_NE(controlScene, nullptr);

            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            step(pushed, 1);
            step(control, 1);
            const double afterOne
                = stateOf(*scene, "Pelvis").linearVelocityMps.x - stateOf(*controlScene, "Pelvis").linearVelocityMps.x;
            step(pushed, 30);
            step(control, 30);
            const double afterThirtyOne
                = stateOf(*scene, "Pelvis").linearVelocityMps.x - stateOf(*controlScene, "Pelvis").linearVelocityMps.x;

            EXPECT_NEAR(afterOne, 1.0, 0.01);
            // Damping may shave a little off, but a second application would double it.
            EXPECT_GT(afterThirtyOne, 0.5);
            EXPECT_LT(afterThirtyOne, 1.01);
            EXPECT_TRUE(scene->lastApplied().has_value());
        }

        TEST(CSMPhysicsPushScene, aPushOffCentreAlsoSpinsTheBody)
        {
            const rig::Rig r = makeSingleBodyRig();
            const rig::Profile p = makeSingleProfile();
            StudioWorld world;
            const std::unique_ptr<PushScene> scene = makeScene(world, r, p, highUp());
            ASSERT_NE(scene, nullptr);
            const BodyState at = stateOf(*scene, "Pelvis");

            // A push along +x at 0.2 m above the centre turns the body about the y axis.
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 20.0, 0.0, 0.0 },
                math::Vec3{ at.positionM.x, at.positionM.y, at.positionM.z + 0.2 }));
            step(world, 1);
            const BodyState after = stateOf(*scene, "Pelvis");
            EXPECT_NEAR(after.linearVelocityMps.x, 2.0, 0.01);
            EXPECT_GT(std::abs(after.angularVelocityRadps.y), 0.1);
        }

        TEST(CSMPhysicsPushScene, aPushOnTheRagdollsPelvisMovesIt)
        {
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            step(world, 600); // let it settle
            const BodyState rested = stateOf(*scene, "Pelvis");

            const math::Vec3 point{ rested.positionM.x, rested.positionM.y, rested.positionM.z };
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 400.0, 0.0, 0.0 }, point));
            step(world, 1);
            EXPECT_GT(stateOf(*scene, "Pelvis").linearVelocityMps.x, 5.0)
                << "400 N s on 5 kg, less what the joints take";

            step(world, 30);
            EXPECT_GT(stateOf(*scene, "Pelvis").positionM.x - rested.positionM.x, 0.1);
        }

        TEST(CSMPhysicsPushScene, aSleepingRagdollWakesWhenPushed)
        {
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            step(world, 1500);
            bool allAsleep = true;
            for (const std::string& bone : scene->boneNames())
                allAsleep = allAsleep && !stateOf(*scene, bone).awake;
            ASSERT_TRUE(allAsleep) << "the settled ragdoll should be asleep before it is pushed";

            const BodyState rested = stateOf(*scene, "Pelvis");
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 0.0, 300.0, 0.0 },
                math::Vec3{ rested.positionM.x, rested.positionM.y, rested.positionM.z }));
            step(world, 1);
            EXPECT_TRUE(stateOf(*scene, "Pelvis").awake);
            EXPECT_GT(stateOf(*scene, "Pelvis").linearVelocityMps.y, 1.0);
        }

        TEST(CSMPhysicsPushScene, whilePlayingAPushIsAppliedAtTheStartOfTheNextTick)
        {
            const rig::Rig r = makeSingleBodyRig();
            const rig::Profile p = makeSingleProfile();
            StudioWorld world;
            const std::unique_ptr<PushScene> scene = makeScene(world, r, p, highUp());
            ASSERT_NE(scene, nullptr);
            world.play();
            ASSERT_TRUE(world.tick());

            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            EXPECT_EQ(scene->pending(), 1u);
            EXPECT_TRUE(world.tick());
            EXPECT_EQ(scene->pending(), 0u);
            EXPECT_GT(stateOf(*scene, "Pelvis").linearVelocityMps.x, 0.9);
        }

        TEST(CSMPhysicsPushScene, whilePausedNothingHappensUntilTheNextStep)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> scene
                = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(scene, nullptr);
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            EXPECT_FALSE(world.tick()); // paused: no step
            EXPECT_EQ(scene->pending(), 1u);
            EXPECT_DOUBLE_EQ(stateOf(*scene, "Pelvis").linearVelocityMps.x, 0.0);
            world.stepOnce(); // the Step button
            EXPECT_EQ(scene->pending(), 0u);
            EXPECT_GT(stateOf(*scene, "Pelvis").linearVelocityMps.x, 0.9);
        }

        TEST(CSMPhysicsPushScene, pushesWaitInOrderAndAreAllAppliedByTheNextStep)
        {
            const rig::Rig r = makeSingleBodyRig();
            const rig::Profile p = makeSingleProfile();
            StudioWorld world;
            const std::unique_ptr<PushScene> scene = makeScene(world, r, p, highUp());
            ASSERT_NE(scene, nullptr);
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 0.0, 20.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            EXPECT_EQ(scene->pending(), 2u);
            world.stepOnce();
            EXPECT_EQ(scene->pending(), 0u);
            const BodyState s = stateOf(*scene, "Pelvis");
            EXPECT_NEAR(s.linearVelocityMps.x, 1.0, 0.01);
            EXPECT_NEAR(s.linearVelocityMps.y, 2.0, 0.01);
            // The last one applied is remembered for the arrow.
            ASSERT_TRUE(scene->lastApplied().has_value());
            EXPECT_EQ(scene->lastApplied()->body, 0u);
            EXPECT_DOUBLE_EQ(scene->lastApplied()->impulseNs.y, 20.0);
            EXPECT_EQ(scene->lastApplied()->stepIndex, 0u) << "applied before the first step ran";
        }

        TEST(CSMPhysicsPushScene, badPushesAreRefusedAndQueueNothing)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> scene
                = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(scene, nullptr);
            const math::Vec3 ok{ 1.0, 0.0, 0.0 };
            const math::Vec3 point{ 0.0, 0.0, 50.0 };
            const double nan = std::numeric_limits<double>::quiet_NaN();
            const double inf = std::numeric_limits<double>::infinity();

            EXPECT_FALSE(scene->push("Head", ok, point)); // no such bone
            EXPECT_FALSE(scene->push("", ok, point));
            EXPECT_FALSE(scene->push("Pelvis", math::Vec3{ nan, 0.0, 0.0 }, point));
            EXPECT_FALSE(scene->push("Pelvis", math::Vec3{ 0.0, inf, 0.0 }, point));
            EXPECT_FALSE(scene->push("Pelvis", ok, math::Vec3{ 0.0, nan, 0.0 }));
            EXPECT_FALSE(scene->push("Pelvis", math::Vec3{ PushScene::sMaxImpulseNs * 2.0, 0.0, 0.0 }, point));
            EXPECT_EQ(scene->pending(), 0u);
            // The largest accepted impulse is accepted.
            EXPECT_TRUE(scene->push("Pelvis", math::Vec3{ PushScene::sMaxImpulseNs, 0.0, 0.0 }, point));
            EXPECT_EQ(scene->pending(), 1u);
        }

        TEST(CSMPhysicsPushScene, theQueueHoldsAFixedNumberOfPushesAndRefusesTheNext)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> scene
                = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(scene, nullptr);
            for (std::size_t i = 0; i < PushScene::sMaxPending; ++i)
            {
                EXPECT_TRUE(scene->push("Pelvis", math::Vec3{ 1.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 })) << i;
            }
            EXPECT_FALSE(scene->push("Pelvis", math::Vec3{ 1.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            EXPECT_EQ(scene->pending(), PushScene::sMaxPending);
            world.stepOnce();
            EXPECT_EQ(scene->pending(), 0u);
            EXPECT_TRUE(scene->push("Pelvis", math::Vec3{ 1.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
        }

        TEST(CSMPhysicsPushScene, resetPutsTheRagdollBackAndDropsQueuedPushes)
        {
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            const std::uint32_t built = world.system().GetNumBodies();
            const BodyState start = stateOf(*scene, "Pelvis");

            step(world, 300);
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 50.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 0.5 }));
            ASSERT_NE(scene->pending(), 0u);

            std::string error;
            ASSERT_TRUE(scene->reset(&error)) << error;

            EXPECT_EQ(scene->pending(), 0u);
            EXPECT_FALSE(scene->lastApplied().has_value());
            EXPECT_EQ(world.system().GetNumBodies(), built) << "reset must not leak or lose bodies";
            const BodyState again = stateOf(*scene, "Pelvis");
            EXPECT_DOUBLE_EQ(again.positionM.z, start.positionM.z);
            EXPECT_DOUBLE_EQ(again.positionM.x, start.positionM.x);
            EXPECT_DOUBLE_EQ(again.linearVelocityMps.z, 0.0);
            // And a step does not apply the dropped push.
            step(world, 1);
            EXPECT_LT(std::abs(stateOf(*scene, "Pelvis").linearVelocityMps.x), 0.01);
        }

        TEST(CSMPhysicsPushScene, destroyingTheSceneLeavesTheWorldAsItFoundIt)
        {
            StudioWorld world;
            addSlab(world);
            const std::uint32_t before = world.system().GetNumBodies();
            {
                const std::unique_ptr<PushScene> scene
                    = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
                ASSERT_NE(scene, nullptr);
                EXPECT_EQ(world.system().GetNumBodies(), before + 4u);
                step(world, 10);
            }
            EXPECT_EQ(world.system().GetNumBodies(), before);
            // The hook is gone with the scene: stepping must not call it (it would be a dangling pointer).
            step(world, 3);
        }

        TEST(CSMPhysicsPushScene, afterTheWorldIsClearedThereIsNoRagdollUntilResetBuildsOneAgain)
        {
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 5.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 1.0 }));

            world.clear();
            EXPECT_EQ(world.system().GetNumBodies(), 0u);
            BodyState state;
            EXPECT_FALSE(scene->boneState("Pelvis", state)) << "the handles are stale";
            step(world, 2); // the pending push must not crash on stale handles

            ASSERT_TRUE(scene->reset());
            EXPECT_EQ(world.system().GetNumBodies(), 4u);
            EXPECT_TRUE(scene->boneState("Pelvis", state));
            EXPECT_TRUE(scene->present());
        }

        // Positions of every body, in rig order, after a scripted scenario.
        std::vector<math::Vec3> runScenario()
        {
            StudioWorld world;
            addSlab(world);
            std::string error;
            const std::unique_ptr<PushScene> scene
                = PushScene::create(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab(), &error);
            EXPECT_NE(scene, nullptr) << error;
            std::vector<math::Vec3> positions;
            if (scene == nullptr)
                return positions;
            for (int i = 0; i < 240; ++i)
            {
                if (i == 100)
                    scene->push("Spine", math::Vec3{ 120.0, -40.0, 30.0 }, math::Vec3{ 0.0, 0.0, 0.9 });
                world.stepOnce();
            }
            for (const std::string& bone : scene->boneNames())
            {
                BodyState s;
                scene->boneState(bone, s);
                positions.push_back(math::Vec3{ s.positionM.x, s.positionM.y, s.positionM.z });
            }
            return positions;
        }

        TEST(CSMPhysicsPushScene, twoRunsOfTheSameScenarioGiveBitIdenticalPositions)
        {
            const std::vector<math::Vec3> first = runScenario();
            const std::vector<math::Vec3> second = runScenario();
            ASSERT_EQ(first.size(), 4u);
            ASSERT_EQ(second.size(), 4u);
            for (std::size_t i = 0; i < first.size(); ++i)
            {
                EXPECT_TRUE(bitIdentical(first[i].x, second[i].x)) << i;
                EXPECT_TRUE(bitIdentical(first[i].y, second[i].y)) << i;
                EXPECT_TRUE(bitIdentical(first[i].z, second[i].z)) << i;
            }
        }

        TEST(CSMPhysicsPushScene, thePushChangesTheOutcomeComparedWithNoPush)
        {
            const std::vector<math::Vec3> pushed = runScenario();
            StudioWorld world;
            addSlab(world);
            const std::unique_ptr<PushScene> scene
                = makeScene(world, CMTest::makeLimitsRig(), CMTest::makeProfile(), aboveTheSlab());
            ASSERT_NE(scene, nullptr);
            step(world, 240);
            const BodyState unpushed = stateOf(*scene, "Spine");
            const double moved
                = std::abs(unpushed.positionM.x - pushed[1].x) + std::abs(unpushed.positionM.y - pushed[1].y);
            EXPECT_GT(moved, 0.05);
        }

        TEST(CSMPhysicsPushScene, twoScenesCanShareOneWorldAndEachOnlyPushesItsOwnBodies)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> a = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            const std::unique_ptr<PushScene> b = makeScene(world, makeSingleBodyRig(), makeSingleProfile(),
                PushScene::Placement{ math::Vec3{ 10.0, 0.0, 50.0 }, 0.0 });
            ASSERT_NE(a, nullptr);
            ASSERT_NE(b, nullptr);
            ASSERT_TRUE(a->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            world.stepOnce();
            EXPECT_GT(stateOf(*a, "Pelvis").linearVelocityMps.x, 0.9);
            EXPECT_LT(std::abs(stateOf(*b, "Pelvis").linearVelocityMps.x), 0.01);
        }

        // ---- review of 2026-11-09 (lane C: C3) ----------------------------------------------------------------------

        TEST(CSMPhysicsPushScene, pushesQueuedBeforeAClearNeverReachAnotherScenesBodiesBuiltAfterIt)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> a = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(a, nullptr);
            ASSERT_TRUE(a->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            world.clear();
            EXPECT_EQ(a->pending(), 0u) << "the ragdoll is gone";
            const std::unique_ptr<PushScene> b = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(b, nullptr);
            EXPECT_EQ(a->pending(), 0u) << "a new scene in the same world must not revive the old scene's queue";
            EXPECT_FALSE(a->present());
            world.stepOnce();
            EXPECT_LT(std::abs(stateOf(*b, "Pelvis").linearVelocityMps.x), 0.01)
                << "a push queued for a scene whose bodies were cleared was applied to another scene's body";
            EXPECT_FALSE(a->lastApplied().has_value());
        }

        TEST(CSMPhysicsPushScene, aClearForgetsTheLastAppliedPushAtTheNextStep)
        {
            StudioWorld world;
            const std::unique_ptr<PushScene> scene
                = makeScene(world, makeSingleBodyRig(), makeSingleProfile(), highUp());
            ASSERT_NE(scene, nullptr);
            ASSERT_TRUE(scene->push("Pelvis", math::Vec3{ 10.0, 0.0, 0.0 }, math::Vec3{ 0.0, 0.0, 50.0 }));
            world.stepOnce();
            ASSERT_TRUE(scene->lastApplied().has_value());
            world.clear();
            world.stepOnce();
            EXPECT_FALSE(scene->lastApplied().has_value()) << "an arrow for a ragdoll that no longer exists";
        }
    }
}
