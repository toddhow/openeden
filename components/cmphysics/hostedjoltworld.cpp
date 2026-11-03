#include "hostedjoltworld.hpp"

// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>

namespace CMPhysics
{
    using namespace opas::physics;

    namespace
    {
        // Position-solver iterations for joints, the value CreativeMotion's own world measured (JoltWorld.cpp,
        // kJointPositionSteps: 10 keeps a fast limb within 2 degrees of its limit at 60 Hz).
        constexpr JPH::uint kJointPositionSteps = 10;
        // Jolt keeps a constraint's position steps in 8 bits (Constraint::SetNumPositionStepsOverride).
        constexpr std::uint32_t kMaxJointPositionSteps = 255;
        // How far an axis may be from unit length before a description is rejected (JoltWorld.cpp, kAxisTolerance).
        constexpr float kAxisTolerance = 1e-3f;
        constexpr float kPi = std::numbers::pi_v<float>;
        // Step listeners are kept in a vector reserved to this size so that adding one never allocates in a step.
        constexpr std::size_t kMaxListeners = 16;
        // Most sub-groups in one group: the largest table that fits Jolt's 32-bit arithmetic (JoltWorld.cpp).
        constexpr std::uint32_t kMaxSubGroups = 65536;
        constexpr std::uint32_t kNoSlot = std::numeric_limits<std::uint32_t>::max();
        // The game uses Jolt v5.5.0, whose joint motors have no PositionAndVelocity mode (the vendored JoltWorld.cpp
        // uses a newer Jolt commit). In Position mode a motor only works if its stiffness is above zero
        // (SwingTwistConstraint.cpp and SixDOFConstraint.cpp, HasStiffness), so a damping-only motor gets this tiny
        // stiffness (in the host's torque units): too small to pull on the target, enough to keep the damping.
        constexpr float kDampingOnlyStiffness = 1e-6f;

        // Jolt group IDs are never reused. They are counted for the whole process, not per adapter, so that two
        // adapters over one PhysicsSystem can never give two different groups the same ID.
        std::atomic<JPH::CollisionGroup::GroupID> sNextGroupId{ 0 };

        // --- The unit boundary: the only code that multiplies by unitsPerMeter ---------------------------------
        // Lengths, positions, linear velocities and impulses (kg m/s -> kg u/s) scale by s; rotational motor
        // gains (a torque: kg m^2/s^2 -> kg u^2/s^2) scale by s squared. Mass, angles, angular velocities and
        // dimensionless numbers do not change.
        double lengthToWorld(double meters, float s)
        {
            return meters * static_cast<double>(s);
        }

        float lengthFromWorld(double units, float s)
        {
            return static_cast<float>(units / static_cast<double>(s));
        }

        JPH::Vec3 vectorToWorld(Vec3 v, float s)
        {
            return { static_cast<float>(lengthToWorld(v.x, s)), static_cast<float>(lengthToWorld(v.y, s)),
                static_cast<float>(lengthToWorld(v.z, s)) };
        }

        JPH::RVec3 positionToWorld(Vec3 v, float s)
        {
            return { lengthToWorld(v.x, s), lengthToWorld(v.y, s), lengthToWorld(v.z, s) };
        }

        Vec3 vectorFromWorld(JPH::Vec3Arg v, float s)
        {
            return { lengthFromWorld(v.GetX(), s), lengthFromWorld(v.GetY(), s), lengthFromWorld(v.GetZ(), s) };
        }

        Vec3 positionFromWorld(JPH::RVec3Arg v, float s)
        {
            return { lengthFromWorld(v.GetX(), s), lengthFromWorld(v.GetY(), s), lengthFromWorld(v.GetZ(), s) };
        }

        // Clamped to the largest float: the default "unlimited" torque is already that value, and times s^2 it
        // would become infinity, which Jolt must never receive.
        float torqueToWorld(float torque, float s)
        {
            const double scaled = static_cast<double>(torque) * static_cast<double>(s) * static_cast<double>(s);
            return static_cast<float>(std::min(scaled, static_cast<double>(std::numeric_limits<float>::max())));
        }

        // Angles and angular velocities are unitless; these only change the type.
        JPH::Vec3 toJolt(Vec3 v)
        {
            return { v.x, v.y, v.z };
        }

        JPH::Quat toJolt(Quat q)
        {
            return { q.x, q.y, q.z, q.w };
        }

        Vec3 fromJolt(JPH::Vec3Arg v)
        {
            return { v.GetX(), v.GetY(), v.GetZ() };
        }

        Quat fromJolt(JPH::QuatArg q)
        {
            return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        }

