#pragma once

#include <openeden/entity.hpp>
#include <openeden/physics.hpp>
#include <openeden/version.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace openeden::contracts
{
    /**
     * @brief Versioned contract for procedural animation and physical ragdoll characters.
     *
     * Provided by CreativeMotion ("creativemotion") under contract ID "openeden.animation".
     * Contains only SDK and standard library types in its interface.
     */
    class AnimationContract
    {
    public:
        static constexpr std::string_view kId = "openeden.animation";
        static constexpr sdk::Version kVersion{ 1, 2 };

        virtual ~AnimationContract() = default;

        /**
         * @brief Returns true if procedural animation / physical ragdoll simulation is enabled.
         * Defaults to false (off-by-default invariant).
         */
        [[nodiscard]] virtual bool isEnabled() const noexcept = 0;

        /**
         * @brief Enables or disables active procedural animation processing at runtime.
         */
        virtual void setEnabled(bool enabled) = 0;

        /**
         * @brief Returns the number of active character ragdoll instances managed by the provider.
         */
        [[nodiscard]] virtual std::size_t getActiveCharacterCount() const noexcept = 0;

        /**
         * @brief Spawns a physical ragdoll and reconciliation harness for the given entity
         * inside the host's shared physics world.
         */
        virtual bool spawnCharacter(
            sdk::EntityId entity, sdk::PhysicsVec3 rootPositionM = {}, float yawRad = 0.0f) = 0;

        /**
         * @brief Despawns the physical ragdoll and frees physics bodies/joints for the entity.
         */
        virtual bool despawnCharacter(sdk::EntityId entity) = 0;

        /**
         * @brief Returns true if the entity currently has an active character ragdoll.
         */
        [[nodiscard]] virtual bool hasCharacter(sdk::EntityId entity) const noexcept = 0;

        /**
         * @brief Applies a physical hit impulse to the character's ragdoll at the specified world point.
         */
        virtual bool applyHitImpulse(
            sdk::EntityId entity, sdk::PhysicsVec3 impulseNs, sdk::PhysicsVec3 contactPointM) = 0;

        /**
         * @brief Queries the current physical pelvis world position in meters.
         */
        [[nodiscard]] virtual std::optional<sdk::PhysicsVec3> getPelvisPosition(sdk::EntityId entity) const = 0;

        /**
         * @brief Queries the reconciled gameplay capsule center position in meters.
         */
        [[nodiscard]] virtual std::optional<sdk::PhysicsVec3> getReconciledCapsulePosition(
            sdk::EntityId entity) const = 0;

        /**
         * @brief Returns the current arbiter state name (e.g., "Standing", "Leaning", "Stepping", "Falling", "Fallen").
         */
        [[nodiscard]] virtual std::string_view getCharacterStateName(sdk::EntityId entity) const = 0;

        // =====================================================================
        // Pure Kinematic Inverse Kinematics (M9)
        // =====================================================================

        struct TwoBoneChain
        {
            sdk::PhysicsVec3 root;
            sdk::PhysicsVec3 mid;
            sdk::PhysicsVec3 end;
        };

        struct HingeLimits
        {
            float minBendRad = 0.0f;
            float maxBendRad = 3.14159265358979323846f;
        };

        struct TwoBoneResult
        {
            bool valid = false;
            bool reached = false;
            sdk::PhysicsVec3 mid;
            sdk::PhysicsVec3 end;
            float bendRad = 0.0f;
            sdk::PhysicsQuat rootRotation;
            sdk::PhysicsQuat midRotation;
        };

        struct FootPlacementInput
        {
            TwoBoneChain legChain;
            float groundDistanceM = 0.0f;
            float nominalHeightM = 0.0f;
            sdk::PhysicsVec3 groundNormal{ 0.0f, 0.0f, 1.0f };
            sdk::PhysicsVec3 poleDirection{ 0.0f, 1.0f, 0.0f };
            HingeLimits kneeLimits{ 0.05f, 2.8f };
        };

        struct FootPlacementResult
        {
            TwoBoneResult ik;
            sdk::PhysicsQuat footAlignment;
            float pelvisOffsetZM = 0.0f;
        };

        struct LookAtInput
        {
            sdk::PhysicsVec3 eyePosition;
            sdk::PhysicsVec3 headForward;
            sdk::PhysicsVec3 targetPosition;
            float currentYawRad = 0.0f;
            float currentPitchRad = 0.0f;
            float maxYawRad = 1.2217f;   // ~70 degrees
            float maxPitchRad = 0.7854f; // ~45 degrees
            float speedRadPerS = 8.0f;
            float dt = 0.0f;
        };

        struct LookAtResult
        {
            float yawRad = 0.0f;
            float pitchRad = 0.0f;
            bool inRange = false;
        };

        struct TurnSmoothingInput
        {
            float currentYawRad = 0.0f;
            float targetYawRad = 0.0f;
            float halfLifeS = 0.08f;
            float dt = 0.0f;
        };

        /**
         * @brief Pure analytic two-bone IK solver with hinge limits on the middle joint.
         */
        [[nodiscard]] virtual TwoBoneResult solveTwoBone(
            const TwoBoneChain& chain, sdk::PhysicsVec3 target, sdk::PhysicsVec3 poleDirection,
            const HingeLimits& limits = {}) const = 0;

        /**
         * @brief Solves foot placement, knee bend, foot normal alignment, and pelvis offset for uneven terrain.
         */
        [[nodiscard]] virtual FootPlacementResult solveFootPlacement(
            const FootPlacementInput& input) const = 0;

        /**
         * @brief Solves gaze tracking with angular limits and smooth temporal interpolation.
         */
        [[nodiscard]] virtual LookAtResult solveLookAt(
            const LookAtInput& input) const = 0;

        /**
         * @brief Exponential angular decay smoothing for actor yaw across +/-pi boundary.
         */
        [[nodiscard]] virtual float solveTurnSmoothing(
            const TurnSmoothingInput& input) const = 0;
    };

    using TwoBoneChain = AnimationContract::TwoBoneChain;
    using HingeLimits = AnimationContract::HingeLimits;
    using TwoBoneResult = AnimationContract::TwoBoneResult;
    using FootPlacementInput = AnimationContract::FootPlacementInput;
    using FootPlacementResult = AnimationContract::FootPlacementResult;
    using LookAtInput = AnimationContract::LookAtInput;
    using LookAtResult = AnimationContract::LookAtResult;
    using TurnSmoothingInput = AnimationContract::TurnSmoothingInput;
} // namespace openeden::contracts
