#include <components/plugin/host_impl.hpp>
#include <components/plugin/physics_service.hpp>
#include <openeden/physics.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>

namespace
{
    using namespace openeden::sdk;
    using namespace openeden::plugin;

    bool bitIdenticalFloat(float a, float b)
    {
        std::uint32_t ua = 0;
        std::uint32_t ub = 0;
        std::memcpy(&ua, &a, sizeof(float));
        std::memcpy(&ub, &b, sizeof(float));
        return ua == ub;
    }

    bool bitIdenticalVec3(const PhysicsVec3& a, const PhysicsVec3& b)
    {
        return bitIdenticalFloat(a.x, b.x) && bitIdenticalFloat(a.y, b.y) && bitIdenticalFloat(a.z, b.z);
    }

    bool bitIdenticalQuat(const PhysicsQuat& a, const PhysicsQuat& b)
    {
        return bitIdenticalFloat(a.x, b.x) && bitIdenticalFloat(a.y, b.y)
            && bitIdenticalFloat(a.z, b.z) && bitIdenticalFloat(a.w, b.w);
    }

    TEST(PhysicsServiceTest, HostWiresPhysicsService)
    {
        HostImpl host;
        PhysicsService* physics = host.getPhysicsService();
        ASSERT_NE(physics, nullptr);
        EXPECT_GT(physics->fixedStepSeconds(), 0.0f);
        EXPECT_NEAR(physics->fixedStepSeconds(), 1.0f / 60.0f, 1e-6f);
    }

    TEST(PhysicsServiceTest, CreateAndDestroyBodyWithGenerationSafety)
    {
        PhysicsServiceImpl service;

        // 1. Dynamic box
        BodyDesc boxDesc{};
        boxDesc.shape = BoxShape{ { 0.5f, 0.5f, 0.5f } };
        boxDesc.motion = MotionType::Dynamic;
        boxDesc.positionM = { 1.0f, 2.0f, 3.0f };
        boxDesc.massKg = 5.0f;

        const auto boxRes = service.createBody(boxDesc);
        ASSERT_TRUE(boxRes.ok()) << boxRes.message;
        EXPECT_TRUE(boxRes.id.isValid());
        EXPECT_TRUE(service.isBodyValid(boxRes.id));

        BodyState state{};
        ASSERT_TRUE(service.readBody(boxRes.id, state));
        EXPECT_NEAR(state.positionM.x, 1.0f, 1e-4f);
        EXPECT_NEAR(state.positionM.y, 2.0f, 1e-4f);
        EXPECT_NEAR(state.positionM.z, 3.0f, 1e-4f);

        // 2. Static sphere
        BodyDesc sphereDesc{};
        sphereDesc.shape = SphereShape{ 0.4f };
        sphereDesc.motion = MotionType::Static;
        sphereDesc.positionM = { -2.0f, 0.0f, 0.0f };

        const auto sphereRes = service.createBody(sphereDesc);
        ASSERT_TRUE(sphereRes.ok()) << sphereRes.message;
        EXPECT_TRUE(service.isBodyValid(sphereRes.id));

        // 3. Kinematic capsule
        BodyDesc capsuleDesc{};
        capsuleDesc.shape = CapsuleShape{ 0.6f, 0.25f };
        capsuleDesc.motion = MotionType::Kinematic;
        capsuleDesc.positionM = { 0.0f, 4.0f, 1.0f };

        const auto capsuleRes = service.createBody(capsuleDesc);
        ASSERT_TRUE(capsuleRes.ok()) << capsuleRes.message;
        EXPECT_TRUE(service.isBodyValid(capsuleRes.id));

        // Batch readBodies
        const std::array<BodyId, 3> ids = { boxRes.id, sphereRes.id, capsuleRes.id };
        std::array<BodyState, 3> states{};
        EXPECT_EQ(service.readBodies(ids, states), 3u);
        EXPECT_NEAR(states[0].positionM.x, 1.0f, 1e-4f);
        EXPECT_NEAR(states[1].positionM.x, -2.0f, 1e-4f);
        EXPECT_NEAR(states[2].positionM.y, 4.0f, 1e-4f);

        // Destroy box and verify stale handle rejection
        EXPECT_TRUE(service.destroyBody(boxRes.id));
        EXPECT_FALSE(service.isBodyValid(boxRes.id));
        EXPECT_FALSE(service.readBody(boxRes.id, state));
        EXPECT_FALSE(service.destroyBody(boxRes.id));

        // Creating another body reuses slot with bumped generation
        const auto reusedRes = service.createBody(boxDesc);
        ASSERT_TRUE(reusedRes.ok());
        EXPECT_EQ(reusedRes.id.index, boxRes.id.index);
        EXPECT_NE(reusedRes.id.generation, boxRes.id.generation);
        EXPECT_FALSE(service.isBodyValid(boxRes.id));
        EXPECT_TRUE(service.isBodyValid(reusedRes.id));
    }

