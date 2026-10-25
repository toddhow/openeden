#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace openeden::sdk
{
    using CollisionLayer = std::uint32_t;

    /**
     * @brief 3D vector in SI units (meters, m/s, rad/s, N*s), right-handed Z-up.
     */
    struct PhysicsVec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        friend bool operator==(const PhysicsVec3&, const PhysicsVec3&) = default;
    };

    /**
     * @brief Unit quaternion representing orientation (default identity).
     */
    struct PhysicsQuat
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        friend bool operator==(const PhysicsQuat&, const PhysicsQuat&) = default;
    };

    /**
     * @brief Generation-counted opaque handle to a physics resource.
     */
    template <typename Tag>
    struct PhysicsHandle
    {
        static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t index = kInvalidIndex;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool isValid() const noexcept { return index != kInvalidIndex; }

        friend bool operator==(const PhysicsHandle&, const PhysicsHandle&) = default;
    };

    using BodyId = PhysicsHandle<struct BodyTag>;
    using JointId = PhysicsHandle<struct JointTag>;
    using ConstraintId = JointId;
    using CollisionGroupId = PhysicsHandle<struct CollisionGroupTag>;

    enum class PhysicsErrorCode : std::uint8_t
    {
        None = 0,
        InvalidDescription,
        InvalidHandle,
        CapacityExceeded,
        CalledDuringStep,
    };

    template <typename Id>
    struct CreateResult
    {
        Id id{};
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message;

        [[nodiscard]] bool ok() const noexcept { return error == PhysicsErrorCode::None; }
    };

    struct BoxShape
    {
        PhysicsVec3 halfExtentsM{ 0.5f, 0.5f, 0.5f };
    };

    struct SphereShape
    {
        float radiusM = 0.5f;
    };

    /**
     * @brief Capsule aligned along the body's local Z axis (Z-up).
     * Total length is 2 * (halfHeightM + radiusM).
     */
    struct CapsuleShape
    {
        float halfHeightM = 0.5f;
        float radiusM = 0.25f;
    };

    using PhysicsShape = std::variant<BoxShape, SphereShape, CapsuleShape>;

    enum class MotionType : std::uint8_t
    {
        Static = 0,
        Dynamic,
        Kinematic,
    };

    struct BodyDesc
    {
        PhysicsShape shape = BoxShape{};
        MotionType motion = MotionType::Dynamic;
        PhysicsVec3 positionM{};
        PhysicsQuat rotation{};
        float massKg = 1.0f;
        float friction = 0.2f;
        float restitution = 0.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        CollisionGroupId collisionGroup{};
        std::uint32_t subGroup = 0;
    };

    enum class SwingShape : std::uint8_t
    {
        Cone = 0,
        Pyramid,
    };

    struct JointDesc
    {
        BodyId bodyA{};
        BodyId bodyB{};
        PhysicsVec3 anchorM{};
        PhysicsVec3 twistAxis{ 1.0f, 0.0f, 0.0f };
        PhysicsVec3 planeAxis{ 0.0f, 1.0f, 0.0f };
        SwingShape swingShape = SwingShape::Cone;
        float planeSwingMinRad = 0.0f;
        float planeSwingMaxRad = 0.0f;
        float normalSwingMinRad = 0.0f;
        float normalSwingMaxRad = 0.0f;
        float twistMinRad = 0.0f;
        float twistMaxRad = 0.0f;
    };

    struct JointMotor
    {
        float stiffnessNmPerRad = 0.0f;
        float dampingNmsPerRad = 0.0f;
        float maxTorqueNm = std::numeric_limits<float>::max();
    };

    struct BodyState
    {
        PhysicsVec3 positionM{};
        PhysicsQuat rotation{};
        PhysicsVec3 linearVelocityMps{};
        PhysicsVec3 angularVelocityRadps{};
        bool awake = false;
    };

    struct RayHit
    {
        BodyId body{};
        PhysicsVec3 pointM{};
        PhysicsVec3 normal{};
        float fraction = 0.0f;
    };

    struct PhysicsWorldConfig
    {
        float fixedStepSeconds = 1.0f / 60.0f;
        PhysicsVec3 gravityMps2{ 0.0f, 0.0f, -9.81f };
        std::uint32_t maxBodies = 2048;
        std::uint32_t maxJoints = 2048;
        std::uint32_t subStepsPerStep = 1;
        std::uint32_t workerThreads = 1;
    };

    /**
     * @brief Physics service interface for accessing the core's shared Jolt world.
     *
     * Plugins use this service to build and drive bodies and joints inside the
     * core's single Jolt PhysicsSystem rather than creating a second physics world.
     */
    class PhysicsService
    {
    public:
        virtual ~PhysicsService() = default;

        // --- Build ---
        virtual CreateResult<BodyId> createBody(const BodyDesc& desc) = 0;
        virtual bool destroyBody(BodyId body) = 0;
        [[nodiscard]] virtual bool isBodyValid(BodyId id) const = 0;

        virtual CreateResult<JointId> createJoint(const JointDesc& desc) = 0;
        virtual bool destroyJoint(JointId joint) = 0;
        [[nodiscard]] virtual bool isConstraintValid(ConstraintId id) const = 0;

        // --- Collision groups ---
        virtual CreateResult<CollisionGroupId> createCollisionGroup(std::uint32_t subGroupCount) = 0;
        virtual bool setSubGroupsCollide(
            CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide) = 0;
        virtual bool destroyCollisionGroup(CollisionGroupId group) = 0;

        // --- Named collision layers ---
        virtual CollisionLayer registerCollisionLayer(std::string_view /*name*/) { return 0; }
        virtual void declareCollisionPair(
            std::string_view /*layerA*/, std::string_view /*layerB*/)
        {
        }

        // --- Drive joints ---
        virtual bool setJointMotor(JointId joint, const JointMotor& motor) = 0;
        virtual bool setJointPositionSteps(JointId joint, std::uint32_t steps) = 0;
        virtual bool setJointTarget(JointId joint, PhysicsQuat targetRotation) = 0;
        virtual bool setJointTarget(
            JointId joint, PhysicsQuat targetRotation, PhysicsVec3 targetAngularVelocityRadps)
        {
            (void)targetAngularVelocityRadps;
            return setJointTarget(joint, targetRotation);
        }

        // --- Drive bodies ---
        virtual bool applyImpulse(BodyId body, PhysicsVec3 impulseNs, PhysicsVec3 worldPointM) = 0;
        virtual bool setVelocity(BodyId body, PhysicsVec3 linearMps, PhysicsVec3 angularRadps) = 0;
        virtual bool setBodyDamping(BodyId body, float linearPerS, float angularPerS) = 0;
        virtual bool setKinematicTarget(BodyId body, PhysicsVec3 positionM, PhysicsQuat rotation) = 0;

        // --- Motion type ---
        virtual bool setMotionType(BodyId body, MotionType motion) = 0;
        [[nodiscard]] virtual std::optional<MotionType> getMotionType(BodyId body) const = 0;

        // --- Read ---
        virtual bool readBody(BodyId body, BodyState& out) const = 0;
        virtual std::size_t readBodies(std::span<const BodyId> bodies, std::span<BodyState> out) const = 0;

        // --- Query ---
        [[nodiscard]] virtual std::optional<RayHit> castRay(
            PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM, BodyId ignore = {}) const = 0;

        // --- Step & Timing ---
        virtual void step(float dtSeconds = 0.0f) = 0;
        [[nodiscard]] virtual float fixedStepSeconds() const = 0;
    };
} // namespace openeden::sdk
