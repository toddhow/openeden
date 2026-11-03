// Tests for the scale features of CMPhysics::HostedJoltWorld: scaledPhysicsSettings, the scaleSystemTolerances flag and
// destroyAll(). Written from the spec alone, before the implementation exists.
//
// In plain words: Jolt's built-in tolerances (how far apart two bodies may be and still count as touching, how much
// sinking is forgiven, how slow counts as "at rest") are written for a world measured in metres. The game's world is
// about 70 times bigger per metre, so at game scale those tolerances are 70 times too tight and a resting body never
// falls asleep. The fix multiplies the tolerances by the units per metre. These tests check the multiplication, that
// the adapter applies it only when asked and puts the old values back when it goes away, and that destroyAll() removes
// everything the adapter made and makes the old handles stale.

#include "hostedjoltfixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace CMPhysicsTest;
    using namespace opas::physics;

    constexpr float kScale = 70.0f; // units per metre, as in the game

    // A base with values that differ from Jolt's defaults, so that a field copied wrongly cannot hide.
    JPH::PhysicsSettings distinctiveBase()
    {
        JPH::PhysicsSettings base;
        base.mSpeculativeContactDistance = 0.03f;
        base.mPenetrationSlop = 0.025f;
        base.mManifoldTolerance = 2.0e-3f;
        base.mMaxPenetrationDistance = 0.25f;
        base.mMinVelocityForRestitution = 1.5f;
        base.mPointVelocitySleepThreshold = 0.04f;
        base.mBodyPairCacheMaxDeltaPositionSq = 2.0e-6f;
        base.mContactPointPreserveLambdaMaxDistSq = 3.0e-4f;
        base.mTimeBeforeSleep = 0.75f;
        base.mLinearCastThreshold = 0.8f;
        base.mLinearCastMaxPenetration = 0.3f;
        base.mBaumgarte = 0.25f;
        base.mNumVelocitySteps = 7;
        base.mNumPositionSteps = 3;
        base.mDeterministicSimulation = false;
        base.mAllowSleeping = false;
        return base;
    }

    // Every field the scaling must NOT touch.
    void expectUnscaledFieldsEqual(const JPH::PhysicsSettings& actual, const JPH::PhysicsSettings& expected)
    {
        EXPECT_EQ(actual.mTimeBeforeSleep, expected.mTimeBeforeSleep);
        EXPECT_EQ(actual.mLinearCastThreshold, expected.mLinearCastThreshold);
        EXPECT_EQ(actual.mLinearCastMaxPenetration, expected.mLinearCastMaxPenetration);
        EXPECT_EQ(actual.mBaumgarte, expected.mBaumgarte);
        EXPECT_EQ(actual.mNumVelocitySteps, expected.mNumVelocitySteps);
        EXPECT_EQ(actual.mNumPositionSteps, expected.mNumPositionSteps);
        EXPECT_EQ(actual.mDeterministicSimulation, expected.mDeterministicSimulation);
        EXPECT_EQ(actual.mAllowSleeping, expected.mAllowSleeping);
        EXPECT_EQ(actual.mBodyPairCacheCosMaxDeltaRotationDiv2, expected.mBodyPairCacheCosMaxDeltaRotationDiv2);
        EXPECT_EQ(actual.mContactNormalCosMaxDeltaRotation, expected.mContactNormalCosMaxDeltaRotation);
        EXPECT_EQ(actual.mConstraintWarmStart, expected.mConstraintWarmStart);
        EXPECT_EQ(actual.mUseBodyPairContactCache, expected.mUseBodyPairContactCache);
        EXPECT_EQ(actual.mUseManifoldReduction, expected.mUseManifoldReduction);
        EXPECT_EQ(actual.mUseLargeIslandSplitter, expected.mUseLargeIslandSplitter);
        EXPECT_EQ(actual.mCheckActiveEdges, expected.mCheckActiveEdges);
        EXPECT_EQ(actual.mMaxInFlightBodyPairs, expected.mMaxInFlightBodyPairs);
        EXPECT_EQ(actual.mStepListenersBatchSize, expected.mStepListenersBatchSize);
        EXPECT_EQ(actual.mStepListenerBatchesPerJob, expected.mStepListenerBatchesPerJob);
    }

    // The eight fields the scaling DOES touch, compared exactly.
    void expectScaledFieldsEqual(const JPH::PhysicsSettings& actual, const JPH::PhysicsSettings& expected)
    {
        EXPECT_EQ(actual.mSpeculativeContactDistance, expected.mSpeculativeContactDistance);
        EXPECT_EQ(actual.mPenetrationSlop, expected.mPenetrationSlop);
        EXPECT_EQ(actual.mManifoldTolerance, expected.mManifoldTolerance);
        EXPECT_EQ(actual.mMaxPenetrationDistance, expected.mMaxPenetrationDistance);
        EXPECT_EQ(actual.mMinVelocityForRestitution, expected.mMinVelocityForRestitution);
        EXPECT_EQ(actual.mPointVelocitySleepThreshold, expected.mPointVelocitySleepThreshold);
        EXPECT_EQ(actual.mBodyPairCacheMaxDeltaPositionSq, expected.mBodyPairCacheMaxDeltaPositionSq);
        EXPECT_EQ(actual.mContactPointPreserveLambdaMaxDistSq, expected.mContactPointPreserveLambdaMaxDistSq);
    }

    void expectAllFieldsEqual(const JPH::PhysicsSettings& actual, const JPH::PhysicsSettings& expected)
    {
        expectScaledFieldsEqual(actual, expected);
        expectUnscaledFieldsEqual(actual, expected);
    }

    JointDesc scaleTestJoint(BodyId a, BodyId b)
    {
        JointDesc joint;
        joint.bodyA = a;
        joint.bodyB = b;
        joint.anchorM = Vec3{ 0.0f, 0.0f, 0.0f };
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

    // ---- scaledPhysicsSettings ----------------------------------------------------------------------------------

    TEST(HostedJoltScaleSettings, lengthToleranceFieldsAreMultipliedByTheScale)
    {
        const JPH::PhysicsSettings base = distinctiveBase();
        const JPH::PhysicsSettings scaled = CMPhysics::scaledPhysicsSettings(base, kScale);

        EXPECT_FLOAT_EQ(scaled.mSpeculativeContactDistance, base.mSpeculativeContactDistance * kScale);
        EXPECT_EQ(scaled.mPenetrationSlop, base.mPenetrationSlop); // deliberately not scaled (how deep bodies rest)
        EXPECT_FLOAT_EQ(scaled.mManifoldTolerance, base.mManifoldTolerance * kScale);
        EXPECT_FLOAT_EQ(scaled.mMaxPenetrationDistance, base.mMaxPenetrationDistance * kScale);
        EXPECT_FLOAT_EQ(scaled.mMinVelocityForRestitution, base.mMinVelocityForRestitution * kScale);
        EXPECT_FLOAT_EQ(scaled.mPointVelocitySleepThreshold, base.mPointVelocitySleepThreshold * kScale);
    }

    TEST(HostedJoltScaleSettings, squaredDistanceFieldsAreMultipliedByTheScaleSquared)
    {
        const JPH::PhysicsSettings base = distinctiveBase();
        const JPH::PhysicsSettings scaled = CMPhysics::scaledPhysicsSettings(base, kScale);

        EXPECT_FLOAT_EQ(
            scaled.mBodyPairCacheMaxDeltaPositionSq, base.mBodyPairCacheMaxDeltaPositionSq * kScale * kScale);
        EXPECT_FLOAT_EQ(
            scaled.mContactPointPreserveLambdaMaxDistSq, base.mContactPointPreserveLambdaMaxDistSq * kScale * kScale);
    }

    TEST(HostedJoltScaleSettings, everyOtherFieldIsUnchanged)
    {
        const JPH::PhysicsSettings base = distinctiveBase();
        const JPH::PhysicsSettings scaled = CMPhysics::scaledPhysicsSettings(base, kScale);
        expectUnscaledFieldsEqual(scaled, base);
    }

    TEST(HostedJoltScaleSettings, aScaleOfOneGivesEqualValues)
    {
        const JPH::PhysicsSettings base = distinctiveBase();
        expectAllFieldsEqual(CMPhysics::scaledPhysicsSettings(base, 1.0f), base);

        const JPH::PhysicsSettings defaults;
        expectAllFieldsEqual(CMPhysics::scaledPhysicsSettings(defaults, 1.0f), defaults);
    }

    TEST(HostedJoltScaleSettings, theBaseIsNotModified)
    {
        const JPH::PhysicsSettings base = distinctiveBase();
        const JPH::PhysicsSettings copy = base;
        const JPH::PhysicsSettings scaled = CMPhysics::scaledPhysicsSettings(base, kScale);
        (void)scaled;
        expectAllFieldsEqual(base, copy);
    }

    TEST(HostedJoltScaleSettings, gameScaleValuesMatchTheNumbersInTheTask)
    {
        const JPH::PhysicsSettings scaled = CMPhysics::scaledPhysicsSettings(JPH::PhysicsSettings(), kScale);
        EXPECT_NEAR(scaled.mPointVelocitySleepThreshold, 0.03f * kScale, 1e-4f);
        EXPECT_EQ(scaled.mPenetrationSlop, 0.02f); // not scaled, see scaledPhysicsSettings
    }

    // ---- scaleSystemTolerances -------------------------------------------------------------------------------------

    TEST(HostedJoltScaleFlag, offByDefault)
    {
        const CMPhysics::HostedWorldConfig config;
        EXPECT_FALSE(config.scaleSystemTolerances);
    }

    TEST(HostedJoltScaleFlag, flagFalseLeavesTheSettingsEqualToTheDefaults)
    {
        HostWorld host;
        const JPH::PhysicsSettings before = host.system().GetPhysicsSettings();
        CMPhysics::HostedWorldConfig config = makeConfig(kScale);
        config.scaleSystemTolerances = false;
        std::string error;
        {
            auto adapter = makeAdapter(host, config, &error);
            ASSERT_NE(adapter, nullptr) << error;
            expectAllFieldsEqual(host.system().GetPhysicsSettings(), before);
            expectAllFieldsEqual(host.system().GetPhysicsSettings(), JPH::PhysicsSettings());
        }
        expectAllFieldsEqual(host.system().GetPhysicsSettings(), before);
    }

    TEST(HostedJoltScaleFlag, flagTrueScalesTheSystemWhileTheAdapterLives)
    {
        HostWorld host;
        const JPH::PhysicsSettings before = host.system().GetPhysicsSettings();
        CMPhysics::HostedWorldConfig config = makeConfig(kScale);
        config.scaleSystemTolerances = true;
        std::string error;
        auto adapter = makeAdapter(host, config, &error);
        ASSERT_NE(adapter, nullptr) << error;

        expectAllFieldsEqual(host.system().GetPhysicsSettings(), CMPhysics::scaledPhysicsSettings(before, kScale));
        EXPECT_NEAR(host.system().GetPhysicsSettings().mPointVelocitySleepThreshold, 0.03f * kScale, 1e-4f);
    }

    TEST(HostedJoltScaleFlag, destroyingTheAdapterPutsTheOldSettingsBack)
    {
        HostWorld host;
        // The host had settings of its own; the adapter must restore what it found, not Jolt's defaults.
        host.system().SetPhysicsSettings(distinctiveBase());
        const JPH::PhysicsSettings before = host.system().GetPhysicsSettings();
        CMPhysics::HostedWorldConfig config = makeConfig(kScale);
        config.scaleSystemTolerances = true;
        std::string error;
        auto adapter = makeAdapter(host, config, &error);
        ASSERT_NE(adapter, nullptr) << error;
        expectAllFieldsEqual(host.system().GetPhysicsSettings(), CMPhysics::scaledPhysicsSettings(before, kScale));

        adapter.reset();
        expectAllFieldsEqual(host.system().GetPhysicsSettings(), before);
    }

    TEST(HostedJoltScaleFlag, aScaleOfOneLeavesTheSettingsAlone)
    {
        HostWorld host;
        host.system().SetPhysicsSettings(distinctiveBase());
        const JPH::PhysicsSettings before = host.system().GetPhysicsSettings();
        CMPhysics::HostedWorldConfig config = makeConfig(1.0f);
        config.scaleSystemTolerances = true;
        std::string error;
        {
            auto adapter = makeAdapter(host, config, &error);
            ASSERT_NE(adapter, nullptr) << error;
            expectAllFieldsEqual(host.system().GetPhysicsSettings(), before);
        }
        expectAllFieldsEqual(host.system().GetPhysicsSettings(), before);
    }

    // The point of the whole change, at the adapter level: a ball dropped on a floor at game scale goes to sleep.
    TEST(HostedJoltScaleFlag, aSphereAtGameScaleWithScalingOnFallsAsleepWithinSixSeconds)
    {
        // Gravity in units: 9.81 m/s^2 times 70 units per metre, pointing down.
        HostWorld host(JPH::Vec3(0.0f, 0.0f, -kGravityMps2 * kScale));
        host.addHostBox(JPH::Vec3(2000.0f, 2000.0f, 50.0f), JPH::RVec3(0.0, 0.0, -50.0)); // top face at z = 0
        UpdateStepper stepper(host);
        CMPhysics::HostedWorldConfig config = makeConfig(kScale, &stepper);
        config.scaleSystemTolerances = true;
        std::string error;
        auto adapter = makeAdapter(host, config, &error);
        ASSERT_NE(adapter, nullptr) << error;

        const std::vector<JPH::BodyID> before = host.allBodies();
        const CreateResult<BodyId> created = adapter->createBody(sphereDesc(Vec3{ 0.0f, 0.0f, 2.0f }, 0.3f));
        ASSERT_TRUE(created.ok()) << created.message;
        const std::vector<JPH::BodyID> added = host.bodiesAddedSince(before);
        ASSERT_EQ(added.size(), 1u);

        constexpr int kSixSeconds = 360;
        int asleepAtStep = -1;
        for (int step = 1; step <= kSixSeconds; ++step)
        {
            adapter->step();
            if (!host.bodies().IsActive(added[0]))
            {
                asleepAtStep = step;
                break;
            }
        }
        EXPECT_GT(asleepAtStep, 0) << "the sphere never fell asleep within 6 s";

        BodyState state;
        ASSERT_TRUE(adapter->readBody(created.id, state));
        // It rests on the floor: its centre is one radius above z = 0 (in metres), give or take a centimetre.
        EXPECT_NEAR(state.positionM.z, 0.3f, 0.01f);
    }

    // ---- destroyAll ------------------------------------------------------------------------------------------------

    class HostedJoltDestroyAllTest : public ::testing::Test
    {
    protected:
        HostedJoltDestroyAllTest()
            : mStepper(mHost)
        {
        }

        void SetUp() override
        {
            mHost.addHostBox(JPH::Vec3(50.0f, 50.0f, 0.5f), JPH::RVec3(0.0, 0.0, -0.5)); // a body the host owns
            mBodiesBefore = mHost.numBodies();
            mConstraintsBefore = mHost.numConstraints();
            std::string error;
            CMPhysics::HostedWorldConfig config = makeConfig(1.0f, &mStepper);
            config.maxBodies = 4;
            config.maxJoints = 4;
            mAdapter = makeAdapter(mHost, config, &error);
            ASSERT_NE(mAdapter, nullptr) << error;
        }

        BodyId makeSphere(Vec3 position, MotionType motion = MotionType::Dynamic)
        {
            BodyDesc desc = sphereDesc(position, 0.25f);
            desc.motion = motion;
            const CreateResult<BodyId> result = mAdapter->createBody(desc);
            EXPECT_TRUE(result.ok()) << result.message;
            return result.id;
        }

        HostWorld mHost;
        UpdateStepper mStepper;
        std::size_t mBodiesBefore = 0;
        std::size_t mConstraintsBefore = 0;
        std::unique_ptr<CMPhysics::HostedJoltWorld> mAdapter;
    };

    TEST_F(HostedJoltDestroyAllTest, removesEveryBodyJointAndGroupTheAdapterMade)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        const BodyId b = makeSphere(Vec3{ 1.0f, 0.0f, 3.0f });
        const CreateResult<JointId> joint = mAdapter->createJoint(scaleTestJoint(a, b));
        ASSERT_TRUE(joint.ok()) << joint.message;
        const CreateResult<CollisionGroupId> group = mAdapter->createCollisionGroup(2);
        ASSERT_TRUE(group.ok()) << group.message;
        ASSERT_EQ(mHost.numBodies(), mBodiesBefore + 2);
        ASSERT_EQ(mHost.numConstraints(), mConstraintsBefore + 1);

        EXPECT_TRUE(mAdapter->destroyAll());

        EXPECT_EQ(mHost.numBodies(), mBodiesBefore);
        EXPECT_EQ(mHost.numConstraints(), mConstraintsBefore);
    }

    TEST_F(HostedJoltDestroyAllTest, bodiesTheHostMadeItselfAreUntouched)
    {
        const std::vector<JPH::BodyID> hostBodies = mHost.allBodies();
        makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        ASSERT_TRUE(mAdapter->destroyAll());

        const std::vector<JPH::BodyID> after = mHost.allBodies();
        EXPECT_EQ(after, hostBodies);
        EXPECT_TRUE(mHost.bodies().IsAdded(hostBodies[0]));
    }

    TEST_F(HostedJoltDestroyAllTest, oldHandlesAreStale)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        const BodyId b = makeSphere(Vec3{ 1.0f, 0.0f, 3.0f });
        const JointId joint = mAdapter->createJoint(scaleTestJoint(a, b)).id;
        const CollisionGroupId group = mAdapter->createCollisionGroup(2).id;
        ASSERT_TRUE(mAdapter->destroyAll());

        BodyState state;
        EXPECT_FALSE(mAdapter->readBody(a, state));
        EXPECT_FALSE(mAdapter->readBody(b, state));
        EXPECT_FALSE(mAdapter->destroyBody(a));
        EXPECT_FALSE(mAdapter->destroyJoint(joint));
        EXPECT_FALSE(mAdapter->setSubGroupsCollide(group, 0, 1, false));
    }

    TEST_F(HostedJoltDestroyAllTest, oldHandlesStayStaleAfterNewBodiesReuseTheSlots)
    {
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        const BodyId b = makeSphere(Vec3{ 1.0f, 0.0f, 3.0f });
        const JointId joint = mAdapter->createJoint(scaleTestJoint(a, b)).id;
        const CollisionGroupId group = mAdapter->createCollisionGroup(2).id;
        ASSERT_TRUE(mAdapter->destroyAll());

        const BodyId c = makeSphere(Vec3{ 0.0f, 2.0f, 3.0f });
        const BodyId d = makeSphere(Vec3{ 1.0f, 2.0f, 3.0f });
        ASSERT_TRUE(c.isValid());
        ASSERT_TRUE(d.isValid());
        const CreateResult<JointId> newJoint = mAdapter->createJoint(scaleTestJoint(c, d));
        ASSERT_TRUE(newJoint.ok()) << newJoint.message;
        ASSERT_TRUE(mAdapter->createCollisionGroup(2).ok());

        BodyState state;
        EXPECT_FALSE(mAdapter->readBody(a, state));
        EXPECT_FALSE(mAdapter->readBody(b, state));
        EXPECT_FALSE(mAdapter->destroyBody(a));
        EXPECT_FALSE(mAdapter->destroyJoint(joint));
        EXPECT_FALSE(mAdapter->setSubGroupsCollide(group, 0, 1, false));
        // The new handles work.
        EXPECT_TRUE(mAdapter->readBody(c, state));
        EXPECT_TRUE(mAdapter->readBody(d, state));
    }

    TEST_F(HostedJoltDestroyAllTest, creationWorksAgainUpToMaxBodies)
    {
        for (int i = 0; i < 4; ++i)
            makeSphere(Vec3{ float(i), 0.0f, 3.0f });
        ASSERT_TRUE(mAdapter->destroyAll());

        for (int i = 0; i < 4; ++i)
        {
            const CreateResult<BodyId> again = mAdapter->createBody(sphereDesc(Vec3{ float(i), 0.0f, 3.0f }, 0.25f));
            EXPECT_TRUE(again.ok()) << "body " << i << ": " << again.message;
        }
    }

    TEST_F(HostedJoltDestroyAllTest, emptyAdapterReturnsTrue)
    {
        EXPECT_TRUE(mAdapter->destroyAll());
        EXPECT_TRUE(mAdapter->destroyAll());
        EXPECT_EQ(mHost.numBodies(), mBodiesBefore);
    }

    class CountingListener final : public StepListener
    {
    public:
        explicit CountingListener(CMPhysics::HostedJoltWorld& world)
            : mWorld(world)
        {
        }

        void onPreStep(PhysicsWorld&, float) override
        {
            ++mPreCalls;
            if (mDestroyInPreStep)
                mDestroyResult = mWorld.destroyAll();
        }

        void onPostStep(PhysicsWorld&, float) override { ++mPostCalls; }

        CMPhysics::HostedJoltWorld& mWorld;
        bool mDestroyInPreStep = false;
        bool mDestroyResult = true;
        int mPreCalls = 0;
        int mPostCalls = 0;
    };

    TEST_F(HostedJoltDestroyAllTest, stepListenersStayRegistered)
    {
        CountingListener listener(*mAdapter);
        ASSERT_TRUE(mAdapter->addStepListener(listener).isValid());
        makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        ASSERT_TRUE(mAdapter->destroyAll());

        mAdapter->step();
        mAdapter->step();
        EXPECT_EQ(listener.mPreCalls, 2);
        EXPECT_EQ(listener.mPostCalls, 2);
    }

    TEST_F(HostedJoltDestroyAllTest, calledDuringAStepItDoesNothingAndReturnsFalse)
    {
        CountingListener listener(*mAdapter);
        ASSERT_TRUE(mAdapter->addStepListener(listener).isValid());
        const BodyId a = makeSphere(Vec3{ 0.0f, 0.0f, 3.0f });
        listener.mDestroyInPreStep = true;

        mAdapter->step();

        EXPECT_FALSE(listener.mDestroyResult);
        EXPECT_EQ(mHost.numBodies(), mBodiesBefore + 1);
        BodyState state;
        EXPECT_TRUE(mAdapter->readBody(a, state));

        // Outside a step it works again.
        listener.mDestroyInPreStep = false;
        EXPECT_TRUE(mAdapter->destroyAll());
        EXPECT_EQ(mHost.numBodies(), mBodiesBefore);
    }
}