    TEST(PhysicsServiceTest, InvalidBodyDescriptionsRejected)
    {
        PhysicsServiceImpl service;

        // Non-positive box half-extents
        {
            BodyDesc desc{};
            desc.shape = BoxShape{ { 0.0f, 0.5f, 0.5f } };
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }

        // Non-positive sphere radius
        {
            BodyDesc desc{};
            desc.shape = SphereShape{ -0.1f };
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }

        // Non-positive capsule dimensions
        {
            BodyDesc desc{};
            desc.shape = CapsuleShape{ 0.0f, 0.2f };
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }

        // Zero or negative mass on dynamic body
        {
            BodyDesc desc{};
            desc.motion = MotionType::Dynamic;
            desc.massKg = 0.0f;
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }

        // Non-unit quaternion
        {
            BodyDesc desc{};
            desc.rotation = { 0.0f, 0.0f, 0.0f, 0.0f };
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }

        // Negative friction or damping
        {
            BodyDesc desc{};
            desc.friction = -0.1f;
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }
        {
            BodyDesc desc{};
            desc.linearDamping = -0.1f;
            const auto res = service.createBody(desc);
            EXPECT_FALSE(res.ok());
            EXPECT_EQ(res.error, PhysicsErrorCode::InvalidDescription);
            EXPECT_FALSE(res.message.empty());
        }
    }

    TEST(PhysicsServiceTest, BoxDroppedOnFloorRestsAtExpectedHeight)
    {
        PhysicsServiceImpl service;

        // Floor: static box with top surface at z = 0
        BodyDesc floorDesc{};
        floorDesc.shape = BoxShape{ { 5.0f, 5.0f, 0.5f } };
        floorDesc.motion = MotionType::Static;
        floorDesc.positionM = { 0.0f, 0.0f, -0.5f };
        const auto floorRes = service.createBody(floorDesc);
        ASSERT_TRUE(floorRes.ok()) << floorRes.message;

        // Dynamic box with half-extent 0.25m dropped from z = 1.0m
        BodyDesc boxDesc{};
        boxDesc.shape = BoxShape{ { 0.25f, 0.25f, 0.25f } };
        boxDesc.motion = MotionType::Dynamic;
        boxDesc.positionM = { 0.0f, 0.0f, 1.0f };
        boxDesc.massKg = 2.0f;
        const auto boxRes = service.createBody(boxDesc);
        ASSERT_TRUE(boxRes.ok()) << boxRes.message;

        for (int i = 0; i < 180; ++i)
        {
            service.step();
        }

        BodyState state{};
        ASSERT_TRUE(service.readBody(boxRes.id, state));
        EXPECT_NEAR(state.positionM.z, 0.25f, 0.05f);
        EXPECT_NEAR(state.linearVelocityMps.z, 0.0f, 0.05f);
    }

