// Tests for CMPhysics::HostedJoltWorld, stage a of task 0035 (docs/tasks/0035-creativemotion-m10-physics-adapter.md).
//
// Written from the task spec alone. The behaviour of each call follows CreativeMotion's own JoltWorld (the reference),
// so the validation rules below are the ones in its validateBody, validateJointGeometry and isValidMotor.
//
// Allocation tests live in testhostedjoltallocation.cpp and the ragdoll builder test in testhostedjoltragdoll.cpp.

#include "hostedjoltfixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace CMPhysicsTest;
    using namespace opas::physics;

    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kFloatMax = std::numeric_limits<float>::max();

    // A step count that is one second at the host's 1/60 s step.
    constexpr int kOneSecondOfSteps = 60;

    // A float comparison tolerance for values that pass through meters -> Jolt units -> meters (a few ulps at most).
    constexpr float kRoundTripTolerance = 1e-4f;

    // Rotation of `angleRad` about a unit axis, as an interface quaternion.
    Quat quatAbout(float ax, float ay, float az, float angleRad)
    {
        const float s = std::sin(angleRad * 0.5f);
        return Quat{ ax * s, ay * s, az * s, std::cos(angleRad * 0.5f) };
    }

    // The angle of a rotation about the X axis, from its quaternion.
    float twistAngleAboutX(const Quat& q)
    {
        return 2.0f * std::atan2(q.x, q.w);
    }

    void expectVecNear(const Vec3& actual, const Vec3& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    bool messageMentions(const std::string& message, const char* word)
    {
        return message.find(word) != std::string::npos;
    }

    JPH::AABox worldBoundsOf(HostWorld& host, JPH::BodyID id)
    {
        JPH::BodyLockRead lock(host.system().GetBodyLockInterface(), id);
        EXPECT_TRUE(lock.Succeeded());
        if (!lock.Succeeded())
            return JPH::AABox();
        return lock.GetBody().GetWorldSpaceBounds();
    }

    // A valid cone joint between two bodies, hanging along -Z from `anchor`.
    JointDesc validJoint(BodyId a, BodyId b, Vec3 anchor = Vec3{ 0.0f, 0.0f, 0.0f })
    {
        JointDesc joint;
        joint.bodyA = a;
        joint.bodyB = b;
        joint.anchorM = anchor;
        joint.twistAxis = Vec3{ 0.0f, 0.0f, -1.0f };
        joint.planeAxis = Vec3{ 1.0f, 0.0f, 0.0f };
        joint.swingShape = SwingShape::Cone;
        joint.planeSwingMinRad = -0.5f;
        joint.planeSwingMaxRad = 0.5f;
        joint.normalSwingMinRad = -0.5f;
        joint.normalSwingMaxRad = 0.5f;
        joint.twistMinRad = -0.2f;
        joint.twistMaxRad = 0.2f;
        return joint;
    }

    // Common set-up: a host with Earth gravity, a stepper that calls the host's Update, and an adapter in meters.
    // The adapter is declared last so it is destroyed first (the host must outlive it).
    class HostedJoltWorldTest : public ::testing::Test
    {
    protected:
        HostedJoltWorldTest()
            : mStepper(mHost)
        {
        }

        void SetUp() override
        {
            std::string error;
            mAdapter = makeAdapter(mHost, makeConfig(1.0f, &mStepper), &error);
            ASSERT_NE(mAdapter, nullptr) << error;
        }

        BodyId makeSphere(Vec3 position, float radius = 0.5f)
        {
            const CreateResult<BodyId> result = mAdapter->createBody(sphereDesc(position, radius));
            EXPECT_TRUE(result.ok()) << result.message;
            return result.id;
        }

        void stepMany(int count)
        {
            for (int i = 0; i < count; ++i)
                mAdapter->step();
        }

        HostWorld mHost;
        UpdateStepper mStepper;
        std::unique_ptr<CMPhysics::HostedJoltWorld> mAdapter;
    };

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: bodies and handles
    // ---------------------------------------------------------------------------------------------------------------

    TEST_F(HostedJoltWorldTest, bodiesAppearInTheHostSystemOnTheMovingLayerWithUserDataZero)
    {
        const std::vector<JPH::BodyID> before = mHost.allBodies();
        const std::size_t countBefore = mHost.numBodies();

        BodyDesc capsule;
        capsule.shape = CapsuleShape{ 0.5f, 0.25f };
        capsule.positionM = Vec3{ 3.0f, 0.0f, 1.0f };

        const CreateResult<BodyId> sphere = mAdapter->createBody(sphereDesc(Vec3{ 1.0f, 2.0f, 3.0f }));
        ASSERT_TRUE(sphere.ok()) << sphere.message;
        EXPECT_EQ(mHost.numBodies(), countBefore + 1);

        const CreateResult<BodyId> box
            = mAdapter->createBody(boxDesc(Vec3{ -2.0f, 0.0f, 1.0f }, Vec3{ 0.5f, 0.4f, 0.3f }));
        ASSERT_TRUE(box.ok()) << box.message;
        EXPECT_EQ(mHost.numBodies(), countBefore + 2);

        const CreateResult<BodyId> capsuleResult = mAdapter->createBody(capsule);
        ASSERT_TRUE(capsuleResult.ok()) << capsuleResult.message;
        EXPECT_EQ(mHost.numBodies(), countBefore + 3);

        const std::vector<JPH::BodyID> created = mHost.bodiesAddedSince(before);
        ASSERT_EQ(created.size(), 3u);
        for (const JPH::BodyID id : created)
        {
            EXPECT_EQ(mHost.bodies().GetObjectLayer(id), kLayerMoving);
            // The game reads user data as a PtrHolder pointer, so the adapter's bodies must carry 0.
            EXPECT_EQ(mHost.bodies().GetUserData(id), 0u);
        }
    }

    TEST_F(HostedJoltWorldTest, bodiesReadBackAtTheCreatedPoseInMeters)
    {
        BodyDesc desc = boxDesc(Vec3{ 1.0f, 2.0f, 3.0f }, Vec3{ 0.5f, 0.4f, 0.3f });
        desc.rotation = quatAbout(0.0f, 0.0f, 1.0f, 0.7f);
        const CreateResult<BodyId> created = mAdapter->createBody(desc);
        ASSERT_TRUE(created.ok()) << created.message;

        BodyState state;
        ASSERT_TRUE(mAdapter->readBody(created.id, state));
        expectVecNear(state.positionM, desc.positionM, kRoundTripTolerance);
        EXPECT_NEAR(state.rotation.x, desc.rotation.x, kRoundTripTolerance);
        EXPECT_NEAR(state.rotation.y, desc.rotation.y, kRoundTripTolerance);
        EXPECT_NEAR(state.rotation.z, desc.rotation.z, kRoundTripTolerance);
        EXPECT_NEAR(state.rotation.w, desc.rotation.w, kRoundTripTolerance);
        expectVecNear(state.linearVelocityMps, Vec3{}, kRoundTripTolerance);
    }

    TEST_F(HostedJoltWorldTest, aCapsuleLiesAlongLocalZ)
    {
        const std::vector<JPH::BodyID> before = mHost.allBodies();
        BodyDesc desc;
        desc.shape = CapsuleShape{ 0.5f, 0.25f };
        desc.positionM = Vec3{ 0.0f, 0.0f, 10.0f };
        ASSERT_TRUE(mAdapter->createBody(desc).ok());

        const std::vector<JPH::BodyID> created = mHost.bodiesAddedSince(before);
        ASSERT_EQ(created.size(), 1u);
        const JPH::AABox bounds = worldBoundsOf(mHost, created.front());
        // Total length is 2 * (halfHeight + radius) = 1.5 along Z; the width is the diameter, 0.5.
        EXPECT_NEAR(bounds.GetExtent().GetZ(), 0.75f, 1e-3f);
        EXPECT_NEAR(bounds.GetExtent().GetX(), 0.25f, 1e-3f);
        EXPECT_NEAR(bounds.GetExtent().GetY(), 0.25f, 1e-3f);
    }

    TEST_F(HostedJoltWorldTest, aDynamicBodyHasTheRequestedMass)
    {
        const std::vector<JPH::BodyID> before = mHost.allBodies();
        BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, 5.0f });
        desc.massKg = 2.0f;
        ASSERT_TRUE(mAdapter->createBody(desc).ok());

        const std::vector<JPH::BodyID> created = mHost.bodiesAddedSince(before);
        ASSERT_EQ(created.size(), 1u);
        JPH::BodyLockRead lock(mHost.system().GetBodyLockInterface(), created.front());
        ASSERT_TRUE(lock.Succeeded());
        EXPECT_NEAR(lock.GetBody().GetMotionProperties()->GetInverseMass(), 0.5f, 1e-5f);
    }

    TEST_F(HostedJoltWorldTest, destroyBodyRemovesItAndStaleHandlesFailEvenAfterTheSlotIsReused)
    {
        const std::size_t countBefore = mHost.numBodies();
        const BodyId first = makeSphere(Vec3{ 0.0f, 0.0f, 1.0f });
        ASSERT_TRUE(first.isValid());
        EXPECT_EQ(mHost.numBodies(), countBefore + 1);

        EXPECT_TRUE(mAdapter->destroyBody(first));
        EXPECT_EQ(mHost.numBodies(), countBefore);

        BodyState state;
        EXPECT_FALSE(mAdapter->destroyBody(first));
        EXPECT_FALSE(mAdapter->readBody(first, state));

        // The next body may reuse the slot; the old handle must stay dead (generation check).
        const BodyId second = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f });
        ASSERT_TRUE(second.isValid());
        EXPECT_NE(first, second);
        EXPECT_FALSE(mAdapter->readBody(first, state));
        EXPECT_FALSE(mAdapter->destroyBody(first));
        EXPECT_TRUE(mAdapter->readBody(second, state));
        EXPECT_EQ(mHost.numBodies(), countBefore + 1);
    }

    TEST_F(HostedJoltWorldTest, invalidAndDefaultHandlesAreRefusedEverywhere)
    {
        BodyState state;
        const BodyId none;
        EXPECT_FALSE(mAdapter->readBody(none, state));
        EXPECT_FALSE(mAdapter->destroyBody(none));
        EXPECT_FALSE(mAdapter->applyImpulse(none, Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{}));
        EXPECT_FALSE(mAdapter->setVelocity(none, Vec3{}, Vec3{}));
        EXPECT_FALSE(mAdapter->setBodyDamping(none, 0.1f, 0.1f));
        EXPECT_FALSE(mAdapter->setKinematicTarget(none, Vec3{}, Quat{}));
        EXPECT_FALSE(mAdapter->setMotionType(none, MotionType::Kinematic));
        EXPECT_FALSE(mAdapter->getMotionType(none).has_value());
        EXPECT_FALSE(mAdapter->destroyJoint(JointId{}));
        EXPECT_FALSE(mAdapter->setJointMotor(JointId{}, JointMotor{}));
        EXPECT_FALSE(mAdapter->setJointTarget(JointId{}, Quat{}));
        EXPECT_FALSE(mAdapter->setJointTarget(JointId{}, Quat{}, Vec3{}));
        EXPECT_FALSE(mAdapter->setJointPositionSteps(JointId{}, 5));
    }

    TEST_F(HostedJoltWorldTest, readBodiesFillsInOrderAndLeavesStaleEntriesUntouched)
    {
        const BodyId a = makeSphere(Vec3{ 1.0f, 0.0f, 10.0f });
        const BodyId b = makeSphere(Vec3{ 2.0f, 0.0f, 10.0f });
        const BodyId stale = makeSphere(Vec3{ 3.0f, 0.0f, 10.0f });
        ASSERT_TRUE(mAdapter->destroyBody(stale));

        const BodyId ids[] = { a, stale, b };
        BodyState sentinel;
        sentinel.positionM = Vec3{ 123.0f, 456.0f, 789.0f };
        BodyState out[] = { sentinel, sentinel, sentinel };
        const std::size_t read = mAdapter->readBodies(ids, out);

        EXPECT_EQ(read, 2u);
        EXPECT_NEAR(out[0].positionM.x, 1.0f, kRoundTripTolerance);
        EXPECT_EQ(out[1].positionM, sentinel.positionM);
        EXPECT_NEAR(out[2].positionM.x, 2.0f, kRoundTripTolerance);
    }

    TEST_F(HostedJoltWorldTest, motionTypeCanBeChangedBetweenDynamicAndKinematic)
    {
        const BodyId body = makeSphere(Vec3{ 0.0f, 0.0f, 5.0f });
        ASSERT_EQ(mAdapter->getMotionType(body), MotionType::Dynamic);
        EXPECT_TRUE(mAdapter->setMotionType(body, MotionType::Kinematic));
        EXPECT_EQ(mAdapter->getMotionType(body), MotionType::Kinematic);
        EXPECT_TRUE(mAdapter->setMotionType(body, MotionType::Dynamic));
        EXPECT_EQ(mAdapter->getMotionType(body), MotionType::Dynamic);
    }

    TEST_F(HostedJoltWorldTest, staticBodiesGoOnTheStaticLayerAndCannotBecomeDynamic)
    {
        const std::vector<JPH::BodyID> before = mHost.allBodies();
        BodyDesc desc = boxDesc(Vec3{ 0.0f, 0.0f, -0.5f }, Vec3{ 5.0f, 5.0f, 0.5f });
        desc.motion = MotionType::Static;
        const CreateResult<BodyId> floor = mAdapter->createBody(desc);
        ASSERT_TRUE(floor.ok()) << floor.message;

        const std::vector<JPH::BodyID> created = mHost.bodiesAddedSince(before);
        ASSERT_EQ(created.size(), 1u);
        EXPECT_EQ(mHost.bodies().GetObjectLayer(created.front()), kLayerStatic);
        // The interface: false "if body was created Static".
        EXPECT_FALSE(mAdapter->setMotionType(floor.id, MotionType::Dynamic));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: validation (same rules and ErrorCodes as JoltWorld)
    // ---------------------------------------------------------------------------------------------------------------

    struct BodyCase
    {
        const char* name;
        BodyDesc desc;
        const char* fieldInMessage;
    };

    TEST_F(HostedJoltWorldTest, invalidBodyDescriptionsAreRefusedWithInvalidDescription)
    {
        std::vector<BodyCase> cases;
        const auto add = [&cases](const char* name, const char* field, auto&& change) {
            BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, 1.0f });
            change(desc);
            cases.push_back(BodyCase{ name, desc, field });
        };
        add("zero mass", "massKg", [](BodyDesc& d) { d.massKg = 0.0f; });
        add("negative mass", "massKg", [](BodyDesc& d) { d.massKg = -1.0f; });
        add("NaN mass", "massKg", [](BodyDesc& d) { d.massKg = kNaN; });
        add("infinite mass", "massKg", [](BodyDesc& d) { d.massKg = kInf; });
        add("NaN position", "positionM", [](BodyDesc& d) { d.positionM.y = kNaN; });
        add("non-unit rotation", "rotation", [](BodyDesc& d) { d.rotation = Quat{ 0.0f, 0.0f, 0.0f, 2.0f }; });
        add("zero rotation", "rotation", [](BodyDesc& d) { d.rotation = Quat{ 0.0f, 0.0f, 0.0f, 0.0f }; });
        add("negative friction", "friction", [](BodyDesc& d) { d.friction = -0.1f; });
        add("NaN friction", "friction", [](BodyDesc& d) { d.friction = kNaN; });
        add("restitution above 1", "restitution", [](BodyDesc& d) { d.restitution = 1.5f; });
        add("negative restitution", "restitution", [](BodyDesc& d) { d.restitution = -0.5f; });
        add("negative linear damping", "linearDamping", [](BodyDesc& d) { d.linearDamping = -1.0f; });
        add("negative angular damping", "angularDamping", [](BodyDesc& d) { d.angularDamping = -1.0f; });
        add("zero sphere radius", "radiusM", [](BodyDesc& d) { d.shape = SphereShape{ 0.0f }; });
        add("NaN sphere radius", "radiusM", [](BodyDesc& d) { d.shape = SphereShape{ kNaN }; });
        add("box with a zero half extent", "halfExtentsM",
            [](BodyDesc& d) { d.shape = BoxShape{ Vec3{ 0.5f, 0.0f, 0.5f } }; });
        add("box with an infinite half extent", "halfExtentsM",
            [](BodyDesc& d) { d.shape = BoxShape{ Vec3{ 0.5f, kInf, 0.5f } }; });
        add("capsule with zero half height", "halfHeightM", [](BodyDesc& d) { d.shape = CapsuleShape{ 0.0f, 0.25f }; });
        add("capsule with zero radius", "radiusM", [](BodyDesc& d) { d.shape = CapsuleShape{ 0.5f, 0.0f }; });

        for (const BodyCase& testCase : cases)
        {
            SCOPED_TRACE(testCase.name);
            const std::size_t countBefore = mHost.numBodies();
            const CreateResult<BodyId> result = mAdapter->createBody(testCase.desc);
            EXPECT_FALSE(result.ok());
            EXPECT_EQ(result.error, ErrorCode::InvalidDescription);
            EXPECT_FALSE(result.id.isValid());
            EXPECT_TRUE(messageMentions(result.message, testCase.fieldInMessage)) << result.message;
            EXPECT_EQ(mHost.numBodies(), countBefore);
        }
    }

    TEST_F(HostedJoltWorldTest, aKinematicBodyDoesNotNeedAPositiveMassButStillValidatesTheRest)
    {
        // JoltWorld::validateBody checks mass for Dynamic bodies only.
        BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, 1.0f });
        desc.motion = MotionType::Kinematic;
        desc.massKg = 0.0f;
        EXPECT_TRUE(mAdapter->createBody(desc).ok());
    }

    TEST_F(HostedJoltWorldTest, collisionGroupMisuseIsRefused)
    {
        BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, 1.0f });
        desc.collisionGroup = CollisionGroupId{ 7, 0 };
        const CreateResult<BodyId> notLive = mAdapter->createBody(desc);
        EXPECT_EQ(notLive.error, ErrorCode::InvalidHandle);

        const CreateResult<CollisionGroupId> group = mAdapter->createCollisionGroup(2);
        ASSERT_TRUE(group.ok()) << group.message;
        desc.collisionGroup = group.id;
        desc.subGroup = 2;
        const CreateResult<BodyId> badSubGroup = mAdapter->createBody(desc);
        EXPECT_EQ(badSubGroup.error, ErrorCode::InvalidDescription);
        EXPECT_TRUE(messageMentions(badSubGroup.message, "subGroup")) << badSubGroup.message;

        EXPECT_EQ(mAdapter->createCollisionGroup(0).error, ErrorCode::InvalidDescription);
        // subGroupCount must be at most min(maxBodies, 65536); the fixture's maxBodies is the default 1024.
        EXPECT_EQ(mAdapter->createCollisionGroup(1025).error, ErrorCode::InvalidDescription);

        EXPECT_FALSE(mAdapter->setSubGroupsCollide(group.id, 0, 0, false)); // a == b
        EXPECT_FALSE(mAdapter->setSubGroupsCollide(group.id, 0, 2, false)); // sub-group out of range
        EXPECT_TRUE(mAdapter->setSubGroupsCollide(group.id, 0, 1, false));
        EXPECT_TRUE(mAdapter->destroyCollisionGroup(group.id));
        EXPECT_FALSE(mAdapter->destroyCollisionGroup(group.id)); // stale now
        EXPECT_FALSE(mAdapter->setSubGroupsCollide(group.id, 0, 1, true));
    }

    TEST(HostedJoltWorldValidation, aStaticBodyIsRefusedWhenNoStaticLayerIsConfigured)
    {
        HostWorld host;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
            = makeAdapter(host, makeConfig(1.0f, nullptr, /*allowStatic=*/false));
        ASSERT_NE(adapter, nullptr);

        BodyDesc desc = boxDesc(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 1.0f, 1.0f, 1.0f });
        desc.motion = MotionType::Static;
        const CreateResult<BodyId> result = adapter->createBody(desc);
        EXPECT_EQ(result.error, ErrorCode::InvalidDescription);
        EXPECT_FALSE(result.id.isValid());

        // Becoming Static is refused as well, and the body stays as it was.
        const CreateResult<BodyId> dynamic = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f }));
        ASSERT_TRUE(dynamic.ok());
        EXPECT_FALSE(adapter->setMotionType(dynamic.id, MotionType::Static));
        EXPECT_EQ(adapter->getMotionType(dynamic.id), MotionType::Dynamic);
    }

    TEST(HostedJoltWorldValidation, bodyCapacityIsEnforcedAndSlotsAreReusedAfterDestroy)
    {
        HostWorld host;
        CMPhysics::HostedWorldConfig config = makeConfig();
        config.maxBodies = 2;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, config);
        ASSERT_NE(adapter, nullptr);

        const CreateResult<BodyId> first = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 1.0f }));
        const CreateResult<BodyId> second = adapter->createBody(sphereDesc(Vec3{ 5.0f, 0.0f, 1.0f }));
        ASSERT_TRUE(first.ok());
        ASSERT_TRUE(second.ok());
        const CreateResult<BodyId> third = adapter->createBody(sphereDesc(Vec3{ 10.0f, 0.0f, 1.0f }));
        EXPECT_EQ(third.error, ErrorCode::CapacityExceeded);
        EXPECT_FALSE(third.id.isValid());

        ASSERT_TRUE(adapter->destroyBody(first.id));
        EXPECT_TRUE(adapter->createBody(sphereDesc(Vec3{ 10.0f, 0.0f, 1.0f })).ok());
    }

    TEST(HostedJoltWorldValidation, jointCapacityIsEnforced)
    {
        HostWorld host;
        CMPhysics::HostedWorldConfig config = makeConfig();
        config.maxJoints = 1;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, config);
        ASSERT_NE(adapter, nullptr);

        const BodyId a = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f)).id;
        const BodyId b = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f)).id;
        const BodyId c = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 1.0f }, 0.2f)).id;
        ASSERT_TRUE(a.isValid() && b.isValid() && c.isValid());

        const CreateResult<JointId> first = adapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        ASSERT_TRUE(first.ok()) << first.message;
        const CreateResult<JointId> second = adapter->createJoint(validJoint(b, c, Vec3{ 0.0f, 0.0f, 1.5f }));
        EXPECT_EQ(second.error, ErrorCode::CapacityExceeded);
        EXPECT_FALSE(second.id.isValid());

        ASSERT_TRUE(adapter->destroyJoint(first.id));
        EXPECT_TRUE(adapter->createJoint(validJoint(b, c, Vec3{ 0.0f, 0.0f, 1.5f })).ok());
    }

    TEST(HostedJoltWorldValidation, invalidConfigurationsGiveNullAndAMessage)
    {
        HostWorld host;
        const auto expectRefused = [&host](const char* name, auto&& change) {
            SCOPED_TRACE(name);
            CMPhysics::HostedWorldConfig config = makeConfig();
            change(config);
            std::string error;
            const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, config, &error);
            EXPECT_EQ(adapter, nullptr);
            EXPECT_FALSE(error.empty());
        };
        expectRefused("zero unitsPerMeter", [](CMPhysics::HostedWorldConfig& c) { c.unitsPerMeter = 0.0f; });
        expectRefused("negative unitsPerMeter", [](CMPhysics::HostedWorldConfig& c) { c.unitsPerMeter = -1.0f; });
        expectRefused("NaN unitsPerMeter", [](CMPhysics::HostedWorldConfig& c) { c.unitsPerMeter = kNaN; });
        expectRefused("infinite unitsPerMeter", [](CMPhysics::HostedWorldConfig& c) { c.unitsPerMeter = kInf; });
        expectRefused("zero stepSeconds", [](CMPhysics::HostedWorldConfig& c) { c.stepSeconds = 0.0f; });
        expectRefused("NaN stepSeconds", [](CMPhysics::HostedWorldConfig& c) { c.stepSeconds = kNaN; });
        expectRefused("zero maxBodies", [](CMPhysics::HostedWorldConfig& c) { c.maxBodies = 0; });
        expectRefused("zero maxJoints", [](CMPhysics::HostedWorldConfig& c) { c.maxJoints = 0; });

        // The refusals must not have touched the host.
        EXPECT_EQ(host.numBodies(), 0u);
        // And a valid config still works afterwards (the error pointer is optional).
        EXPECT_NE(makeAdapter(host, makeConfig()), nullptr);
    }

    TEST_F(HostedJoltWorldTest, invalidJointDescriptionsAreRefusedWithTheReferenceErrorCodes)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        ASSERT_TRUE(a.isValid() && b.isValid());
        const std::size_t constraintsBefore = mHost.numConstraints();

        const auto expectRefused = [&](const char* name, ErrorCode code, const char* field, auto&& change) {
            SCOPED_TRACE(name);
            JointDesc joint = validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f });
            change(joint);
            const CreateResult<JointId> result = mAdapter->createJoint(joint);
            EXPECT_EQ(result.error, code);
            EXPECT_FALSE(result.id.isValid());
            if (field != nullptr)
                EXPECT_TRUE(messageMentions(result.message, field)) << result.message;
            EXPECT_EQ(mHost.numConstraints(), constraintsBefore);
        };
        const ErrorCode invalid = ErrorCode::InvalidDescription;

        expectRefused("unknown bodyA", ErrorCode::InvalidHandle, "bodyA", [](JointDesc& j) { j.bodyA = BodyId{}; });
        expectRefused("unknown bodyB", ErrorCode::InvalidHandle, "bodyB", [](JointDesc& j) { j.bodyB = BodyId{}; });
        expectRefused("same body twice", invalid, "bodyB", [&](JointDesc& j) { j.bodyB = a; });
        expectRefused("NaN anchor", invalid, "anchorM", [](JointDesc& j) { j.anchorM.x = kNaN; });
        expectRefused(
            "twist axis not unit", invalid, "twistAxis", [](JointDesc& j) { j.twistAxis = Vec3{ 0, 0, -2 }; });
        expectRefused("plane axis not unit", invalid, "planeAxis", [](JointDesc& j) { j.planeAxis = Vec3{ 2, 0, 0 }; });
        expectRefused("plane axis not perpendicular", invalid, "planeAxis",
            [](JointDesc& j) { j.planeAxis = Vec3{ 0, 0, 1 }; });
        expectRefused("cone that is not symmetric", invalid, "planeSwing", [](JointDesc& j) {
            j.planeSwingMinRad = -0.2f;
            j.planeSwingMaxRad = 0.5f;
        });
        expectRefused("cone normal swing that is not symmetric", invalid, "normalSwing", [](JointDesc& j) {
            j.normalSwingMinRad = -0.5f;
            j.normalSwingMaxRad = 0.2f;
        });
        expectRefused("swing minimum above maximum", invalid, "planeSwing", [](JointDesc& j) {
            j.planeSwingMinRad = 0.5f;
            j.planeSwingMaxRad = -0.5f;
        });
        expectRefused("swing angle beyond pi", invalid, "planeSwing", [](JointDesc& j) {
            j.planeSwingMinRad = -4.0f;
            j.planeSwingMaxRad = 4.0f;
        });
        expectRefused("NaN swing angle", invalid, "planeSwing", [](JointDesc& j) { j.planeSwingMaxRad = kNaN; });
        expectRefused("pyramid range above zero", invalid, "planeSwing", [](JointDesc& j) {
            j.swingShape = SwingShape::Pyramid;
            j.planeSwingMinRad = 0.1f;
            j.planeSwingMaxRad = 0.5f;
        });
        expectRefused("pyramid range below zero", invalid, "normalSwing", [](JointDesc& j) {
            j.swingShape = SwingShape::Pyramid;
            j.normalSwingMinRad = -0.5f;
            j.normalSwingMaxRad = -0.1f;
        });
        expectRefused("twist minimum above maximum", invalid, "twistMinRad", [](JointDesc& j) {
            j.twistMinRad = 0.3f;
            j.twistMaxRad = -0.3f;
        });
        expectRefused("twist beyond pi", invalid, "twistMaxRad", [](JointDesc& j) { j.twistMaxRad = 4.0f; });
        expectRefused("unknown swing shape", invalid, "swingShape",
            [](JointDesc& j) { j.swingShape = static_cast<SwingShape>(7); });
    }

    TEST_F(HostedJoltWorldTest, aJointOnADestroyedBodyIsRefusedWithInvalidHandle)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        ASSERT_TRUE(mAdapter->destroyBody(b));
        const CreateResult<JointId> result = mAdapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        EXPECT_EQ(result.error, ErrorCode::InvalidHandle);
    }

    TEST_F(HostedJoltWorldTest, validPyramidAndConeJointsAreAccepted)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        const BodyId c = makeSphere(Vec3{ 0.0f, 0.0f, 1.0f }, 0.2f);

        const CreateResult<JointId> cone = mAdapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        EXPECT_TRUE(cone.ok()) << cone.message;

        JointDesc pyramid = validJoint(b, c, Vec3{ 0.0f, 0.0f, 1.5f });
        pyramid.swingShape = SwingShape::Pyramid;
        pyramid.planeSwingMinRad = -0.1f;
        pyramid.planeSwingMaxRad = 0.8f;
        pyramid.normalSwingMinRad = -0.4f;
        pyramid.normalSwingMaxRad = 0.0f; // a range that touches 0 is allowed
        const CreateResult<JointId> pyramidResult = mAdapter->createJoint(pyramid);
        EXPECT_TRUE(pyramidResult.ok()) << pyramidResult.message;
    }

    TEST_F(HostedJoltWorldTest, motorsAndJointStepSettingsAreValidated)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        const CreateResult<JointId> joint = mAdapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        ASSERT_TRUE(joint.ok());

        EXPECT_TRUE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, 1.0f, 100.0f }));
        EXPECT_TRUE(mAdapter->setJointMotor(joint.id, JointMotor{ 0.0f, 0.0f, 0.0f })); // off
        EXPECT_TRUE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, 1.0f, kFloatMax })); // "unlimited"
        EXPECT_TRUE(mAdapter->setJointMotor(joint.id, JointMotor{})); // all defaults

        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ kNaN, 1.0f, 100.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, kNaN, 100.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, 1.0f, kNaN }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ -1.0f, 1.0f, 100.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, -1.0f, 100.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, 1.0f, -5.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ kInf, 1.0f, 100.0f }));
        EXPECT_FALSE(mAdapter->setJointMotor(joint.id, JointMotor{ 10.0f, 1.0f, kInf }));

        EXPECT_TRUE(mAdapter->setJointPositionSteps(joint.id, 1));
        EXPECT_TRUE(mAdapter->setJointPositionSteps(joint.id, 255));
        EXPECT_FALSE(mAdapter->setJointPositionSteps(joint.id, 0));
        EXPECT_FALSE(mAdapter->setJointPositionSteps(joint.id, 256));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: units
    // ---------------------------------------------------------------------------------------------------------------

    TEST(HostedJoltWorldUnits, aHalfMeterSphereIsSeventyFiveJoltUnitsAcrossAtSeventyUnitsPerMeter)
    {
        HostWorld host;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(kUnitsPerMeter));
        ASSERT_NE(adapter, nullptr);

        const std::vector<JPH::BodyID> before = host.allBodies();
        const CreateResult<BodyId> created = adapter->createBody(sphereDesc(Vec3{ 1.0f, 2.0f, 3.0f }, 0.5f));
        ASSERT_TRUE(created.ok()) << created.message;

        const std::vector<JPH::BodyID> added = host.bodiesAddedSince(before);
        ASSERT_EQ(added.size(), 1u);
        // A sphere's inner radius is its radius: 0.5 m * 70 = 35 units.
        JPH::BodyLockRead lock(host.system().GetBodyLockInterface(), added.front());
        ASSERT_TRUE(lock.Succeeded());
        EXPECT_NEAR(lock.GetBody().GetShape()->GetInnerRadius(), 0.5f * kUnitsPerMeter, 1e-3f);
        // The position is converted too (meters -> units), and read back in meters.
        const JPH::Vec3 position = toFloat(lock.GetBody().GetPosition());
        EXPECT_NEAR(position.GetX(), 1.0f * kUnitsPerMeter, 1e-2f);
        EXPECT_NEAR(position.GetY(), 2.0f * kUnitsPerMeter, 1e-2f);
        EXPECT_NEAR(position.GetZ(), 3.0f * kUnitsPerMeter, 1e-2f);

        BodyState state;
        ASSERT_TRUE(adapter->readBody(created.id, state));
        expectVecNear(state.positionM, Vec3{ 1.0f, 2.0f, 3.0f }, 1e-4f);
    }

    TEST(HostedJoltWorldUnits, aBoxIsScaledOnEveryAxisAtSeventyUnitsPerMeter)
    {
        HostWorld host;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(kUnitsPerMeter));
        ASSERT_NE(adapter, nullptr);
        const std::vector<JPH::BodyID> before = host.allBodies();
        ASSERT_TRUE(adapter->createBody(boxDesc(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.4f, 0.3f })).ok());
        const std::vector<JPH::BodyID> added = host.bodiesAddedSince(before);
        ASSERT_EQ(added.size(), 1u);

        const JPH::Vec3 extent = worldBoundsOf(host, added.front()).GetExtent();
        EXPECT_NEAR(extent.GetX(), 0.5f * kUnitsPerMeter, 1e-2f);
        EXPECT_NEAR(extent.GetY(), 0.4f * kUnitsPerMeter, 1e-2f);
        EXPECT_NEAR(extent.GetZ(), 0.3f * kUnitsPerMeter, 1e-2f);
    }

    TEST(HostedJoltWorldUnits, aBodyDroppedTwentyMetersFallsTheExpectedDistanceInOneSecond)
    {
        // The game's gravity in Jolt units: 8.96 * 70 along -Z (task 0035, "Units").
        HostWorld host(JPH::Vec3(0.0f, 0.0f, -kGameGravityConst * kUnitsPerMeter));
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
            = makeAdapter(host, makeConfig(kUnitsPerMeter, &stepper));
        ASSERT_NE(adapter, nullptr);

        constexpr float kDropHeightM = 20.0f;
        BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, kDropHeightM });
        desc.linearDamping = 0.0f; // nothing slows the fall except Jolt's own velocity limit
        const CreateResult<BodyId> created = adapter->createBody(desc);
        ASSERT_TRUE(created.ok());
        const std::vector<JPH::BodyID> ids = host.allBodies();
        ASSERT_EQ(ids.size(), 1u);

        for (int i = 0; i < kOneSecondOfSteps; ++i)
            adapter->step();

        // d = 1/2 g t^2 with t = 1 s. 15 percent is the spec's tolerance (Jolt clamps speed at 500 units/s, which
        // shortens the fall by a few percent).
        constexpr float kExpectedFallM = 0.5f * kGameGravityConst;
        constexpr float kExpectedFallUnits = kExpectedFallM * kUnitsPerMeter;
        constexpr float kTolerance = 0.15f;

        BodyState state;
        ASSERT_TRUE(adapter->readBody(created.id, state));
        EXPECT_NEAR(kDropHeightM - state.positionM.z, kExpectedFallM, kTolerance * kExpectedFallM);

        const float finalZUnits = toFloat(host.bodies().GetPosition(ids.front())).GetZ();
        const float fallenUnits = kDropHeightM * kUnitsPerMeter - finalZUnits;
        EXPECT_NEAR(fallenUnits, kExpectedFallUnits, kTolerance * kExpectedFallUnits);
    }

    TEST(HostedJoltWorldUnits, anImpulseOfOneNewtonSecondOnOneKilogramGivesOneMeterPerSecond)
    {
        // No gravity, so the only velocity is the one the impulse gives.
        HostWorld host(JPH::Vec3::sZero());
        UpdateStepper stepper(host);
        for (const float unitsPerMeter : { 1.0f, kUnitsPerMeter })
        {
            SCOPED_TRACE(unitsPerMeter);
            const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
                = makeAdapter(host, makeConfig(unitsPerMeter, &stepper));
            ASSERT_NE(adapter, nullptr);

            BodyDesc desc = sphereDesc(Vec3{ 0.0f, 0.0f, 5.0f });
            desc.massKg = 1.0f;
            desc.linearDamping = 0.0f;
            desc.angularDamping = 0.0f;
            const CreateResult<BodyId> created = adapter->createBody(desc);
            ASSERT_TRUE(created.ok());

            // Applied at the centre of mass, so the body does not spin.
            ASSERT_TRUE(adapter->applyImpulse(created.id, Vec3{ 1.0f, 0.0f, 0.0f }, desc.positionM));
            adapter->step();

            BodyState state;
            ASSERT_TRUE(adapter->readBody(created.id, state));
            EXPECT_NEAR(state.linearVelocityMps.x, 1.0f, 0.02f);
            EXPECT_NEAR(state.linearVelocityMps.y, 0.0f, 0.02f);
            EXPECT_NEAR(state.linearVelocityMps.z, 0.0f, 0.02f);
        }
    }

    TEST(HostedJoltWorldUnits, setVelocityTakesMetersPerSecond)
    {
        HostWorld host(JPH::Vec3::sZero());
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(kUnitsPerMeter));
        ASSERT_NE(adapter, nullptr);
        const CreateResult<BodyId> created = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 5.0f }));
        ASSERT_TRUE(created.ok());
        const std::vector<JPH::BodyID> ids = host.allBodies();

        ASSERT_TRUE(adapter->setVelocity(created.id, Vec3{ 2.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 3.0f }));
        BodyState state;
        ASSERT_TRUE(adapter->readBody(created.id, state));
        expectVecNear(state.linearVelocityMps, Vec3{ 2.0f, 0.0f, 0.0f }, 1e-3f);
        // Angular velocity is in radians per second and does not change with the length unit.
        expectVecNear(state.angularVelocityRadps, Vec3{ 0.0f, 0.0f, 3.0f }, 1e-3f);
        // In Jolt units the linear speed is 70 times bigger.
        EXPECT_NEAR(host.bodies().GetLinearVelocity(ids.front()).GetX(), 2.0f * kUnitsPerMeter, 1e-2f);
        EXPECT_NEAR(host.bodies().GetAngularVelocity(ids.front()).GetZ(), 3.0f, 1e-3f);
    }

    // A hinge: a static plate and a dynamic box on one axis, joined by a joint that may only twist about X, with a
    // motor pulling it to a target angle. No gravity, so the motor alone decides where it settles.
    struct HingeResult
    {
        float angleRad = 0.0f;
        bool finite = false;
    };

    HingeResult runMotorHinge(float unitsPerMeter, float targetAngleRad, JointMotor motor)
    {
        HostWorld host(JPH::Vec3::sZero());
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter
            = makeAdapter(host, makeConfig(unitsPerMeter, &stepper));
        HingeResult result;
        if (adapter == nullptr)
            return result;

        BodyDesc plate = boxDesc(Vec3{ -0.3f, 0.0f, 0.0f }, Vec3{ 0.2f, 0.2f, 0.2f });
        plate.motion = MotionType::Static;
        BodyDesc moving = boxDesc(Vec3{ 0.3f, 0.0f, 0.0f }, Vec3{ 0.2f, 0.2f, 0.2f });
        moving.massKg = 1.0f;
        const BodyId a = adapter->createBody(plate).id;
        const BodyId b = adapter->createBody(moving).id;

        JointDesc joint;
        joint.bodyA = a;
        joint.bodyB = b;
        joint.anchorM = Vec3{ 0.0f, 0.0f, 0.0f };
        joint.twistAxis = Vec3{ 1.0f, 0.0f, 0.0f };
        joint.planeAxis = Vec3{ 0.0f, 1.0f, 0.0f };
        joint.twistMinRad = -1.0f; // a hinge about X: both swing ranges stay [0, 0]
        joint.twistMaxRad = 1.0f;
        const CreateResult<JointId> created = adapter->createJoint(joint);
        if (!created.ok())
            return result;

        adapter->setJointMotor(created.id, motor);
        adapter->setJointTarget(created.id, quatAbout(1.0f, 0.0f, 0.0f, targetAngleRad));

        // Four seconds is far longer than the spring needs to settle with these gains.
        constexpr int kSettleSteps = 240;
        result.finite = true;
        for (int i = 0; i < kSettleSteps; ++i)
        {
            adapter->step();
            BodyState state;
            adapter->readBody(b, state);
            result.finite = result.finite && isFiniteState(state);
            result.angleRad = twistAngleAboutX(state.rotation);
        }
        return result;
    }

    TEST(HostedJoltWorldUnits, aMotorHoldsAHingeAtItsTargetAngleAtAnyLengthUnit)
    {
        // The spring is 50 N m/rad with 3 N m s/rad of damping on a 1 kg, 0.4 m box: well damped. The tolerance is
        // 0.05 rad (about 3 degrees), a few percent of the 0.5 rad target.
        constexpr float kTargetRad = 0.5f;
        constexpr float kToleranceRad = 0.05f;
        const JointMotor motor{ 50.0f, 3.0f, kFloatMax };

        for (const float unitsPerMeter : { 1.0f, kUnitsPerMeter })
        {
            SCOPED_TRACE(unitsPerMeter);
            const HingeResult result = runMotorHinge(unitsPerMeter, kTargetRad, motor);
            EXPECT_TRUE(result.finite);
            EXPECT_NEAR(result.angleRad, kTargetRad, kToleranceRad);
        }
    }

    TEST(HostedJoltWorldUnits, anUnlimitedMotorTorqueNeverBecomesInfinity)
    {
        // The default JointMotor maximum is float max; multiplying it by the torque factor (s squared, about 4900)
        // would overflow to infinity, which Jolt must never receive. Nothing may be NaN or infinite afterwards.
        const JointMotor unlimited{ 50.0f, 3.0f, std::numeric_limits<float>::max() };
        const HingeResult result = runMotorHinge(kUnitsPerMeter, 0.5f, unlimited);
        EXPECT_TRUE(result.finite);
        EXPECT_TRUE(std::isfinite(result.angleRad));
    }

    TEST(HostedJoltWorldUnits, resultsEqualDirectJoltCallsAtOneUnitPerMeterBitForBit)
    {
        // The same scene twice: once through the adapter (unitsPerMeter 1), once with plain Jolt calls that follow
        // the spec's mapping. With no conversion in the way the numbers must be identical, not merely close.
        constexpr float kSphereRadius = 0.3f;
        constexpr float kSphereMass = 2.0f;
        constexpr float kFriction = 0.4f;
        constexpr float kRestitution = 0.3f;
        constexpr float kLinearDamping = 0.1f;
        constexpr float kAngularDamping = 0.2f;
        const Vec3 kBoxHalf{ 0.5f, 0.3f, 0.2f };
        constexpr int kSteps = 120;
        const Vec3 spherePosition{ 0.0f, 0.0f, 2.0f };
        const Vec3 boxPosition{ 3.0f, 0.0f, 2.0f };
        // An exactly unit, tilted rotation (120 degrees about the (1,1,1) diagonal), so the box tumbles when it lands
        // and no renormalising can change a single bit.
        const Quat boxRotation{ 0.5f, 0.5f, 0.5f, 0.5f };

        // --- Through the adapter.
        HostWorld hostA;
        UpdateStepper stepper(hostA);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(hostA, makeConfig(1.0f, &stepper));
        ASSERT_NE(adapter, nullptr);
        hostA.addHostBox(JPH::Vec3(10.0f, 10.0f, 0.5f), JPH::RVec3(0.0, 0.0, -0.5));
        BodyDesc sphere = sphereDesc(spherePosition, kSphereRadius);
        sphere.massKg = kSphereMass;
        sphere.friction = kFriction;
        sphere.restitution = kRestitution;
        sphere.linearDamping = kLinearDamping;
        sphere.angularDamping = kAngularDamping;
        BodyDesc box = boxDesc(boxPosition, kBoxHalf);
        box.rotation = boxRotation;
        box.massKg = kSphereMass;
        box.friction = kFriction;
        box.restitution = kRestitution;
        box.linearDamping = kLinearDamping;
        box.angularDamping = kAngularDamping;
        const BodyId adapterSphere = adapter->createBody(sphere).id;
        const BodyId adapterBox = adapter->createBody(box).id;
        ASSERT_TRUE(adapterSphere.isValid() && adapterBox.isValid());
        for (int i = 0; i < kSteps; ++i)
            adapter->step();

        // --- Directly. Same creation order, so Jolt hands out the same body IDs.
        HostWorld hostB;
        hostB.addHostBox(JPH::Vec3(10.0f, 10.0f, 0.5f), JPH::RVec3(0.0, 0.0, -0.5));
        const auto makeDirect = [&](const JPH::Shape* shape, const Vec3& position, JPH::Quat rotation) {
            JPH::BodyCreationSettings settings(shape, JPH::RVec3(position.x, position.y, position.z), rotation,
                JPH::EMotionType::Dynamic, kLayerMoving);
            settings.mUserData = 0;
            settings.mFriction = kFriction;
            settings.mRestitution = kRestitution;
            settings.mLinearDamping = kLinearDamping;
            settings.mAngularDamping = kAngularDamping;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = kSphereMass;
            return hostB.bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
        };
        const JPH::RefConst<JPH::Shape> directSphereShape = new JPH::SphereShape(kSphereRadius);
        // The box's convex radius is the smaller of Jolt's default and half its smallest half extent.
        const float smallestHalf = std::min({ kBoxHalf.x, kBoxHalf.y, kBoxHalf.z });
        const float boxRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * smallestHalf);
        const JPH::RefConst<JPH::Shape> directBoxShape
            = new JPH::BoxShape(JPH::Vec3(kBoxHalf.x, kBoxHalf.y, kBoxHalf.z), boxRadius);
        const JPH::BodyID directSphere = makeDirect(directSphereShape, spherePosition, JPH::Quat::sIdentity());
        const JPH::BodyID directBox = makeDirect(
            directBoxShape, boxPosition, JPH::Quat(boxRotation.x, boxRotation.y, boxRotation.z, boxRotation.w));
        for (int i = 0; i < kSteps; ++i)
            hostB.update(kStepSeconds);

        const auto expectSame = [](const JPH::BodyInterface& direct, JPH::BodyID id, const BodyState& adapted) {
            const JPH::Vec3 position = toFloat(direct.GetPosition(id));
            const JPH::Quat rotation = direct.GetRotation(id);
            const JPH::Vec3 velocity = direct.GetLinearVelocity(id);
            const JPH::Vec3 angular = direct.GetAngularVelocity(id);
            EXPECT_EQ(adapted.positionM.x, position.GetX());
            EXPECT_EQ(adapted.positionM.y, position.GetY());
            EXPECT_EQ(adapted.positionM.z, position.GetZ());
            EXPECT_EQ(adapted.rotation.x, rotation.GetX());
            EXPECT_EQ(adapted.rotation.y, rotation.GetY());
            EXPECT_EQ(adapted.rotation.z, rotation.GetZ());
            EXPECT_EQ(adapted.rotation.w, rotation.GetW());
            EXPECT_EQ(adapted.linearVelocityMps.x, velocity.GetX());
            EXPECT_EQ(adapted.linearVelocityMps.y, velocity.GetY());
            EXPECT_EQ(adapted.linearVelocityMps.z, velocity.GetZ());
            EXPECT_EQ(adapted.angularVelocityRadps.x, angular.GetX());
            EXPECT_EQ(adapted.angularVelocityRadps.y, angular.GetY());
            EXPECT_EQ(adapted.angularVelocityRadps.z, angular.GetZ());
        };
        BodyState state;
        ASSERT_TRUE(adapter->readBody(adapterSphere, state));
        SCOPED_TRACE("sphere");
        expectSame(hostB.bodies(), directSphere, state);
        ASSERT_TRUE(adapter->readBody(adapterBox, state));
        SCOPED_TRACE("box");
        expectSame(hostB.bodies(), directBox, state);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: joints and collision groups
    // ---------------------------------------------------------------------------------------------------------------

    TEST_F(HostedJoltWorldTest, aConePendulumSwingsAndStaysInsideItsLimits)
    {
        // The cone is centred on the creation pose. The bob starts 0.3 rad off the vertical, so gravity swings it
        // through the vertical to about 0.6 rad from where it started: beyond the 0.5 rad limit, which must hold.
        constexpr float kStartOffVerticalRad = 0.3f;
        constexpr float kConeHalfAngleRad = 0.5f;
        constexpr float kLimitTolerance = 0.06f; // the solver lets a limit stretch a little
        constexpr float kArmLength = 1.0f;
        const Vec3 anchor{ 0.0f, 0.0f, 5.0f };
        const Vec3 startDirection{ std::sin(kStartOffVerticalRad), 0.0f, -std::cos(kStartOffVerticalRad) };

        BodyDesc pivot = boxDesc(anchor, Vec3{ 0.1f, 0.1f, 0.1f });
        pivot.motion = MotionType::Static;
        const BodyId a = mAdapter->createBody(pivot).id;
        const BodyId b = makeSphere(
            Vec3{ anchor.x + startDirection.x * kArmLength, 0.0f, anchor.z + startDirection.z * kArmLength }, 0.2f);
        ASSERT_TRUE(a.isValid() && b.isValid());

        JointDesc joint;
        joint.bodyA = a;
        joint.bodyB = b;
        joint.anchorM = anchor;
        joint.twistAxis = startDirection;
        joint.planeAxis = Vec3{ std::cos(kStartOffVerticalRad), 0.0f, std::sin(kStartOffVerticalRad) };
        joint.planeSwingMinRad = -kConeHalfAngleRad;
        joint.planeSwingMaxRad = kConeHalfAngleRad;
        joint.normalSwingMinRad = -kConeHalfAngleRad;
        joint.normalSwingMaxRad = kConeHalfAngleRad;
        joint.twistMinRad = -0.1f;
        joint.twistMaxRad = 0.1f;
        const CreateResult<JointId> created = mAdapter->createJoint(joint);
        ASSERT_TRUE(created.ok()) << created.message;

        float largestAngle = 0.0f;
        float smallestArm = kArmLength;
        float largestArm = kArmLength;
        for (int i = 0; i < 4 * kOneSecondOfSteps; ++i)
        {
            mAdapter->step();
            BodyState state;
            ASSERT_TRUE(mAdapter->readBody(b, state));
            const Vec3 arm{ state.positionM.x - anchor.x, state.positionM.y - anchor.y, state.positionM.z - anchor.z };
            const float length = std::sqrt(arm.x * arm.x + arm.y * arm.y + arm.z * arm.z);
            const float dotProduct = arm.x * startDirection.x + arm.y * startDirection.y + arm.z * startDirection.z;
            const float cosAngle = dotProduct / length;
            largestAngle = std::max(largestAngle, std::acos(std::min(1.0f, std::max(-1.0f, cosAngle))));
            smallestArm = std::min(smallestArm, length);
            largestArm = std::max(largestArm, length);
        }
        // It swung (more than the 0.3 rad it falls to the vertical), and was stopped by the limit.
        EXPECT_GT(largestAngle, 0.35f);
        EXPECT_LT(largestAngle, kConeHalfAngleRad + kLimitTolerance);
        // The joint also holds the bob at the arm's length.
        EXPECT_GT(smallestArm, kArmLength - 0.05f);
        EXPECT_LT(largestArm, kArmLength + 0.05f);
    }

    // The arm of a pyramid joint lies along +X from the anchor at the origin. The joint's frame is X = twist axis,
    // Y = plane x twist = +Y, Z = plane axis = +Z. A rotation of the arm about +Y by an angle t takes +X to
    // (cos t, 0, -sin t): so a push towards -Z is positive plane swing. A rotation about +Z by t takes +X to
    // (cos t, sin t, 0): a push towards +Y is positive normal swing.
    float largestSwingTowards(Vec3 pushDirection, SwingShape shape, bool measurePlane)
    {
        HostWorld host(JPH::Vec3::sZero());
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        if (adapter == nullptr)
            return -100.0f;

        BodyDesc plate = boxDesc(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.1f, 0.1f, 0.1f });
        plate.motion = MotionType::Static;
        const BodyId a = adapter->createBody(plate).id;
        const BodyId b = adapter->createBody(sphereDesc(Vec3{ 1.0f, 0.0f, 0.0f }, 0.1f)).id;

        JointDesc joint;
        joint.bodyA = a;
        joint.bodyB = b;
        joint.anchorM = Vec3{ 0.0f, 0.0f, 0.0f };
        joint.twistAxis = Vec3{ 1.0f, 0.0f, 0.0f };
        joint.planeAxis = Vec3{ 0.0f, 0.0f, 1.0f };
        joint.swingShape = shape;
        // Unequal ranges: plane swing may go 0.4 rad one way and 0.1 the other; normal swing 0.05 and 0.3.
        joint.planeSwingMinRad = -0.1f;
        joint.planeSwingMaxRad = 0.4f;
        joint.normalSwingMinRad = -0.3f;
        joint.normalSwingMaxRad = 0.05f;
        if (!adapter->createJoint(joint).ok())
            return -100.0f;

        // Push slowly (0.25 m/s) for three seconds: the arm reaches its limit within 1.6 s. Jolt lets a swing limit
        // be passed by an amount proportional to the speed it is reached at (about 0.08 rad at 1 m/s), so a slow push
        // keeps that small and the limit itself can be checked tightly.
        constexpr float kPushSpeed = 0.25f;
        const Vec3 push{ pushDirection.x * kPushSpeed, pushDirection.y * kPushSpeed, pushDirection.z * kPushSpeed };
        adapter->setVelocity(b, push, Vec3{});
        // +1 when the push turns the arm to positive swing angles, -1 when to negative ones.
        const float pushSign = (measurePlane ? -pushDirection.z : pushDirection.y) > 0.0f ? 1.0f : -1.0f;
        float extreme = 0.0f;
        for (int i = 0; i < 3 * kOneSecondOfSteps; ++i)
        {
            adapter->step();
            BodyState state;
            adapter->readBody(b, state);
            const float angle = measurePlane ? std::atan2(-state.positionM.z, state.positionM.x)
                                             : std::atan2(state.positionM.y, state.positionM.x);
            // The furthest the arm got in the pushed direction; what it does after bouncing back is not the question.
            if (pushSign * angle > pushSign * extreme)
                extreme = angle;
        }
        return extreme;
    }

    TEST(HostedJoltWorldJoints, aPyramidJointRespectsItsUnequalRangesOnBothAxes)
    {
        // A solver lets a limit stretch a little, and the arm may bounce back a touch before the end.
        constexpr float kTolerance = 0.06f;
        const SwingShape pyramid = SwingShape::Pyramid;

        // Plane swing (rotation about Y): positive towards -Z up to 0.4, negative towards +Z down to -0.1.
        EXPECT_NEAR(largestSwingTowards(Vec3{ 0.0f, 0.0f, -1.0f }, pyramid, true), 0.4f, kTolerance);
        EXPECT_NEAR(largestSwingTowards(Vec3{ 0.0f, 0.0f, 1.0f }, pyramid, true), -0.1f, kTolerance);
        // Normal swing (rotation about Z): positive towards +Y up to 0.05, negative towards -Y down to -0.3.
        EXPECT_NEAR(largestSwingTowards(Vec3{ 0.0f, 1.0f, 0.0f }, pyramid, false), 0.05f, kTolerance);
        EXPECT_NEAR(largestSwingTowards(Vec3{ 0.0f, -1.0f, 0.0f }, pyramid, false), -0.3f, kTolerance);
    }

    TEST(HostedJoltWorldJoints, setJointTargetMovesBodyBTowardsTheTargetAndBothOverloadsWork)
    {
        constexpr float kTargetRad = 0.5f;
        const HingeResult result = runMotorHinge(1.0f, kTargetRad, JointMotor{ 50.0f, 3.0f, kFloatMax });
        // Not near zero (where B started) but near the target.
        EXPECT_GT(result.angleRad, 0.4f);
        EXPECT_NEAR(result.angleRad, kTargetRad, 0.05f);

        // The second overload (with a feed-forward angular velocity) is accepted too, and returns the same way.
        HostWorld host(JPH::Vec3::sZero());
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig());
        ASSERT_NE(adapter, nullptr);
        const BodyId a = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f)).id;
        const BodyId b = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f)).id;
        const CreateResult<JointId> joint = adapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        ASSERT_TRUE(joint.ok());
        EXPECT_TRUE(adapter->setJointTarget(joint.id, Quat{}));
        EXPECT_TRUE(adapter->setJointTarget(joint.id, Quat{}, Vec3{ 0.0f, 0.1f, 0.0f }));
    }

    // Two overlapping spheres (centres 0.5 m apart, radius 0.5) with no gravity. If they collide they are pushed
    // apart; if not, they stay where they are. Returns the distance between them after one second.
    float overlappingPairDistanceAfterOneSecond(bool subGroupsCollide)
    {
        HostWorld host(JPH::Vec3::sZero());
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        if (adapter == nullptr)
            return -1.0f;

        const CreateResult<CollisionGroupId> group = adapter->createCollisionGroup(2);
        if (!group.ok() || !adapter->setSubGroupsCollide(group.id, 0, 1, subGroupsCollide))
            return -1.0f;

        BodyDesc first = sphereDesc(Vec3{ 0.0f, 0.0f, 5.0f });
        first.collisionGroup = group.id;
        first.subGroup = 0;
        BodyDesc second = sphereDesc(Vec3{ 0.5f, 0.0f, 5.0f });
        second.collisionGroup = group.id;
        second.subGroup = 1;
        const BodyId a = adapter->createBody(first).id;
        const BodyId b = adapter->createBody(second).id;
        if (!a.isValid() || !b.isValid())
            return -1.0f;

        for (int i = 0; i < kOneSecondOfSteps; ++i)
            adapter->step();
        BodyState stateA;
        BodyState stateB;
        adapter->readBody(a, stateA);
        adapter->readBody(b, stateB);
        const float dx = stateB.positionM.x - stateA.positionM.x;
        const float dy = stateB.positionM.y - stateA.positionM.y;
        const float dz = stateB.positionM.z - stateA.positionM.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    TEST(HostedJoltWorldGroups, subGroupsSetNotToCollideSkipEachOtherAndSubGroupsSetToCollideDoNot)
    {
        // Radius 0.5 each: touching at a distance of 1.0. Skipping means they stay at their 0.5 start.
        EXPECT_LT(overlappingPairDistanceAfterOneSecond(false), 0.55f);
        EXPECT_GT(overlappingPairDistanceAfterOneSecond(true), 0.9f);
    }

    TEST_F(HostedJoltWorldTest, destroyingABodyDestroysItsJoints)
    {
        const std::size_t constraintsBefore = mHost.numConstraints();
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        const BodyId c = makeSphere(Vec3{ 0.0f, 0.0f, 1.0f }, 0.2f);
        const CreateResult<JointId> ab = mAdapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        const CreateResult<JointId> bc = mAdapter->createJoint(validJoint(b, c, Vec3{ 0.0f, 0.0f, 1.5f }));
        ASSERT_TRUE(ab.ok() && bc.ok());
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore + 2);

        // b has both joints: destroying it takes both with it.
        EXPECT_TRUE(mAdapter->destroyBody(b));
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore);
        EXPECT_FALSE(mAdapter->destroyJoint(ab.id));
        EXPECT_FALSE(mAdapter->destroyJoint(bc.id));
        EXPECT_FALSE(mAdapter->setJointMotor(ab.id, JointMotor{ 1.0f, 1.0f, 1.0f }));
        BodyState state;
        EXPECT_TRUE(mAdapter->readBody(a, state));
        EXPECT_TRUE(mAdapter->readBody(c, state));
    }

    TEST_F(HostedJoltWorldTest, destroyJointRemovesTheConstraintFromTheHost)
    {
        const std::size_t constraintsBefore = mHost.numConstraints();
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        const BodyId b = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        const CreateResult<JointId> joint = mAdapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f }));
        ASSERT_TRUE(joint.ok());
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore + 1);
        EXPECT_TRUE(mAdapter->destroyJoint(joint.id));
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore);
        EXPECT_FALSE(mAdapter->destroyJoint(joint.id));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: stepping
    // ---------------------------------------------------------------------------------------------------------------

    // Everything the stepping tests observe, in the order it happened.
    struct EventLog
    {
        std::vector<std::string> events;
        std::vector<float> dts;
    };

    class LoggingListener : public StepListener
    {
    public:
        LoggingListener(std::string name, EventLog& log)
            : mName(std::move(name))
            , mLog(log)
        {
        }

        void onPreStep(PhysicsWorld&, float dtSeconds) override
        {
            mLog.events.push_back(mName + ".pre");
            mLog.dts.push_back(dtSeconds);
        }

        void onPostStep(PhysicsWorld&, float dtSeconds) override
        {
            mLog.events.push_back(mName + ".post");
            mLog.dts.push_back(dtSeconds);
        }

    protected:
        std::string mName;
        EventLog& mLog;
    };

    // A stepper that logs when the host's Update runs, so the order against the listeners is visible.
    class LoggingStepper final : public CMPhysics::SystemStepper
    {
    public:
        LoggingStepper(HostWorld& host, EventLog& log)
            : mHost(host)
            , mLog(log)
        {
        }

        void update(float dtSeconds) override
        {
            mLog.events.push_back("update");
            mLog.dts.push_back(dtSeconds);
            mHost.update(dtSeconds);
        }

    private:
        HostWorld& mHost;
        EventLog& mLog;
    };

    // A non-default step length makes "dt equals stepSeconds" a real check.
    constexpr float kOddStepSeconds = 1.0f / 30.0f;

    CMPhysics::HostedWorldConfig oddStepConfig(CMPhysics::SystemStepper* stepper)
    {
        CMPhysics::HostedWorldConfig config = makeConfig(1.0f, stepper);
        config.stepSeconds = kOddStepSeconds;
        return config;
    }

    TEST(HostedJoltWorldStepping, stepRunsPreListenersThenUpdateThenPostListenersEachOnceWithTheConfiguredDt)
    {
        HostWorld host;
        EventLog log;
        LoggingStepper stepper(host, log);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, oddStepConfig(&stepper));
        ASSERT_NE(adapter, nullptr);
        EXPECT_EQ(adapter->fixedStepSeconds(), kOddStepSeconds);

        LoggingListener first("first", log);
        LoggingListener second("second", log);
        ASSERT_TRUE(adapter->addStepListener(first).isValid());
        ASSERT_TRUE(adapter->addStepListener(second).isValid());

        adapter->step();
        adapter->step();

        const std::vector<std::string> oneStep
            = { "first.pre", "second.pre", "update", "first.post", "second.post" };
        std::vector<std::string> expected = oneStep;
        expected.insert(expected.end(), oneStep.begin(), oneStep.end());
        EXPECT_EQ(log.events, expected);
        for (const float dt : log.dts)
            EXPECT_EQ(dt, kOddStepSeconds);
    }

    TEST(HostedJoltWorldStepping, beginAndEndStepAroundTheHostsOwnUpdateGiveTheSameOrder)
    {
        HostWorld host;
        EventLog log;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, oddStepConfig(nullptr));
        ASSERT_NE(adapter, nullptr);
        LoggingListener first("first", log);
        LoggingListener second("second", log);
        adapter->addStepListener(first);
        adapter->addStepListener(second);

        adapter->beginStep();
        log.events.push_back("update");
        host.update(kStepSeconds);
        adapter->endStep();

        const std::vector<std::string> expected = { "first.pre", "second.pre", "update", "first.post", "second.post" };
        EXPECT_EQ(log.events, expected);
        for (std::size_t i = 0; i < log.dts.size(); ++i)
            EXPECT_EQ(log.dts[i], kOddStepSeconds) << i;
    }

    TEST(HostedJoltWorldStepping, beginStepInsideAStepAndEndStepOutsideOneDoNothing)
    {
        HostWorld host;
        EventLog log;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig());
        ASSERT_NE(adapter, nullptr);
        LoggingListener listener("l", log);
        adapter->addStepListener(listener);

        adapter->endStep(); // not in a step
        EXPECT_TRUE(log.events.empty());

        adapter->beginStep();
        adapter->beginStep(); // already in a step
        const std::vector<std::string> afterBegin = { "l.pre" };
        EXPECT_EQ(log.events, afterBegin);

        adapter->endStep();
        adapter->endStep(); // the step is over
        const std::vector<std::string> afterEnd = { "l.pre", "l.post" };
        EXPECT_EQ(log.events, afterEnd);
    }

    TEST(HostedJoltWorldStepping, stepWithoutAStepperDoesNothing)
    {
        // The spec: "Without a stepper, step() does nothing" (no Update, and no listener calls either).
        HostWorld host;
        EventLog log;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, nullptr));
        ASSERT_NE(adapter, nullptr);
        LoggingListener listener("l", log);
        adapter->addStepListener(listener);
        const BodyId body = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 10.0f })).id;
        BodyState before;
        ASSERT_TRUE(adapter->readBody(body, before));

        for (int i = 0; i < 10; ++i)
            adapter->step();

        EXPECT_TRUE(log.events.empty());
        BodyState after;
        ASSERT_TRUE(adapter->readBody(body, after));
        EXPECT_EQ(after.positionM, before.positionM);
    }

    // Tries every structural call from inside a step callback and remembers what came back.
    class StructuralProbe final : public StepListener
    {
    public:
        BodyId body;
        BodyId otherBody;
        JointId joint;
        CollisionGroupId group;
        JointDesc jointDesc;

        ErrorCode createBodyError = ErrorCode::None;
        ErrorCode createJointError = ErrorCode::None;
        ErrorCode createGroupError = ErrorCode::None;
        bool destroyBodyResult = true;
        bool destroyJointResult = true;
        bool setSubGroupsResult = true;
        bool destroyGroupResult = true;
        bool setDampingResult = true;
        bool setPositionStepsResult = true;
        bool setMotionTypeResult = true;
        bool setVelocityResult = false; // driving a body is allowed during a step
        bool readResult = false;
        int preCalls = 0;

        void onPreStep(PhysicsWorld& world, float) override
        {
            ++preCalls;
            createBodyError = world.createBody(sphereDesc(Vec3{ 9.0f, 9.0f, 9.0f })).error;
            createJointError = world.createJoint(jointDesc).error;
            createGroupError = world.createCollisionGroup(2).error;
            destroyBodyResult = world.destroyBody(otherBody);
            destroyJointResult = world.destroyJoint(joint);
            setSubGroupsResult = world.setSubGroupsCollide(group, 0, 1, false);
            destroyGroupResult = world.destroyCollisionGroup(group);
            setDampingResult = world.setBodyDamping(body, 0.5f, 0.5f);
            setPositionStepsResult = world.setJointPositionSteps(joint, 5);
            setMotionTypeResult = world.setMotionType(body, MotionType::Kinematic);
            setVelocityResult = world.setVelocity(body, Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{});
            BodyState state;
            readResult = world.readBody(body, state);
        }

        void onPostStep(PhysicsWorld&, float) override {}
    };

    TEST_F(HostedJoltWorldTest, structuralCallsInsideAListenerFailWithCalledDuringStep)
    {
        StructuralProbe probe;
        probe.body = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
        probe.otherBody = makeSphere(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
        probe.jointDesc = validJoint(probe.body, probe.otherBody, Vec3{ 0.0f, 0.0f, 2.5f });
        const CreateResult<JointId> joint = mAdapter->createJoint(probe.jointDesc);
        ASSERT_TRUE(joint.ok());
        probe.joint = joint.id;
        const CreateResult<CollisionGroupId> group = mAdapter->createCollisionGroup(2);
        ASSERT_TRUE(group.ok());
        probe.group = group.id;
        const std::size_t bodiesBefore = mHost.numBodies();

        const StepListenerId probeId = mAdapter->addStepListener(probe);
        mAdapter->step();
        mAdapter->removeStepListener(probeId); // listeners are not owned by the world: remove before destroying

        ASSERT_EQ(probe.preCalls, 1);
        EXPECT_EQ(probe.createBodyError, ErrorCode::CalledDuringStep);
        EXPECT_EQ(probe.createJointError, ErrorCode::CalledDuringStep);
        EXPECT_EQ(probe.createGroupError, ErrorCode::CalledDuringStep);
        EXPECT_FALSE(probe.destroyBodyResult);
        EXPECT_FALSE(probe.destroyJointResult);
        EXPECT_FALSE(probe.setSubGroupsResult);
        EXPECT_FALSE(probe.destroyGroupResult);
        EXPECT_FALSE(probe.setDampingResult);
        EXPECT_FALSE(probe.setPositionStepsResult);
        EXPECT_FALSE(probe.setMotionTypeResult);
        // Reading and driving are fine during a step.
        EXPECT_TRUE(probe.setVelocityResult);
        EXPECT_TRUE(probe.readResult);
        // Nothing was created or destroyed, and the refused changes did not happen.
        EXPECT_EQ(mHost.numBodies(), bodiesBefore);
        EXPECT_EQ(mAdapter->getMotionType(probe.body), MotionType::Dynamic);
        // After the step the same calls work again.
        EXPECT_TRUE(mAdapter->setBodyDamping(probe.body, 0.5f, 0.5f));
        EXPECT_TRUE(mAdapter->createBody(sphereDesc(Vec3{ 9.0f, 9.0f, 9.0f })).ok());
    }

    class AddingListener final : public LoggingListener
    {
    public:
        AddingListener(std::string name, EventLog& log, StepListener& toAdd)
            : LoggingListener(std::move(name), log)
            , mToAdd(toAdd)
        {
        }

        void onPreStep(PhysicsWorld& world, float dtSeconds) override
        {
            LoggingListener::onPreStep(world, dtSeconds);
            if (!mAdded)
            {
                mAdded = true;
                mAddedId = world.addStepListener(mToAdd);
            }
        }

        StepListenerId mAddedId;

    private:
        StepListener& mToAdd;
        bool mAdded = false;
    };

    TEST(HostedJoltWorldStepping, aListenerAddedDuringAStepIsFirstCalledOnTheNextStep)
    {
        HostWorld host;
        EventLog log;
        LoggingStepper stepper(host, log);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        ASSERT_NE(adapter, nullptr);

        LoggingListener late("late", log);
        AddingListener adder("adder", log, late);
        adapter->addStepListener(adder);

        adapter->step();
        const std::vector<std::string> firstStep = { "adder.pre", "update", "adder.post" };
        EXPECT_EQ(log.events, firstStep);
        EXPECT_TRUE(adder.mAddedId.isValid());

        log.events.clear();
        adapter->step();
        const std::vector<std::string> secondStep = { "adder.pre", "late.pre", "update", "adder.post", "late.post" };
        EXPECT_EQ(log.events, secondStep);
    }

    class RemovingListener final : public LoggingListener
    {
    public:
        RemovingListener(std::string name, EventLog& log)
            : LoggingListener(std::move(name), log)
        {
        }

        StepListenerId victim;
        bool removedVictim = false;

        void onPreStep(PhysicsWorld& world, float dtSeconds) override
        {
            LoggingListener::onPreStep(world, dtSeconds);
            if (victim.isValid())
            {
                removedVictim = world.removeStepListener(victim);
                victim = StepListenerId{};
            }
        }
    };

    TEST(HostedJoltWorldStepping, aListenerRemovedDuringAStepIsNotCalledAgainAndTheOthersAreUnaffected)
    {
        HostWorld host;
        EventLog log;
        LoggingStepper stepper(host, log);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        ASSERT_NE(adapter, nullptr);

        RemovingListener remover("remover", log);
        LoggingListener victim("victim", log);
        LoggingListener bystander("bystander", log);
        adapter->addStepListener(remover);
        remover.victim = adapter->addStepListener(victim);
        adapter->addStepListener(bystander);

        adapter->step();
        EXPECT_TRUE(remover.removedVictim);
        // The victim is removed by the remover's pre-step, so it is never called at all.
        const std::vector<std::string> firstStep
            = { "remover.pre", "bystander.pre", "update", "remover.post", "bystander.post" };
        EXPECT_EQ(log.events, firstStep);

        log.events.clear();
        adapter->step();
        const std::vector<std::string> secondStep
            = { "remover.pre", "bystander.pre", "update", "remover.post", "bystander.post" };
        EXPECT_EQ(log.events, secondStep);
    }

    class SelfRemovingListener final : public LoggingListener
    {
    public:
        using LoggingListener::LoggingListener;
        StepListenerId myId;

        void onPostStep(PhysicsWorld& world, float dtSeconds) override
        {
            LoggingListener::onPostStep(world, dtSeconds);
            world.removeStepListener(myId);
        }
    };

    TEST(HostedJoltWorldStepping, aListenerThatRemovesItselfInItsPostStepIsNotCalledOnTheNextStep)
    {
        HostWorld host;
        EventLog log;
        LoggingStepper stepper(host, log);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        ASSERT_NE(adapter, nullptr);

        SelfRemovingListener once("once", log);
        once.myId = adapter->addStepListener(once);
        adapter->step();
        const std::vector<std::string> firstStep = { "once.pre", "update", "once.post" };
        EXPECT_EQ(log.events, firstStep);

        log.events.clear();
        adapter->step();
        const std::vector<std::string> secondStep = { "update" };
        EXPECT_EQ(log.events, secondStep);
    }

    TEST(HostedJoltWorldStepping, removingAListenerOutsideAStepStopsItAndAStaleIdIsRefused)
    {
        HostWorld host;
        EventLog log;
        LoggingStepper stepper(host, log);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        ASSERT_NE(adapter, nullptr);

        LoggingListener listener("l", log);
        const StepListenerId id = adapter->addStepListener(listener);
        ASSERT_TRUE(id.isValid());
        EXPECT_TRUE(adapter->removeStepListener(id));
        EXPECT_FALSE(adapter->removeStepListener(id));
        EXPECT_FALSE(adapter->removeStepListener(StepListenerId{}));

        adapter->step();
        const std::vector<std::string> expected = { "update" };
        EXPECT_EQ(log.events, expected);
    }

    TEST(HostedJoltWorldStepping, atMostSixteenListenersCanBeRegistered)
    {
        // The spec: "a fixed number of listeners: 16, creation fails beyond it" (an invalid id is returned).
        HostWorld host;
        EventLog log;
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig());
        ASSERT_NE(adapter, nullptr);

        constexpr int kMaxListeners = 16;
        std::vector<std::unique_ptr<LoggingListener>> listeners;
        for (int i = 0; i < kMaxListeners; ++i)
        {
            listeners.push_back(std::make_unique<LoggingListener>("l" + std::to_string(i), log));
            EXPECT_TRUE(adapter->addStepListener(*listeners.back()).isValid()) << i;
        }
        LoggingListener extra("extra", log);
        EXPECT_FALSE(adapter->addStepListener(extra).isValid());
    }

    TEST_F(HostedJoltWorldTest, aKinematicBodyEndsTheStepAtItsTargetWithZeroVelocity)
    {
        BodyDesc desc = boxDesc(Vec3{ 0.0f, 0.0f, 3.0f }, Vec3{ 0.3f, 0.3f, 0.3f });
        desc.motion = MotionType::Kinematic;
        const CreateResult<BodyId> created = mAdapter->createBody(desc);
        ASSERT_TRUE(created.ok()) << created.message;

        const Vec3 target{ 2.0f, 1.0f, 3.5f };
        const Quat targetRotation = quatAbout(0.0f, 0.0f, 1.0f, 0.6f);
        ASSERT_TRUE(mAdapter->setKinematicTarget(created.id, target, targetRotation));
        mAdapter->step();

        BodyState state;
        ASSERT_TRUE(mAdapter->readBody(created.id, state));
        expectVecNear(state.positionM, target, 1e-3f);
        EXPECT_NEAR(state.rotation.z, targetRotation.z, 1e-3f);
        EXPECT_NEAR(state.rotation.w, targetRotation.w, 1e-3f);
        expectVecNear(state.linearVelocityMps, Vec3{}, 1e-5f);
        expectVecNear(state.angularVelocityRadps, Vec3{}, 1e-5f);

        // It stays there until moved again.
        mAdapter->step();
        ASSERT_TRUE(mAdapter->readBody(created.id, state));
        expectVecNear(state.positionM, target, 1e-3f);
        expectVecNear(state.linearVelocityMps, Vec3{}, 1e-5f);
    }

    TEST_F(HostedJoltWorldTest, setKinematicTargetIsRefusedForADynamicBody)
    {
        const BodyId dynamic = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        EXPECT_FALSE(mAdapter->setKinematicTarget(dynamic, Vec3{ 1.0f, 0.0f, 0.0f }, Quat{}));
    }

    TEST_F(HostedJoltWorldTest, setBodyDampingChangesHowFastADynamicBodySlowsDown)
    {
        mHost.system().SetGravity(JPH::Vec3::sZero());
        const BodyId slowed = makeSphere(Vec3{ 0.0f, 0.0f, 5.0f });
        const BodyId free = makeSphere(Vec3{ 5.0f, 0.0f, 5.0f });
        ASSERT_TRUE(mAdapter->setBodyDamping(slowed, 5.0f, 0.0f));
        ASSERT_TRUE(mAdapter->setBodyDamping(free, 0.0f, 0.0f));
        ASSERT_TRUE(mAdapter->setVelocity(slowed, Vec3{ 4.0f, 0.0f, 0.0f }, Vec3{}));
        ASSERT_TRUE(mAdapter->setVelocity(free, Vec3{ 4.0f, 0.0f, 0.0f }, Vec3{}));
        stepMany(kOneSecondOfSteps);

        BodyState slowedState;
        BodyState freeState;
        ASSERT_TRUE(mAdapter->readBody(slowed, slowedState));
        ASSERT_TRUE(mAdapter->readBody(free, freeState));
        EXPECT_NEAR(freeState.linearVelocityMps.x, 4.0f, 0.01f);
        EXPECT_LT(slowedState.linearVelocityMps.x, 0.1f * freeState.linearVelocityMps.x);

        // Damping must be >= 0 and finite.
        EXPECT_FALSE(mAdapter->setBodyDamping(slowed, -1.0f, 0.0f));
        EXPECT_FALSE(mAdapter->setBodyDamping(slowed, 0.0f, kNaN));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: ray
    // ---------------------------------------------------------------------------------------------------------------

    constexpr float kRayTolerance = 1e-3f;

    TEST(HostedJoltWorldRay, hitsTheAdaptersBodyAndABodyTheHostCreatedOutsideIt)
    {
        HostWorld host(JPH::Vec3::sZero());
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig());
        ASSERT_NE(adapter, nullptr);

        const BodyId mine = adapter->createBody(boxDesc(Vec3{ 5.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;
        ASSERT_TRUE(mine.isValid());
        // A body in the host's world that the adapter knows nothing about (like an actor or the static world).
        host.addHostBox(JPH::Vec3(0.5f, 0.5f, 0.5f), JPH::RVec3(0.0, 5.0, 0.0));

        const std::optional<RayHit> hitMine = adapter->castRay(Vec3{}, Vec3{ 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(hitMine.has_value());
        EXPECT_EQ(hitMine->body, mine);
        expectVecNear(hitMine->pointM, Vec3{ 4.5f, 0.0f, 0.0f }, kRayTolerance);
        expectVecNear(hitMine->normal, Vec3{ -1.0f, 0.0f, 0.0f }, kRayTolerance);
        EXPECT_NEAR(hitMine->fraction, 0.45f, kRayTolerance);

        const std::optional<RayHit> hitHost = adapter->castRay(Vec3{}, Vec3{ 0.0f, 10.0f, 0.0f });
        ASSERT_TRUE(hitHost.has_value());
        EXPECT_FALSE(hitHost->body.isValid()); // not the adapter's body: an invalid id, but a correct hit
        expectVecNear(hitHost->pointM, Vec3{ 0.0f, 4.5f, 0.0f }, kRayTolerance);
        expectVecNear(hitHost->normal, Vec3{ 0.0f, -1.0f, 0.0f }, kRayTolerance);
        EXPECT_NEAR(hitHost->fraction, 0.45f, kRayTolerance);

        // Free space.
        EXPECT_FALSE(adapter->castRay(Vec3{}, Vec3{ -10.0f, 0.0f, 0.0f }).has_value());
        EXPECT_FALSE(adapter->castRay(Vec3{}, Vec3{ 0.0f, 0.0f, 10.0f }).has_value());
        // A ray that stops short of the box.
        EXPECT_FALSE(adapter->castRay(Vec3{}, Vec3{ 3.0f, 0.0f, 0.0f }).has_value());
    }

    TEST(HostedJoltWorldRay, ignoreSkipsOneBodyAndTheRayContinuesToTheNext)
    {
        HostWorld host(JPH::Vec3::sZero());
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig());
        ASSERT_NE(adapter, nullptr);
        const BodyId near = adapter->createBody(boxDesc(Vec3{ 5.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;
        const BodyId far = adapter->createBody(boxDesc(Vec3{ 8.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;

        const std::optional<RayHit> first = adapter->castRay(Vec3{}, Vec3{ 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->body, near);

        const std::optional<RayHit> second = adapter->castRay(Vec3{}, Vec3{ 10.0f, 0.0f, 0.0f }, near);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->body, far);
        EXPECT_NEAR(second->pointM.x, 7.5f, kRayTolerance);
        EXPECT_NEAR(second->fraction, 0.75f, kRayTolerance);

        // Ignoring the only body in the way gives a miss.
        EXPECT_FALSE(adapter->castRay(Vec3{ 4.0f, 0.0f, 0.0f }, Vec3{ 2.0f, 0.0f, 0.0f }, near).has_value());
    }

    TEST(HostedJoltWorldRay, theConfiguredFiltersDecideWhichBodiesAreHit)
    {
        // Two bodies on different layers: the adapter's box (moving layer, +X) and a host box (static layer, +Y).
        HostWorld host(JPH::Vec3::sZero());
        host.addHostBox(JPH::Vec3(0.5f, 0.5f, 0.5f), JPH::RVec3(0.0, 5.0, 0.0));

        const auto castAtBoth = [&host](const CMPhysics::HostedWorldConfig& config, bool& hitMoving, bool& hitStatic) {
            const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, config);
            ASSERT_NE(adapter, nullptr);
            ASSERT_TRUE(adapter->createBody(boxDesc(Vec3{ 5.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).ok());
            hitMoving = adapter->castRay(Vec3{}, Vec3{ 10.0f, 0.0f, 0.0f }).has_value();
            hitStatic = adapter->castRay(Vec3{}, Vec3{ 0.0f, 10.0f, 0.0f }).has_value();
        };
        bool hitMoving = false;
        bool hitStatic = false;

        // No filters: everything is hit.
        castAtBoth(makeConfig(), hitMoving, hitStatic);
        EXPECT_TRUE(hitMoving);
        EXPECT_TRUE(hitStatic);

        // Object layer filter that lets only the static layer through.
        const OnlyObjectLayerFilter onlyStaticLayer(kLayerStatic);
        CMPhysics::HostedWorldConfig config = makeConfig();
        config.rayObjectLayerFilter = &onlyStaticLayer;
        castAtBoth(config, hitMoving, hitStatic);
        EXPECT_FALSE(hitMoving);
        EXPECT_TRUE(hitStatic);

        // ...and only the moving layer.
        const OnlyObjectLayerFilter onlyMovingLayer(kLayerMoving);
        config.rayObjectLayerFilter = &onlyMovingLayer;
        castAtBoth(config, hitMoving, hitStatic);
        EXPECT_TRUE(hitMoving);
        EXPECT_FALSE(hitStatic);

        // Broad phase filter, the other filter the config offers.
        config.rayObjectLayerFilter = nullptr;
        const OnlyBroadPhaseLayerFilter onlyStaticBroadPhase(kBroadPhaseStatic);
        config.rayBroadPhaseFilter = &onlyStaticBroadPhase;
        castAtBoth(config, hitMoving, hitStatic);
        EXPECT_FALSE(hitMoving);
        EXPECT_TRUE(hitStatic);
    }

    TEST(HostedJoltWorldRay, rayDistancesAreInMetersAtSeventyUnitsPerMeter)
    {
        HostWorld host(JPH::Vec3::sZero());
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(kUnitsPerMeter));
        ASSERT_NE(adapter, nullptr);
        const BodyId box = adapter->createBody(boxDesc(Vec3{ 5.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;
        ASSERT_TRUE(box.isValid());

        const std::optional<RayHit> hit = adapter->castRay(Vec3{}, Vec3{ 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->body, box);
        // The point comes back in meters; the fraction is a ratio and does not change with the unit.
        EXPECT_NEAR(hit->pointM.x, 4.5f, 1e-2f);
        EXPECT_NEAR(hit->fraction, 0.45f, kRayTolerance);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: no second world, clean host, two adapters
    // ---------------------------------------------------------------------------------------------------------------

    TEST(HostedJoltWorldHost, theHostIsLeftAsItWasFoundAndStillSteps)
    {
        HostWorld host;
        const JPH::BodyID hostBox = host.addHostBox(JPH::Vec3(0.5f, 0.5f, 0.5f), JPH::RVec3(0.0, 0.0, 10.0), false);
        const std::size_t bodiesBefore = host.numBodies();
        const std::size_t constraintsBefore = host.numConstraints();
        {
            UpdateStepper stepper(host);
            const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
            ASSERT_NE(adapter, nullptr);

            const CreateResult<CollisionGroupId> group = adapter->createCollisionGroup(3);
            ASSERT_TRUE(group.ok());
            BodyDesc first = sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f }, 0.2f);
            first.collisionGroup = group.id;
            first.subGroup = 0;
            BodyDesc second = sphereDesc(Vec3{ 0.0f, 0.0f, 2.0f }, 0.2f);
            second.collisionGroup = group.id;
            second.subGroup = 1;
            const BodyId a = adapter->createBody(first).id;
            const BodyId b = adapter->createBody(second).id;
            BodyDesc floor = boxDesc(Vec3{ 0.0f, 0.0f, -0.5f }, Vec3{ 5.0f, 5.0f, 0.5f });
            floor.motion = MotionType::Static;
            ASSERT_TRUE(adapter->createBody(floor).ok());
            ASSERT_TRUE(adapter->createJoint(validJoint(a, b, Vec3{ 0.0f, 0.0f, 2.5f })).ok());
            EXPECT_EQ(host.numBodies(), bodiesBefore + 3);
            EXPECT_EQ(host.numConstraints(), constraintsBefore + 1);
            for (int i = 0; i < 30; ++i)
                adapter->step();
        }

        // The adapter is gone: its bodies, constraint and everything else it added are gone with it.
        EXPECT_EQ(host.numBodies(), bodiesBefore);
        EXPECT_EQ(host.numConstraints(), constraintsBefore);
        ASSERT_EQ(host.allBodies().size(), 1u);
        EXPECT_EQ(host.allBodies().front(), hostBox);

        // The host's own world still works: its dynamic box keeps falling under gravity.
        const float heightBefore = toFloat(host.bodies().GetPosition(hostBox)).GetZ();
        for (int i = 0; i < kOneSecondOfSteps; ++i)
            host.update();
        EXPECT_LT(toFloat(host.bodies().GetPosition(hostBox)).GetZ(), heightBefore - 1.0f);
    }

    TEST(HostedJoltWorldHost, twoAdaptersOverOneSystemDoNotDisturbEachOther)
    {
        HostWorld host(JPH::Vec3::sZero());
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> first = makeAdapter(host, makeConfig(1.0f, &stepper));
        std::unique_ptr<CMPhysics::HostedJoltWorld> second = makeAdapter(host, makeConfig(1.0f, &stepper));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        const BodyId inFirst = first->createBody(boxDesc(Vec3{ 5.0f, 0.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;
        const BodyId inSecond = second->createBody(boxDesc(Vec3{ 0.0f, 5.0f, 0.0f }, Vec3{ 0.5f, 0.5f, 0.5f })).id;
        ASSERT_TRUE(inFirst.isValid() && inSecond.isValid());
        const std::size_t bodiesBoth = host.numBodies();

        // Each adapter reads only its own bodies, even when the handle numbers happen to coincide.
        BodyState state;
        ASSERT_TRUE(first->readBody(inFirst, state));
        EXPECT_NEAR(state.positionM.x, 5.0f, kRoundTripTolerance);
        ASSERT_TRUE(second->readBody(inSecond, state));
        EXPECT_NEAR(state.positionM.y, 5.0f, kRoundTripTolerance);

        // A ray from the first adapter that hits the second adapter's body: a correct hit, but not "its" body.
        const std::optional<RayHit> crossHit = first->castRay(Vec3{}, Vec3{ 0.0f, 10.0f, 0.0f });
        ASSERT_TRUE(crossHit.has_value());
        EXPECT_FALSE(crossHit->body.isValid());
        EXPECT_NEAR(crossHit->pointM.y, 4.5f, kRayTolerance);

        // Both step, and destroying one leaves the other's body alone.
        first->step();
        second->step();
        second.reset();
        EXPECT_EQ(host.numBodies(), bodiesBoth - 1);
        EXPECT_TRUE(first->readBody(inFirst, state));
        EXPECT_NEAR(state.positionM.x, 5.0f, kRoundTripTolerance);
        first->step();
        EXPECT_TRUE(first->readBody(inFirst, state));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Acceptance: determinism
    // ---------------------------------------------------------------------------------------------------------------

    void appendBits(std::vector<std::uint32_t>& trace, float value)
    {
        trace.push_back(std::bit_cast<std::uint32_t>(value));
    }

    void appendState(std::vector<std::uint32_t>& trace, const BodyState& s)
    {
        for (const float v : { s.positionM.x, s.positionM.y, s.positionM.z, s.rotation.x, s.rotation.y, s.rotation.z,
                 s.rotation.w, s.linearVelocityMps.x, s.linearVelocityMps.y, s.linearVelocityMps.z,
                 s.angularVelocityRadps.x, s.angularVelocityRadps.y, s.angularVelocityRadps.z })
            appendBits(trace, v);
        trace.push_back(s.awake ? 1u : 0u);
    }

    // The same scene, every call the same, every time: a floor, a three-link chain with motors in a collision group,
    // a tumbling box, a kinematic platform, an impulse and a velocity change. Returns every body's state after each
    // of the 240 steps, as raw bits.
    std::vector<std::uint32_t> runDeterministicScene()
    {
        constexpr int kSteps = 240;
        HostWorld host;
        UpdateStepper stepper(host);
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, makeConfig(1.0f, &stepper));
        std::vector<std::uint32_t> trace;
        if (adapter == nullptr)
            return trace;

        std::vector<BodyId> bodies;
        BodyDesc floor = boxDesc(Vec3{ 0.0f, 0.0f, -0.5f }, Vec3{ 10.0f, 10.0f, 0.5f });
        floor.motion = MotionType::Static;
        bodies.push_back(adapter->createBody(floor).id);

        const CreateResult<CollisionGroupId> group = adapter->createCollisionGroup(3);
        adapter->setSubGroupsCollide(group.id, 0, 1, false);
        adapter->setSubGroupsCollide(group.id, 1, 2, false);
        std::vector<BodyId> chain;
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            BodyDesc link = sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f - 0.6f * static_cast<float>(i) }, 0.2f);
            link.collisionGroup = group.id;
            link.subGroup = i;
            chain.push_back(adapter->createBody(link).id);
        }
        bodies.insert(bodies.end(), chain.begin(), chain.end());
        std::vector<JointId> joints;
        for (std::size_t i = 0; i + 1 < chain.size(); ++i)
        {
            const CreateResult<JointId> joint = adapter->createJoint(
                validJoint(chain[i], chain[i + 1], Vec3{ 0.0f, 0.0f, 2.7f - 0.6f * static_cast<float>(i) }));
            joints.push_back(joint.id);
            adapter->setJointMotor(joint.id, JointMotor{ 5.0f, 0.5f, kFloatMax });
        }

        BodyDesc tumbler = boxDesc(Vec3{ 3.0f, 0.0f, 2.0f }, Vec3{ 0.5f, 0.3f, 0.2f });
        tumbler.rotation = quatAbout(1.0f, 0.0f, 0.0f, 0.4f);
        const BodyId tumblerId = adapter->createBody(tumbler).id;
        bodies.push_back(tumblerId);

        BodyDesc platform = boxDesc(Vec3{ -3.0f, 0.0f, 1.0f }, Vec3{ 1.0f, 1.0f, 0.1f });
        platform.motion = MotionType::Kinematic;
        const BodyId platformId = adapter->createBody(platform).id;
        bodies.push_back(platformId);

        for (int step = 0; step < kSteps; ++step)
        {
            const float t = static_cast<float>(step) * kStepSeconds;
            adapter->setKinematicTarget(platformId, Vec3{ -3.0f + std::sin(t), 0.0f, 1.0f }, Quat{});
            if (step == 10)
                adapter->applyImpulse(chain.back(), Vec3{ 0.5f, 0.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.8f });
            if (step == 20)
                adapter->setVelocity(tumblerId, Vec3{ -1.0f, 0.5f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f });
            if (step == 30)
                adapter->setJointTarget(joints.front(), quatAbout(0.0f, 1.0f, 0.0f, 0.2f), Vec3{});
            adapter->step();
            for (const BodyId id : bodies)
            {
                BodyState state;
                adapter->readBody(id, state);
                appendState(trace, state);
            }
        }
        return trace;
    }

    TEST(HostedJoltWorldDeterminism, twoWorldsDrivenWithTheSameCallsAgreeBitForBitOverTwoHundredFortySteps)
    {
        const std::vector<std::uint32_t> first = runDeterministicScene();
        const std::vector<std::uint32_t> second = runDeterministicScene();
        ASSERT_FALSE(first.empty());
        ASSERT_EQ(first.size(), second.size());

        std::size_t firstDifference = first.size();
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            if (first[i] != second[i])
            {
                firstDifference = i;
                break;
            }
        }
        EXPECT_EQ(firstDifference, first.size()) << "traces first differ at value " << firstDifference;
    }
}