        // --- Validation, the same rules as JoltWorld.cpp -------------------------------------------------------
        float length(Vec3 v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        float dot(Vec3 a, Vec3 b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        bool isUnit(Vec3 v)
        {
            return std::abs(length(v) - 1.0f) <= kAxisTolerance;
        }

        bool isUnit(Quat q)
        {
            const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return std::abs(len - 1.0f) <= kAxisTolerance;
        }

        bool isFinite(Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        template <typename Id>
        CreateResult<Id> failure(ErrorCode code, std::string message)
        {
            return CreateResult<Id>{ Id{}, code, std::move(message) };
        }

        std::string validateShape(const Shape& shape)
        {
            if (const auto* box = std::get_if<BoxShape>(&shape))
            {
                const Vec3 h = box->halfExtentsM;
                if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f) || !isFinite(h))
                    return "BoxShape.halfExtentsM must be > 0 on every axis";
            }
            else if (const auto* sphere = std::get_if<SphereShape>(&shape))
            {
                if (!(sphere->radiusM > 0.0f) || !std::isfinite(sphere->radiusM))
                    return "SphereShape.radiusM must be > 0";
            }
            else if (const auto* capsule = std::get_if<CapsuleShape>(&shape))
            {
                if (!(capsule->radiusM > 0.0f) || !std::isfinite(capsule->radiusM))
                    return "CapsuleShape.radiusM must be > 0";
                if (!(capsule->halfHeightM > 0.0f) || !std::isfinite(capsule->halfHeightM))
                    return "CapsuleShape.halfHeightM must be > 0";
            }
            return {};
        }

        std::string validateBody(const BodyDesc& desc)
        {
            if (std::string shapeError = validateShape(desc.shape); !shapeError.empty())
                return shapeError;
            if (desc.motion == MotionType::Dynamic && (!(desc.massKg > 0.0f) || !std::isfinite(desc.massKg)))
                return "massKg must be > 0 for a dynamic body";
            if (!isFinite(desc.positionM))
                return "positionM must be finite";
            if (!isUnit(desc.rotation))
                return "rotation must be a unit quaternion";
            if (!(desc.friction >= 0.0f))
                return "friction must be >= 0";
            if (!(desc.restitution >= 0.0f && desc.restitution <= 1.0f))
                return "restitution must be in [0, 1]";
            if (!(desc.linearDamping >= 0.0f))
                return "linearDamping must be >= 0";
            if (!(desc.angularDamping >= 0.0f))
                return "angularDamping must be >= 0";
            return {};
        }

        std::string validateSwingRange(SwingShape shape, const std::string& name, float minRad, float maxRad)
        {
            if (!(minRad >= -kPi && minRad <= kPi))
                return name + "MinRad must be in [-pi, pi]";
            if (!(maxRad >= -kPi && maxRad <= kPi))
                return name + "MaxRad must be in [-pi, pi]";
            if (minRad > maxRad)
                return name + "MinRad must be <= " + name + "MaxRad";
            if (shape == SwingShape::Cone && minRad != -maxRad)
                return name + "MinRad must be -" + name + "MaxRad for a cone";
            if (shape == SwingShape::Pyramid && (minRad > 0.0f || maxRad < 0.0f))
                return name + " range must contain 0 for a pyramid";
            return {};
        }

        std::string validateJointGeometry(const JointDesc& desc)
        {
            if (!isFinite(desc.anchorM))
                return "anchorM must be finite";
            if (!isUnit(desc.twistAxis))
                return "twistAxis must be unit length";
            if (!isUnit(desc.planeAxis))
                return "planeAxis must be unit length";
            if (std::abs(dot(desc.twistAxis, desc.planeAxis)) > kAxisTolerance)
                return "planeAxis must be perpendicular to twistAxis";
            if (desc.swingShape != SwingShape::Cone && desc.swingShape != SwingShape::Pyramid)
                return "swingShape must be Cone or Pyramid";
            if (std::string error
                = validateSwingRange(desc.swingShape, "planeSwing", desc.planeSwingMinRad, desc.planeSwingMaxRad);
                !error.empty())
                return error;
            if (std::string error
                = validateSwingRange(desc.swingShape, "normalSwing", desc.normalSwingMinRad, desc.normalSwingMaxRad);
                !error.empty())
                return error;
            if (!(desc.twistMinRad >= -kPi && desc.twistMinRad <= kPi))
                return "twistMinRad must be in [-pi, pi]";
            if (!(desc.twistMaxRad >= -kPi && desc.twistMaxRad <= kPi))
                return "twistMaxRad must be in [-pi, pi]";
            if (desc.twistMinRad > desc.twistMaxRad)
                return "twistMinRad must be <= twistMaxRad";
            return {};
        }

        bool isValidMotor(const JointMotor& motor)
        {
            return motor.stiffnessNmPerRad >= 0.0f && std::isfinite(motor.stiffnessNmPerRad)
                && motor.dampingNmsPerRad >= 0.0f && std::isfinite(motor.dampingNmsPerRad) && motor.maxTorqueNm >= 0.0f
                && std::isfinite(motor.maxTorqueNm);
        }

        // Same shapes as JoltWorld.cpp, with every length converted to the host's units first.
        JPH::ShapeRefC createJoltShape(const Shape& shape, float s)
        {
            JPH::ShapeSettings::ShapeResult result;
            if (const auto* box = std::get_if<BoxShape>(&shape))
            {
                const JPH::Vec3 h = vectorToWorld(box->halfExtentsM, s);
                // The convex radius must not exceed any half extent.
                const float smallest = std::min({ h.GetX(), h.GetY(), h.GetZ() });
                const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * smallest);
                result = JPH::BoxShapeSettings(h, convexRadius).Create();
            }
            else if (const auto* sphere = std::get_if<SphereShape>(&shape))
            {
                result = JPH::SphereShapeSettings(static_cast<float>(lengthToWorld(sphere->radiusM, s))).Create();
            }
            else
            {
                const auto& capsule = std::get<CapsuleShape>(shape);
                // Jolt capsules run along local Y; ours run along local Z. Rotating +90 degrees about X turns +Y
                // into +Z.
                const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * kPi);
                const JPH::RefConst<JPH::ShapeSettings> inner
                    = new JPH::CapsuleShapeSettings(static_cast<float>(lengthToWorld(capsule.halfHeightM, s)),
                        static_cast<float>(lengthToWorld(capsule.radiusM, s)));
                result = JPH::RotatedTranslatedShapeSettings(JPH::Vec3::sZero(), yToZ, inner).Create();
            }
            return result.HasError() ? nullptr : result.Get();
        }