    TEST(PhysicsServiceTest, TwoIdenticalRunsProduceBitIdenticalResults)
    {
        auto runScenario = []() {
            PhysicsServiceImpl service;

            BodyDesc floorDesc{};
            floorDesc.shape = BoxShape{ { 10.0f, 10.0f, 0.5f } };
            floorDesc.motion = MotionType::Static;
            floorDesc.positionM = { 0.0f, 0.0f, -0.5f };
            const auto floorRes = service.createBody(floorDesc);
            EXPECT_TRUE(floorRes.ok());

            BodyDesc bodyADesc{};
            bodyADesc.shape = CapsuleShape{ 0.2f, 0.1f };
            bodyADesc.motion = MotionType::Dynamic;
            bodyADesc.positionM = { 0.0f, 0.0f, 1.2f };
            bodyADesc.massKg = 5.0f;
            const auto bodyA = service.createBody(bodyADesc);
            EXPECT_TRUE(bodyA.ok());

            BodyDesc bodyBDesc{};
            bodyBDesc.shape = CapsuleShape{ 0.2f, 0.1f };
            bodyBDesc.motion = MotionType::Dynamic;
            bodyBDesc.positionM = { 0.0f, 0.0f, 0.6f };
            bodyBDesc.massKg = 4.0f;
            const auto bodyB = service.createBody(bodyBDesc);
            EXPECT_TRUE(bodyB.ok());

            JointDesc jointDesc{};
            jointDesc.bodyA = bodyA.id;
            jointDesc.bodyB = bodyB.id;
            jointDesc.anchorM = { 0.0f, 0.0f, 0.9f };
            jointDesc.twistAxis = { 0.0f, 0.0f, 1.0f };
            jointDesc.planeAxis = { 1.0f, 0.0f, 0.0f };
            jointDesc.swingShape = SwingShape::Cone;
            jointDesc.planeSwingMinRad = -0.5f;
            jointDesc.planeSwingMaxRad = 0.5f;
            jointDesc.normalSwingMinRad = -0.5f;
            jointDesc.normalSwingMaxRad = 0.5f;
            jointDesc.twistMinRad = -0.3f;
            jointDesc.twistMaxRad = 0.3f;

            const auto joint = service.createJoint(jointDesc);
            EXPECT_TRUE(joint.ok());
            EXPECT_TRUE(service.setJointMotor(joint.id, JointMotor{ 120.0f, 12.0f, 200.0f }));
            EXPECT_TRUE(service.setJointTarget(joint.id, PhysicsQuat{ 0.0f, 0.1f, 0.0f, 0.9949874f }));

            EXPECT_TRUE(service.applyImpulse(bodyA.id, { 3.5f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.3f }));

            for (int i = 0; i < 60; ++i)
            {
                service.step();
            }

            std::array<BodyState, 2> out{};
            const std::array<BodyId, 2> ids = { bodyA.id, bodyB.id };
            EXPECT_EQ(service.readBodies(ids, out), 2u);
            return out;
        };

        const auto run1 = runScenario();
        const auto run2 = runScenario();

        for (std::size_t i = 0; i < run1.size(); ++i)
        {
            EXPECT_TRUE(bitIdenticalVec3(run1[i].positionM, run2[i].positionM));
            EXPECT_TRUE(bitIdenticalQuat(run1[i].rotation, run2[i].rotation));
            EXPECT_TRUE(bitIdenticalVec3(run1[i].linearVelocityMps, run2[i].linearVelocityMps));
            EXPECT_TRUE(bitIdenticalVec3(run1[i].angularVelocityRadps, run2[i].angularVelocityRadps));
        }
    }

