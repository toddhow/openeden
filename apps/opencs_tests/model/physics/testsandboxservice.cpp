// Tests for CSMPhysics::SandboxService: Eden Studio's side of the Editor Module API 1.3 simulation sandbox (task 0065,
// stage B). Written from the spec alone, before the implementation existed (the stub answers "nothing", so every test
// started red).
//
// In plain words: a module hands Studio the PATHS of a rig and a profile; Studio builds the ragdoll in the editor's one
// physics world over a ground plane at z = 0, queues pushes on named bones and applies each once just before the next
// step, and lets the module read every body's pose, reset the ragdoll and release everything. Main thread only.

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/physics/sandboxservice.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include "apps/opencs_tests/model/module/creativemotion/cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        using openeden::editor::SandboxBodyPose;
        using openeden::editor::Vec3;

        bool bitIdentical(double a, double b)
        {
            return std::memcmp(&a, &b, sizeof(double)) == 0;
        }

        class SandboxServiceTest : public CMTest::TempDirTest
        {
        protected:
            void SetUp() override
            {
                CMTest::TempDirTest::SetUp();
                mRigPath = mDir / "test.rig.json";
                mProfilePath = mDir / "test.profile.json";
                CMTest::writeText(mRigPath, opas::rig::writeRig(CMTest::makeLimitsRig()));
                CMTest::writeText(mProfilePath, opas::rig::writeProfile(CMTest::makeProfile()));
            }

            bool build(SandboxService& service, double offsetZ = 0.6, double yaw = 0.0)
            {
                std::string error;
                const bool ok = service.build(mRigPath, mProfilePath, Vec3{ 0.0, 0.0, offsetZ }, yaw, error);
                EXPECT_TRUE(ok) << error;
                return ok;
            }

            static SandboxBodyPose pose(const SandboxService& service, const std::string& bone)
            {
                SandboxBodyPose result;
                EXPECT_TRUE(service.bodyPose(bone, result)) << bone;
                return result;
            }

            static void steps(StudioWorld& world, int count)
            {
                for (int i = 0; i < count; ++i)
                    world.stepOnce();
            }

            std::filesystem::path mRigPath;
            std::filesystem::path mProfilePath;
        };

        TEST_F(SandboxServiceTest, buildsTheRagdollFromTwoFilesAndNamesItsBodiesInRigOrder)
        {
            StudioWorld world;
            SandboxService service(world);
            EXPECT_FALSE(service.built());
            EXPECT_TRUE(service.bodyNames().empty());
            const std::uint32_t before = world.system().GetNumBodies();

            ASSERT_TRUE(build(service));
            EXPECT_TRUE(service.built());
            const std::vector<std::string> expected{ "Pelvis", "Spine", "LeftThigh", "LeftShin" };
            EXPECT_EQ(service.bodyNames(), expected);
            // Four bodies and the ground.
            EXPECT_EQ(world.system().GetNumBodies(), before + 5u);
            EXPECT_GT(pose(service, "Pelvis").positionM.z, 1.0);
        }

        TEST_F(SandboxServiceTest, aFileThatCannotBeReadIsRefusedWithAMessageAndLeavesNothing)
        {
            StudioWorld world;
            SandboxService service(world);
            const std::uint32_t before = world.system().GetNumBodies();
            std::string error;
            EXPECT_FALSE(service.build(mDir / "missing.rig.json", mProfilePath, Vec3{}, 0.0, error));
            EXPECT_NE(error.find("missing.rig.json"), std::string::npos) << error;
            EXPECT_FALSE(service.built());
            EXPECT_EQ(world.system().GetNumBodies(), before);

            error.clear();
            EXPECT_FALSE(service.build(mRigPath, mDir / "missing.profile.json", Vec3{}, 0.0, error));
            EXPECT_NE(error.find("missing.profile.json"), std::string::npos) << error;
            EXPECT_EQ(world.system().GetNumBodies(), before);
        }

        TEST_F(SandboxServiceTest, aProfileForAnotherRigIsRefusedAndLeavesNothingInTheWorld)
        {
            StudioWorld world;
            SandboxService service(world);
            opas::rig::Profile other = CMTest::makeProfile();
            other.rig = "another-rig";
            CMTest::writeText(mProfilePath, opas::rig::writeProfile(other));
            const std::uint32_t before = world.system().GetNumBodies();
            std::string error;
            EXPECT_FALSE(service.build(mRigPath, mProfilePath, Vec3{}, 0.0, error));
            EXPECT_FALSE(error.empty());
            EXPECT_FALSE(service.built());
            EXPECT_EQ(world.system().GetNumBodies(), before);
        }

        TEST_F(SandboxServiceTest, aNonFiniteOffsetIsRefused)
        {
            StudioWorld world;
            SandboxService service(world);
            std::string error;
            EXPECT_FALSE(service.build(
                mRigPath, mProfilePath, Vec3{ std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }, 0.0, error));
            EXPECT_FALSE(error.empty());
            EXPECT_FALSE(service.built());
        }

        TEST_F(SandboxServiceTest, theRagdollFallsAndRestsOnTheGroundAtZ0)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 0.3));
            steps(world, 600);
            for (const std::string& bone : service.bodyNames())
            {
                EXPECT_GT(pose(service, bone).positionM.z, 0.0) << bone;
            }
            // It came down: the pelvis started above 1.2 m.
            EXPECT_LT(pose(service, "Pelvis").positionM.z, 1.0);
        }

        TEST_F(SandboxServiceTest, aPushIsPendingUntilTheNextStepAndThenMovesTheBody)
        {
            StudioWorld controlWorld;
            SandboxService control(controlWorld);
            ASSERT_TRUE(build(control, 5.0));
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 5.0));

            const SandboxBodyPose start = pose(service, "Pelvis");
            ASSERT_TRUE(service.push("Pelvis", Vec3{ 40.0, 0.0, 0.0 }, start.positionM));
            EXPECT_EQ(service.pendingPushes(), 1u);
            EXPECT_TRUE(bitIdentical(pose(service, "Pelvis").positionM.x, start.positionM.x)) << "not before the step";

            steps(world, 30);
            steps(controlWorld, 30);
            EXPECT_EQ(service.pendingPushes(), 0u);
            const double pushedX = pose(service, "Pelvis").positionM.x;
            const double controlX = pose(control, "Pelvis").positionM.x;
            // 40 N s on a 5 kg body is 8 m/s at the start; half a second moves it by metres, less the joints' drag on
            // the rest of the ragdoll, so only a wide bound is asserted.
            EXPECT_GT(pushedX - controlX, 0.3);
        }

        TEST_F(SandboxServiceTest, aPushIsAppliedOnceNotOnEveryStep)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 5.0));
            ASSERT_TRUE(service.push("Pelvis", Vec3{ 40.0, 0.0, 0.0 }, pose(service, "Pelvis").positionM));
            steps(world, 1);
            EXPECT_EQ(service.pendingPushes(), 0u);
            ASSERT_NE(service.scene(), nullptr);
            ASSERT_TRUE(service.scene()->lastApplied().has_value());
            const std::uint64_t stepOfPush = service.scene()->lastApplied()->stepIndex;
            steps(world, 5);
            EXPECT_EQ(service.scene()->lastApplied()->stepIndex, stepOfPush);
        }

        TEST_F(SandboxServiceTest, badPushesQueueNothing)
        {
            StudioWorld world;
            SandboxService service(world);
            // Nothing built yet.
            EXPECT_FALSE(service.push("Pelvis", Vec3{ 1.0, 0.0, 0.0 }, Vec3{}));
            ASSERT_TRUE(build(service));
            EXPECT_FALSE(service.push("Head", Vec3{ 1.0, 0.0, 0.0 }, Vec3{}));
            EXPECT_FALSE(service.push("Pelvis", Vec3{ std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }, Vec3{}));
            EXPECT_FALSE(service.push(
                "Pelvis", Vec3{ 1.0, 0.0, 0.0 }, Vec3{ 0.0, std::numeric_limits<double>::infinity(), 0.0 }));
            EXPECT_FALSE(service.push("Pelvis", Vec3{ 1.0e9, 0.0, 0.0 }, Vec3{}));
            EXPECT_EQ(service.pendingPushes(), 0u);
        }

        TEST_F(SandboxServiceTest, resetPutsTheRagdollBackAndDropsPushesThatWait)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 2.0));
            const SandboxBodyPose start = pose(service, "LeftShin");
            steps(world, 60);
            ASSERT_TRUE(service.push("Spine", Vec3{ 10.0, 0.0, 0.0 }, pose(service, "Spine").positionM));
            EXPECT_EQ(service.pendingPushes(), 1u);
            std::string error;
            ASSERT_TRUE(service.reset(error)) << error;
            EXPECT_EQ(service.pendingPushes(), 0u);
            const SandboxBodyPose again = pose(service, "LeftShin");
            EXPECT_NEAR(again.positionM.x, start.positionM.x, 1.0e-6);
            EXPECT_NEAR(again.positionM.y, start.positionM.y, 1.0e-6);
            EXPECT_NEAR(again.positionM.z, start.positionM.z, 1.0e-6);
            // The step after a reset applies nothing.
            steps(world, 1);
            ASSERT_NE(service.scene(), nullptr);
            EXPECT_FALSE(service.scene()->lastApplied().has_value());
        }

        TEST_F(SandboxServiceTest, resetBeforeAnythingIsBuiltFailsWithAMessage)
        {
            StudioWorld world;
            SandboxService service(world);
            std::string error;
            EXPECT_FALSE(service.reset(error));
            EXPECT_FALSE(error.empty());
        }

        TEST_F(SandboxServiceTest, releaseAndDestructionLeaveTheWorldAsFound)
        {
            StudioWorld world;
            const std::uint32_t before = world.system().GetNumBodies();
            {
                SandboxService service(world);
                ASSERT_TRUE(build(service));
                service.release();
                EXPECT_FALSE(service.built());
                EXPECT_EQ(world.system().GetNumBodies(), before);
                service.release(); // safe twice
                EXPECT_FALSE(service.push("Pelvis", Vec3{ 1.0, 0.0, 0.0 }, Vec3{}));
                ASSERT_TRUE(build(service));
                EXPECT_EQ(world.system().GetNumBodies(), before + 5u);
            }
            EXPECT_EQ(world.system().GetNumBodies(), before);
            steps(world, 3); // the removed pre-step hook is not called
        }

        TEST_F(SandboxServiceTest, buildingAgainReplacesTheRagdollInsteadOfAddingAnother)
        {
            StudioWorld world;
            SandboxService service(world);
            const std::uint32_t before = world.system().GetNumBodies();
            ASSERT_TRUE(build(service));
            ASSERT_TRUE(build(service, 1.0));
            EXPECT_EQ(world.system().GetNumBodies(), before + 5u);
        }

        TEST_F(SandboxServiceTest, aFailedBuildAfterAGoodOneLeavesNothingBuilt)
        {
            StudioWorld world;
            SandboxService service(world);
            const std::uint32_t before = world.system().GetNumBodies();
            ASSERT_TRUE(build(service));
            std::string error;
            EXPECT_FALSE(service.build(mDir / "missing.rig.json", mProfilePath, Vec3{}, 0.0, error));
            EXPECT_FALSE(service.built());
            EXPECT_EQ(world.system().GetNumBodies(), before);
        }

        TEST_F(SandboxServiceTest, theOffsetAndTheYawPlaceTheRagdoll)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 1.0, 0.0));
            const SandboxBodyPose low = pose(service, "Pelvis");
            ASSERT_TRUE(build(service, 3.0, 0.0));
            EXPECT_NEAR(pose(service, "Pelvis").positionM.z - low.positionM.z, 2.0, 1.0e-6);
            ASSERT_TRUE(build(service, 1.0, 1.5707963267948966));
            const SandboxBodyPose turned = pose(service, "Pelvis");
            EXPECT_GT(
                std::abs(turned.positionM.x - low.positionM.x) + std::abs(turned.positionM.y - low.positionM.y), 0.1);
        }

        TEST_F(SandboxServiceTest, clearingTheWorldMakesItNotBuiltUntilBuiltAgain)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            world.clear();
            EXPECT_FALSE(service.built());
            EXPECT_FALSE(service.push("Pelvis", Vec3{ 1.0, 0.0, 0.0 }, Vec3{}));
            SandboxBodyPose unused;
            EXPECT_FALSE(service.bodyPose("Pelvis", unused));
            steps(world, 2);
            ASSERT_TRUE(build(service));
            EXPECT_TRUE(service.built());
        }

        TEST_F(SandboxServiceTest, bodyPoseOfAnUnknownBoneIsFalse)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            SandboxBodyPose unused;
            EXPECT_FALSE(service.bodyPose("Head", unused));
        }

        TEST_F(SandboxServiceTest, twoRunsOfTheSameScenarioGiveBitIdenticalPoses)
        {
            auto run = [this]() {
                StudioWorld world;
                SandboxService service(world);
                EXPECT_TRUE(build(service, 0.4));
                std::vector<double> numbers;
                for (int i = 0; i < 240; ++i)
                {
                    if (i == 100)
                    {
                        EXPECT_TRUE(service.push("Spine", Vec3{ 12.0, 5.0, 3.0 }, pose(service, "Spine").positionM));
                    }
                    world.stepOnce();
                }
                for (const std::string& bone : service.bodyNames())
                {
                    const SandboxBodyPose p = pose(service, bone);
                    numbers.insert(
                        numbers.end(), { p.positionM.x, p.positionM.y, p.positionM.z, p.qx, p.qy, p.qz, p.qw });
                }
                return numbers;
            };
            const std::vector<double> first = run();
            const std::vector<double> second = run();
            ASSERT_EQ(first.size(), second.size());
            for (std::size_t i = 0; i < first.size(); ++i)
            {
                EXPECT_TRUE(bitIdentical(first[i], second[i])) << i;
            }
        }

        TEST_F(SandboxServiceTest, aRotationIsAUnitQuaternion)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service, 0.4, 0.7));
            steps(world, 40);
            for (const std::string& bone : service.bodyNames())
            {
                const SandboxBodyPose p = pose(service, bone);
                EXPECT_NEAR(p.qx * p.qx + p.qy * p.qy + p.qz * p.qz + p.qw * p.qw, 1.0, 1.0e-6) << bone;
            }
        }

        // ---- review of 2026-11-08 (round 5) -------------------------------------------------------------------------

        TEST_F(SandboxServiceTest, noPushIsReportedWaitingOnceTheWorldWasCleared)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            ASSERT_TRUE(service.push("Pelvis", Vec3{ 10.0, 0.0, 0.0 }, Vec3{}));
            ASSERT_EQ(service.pendingPushes(), 1u);
            world.clear();
            EXPECT_EQ(service.pendingPushes(), 0u) << "the ragdoll is gone: nothing is waiting to push it";
        }

        TEST_F(SandboxServiceTest, aResetKeepsTheGroundBodyItHas)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            const JPH::BodyID ground = service.ground();
            const std::size_t bodies = world.bodyCount();
            std::string error;
            ASSERT_TRUE(service.reset(error)) << error;
            EXPECT_TRUE(service.ground() == ground) << "a ground that is still there is not taken away and made again";
            EXPECT_EQ(world.bodyCount(), bodies);
        }

        TEST_F(SandboxServiceTest, aResetInAFullWorldStillWorks)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            for (int i = 0; i < 10000; ++i)
            {
                try
                {
                    world.addStaticBox(JPH::RVec3(1000.0 + i, 1000.0, 1000.0), JPH::Vec3(1.0f, 1.0f, 1.0f));
                }
                catch (const std::exception&)
                {
                    break; // no room: the world is full
                }
            }
            std::string error;
            EXPECT_TRUE(service.reset(error)) << error;
            EXPECT_TRUE(service.built());
        }

        TEST_F(SandboxServiceTest, aResetThatCannotMakeTheGroundSaysSoAndKeepsWhatItHas)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            world.clear(); // the ground and the ragdoll are gone
            for (int i = 0; i < 10000; ++i)
            {
                try
                {
                    world.addStaticBox(JPH::RVec3(1000.0 + i, 1000.0, 1000.0), JPH::Vec3(1.0f, 1.0f, 1.0f));
                }
                catch (const std::exception&)
                {
                    break;
                }
            }
            std::string error;
            EXPECT_FALSE(service.reset(error));
            EXPECT_FALSE(error.empty());
            EXPECT_FALSE(service.built());
        }

        // The ground is 20 m wide, the impulse cap 100000 N s: a push of the cap on a light body sends it far past the
        // ground's edge (documented in sandboxservice.hpp). Nothing breaks: every pose stays a finite number.
        TEST_F(SandboxServiceTest, aPushOfTheCapSendsABodyOffTheGroundWithoutAnyPoseTurningNonFinite)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            ASSERT_TRUE(service.push("Pelvis", Vec3{ 99999.0, 0.0, 0.0 }, Vec3{}));
            steps(world, 240);
            for (const std::string& bone : service.bodyNames())
            {
                const SandboxBodyPose p = pose(service, bone);
                EXPECT_TRUE(
                    std::isfinite(p.positionM.x) && std::isfinite(p.positionM.y) && std::isfinite(p.positionM.z))
                    << bone;
                EXPECT_TRUE(std::isfinite(p.qw)) << bone;
            }
            SandboxBodyPose pelvis = pose(service, "Pelvis");
            EXPECT_GT(std::fabs(pelvis.positionM.x), SandboxService::sGroundHalfExtentM)
                << "it left the 20 m ground; the documented limit";
        }

        // ---- review of 2026-11-09 (lane C: C2) ----------------------------------------------------------------------

        TEST_F(SandboxServiceTest, aBodyAddedAfterClearIsNeverTakenForTheStaleGroundByResetOrRelease)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            const JPH::BodyID staleGround = service.ground();
            world.clear();
            // Jolt may hand the first new body the freed index; whatever it hands out must belong to the bystander.
            const JPH::BodyID bystander
                = world.addStaticBox(JPH::RVec3(500.0, 500.0, 0.0), JPH::Vec3(1.0f, 1.0f, 1.0f));
            ASSERT_TRUE(world.hasBody(bystander));
            std::string error;
            ASSERT_TRUE(service.reset(error)) << error;
            EXPECT_TRUE(world.hasBody(bystander)) << "a reset must not remove a body it does not own";
            EXPECT_FALSE(service.ground() == bystander);
            EXPECT_TRUE(world.hasBody(service.ground()));
            EXPECT_FALSE(staleGround == bystander) << "Jolt's sequence number keeps a recycled slot's id apart";
            service.release();
            EXPECT_TRUE(world.hasBody(bystander)) << "release must not remove a body it does not own";
            EXPECT_EQ(world.bodyCount(), 1u);
        }

        TEST_F(SandboxServiceTest, releaseAfterClearRemovesNothingThatIsNotTheServices)
        {
            StudioWorld world;
            SandboxService service(world);
            ASSERT_TRUE(build(service));
            world.clear();
            const JPH::BodyID bystander
                = world.addStaticBox(JPH::RVec3(500.0, 500.0, 0.0), JPH::Vec3(1.0f, 1.0f, 1.0f));
            service.release();
            EXPECT_TRUE(world.hasBody(bystander));
            EXPECT_EQ(world.bodyCount(), 1u);
            EXPECT_TRUE(service.ground() == JPH::BodyID());
        }
    }
}