        template <typename Slot>
        std::optional<std::uint32_t> takeSlot(
            std::vector<Slot>& slots, std::vector<std::uint32_t>& freeList, std::uint32_t capacity)
        {
            if (!freeList.empty())
            {
                const std::uint32_t index = freeList.back();
                freeList.pop_back();
                return index;
            }
            if (slots.size() >= capacity)
                return std::nullopt;
            slots.emplace_back();
            return static_cast<std::uint32_t>(slots.size() - 1);
        }

        std::string validateConfig(const HostedWorldConfig& config)
        {
            if (!(config.unitsPerMeter > 0.0f) || !std::isfinite(config.unitsPerMeter))
                return "unitsPerMeter must be > 0";
            if (!(config.stepSeconds > 0.0f) || !std::isfinite(config.stepSeconds))
                return "stepSeconds must be > 0";
            if (config.maxBodies == 0)
                return "maxBodies must be > 0";
            if (config.maxJoints == 0)
                return "maxJoints must be > 0";
            if (!std::isfinite(config.gravityFactor))
                return "gravityFactor must be finite";
            return {};
        }
    }

    JPH::PhysicsSettings scaledPhysicsSettings(const JPH::PhysicsSettings& base, float unitsPerMeter)
    {
        const float s = unitsPerMeter;
        JPH::PhysicsSettings scaled = base;
        // mPenetrationSlop is left alone on purpose: it is how deep bodies may rest in each other, so scaling it (2 cm in
        // metres, 1.4 units here) would let a ragdoll sink 2 cm into the floor; at its metre-world value of 0.02 units
        // it rests within about a millimetre (task 0035, stage c log).
        scaled.mSpeculativeContactDistance *= s;
        scaled.mManifoldTolerance *= s;
        scaled.mMaxPenetrationDistance *= s;
        scaled.mMinVelocityForRestitution *= s;
        scaled.mPointVelocitySleepThreshold *= s;
        scaled.mBodyPairCacheMaxDeltaPositionSq *= s * s;
        scaled.mContactPointPreserveLambdaMaxDistSq *= s * s;
        return scaled;
    }

    std::unique_ptr<HostedJoltWorld> HostedJoltWorld::create(
        JPH::PhysicsSystem& system, const HostedWorldConfig& config, std::string* error)
    {
        if (std::string message = validateConfig(config); !message.empty())
        {
            if (error != nullptr)
                *error = std::move(message);
            return nullptr;
        }
        // The constructor is private, so make_unique cannot be used.
        return std::unique_ptr<HostedJoltWorld>(new HostedJoltWorld(system, config));
    }

    HostedJoltWorld::HostedJoltWorld(JPH::PhysicsSystem& system, const HostedWorldConfig& config)
        : mSystem(system)
        , mConfig(config)
    {
        // Never more bodies than the host's system can hold (Jolt would refuse them anyway).
        mConfig.maxBodies = std::min(config.maxBodies, system.GetMaxBodies());
        // Everything is reserved up front so that no per-step call allocates.
        mBodySlots.reserve(mConfig.maxBodies);
        mFreeBodySlots.reserve(mConfig.maxBodies);
        mBodyIndexToSlot.assign(system.GetMaxBodies(), kNoSlot);
        mJointSlots.reserve(config.maxJoints);
        mFreeJointSlots.reserve(config.maxJoints);
        mGroupSlots.reserve(mConfig.maxBodies);
        mFreeGroupSlots.reserve(mConfig.maxBodies);
        mKinematicTargets.reserve(mConfig.maxBodies);
        mListeners.reserve(kMaxListeners);
        if (mConfig.scaleSystemTolerances && mConfig.unitsPerMeter != 1.0f)
        {
            mSettingsBefore = system.GetPhysicsSettings();
            system.SetPhysicsSettings(scaledPhysicsSettings(*mSettingsBefore, mConfig.unitsPerMeter));
        }
    }

    HostedJoltWorld::~HostedJoltWorld()
    {
        // Constraints reference bodies, so they go first. The host's world is left as it was found.
        for (const JointSlot& slot : mJointSlots)
        {
            if (slot.alive)
                mSystem.RemoveConstraint(slot.constraint);
        }
        for (const BodySlot& slot : mBodySlots)
        {
            if (slot.alive)
            {
                bodies().RemoveBody(slot.joltId);
                bodies().DestroyBody(slot.joltId);
            }
        }
        if (mSettingsBefore)
            mSystem.SetPhysicsSettings(*mSettingsBefore);
    }

    const HostedJoltWorld::BodySlot* HostedJoltWorld::findBody(BodyId body) const
    {
        if (!body.isValid() || body.index >= mBodySlots.size())
            return nullptr;
        const BodySlot& slot = mBodySlots[body.index];
        return (slot.alive && slot.generation == body.generation) ? &slot : nullptr;
    }

    HostedJoltWorld::JointSlot* HostedJoltWorld::findJoint(JointId joint)
    {
        if (!joint.isValid() || joint.index >= mJointSlots.size())
            return nullptr;
        JointSlot& slot = mJointSlots[joint.index];
        return (slot.alive && slot.generation == joint.generation) ? &slot : nullptr;
    }

    HostedJoltWorld::GroupSlot* HostedJoltWorld::findGroup(CollisionGroupId group)
    {
        if (!group.isValid() || group.index >= mGroupSlots.size())
            return nullptr;
        GroupSlot& slot = mGroupSlots[group.index];
        return (slot.alive && slot.generation == group.generation) ? &slot : nullptr;
    }

    CreateResult<BodyId> HostedJoltWorld::createBody(const BodyDesc& desc)
    {
        if (mInStep)
            return failure<BodyId>(ErrorCode::CalledDuringStep, "createBody cannot be called from a step listener");
        if (std::string error = validateBody(desc); !error.empty())
            return failure<BodyId>(ErrorCode::InvalidDescription, std::move(error));
        if (desc.motion == MotionType::Static && !mConfig.staticLayer)
            return failure<BodyId>(
                ErrorCode::InvalidDescription, "this host does not allow static bodies (no static layer)");
        const GroupSlot* group = nullptr;
        if (desc.collisionGroup.isValid())
        {
            group = findGroup(desc.collisionGroup);
            if (group == nullptr)
                return failure<BodyId>(ErrorCode::InvalidHandle, "collisionGroup is not a live group");
            if (desc.subGroup >= group->subGroupCount)
                return failure<BodyId>(ErrorCode::InvalidDescription, "subGroup must be < the group's subGroupCount");
        }
        const float s = mConfig.unitsPerMeter;
        const JPH::ShapeRefC shape = createJoltShape(desc.shape, s);
        if (shape == nullptr)
            return failure<BodyId>(ErrorCode::InvalidDescription, "shape could not be created");
        const std::optional<std::uint32_t> index = takeSlot(mBodySlots, mFreeBodySlots, mConfig.maxBodies);
        if (!index)
            return failure<BodyId>(ErrorCode::CapacityExceeded, "maxBodies reached");

        JPH::EMotionType motion = JPH::EMotionType::Static;
        JPH::ObjectLayer layer = mConfig.staticLayer.value_or(mConfig.movingLayer);
        if (desc.motion == MotionType::Dynamic)
        {
            motion = JPH::EMotionType::Dynamic;
            layer = mConfig.movingLayer;
        }
        else if (desc.motion == MotionType::Kinematic)
        {
            motion = JPH::EMotionType::Kinematic;
            layer = mConfig.movingLayer;
        }

        JPH::BodyCreationSettings settings(
            shape, positionToWorld(desc.positionM, s), toJolt(desc.rotation).Normalized(), motion, layer);
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        // User data stays 0: the game reads it as a PtrHolder pointer (joltlisteners.hpp), 0 meaning none.
        settings.mUserData = 0;
        // Jolt caps linear speed at 500 world units per second by default, which is about 7 m/s at the game's scale.
        settings.mMaxLinearVelocity *= s;
        // The host decides which way gravity pulls ragdolls (the game: -1, because its Jolt gravity points up).
        settings.mGravityFactor = mConfig.gravityFactor;
        if (group != nullptr)
            settings.mCollisionGroup = JPH::CollisionGroup(group->table, group->joltGroupId, desc.subGroup);
        if (desc.motion == MotionType::Dynamic || (desc.motion == MotionType::Kinematic && desc.massKg > 0.0f))
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.massKg;
        }

        const JPH::EActivation activation
            = desc.motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        const JPH::BodyID joltId = bodies().CreateAndAddBody(settings, activation);
        if (joltId.IsInvalid())
        {
            mFreeBodySlots.push_back(*index);
            return failure<BodyId>(ErrorCode::CapacityExceeded, "Jolt body limit reached");
        }

        BodySlot& slot = mBodySlots[*index];
        slot.joltId = joltId;
        slot.alive = true;
        slot.motion = desc.motion;
        slot.kinematicTargetPending = false;
        mBodyIndexToSlot[joltId.GetIndex()] = *index;
        return CreateResult<BodyId>{ BodyId{ *index, slot.generation }, ErrorCode::None, {} };
    }

    bool HostedJoltWorld::destroyBody(BodyId body)
    {
        if (mInStep || findBody(body) == nullptr)
            return false;
        for (std::uint32_t i = 0; i < mJointSlots.size(); ++i)
        {
            const JointSlot& joint = mJointSlots[i];
            if (joint.alive && (joint.bodyA == body.index || joint.bodyB == body.index))
                destroyJointSlot(i);
        }
        BodySlot& slot = mBodySlots[body.index];
        if (slot.kinematicTargetPending)
        {
            std::erase(mKinematicTargets, body.index);
            slot.kinematicTargetPending = false;
        }
        mBodyIndexToSlot[slot.joltId.GetIndex()] = kNoSlot;
        bodies().RemoveBody(slot.joltId);
        bodies().DestroyBody(slot.joltId);
        slot.alive = false;
        ++slot.generation;
        mFreeBodySlots.push_back(body.index);
        return true;
    }

    bool HostedJoltWorld::destroyAll()
    {
        if (mInStep)
            return false;
        // Bodies first: destroyBody also destroys the joints that use the body.
        for (std::uint32_t i = 0; i < mBodySlots.size(); ++i)
        {
            if (mBodySlots[i].alive)
                destroyBody(BodyId{ i, mBodySlots[i].generation });
        }
        for (std::uint32_t i = 0; i < mGroupSlots.size(); ++i)
        {
            if (mGroupSlots[i].alive)
                destroyCollisionGroup(CollisionGroupId{ i, mGroupSlots[i].generation });
        }
        return true;
    }

    CreateResult<JointId> HostedJoltWorld::createJoint(const JointDesc& desc)
    {
        if (mInStep)
            return failure<JointId>(ErrorCode::CalledDuringStep, "createJoint cannot be called from a step listener");
        const BodySlot* bodyA = findBody(desc.bodyA);
        if (bodyA == nullptr)
            return failure<JointId>(ErrorCode::InvalidHandle, "bodyA is not a live body");
        const BodySlot* bodyB = findBody(desc.bodyB);
        if (bodyB == nullptr)
            return failure<JointId>(ErrorCode::InvalidHandle, "bodyB is not a live body");
        if (desc.bodyA == desc.bodyB)
            return failure<JointId>(ErrorCode::InvalidDescription, "bodyB must be a different body from bodyA");
        if (std::string error = validateJointGeometry(desc); !error.empty())
            return failure<JointId>(ErrorCode::InvalidDescription, std::move(error));
        const std::optional<std::uint32_t> index = takeSlot(mJointSlots, mFreeJointSlots, mConfig.maxJoints);
        if (!index)
            return failure<JointId>(ErrorCode::CapacityExceeded, "maxJoints reached");

        const float s = mConfig.unitsPerMeter;
        const JPH::TwoBodyConstraintSettings* settings = nullptr;
        JPH::SwingTwistConstraintSettings cone;
        JPH::SixDOFConstraintSettings pyramid;
        if (desc.swingShape == SwingShape::Cone)
        {
            cone.mSpace = JPH::EConstraintSpace::WorldSpace;
            cone.mPosition1 = cone.mPosition2 = positionToWorld(desc.anchorM, s);
            cone.mTwistAxis1 = cone.mTwistAxis2 = toJolt(desc.twistAxis).Normalized();
            cone.mPlaneAxis1 = cone.mPlaneAxis2 = toJolt(desc.planeAxis).Normalized();
            cone.mNormalHalfConeAngle = desc.normalSwingMaxRad;
            cone.mPlaneHalfConeAngle = desc.planeSwingMaxRad;
            cone.mTwistMinAngle = desc.twistMinRad;
            cone.mTwistMaxAngle = desc.twistMaxRad;
            cone.mNumPositionStepsOverride = kJointPositionSteps;
            settings = &cone;
        }
        else
        {
            // Same constraint space as the cone: X = twist, Y = plane x twist, Z = plane (JoltWorld.cpp).
            const JPH::Vec3 twist = toJolt(desc.twistAxis).Normalized();
            const JPH::Vec3 plane = toJolt(desc.planeAxis).Normalized();
            pyramid.mSpace = JPH::EConstraintSpace::WorldSpace;
            pyramid.mPosition1 = pyramid.mPosition2 = positionToWorld(desc.anchorM, s);
            pyramid.mAxisX1 = pyramid.mAxisX2 = twist;
            pyramid.mAxisY1 = pyramid.mAxisY2 = plane.Cross(twist).Normalized();
            pyramid.mSwingType = JPH::ESwingType::Pyramid;
            using Axis = JPH::SixDOFConstraintSettings::EAxis;
            pyramid.MakeFixedAxis(Axis::TranslationX);
            pyramid.MakeFixedAxis(Axis::TranslationY);
            pyramid.MakeFixedAxis(Axis::TranslationZ);
            pyramid.SetLimitedAxis(Axis::RotationX, desc.twistMinRad, desc.twistMaxRad);
            pyramid.SetLimitedAxis(Axis::RotationY, desc.planeSwingMinRad, desc.planeSwingMaxRad);
            pyramid.SetLimitedAxis(Axis::RotationZ, desc.normalSwingMinRad, desc.normalSwingMaxRad);
            pyramid.mNumPositionStepsOverride = kJointPositionSteps;
            settings = &pyramid;
        }

        JPH::TwoBodyConstraint* created = bodies().CreateConstraint(settings, bodyA->joltId, bodyB->joltId);
        if (created == nullptr)
        {
            mFreeJointSlots.push_back(*index);
            return failure<JointId>(ErrorCode::InvalidHandle, "bodyA or bodyB could not be locked");
        }
        JointSlot& slot = mJointSlots[*index];
        // Jolt constraints are reference counted: the slot holds one reference, the system another while added.
        slot.constraint = created;
        slot.shape = desc.swingShape;
        slot.alive = true;
        slot.bodyA = desc.bodyA.index;
        slot.bodyB = desc.bodyB.index;
        mSystem.AddConstraint(slot.constraint);
        return CreateResult<JointId>{ JointId{ *index, slot.generation }, ErrorCode::None, {} };
    }

    void HostedJoltWorld::destroyJointSlot(std::uint32_t index)
    {
        JointSlot& slot = mJointSlots[index];
        mSystem.RemoveConstraint(slot.constraint);
        slot.constraint = nullptr;
        slot.alive = false;
        ++slot.generation;
        mFreeJointSlots.push_back(index);
    }

    bool HostedJoltWorld::destroyJoint(JointId joint)
    {
        if (mInStep || findJoint(joint) == nullptr)
            return false;
        destroyJointSlot(joint.index);
        return true;
    }

    CreateResult<CollisionGroupId> HostedJoltWorld::createCollisionGroup(std::uint32_t subGroupCount)
    {
        if (mInStep)
            return failure<CollisionGroupId>(
                ErrorCode::CalledDuringStep, "createCollisionGroup cannot be called from a step listener");
        if (subGroupCount == 0 || subGroupCount > std::min(mConfig.maxBodies, kMaxSubGroups))
            return failure<CollisionGroupId>(
                ErrorCode::InvalidDescription, "subGroupCount must be in [1, min(maxBodies, 65536)]");
        const std::optional<std::uint32_t> index = takeSlot(mGroupSlots, mFreeGroupSlots, mConfig.maxBodies);
        if (!index)
            return failure<CollisionGroupId>(
                ErrorCode::CapacityExceeded, "too many collision groups (limit maxBodies)");
        const JPH::CollisionGroup::GroupID groupId = sNextGroupId.fetch_add(1);
        if (groupId == JPH::CollisionGroup::cInvalidGroup)
            return failure<CollisionGroupId>(ErrorCode::CapacityExceeded, "collision group IDs are used up");
        // A JPH::Ref takes ownership of the new table at once (Jolt's reference counting idiom).
        const JPH::Ref<JPH::GroupFilterTable> table = new JPH::GroupFilterTable(subGroupCount);
        GroupSlot& slot = mGroupSlots[*index];
        slot.table = table;
        slot.alive = true;
        slot.subGroupCount = subGroupCount;
        slot.joltGroupId = groupId;
        return CreateResult<CollisionGroupId>{ CollisionGroupId{ *index, slot.generation }, ErrorCode::None, {} };
    }

    bool HostedJoltWorld::setSubGroupsCollide(CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide)
    {
        const GroupSlot* slot = findGroup(group);
        if (mInStep || slot == nullptr || a == b || a >= slot->subGroupCount || b >= slot->subGroupCount)
            return false;
        // GroupFilterTable wants the smaller sub-group first.
        const std::uint32_t low = std::min(a, b);
        const std::uint32_t high = std::max(a, b);
        if (collide)
            slot->table->EnableCollision(low, high);
        else
            slot->table->DisableCollision(low, high);
        return true;
    }

    bool HostedJoltWorld::destroyCollisionGroup(CollisionGroupId group)
    {
        GroupSlot* slot = findGroup(group);
        if (mInStep || slot == nullptr)
            return false;
        slot->table = nullptr; // member bodies keep their own references
        slot->alive = false;
        ++slot->generation;
        mFreeGroupSlots.push_back(group.index);
        return true;
    }

    void HostedJoltWorld::wakeJointBodies(const JointSlot& slot)
    {
        for (const std::uint32_t bodyIndex : { slot.bodyA, slot.bodyB })
        {
            const BodySlot& body = mBodySlots[bodyIndex];
            if (body.motion != MotionType::Static)
                bodies().ActivateBody(body.joltId);
        }
    }

    bool HostedJoltWorld::setJointPositionSteps(JointId joint, std::uint32_t steps)
    {
        const JointSlot* slot = findJoint(joint);
        if (mInStep || slot == nullptr || steps < 1 || steps > kMaxJointPositionSteps)
            return false;
        slot->constraint->SetNumPositionStepsOverride(steps);
        return true;
    }

    bool HostedJoltWorld::setJointMotor(JointId joint, const JointMotor& motor)
    {
        const JointSlot* slot = findJoint(joint);
        if (slot == nullptr || !isValidMotor(motor))
            return false;
        const float s = mConfig.unitsPerMeter;
        const float stiffness = std::max(torqueToWorld(motor.stiffnessNmPerRad, s), kDampingOnlyStiffness);
        const float damping = torqueToWorld(motor.dampingNmsPerRad, s);
        const float maxTorque = torqueToWorld(motor.maxTorqueNm, s);
        const bool off = motor.stiffnessNmPerRad == 0.0f && motor.dampingNmsPerRad == 0.0f;
        const auto configure = [&](JPH::MotorSettings& settings) {
            settings.mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, stiffness, damping);
            settings.SetTorqueLimit(maxTorque);
        };
        // Position mode gives stiffness * angle error - damping * relative velocity (see kDampingOnlyStiffness).
        const JPH::EMotorState state = off ? JPH::EMotorState::Off : JPH::EMotorState::Position;
        if (slot->shape == SwingShape::Cone)
        {
            auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
            if (!off)
            {
                configure(constraint.GetSwingMotorSettings());
                configure(constraint.GetTwistMotorSettings());
                constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
            }
            constraint.SetSwingMotorState(state);
            constraint.SetTwistMotorState(state);
        }
        else
        {
            auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
            using Axis = JPH::SixDOFConstraintSettings::EAxis;
            if (!off)
                constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
            for (const Axis axis : { Axis::RotationX, Axis::RotationY, Axis::RotationZ })
            {
                if (!off)
                    configure(constraint.GetMotorSettings(axis));
                constraint.SetMotorState(axis, state);
            }
        }
        wakeJointBodies(*slot);
        return true;
    }

    bool HostedJoltWorld::setJointTarget(JointId joint, Quat targetRotation)
    {
        return setJointTarget(joint, targetRotation, Vec3{});
    }

    bool HostedJoltWorld::setJointTarget(JointId joint, Quat targetRotation, Vec3 targetAngularVelocityRadps)
    {
        const JointSlot* slot = findJoint(joint);
        if (slot == nullptr || !isUnit(targetRotation) || !isFinite(targetAngularVelocityRadps))
            return false;
        // Note: Jolt v5.5.0 uses the target angular velocity only in Velocity motor state, so with the Position state
        // used by setJointMotor it has no effect (the newer Jolt behind CreativeMotion's own world feeds it forward).
        const JPH::Quat target = toJolt(targetRotation).Normalized();
        const JPH::Vec3 targetOmega = toJolt(targetAngularVelocityRadps);
        if (slot->shape == SwingShape::Cone)
        {
            auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
            constraint.SetTargetOrientationCS(target);
            constraint.SetTargetAngularVelocityCS(targetOmega);
        }
        else
        {
            auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
            constraint.SetTargetOrientationCS(target);
            constraint.SetTargetAngularVelocityCS(targetOmega);
        }
        wakeJointBodies(*slot);
        return true;
    }

    bool HostedJoltWorld::applyImpulse(BodyId body, Vec3 impulseNs, Vec3 worldPointM)
    {
        const BodySlot* slot = findBody(body);
        if (slot == nullptr || slot->motion != MotionType::Dynamic)
            return false;
        const float s = mConfig.unitsPerMeter;
        bodies().AddImpulse(slot->joltId, vectorToWorld(impulseNs, s), positionToWorld(worldPointM, s));
        return true;
    }

    bool HostedJoltWorld::setVelocity(BodyId body, Vec3 linearMps, Vec3 angularRadps)
    {
        const BodySlot* slot = findBody(body);
        if (slot == nullptr || slot->motion == MotionType::Static)
            return false;
        bodies().SetLinearAndAngularVelocity(
            slot->joltId, vectorToWorld(linearMps, mConfig.unitsPerMeter), toJolt(angularRadps));
        return true;
    }

    bool HostedJoltWorld::setBodyDamping(BodyId body, float linearPerS, float angularPerS)
    {
        const BodySlot* slot = findBody(body);
        const bool valid
            = linearPerS >= 0.0f && std::isfinite(linearPerS) && angularPerS >= 0.0f && std::isfinite(angularPerS);
        if (mInStep || slot == nullptr || slot->motion != MotionType::Dynamic || !valid)
            return false;
        const JPH::BodyLockWrite lock(mSystem.GetBodyLockInterface(), slot->joltId);
        if (!lock.Succeeded())
            return false;
        JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();
        motion->SetLinearDamping(linearPerS);
        motion->SetAngularDamping(angularPerS);
        return true;
    }

    bool HostedJoltWorld::setKinematicTarget(BodyId body, Vec3 positionM, Quat rotation)
    {
        const BodySlot* found = findBody(body);
        if (found == nullptr || found->motion != MotionType::Kinematic || !isUnit(rotation))
            return false;
        BodySlot& slot = mBodySlots[body.index];
        bodies().MoveKinematic(slot.joltId, positionToWorld(positionM, mConfig.unitsPerMeter),
            toJolt(rotation).Normalized(), mConfig.stepSeconds);
        if (!slot.kinematicTargetPending)
        {
            // Capacity was reserved up front, and each body is listed at most once.
            slot.kinematicTargetPending = true;
            mKinematicTargets.push_back(body.index);
        }
        return true;
    }

    bool HostedJoltWorld::setMotionType(BodyId body, MotionType motion)
    {
        const BodySlot* found = findBody(body);
        if (mInStep || found == nullptr)
            return false;
        if (found->motion == motion)
            return true;
        if (found->motion == MotionType::Static)
            return false; // created static: no mass properties
        if (motion == MotionType::Static && !mConfig.staticLayer)
            return false;

        BodySlot& slot = mBodySlots[body.index];
        JPH::EMotionType joltMotion = JPH::EMotionType::Static;
        switch (motion)
        {
            case MotionType::Static:
                joltMotion = JPH::EMotionType::Static;
                break;
            case MotionType::Dynamic:
                joltMotion = JPH::EMotionType::Dynamic;
                break;
            case MotionType::Kinematic:
                joltMotion = JPH::EMotionType::Kinematic;
                break;
            default:
                return false;
        }

        if (motion == MotionType::Static)
        {
            bodies().DeactivateBody(slot.joltId);
            bodies().SetObjectLayer(slot.joltId, *mConfig.staticLayer);
        }
        const JPH::EActivation activation
            = (motion == MotionType::Dynamic) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        bodies().SetMotionType(slot.joltId, joltMotion, activation);

        if (slot.kinematicTargetPending && motion != MotionType::Kinematic)
        {
            slot.kinematicTargetPending = false;
            std::erase(mKinematicTargets, body.index);
        }
        slot.motion = motion;
        return true;
    }

    std::optional<MotionType> HostedJoltWorld::getMotionType(BodyId body) const
    {
        const BodySlot* slot = findBody(body);
        if (slot == nullptr)
            return std::nullopt;
        return slot->motion;
    }

    bool HostedJoltWorld::readBody(BodyId body, BodyState& out) const
    {
        const BodySlot* slot = findBody(body);
        if (slot == nullptr)
            return false;
        const float s = mConfig.unitsPerMeter;
        const JPH::BodyInterface& bi = bodies();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bi.GetPositionAndRotation(slot->joltId, position, rotation);
        out.positionM = positionFromWorld(position, s);
        out.rotation = fromJolt(rotation);
        out.linearVelocityMps = vectorFromWorld(bi.GetLinearVelocity(slot->joltId), s);
        out.angularVelocityRadps = fromJolt(bi.GetAngularVelocity(slot->joltId));
        out.awake = bi.IsActive(slot->joltId);
        return true;
    }

    std::size_t HostedJoltWorld::readBodies(std::span<const BodyId> ids, std::span<BodyState> out) const
    {
        const std::size_t count = std::min(ids.size(), out.size());
        std::size_t read = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (readBody(ids[i], out[i]))
                ++read;
        }
        return read;
    }

    std::optional<RayHit> HostedJoltWorld::castRay(Vec3 originM, Vec3 directionTimesLengthM, BodyId ignore) const
    {
        const float s = mConfig.unitsPerMeter;
        const JPH::RRayCast ray{ positionToWorld(originM, s), vectorToWorld(directionTimesLengthM, s) };
        JPH::RayCastResult hit;
        const BodySlot* ignored = findBody(ignore);
        const JPH::IgnoreSingleBodyFilter ignoreFilter(ignored != nullptr ? ignored->joltId : JPH::BodyID());
        const JPH::BroadPhaseLayerFilter allBroadPhase;
        const JPH::ObjectLayerFilter allLayers;
        const bool found = mSystem.GetNarrowPhaseQuery().CastRay(ray, hit,
            mConfig.rayBroadPhaseFilter != nullptr ? *mConfig.rayBroadPhaseFilter : allBroadPhase,
            mConfig.rayObjectLayerFilter != nullptr ? *mConfig.rayObjectLayerFilter : allLayers, ignoreFilter);
        if (!found)
            return std::nullopt;

        const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
        const JPH::BodyLockRead lock(mSystem.GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded())
            return std::nullopt;

        RayHit result;
        // A body that is not ours (the host's world, an actor) has no handle: the BodyId stays invalid.
        const std::uint32_t joltIndex = hit.mBodyID.GetIndex();
        if (joltIndex < mBodyIndexToSlot.size() && mBodyIndexToSlot[joltIndex] != kNoSlot)
        {
            const BodySlot& slot = mBodySlots[mBodyIndexToSlot[joltIndex]];
            if (slot.alive && slot.joltId == hit.mBodyID)
                result.body = BodyId{ mBodyIndexToSlot[joltIndex], slot.generation };
        }
        result.pointM = positionFromWorld(point, s);
        result.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
        result.fraction = hit.mFraction;
        return result;
    }

    void HostedJoltWorld::beginStep()
    {
        if (mInStep)
            return;
        mInStep = true;
        // Only listeners registered before this step are called, in both phases. One removed during the step is
        // nulled out (not erased) so the indices stay put, and is skipped.
        mStepListenerCount = mListeners.size();
        const float dt = mConfig.stepSeconds;
        for (std::size_t i = 0; i < mStepListenerCount; ++i)
        {
            if (mListeners[i].listener != nullptr)
                mListeners[i].listener->onPreStep(*this, dt);
        }
    }

    void HostedJoltWorld::endStep()
    {
        if (!mInStep)
            return;
        // MoveKinematic gives a body the velocity that reaches its target in one step, and Jolt keeps it afterwards.
        // Zero it so the body stays at its target until it is moved again.
        for (const std::uint32_t index : mKinematicTargets)
        {
            BodySlot& slot = mBodySlots[index];
            if (slot.alive && slot.kinematicTargetPending)
            {
                bodies().SetLinearAndAngularVelocity(slot.joltId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
                slot.kinematicTargetPending = false;
            }
        }
        mKinematicTargets.clear();
        const float dt = mConfig.stepSeconds;
        for (std::size_t i = 0; i < mStepListenerCount; ++i)
        {
            if (mListeners[i].listener != nullptr)
                mListeners[i].listener->onPostStep(*this, dt);
        }
        std::erase_if(mListeners, [](const ListenerEntry& entry) { return entry.listener == nullptr; });
        mInStep = false;
    }

    void HostedJoltWorld::step()
    {
        if (mInStep || mConfig.stepper == nullptr)
            return;
        beginStep();
        mConfig.stepper->update(mConfig.stepSeconds);
        endStep();
    }

    StepListenerId HostedJoltWorld::addStepListener(StepListener& listener)
    {
        if (mListeners.size() >= kMaxListeners)
            return StepListenerId{}; // invalid: the vector never grows past its reserved size
        // Serial numbers are never reused, so a removed id never matches again.
        const StepListenerId id{ mNextListenerSerial++, 1 };
        mListeners.push_back(ListenerEntry{ &listener, id });
        return id;
    }

    bool HostedJoltWorld::removeStepListener(StepListenerId id)
    {
        const auto it = std::find_if(mListeners.begin(), mListeners.end(),
            [id](const ListenerEntry& e) { return e.listener != nullptr && e.id == id; });
        if (it == mListeners.end())
            return false;
        if (mInStep)
            it->listener = nullptr; // erased in endStep()
        else
            mListeners.erase(it);
        return true;
    }
}