    TEST(PhysicsServiceTest, ConeAndPyramidJointsAndMotors)
    {
        PhysicsServiceImpl service;

        BodyDesc rootDesc{};
        rootDesc.shape = BoxShape{ { 0.2f, 0.2f, 0.2f } };
        rootDesc.motion = MotionType::Static;
        rootDesc.positionM = { 0.0f, 0.0f, 2.0f };
        const auto root = service.createBody(rootDesc);
        ASSERT_TRUE(root.ok());

        BodyDesc midDesc{};
        midDesc.shape = CapsuleShape{ 0.2f, 0.08f };
        midDesc.motion = MotionType::Dynamic;
        midDesc.positionM = { 0.0f, 0.0f, 1.5f };
        midDesc.massKg = 2.0f;
        const auto mid = service.createBody(midDesc);
        ASSERT_TRUE(mid.ok());

        BodyDesc tipDesc{};
        tipDesc.shape = CapsuleShape{ 0.2f, 0.08f };
        tipDesc.motion = MotionType::Dynamic;
        tipDesc.positionM = { 0.0f, 0.0f, 1.0f };
        tipDesc.massKg = 1.5f;
        const auto tip = service.createBody(tipDesc);
        ASSERT_TRUE(tip.ok());

        // Cone joint between root and mid
        JointDesc coneDesc{};
        coneDesc.bodyA = root.id;
        coneDesc.bodyB = mid.id;
        coneDesc.anchorM = { 0.0f, 0.0f, 1.75f };
        coneDesc.twistAxis = { 0.0f, 0.0f, -1.0f };
        coneDesc.planeAxis = { 1.0f, 0.0f, 0.0f };
        coneDesc.swingShape = SwingShape::Cone;
        coneDesc.planeSwingMinRad = -0.6f;
        coneDesc.planeSwingMaxRad = 0.6f;
        coneDesc.normalSwingMinRad = -0.6f;
        coneDesc.normalSwingMaxRad = 0.6f;
        coneDesc.twistMinRad = -0.4f;
        coneDesc.twistMaxRad = 0.4f;

        const auto coneJoint = service.createJoint(coneDesc);
        ASSERT_TRUE(coneJoint.ok()) << coneJoint.message;
        EXPECT_TRUE(service.isConstraintValid(coneJoint.id));

        // Pyramid joint between mid and tip
        JointDesc pyramidDesc{};
        pyramidDesc.bodyA = mid.id;
        pyramidDesc.bodyB = tip.id;
        pyramidDesc.anchorM = { 0.0f, 0.0f, 1.25f };
        pyramidDesc.twistAxis = { 0.0f, 0.0f, -1.0f };
        pyramidDesc.planeAxis = { 1.0f, 0.0f, 0.0f };
        pyramidDesc.swingShape = SwingShape::Pyramid;
        pyramidDesc.planeSwingMinRad = -0.8f;
        pyramidDesc.planeSwingMaxRad = 0.2f;
        pyramidDesc.normalSwingMinRad = -0.3f;
        pyramidDesc.normalSwingMaxRad = 0.5f;
        pyramidDesc.twistMinRad = -0.2f;
        pyramidDesc.twistMaxRad = 0.2f;

        const auto pyramidJoint = service.createJoint(pyramidDesc);
        ASSERT_TRUE(pyramidJoint.ok()) << pyramidJoint.message;
        EXPECT_TRUE(service.isConstraintValid(pyramidJoint.id));

        // Configure motors, position steps, and targets (orientation-only & feed-forward)
        const JointMotor motor{ 150.0f, 15.0f, 300.0f };
        EXPECT_TRUE(service.setJointMotor(coneJoint.id, motor));
        EXPECT_TRUE(service.setJointMotor(pyramidJoint.id, motor));
        EXPECT_TRUE(service.setJointPositionSteps(coneJoint.id, 8));
        EXPECT_TRUE(service.setJointPositionSteps(pyramidJoint.id, 8));

        EXPECT_TRUE(service.setJointTarget(coneJoint.id, PhysicsQuat{ 0.0f, 0.0f, 0.0f, 1.0f }));
        EXPECT_TRUE(service.setJointTarget(
            pyramidJoint.id, PhysicsQuat{ 0.0f, 0.1f, 0.0f, 0.9949874f }, PhysicsVec3{ 0.0f, 0.5f, 0.0f }));

        for (int i = 0; i < 30; ++i)
        {
            service.step();
        }

        BodyState midState{};
        BodyState tipState{};
        ASSERT_TRUE(service.readBody(mid.id, midState));
        ASSERT_TRUE(service.readBody(tip.id, tipState));
        EXPECT_GT(midState.positionM.z, 1.0f);
        EXPECT_GT(tipState.positionM.z, 0.5f);

        // Destroy joints and verify stale handle rejection
        EXPECT_TRUE(service.destroyJoint(pyramidJoint.id));
        EXPECT_FALSE(service.isConstraintValid(pyramidJoint.id));
        EXPECT_FALSE(service.setJointMotor(pyramidJoint.id, motor));
    }

    TEST(PhysicsServiceTest, CollisionGroupSubGroupFiltering)
    {
        PhysicsWorldConfig cfg{};
        cfg.gravityMps2 = { 0.0f, 0.0f, 0.0f };
        PhysicsServiceImpl service(cfg);

        const auto groupRes = service.createCollisionGroup(2);
        ASSERT_TRUE(groupRes.ok()) << groupRes.message;
        EXPECT_TRUE(service.setSubGroupsCollide(groupRes.id, 0, 1, false));

        // Place two overlapping dynamic spheres at the exact same location in subgroups 0 and 1
        BodyDesc bodyADesc{};
        bodyADesc.shape = SphereShape{ 0.5f };
        bodyADesc.motion = MotionType::Dynamic;
        bodyADesc.positionM = { 0.0f, 0.0f, 0.0f };
        bodyADesc.collisionGroup = groupRes.id;
        bodyADesc.subGroup = 0;

        BodyDesc bodyBDesc = bodyADesc;
        bodyBDesc.positionM = { 0.1f, 0.0f, 0.0f };
        bodyBDesc.subGroup = 1;

        const auto bodyA = service.createBody(bodyADesc);
        const auto bodyB = service.createBody(bodyBDesc);
        ASSERT_TRUE(bodyA.ok());
        ASSERT_TRUE(bodyB.ok());

        for (int i = 0; i < 15; ++i)
        {
            service.step();
        }

        BodyState stateA{};
        BodyState stateB{};
        ASSERT_TRUE(service.readBody(bodyA.id, stateA));
        ASSERT_TRUE(service.readBody(bodyB.id, stateB));

        // Because collision between subgroups 0 and 1 is disabled, they do not push apart
        EXPECT_NEAR(stateA.positionM.x, 0.0f, 1e-4f);
        EXPECT_NEAR(stateB.positionM.x, 0.1f, 1e-4f);

        EXPECT_TRUE(service.destroyBody(bodyA.id));
        EXPECT_TRUE(service.destroyBody(bodyB.id));
        EXPECT_TRUE(service.destroyCollisionGroup(groupRes.id));
    }

    TEST(PhysicsServiceTest, DriveBodiesImpulseVelocityDampingKinematicAndMotionType)
    {
        PhysicsWorldConfig cfg{};
        cfg.gravityMps2 = { 0.0f, 0.0f, 0.0f };
        PhysicsServiceImpl service(cfg);

        BodyDesc desc{};
        desc.shape = SphereShape{ 0.3f };
        desc.motion = MotionType::Dynamic;
        desc.positionM = { 0.0f, 0.0f, 0.0f };
        desc.massKg = 2.0f;
        desc.linearDamping = 0.0f;
        desc.angularDamping = 0.0f;

        const auto body = service.createBody(desc);
        ASSERT_TRUE(body.ok());

        // 1. applyImpulse
        EXPECT_TRUE(service.applyImpulse(body.id, { 4.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }));
        BodyState state{};
        ASSERT_TRUE(service.readBody(body.id, state));
        EXPECT_NEAR(state.linearVelocityMps.x, 2.0f, 1e-3f);

        // 2. setVelocity
        EXPECT_TRUE(service.setVelocity(body.id, { 0.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 1.5f }));
        ASSERT_TRUE(service.readBody(body.id, state));
        EXPECT_NEAR(state.linearVelocityMps.y, 3.0f, 1e-3f);
        EXPECT_NEAR(state.angularVelocityRadps.z, 1.5f, 1e-3f);

        // 3. setBodyDamping
        EXPECT_TRUE(service.setBodyDamping(body.id, 0.8f, 0.8f));
        service.step();

        // 4. Runtime motion type transition Dynamic -> Kinematic -> Dynamic
        ASSERT_EQ(service.getMotionType(body.id), MotionType::Dynamic);
        EXPECT_TRUE(service.setMotionType(body.id, MotionType::Kinematic));
        EXPECT_EQ(service.getMotionType(body.id), MotionType::Kinematic);

        // 5. setKinematicTarget moves body on next step
        EXPECT_TRUE(service.setKinematicTarget(body.id, { 1.0f, 2.0f, 3.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }));
        service.step();
        ASSERT_TRUE(service.readBody(body.id, state));
        EXPECT_NEAR(state.positionM.x, 1.0f, 1e-3f);
        EXPECT_NEAR(state.positionM.y, 2.0f, 1e-3f);
        EXPECT_NEAR(state.positionM.z, 3.0f, 1e-3f);

        // Switch back to Dynamic
        EXPECT_TRUE(service.setMotionType(body.id, MotionType::Dynamic));
        EXPECT_EQ(service.getMotionType(body.id), MotionType::Dynamic);
    }

    TEST(PhysicsServiceTest, RayCastFindsClosestHitAndHonorsIgnoreBody)
    {
        PhysicsServiceImpl service;

        BodyDesc nearDesc{};
        nearDesc.shape = BoxShape{ { 0.5f, 0.5f, 0.5f } };
        nearDesc.motion = MotionType::Static;
        nearDesc.positionM = { 3.0f, 0.0f, 0.0f };
        const auto nearBody = service.createBody(nearDesc);
        ASSERT_TRUE(nearBody.ok());

        BodyDesc farDesc = nearDesc;
        farDesc.positionM = { 6.0f, 0.0f, 0.0f };
        const auto farBody = service.createBody(farDesc);
        ASSERT_TRUE(farBody.ok());

        // Ray along +X of length 10m hits nearBody at x = 2.5m (fraction 0.25)
        const auto hit1 = service.castRay({ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(hit1.has_value());
        EXPECT_EQ(hit1->body, nearBody.id);
        EXPECT_NEAR(hit1->pointM.x, 2.5f, 1e-3f);
        EXPECT_NEAR(hit1->normal.x, -1.0f, 1e-3f);
        EXPECT_NEAR(hit1->fraction, 0.25f, 1e-3f);

        // Ignoring nearBody hits farBody at x = 5.5m (fraction 0.55)
        const auto hit2 = service.castRay({ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, nearBody.id);
        ASSERT_TRUE(hit2.has_value());
        EXPECT_EQ(hit2->body, farBody.id);
        EXPECT_NEAR(hit2->pointM.x, 5.5f, 1e-3f);
        EXPECT_NEAR(hit2->fraction, 0.55f, 1e-3f);
    }

    TEST(PhysicsServiceTest, StructuralChangesRejectedDuringStep)
    {
        PhysicsServiceImpl service;

        BodyDesc desc{};
        desc.shape = SphereShape{ 0.25f };
        desc.motion = MotionType::Dynamic;
        const auto b1 = service.createBody(desc);
        const auto b2 = service.createBody(desc);
        ASSERT_TRUE(b1.ok());
        ASSERT_TRUE(b2.ok());

        JointDesc jDesc{};
        jDesc.bodyA = b1.id;
        jDesc.bodyB = b2.id;
        const auto joint = service.createJoint(jDesc);
        ASSERT_TRUE(joint.ok());

        service.setInStep(true);
        EXPECT_TRUE(service.isInStep());

        const auto createBodyRes = service.createBody(desc);
        EXPECT_FALSE(createBodyRes.ok());
        EXPECT_EQ(createBodyRes.error, PhysicsErrorCode::CalledDuringStep);

        EXPECT_FALSE(service.destroyBody(b1.id));

        const auto createJointRes = service.createJoint(jDesc);
        EXPECT_FALSE(createJointRes.ok());
        EXPECT_EQ(createJointRes.error, PhysicsErrorCode::CalledDuringStep);

        EXPECT_FALSE(service.destroyJoint(joint.id));

        const auto createGroupRes = service.createCollisionGroup(2);
        EXPECT_FALSE(createGroupRes.ok());
        EXPECT_EQ(createGroupRes.error, PhysicsErrorCode::CalledDuringStep);

        service.setInStep(false);
        EXPECT_FALSE(service.isInStep());
    }
} // namespace
