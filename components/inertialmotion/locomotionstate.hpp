#ifndef OPENEDEN_COMPONENTS_INERTIALMOTION_LOCOMOTIONSTATE_H
#define OPENEDEN_COMPONENTS_INERTIALMOTION_LOCOMOTIONSTATE_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec2.h>

#include <cmath>
#include <string>
#include <string_view>

namespace InertialMotion
{
    enum class Preset
    {
        Off,
        Low,
        Medium,
        High,
        Custom
    };

    enum class LocomotionPhase
    {
        Idle,
        Starting,
        Moving,
        Stopping,
        Skidding,
        Pivot
    };

    struct LocomotionParameters
    {
        float mAccelTime = 0.25f; // Seconds from rest to full speed
        float mStopTime = 0.18f; // Seconds from full speed to zero
        float mSkidTurnTime = 0.25f; // Seconds for 180-deg reversal skid
        float mTurnHalfLife = 0.12f; // Half-life in seconds for body yaw heading tracking
        float mTurnInPlaceThreshold = 50.0f; // Look error threshold in degrees to trigger stationary pivot
        float mMaxLeanDegrees = 7.0f; // Maximum torso lean/bank tilt in degrees
        bool mEnableAuthoredTransitions = true;
        bool mProceduralLean = true;
        float mLeanScale = 1.0f;
    };

    /// Fraction of the intended speed below which a moving actor's movement animation is never slowed: the walk
    /// would otherwise start in slow motion (the animation speed follows the smoothed ground speed, which is 0 at
    /// the start). Value from the first inertial locomotion version (task 0006 follow-up); the animation reaches the
    /// full speed as the smoothed speed rises.
    inline constexpr float sMinimumAnimationSpeedFraction = 0.15f;

    /// Where an actor's requested movement comes from while it walks or runs: the root motion of the playing
    /// animation (vanilla) or the intended velocity (the input direction times the actor's current speed).
    enum class MovementRequestSource
    {
        Animation,
        Intended
    };

    /// Intended only while inertial locomotion drives the actor, no hit reaction plays, a movement group plays
    /// and no authored start, stop, skid or turn clip does (those carry their own root motion). Everything else, in
    /// particular every actor with inertial locomotion off, keeps the animation's root motion.
    ///
    /// Why not always the animation: with inertial locomotion on, the animation speed follows the smoothed ground
    /// speed, and the smoothed speed follows the request. If the request is the animation's root motion, the loop
    /// stalls at sMinimumAnimationSpeedFraction of the walking speed (see docs/research/0022).
    constexpr MovementRequestSource chooseMovementRequestSource(
        bool inertialActive, bool hitReaction, bool movementGroupPlaying, bool authoredTransitionPlaying)
    {
        return inertialActive && !hitReaction && movementGroupPlaying && !authoredTransitionPlaying
            ? MovementRequestSource::Intended
            : MovementRequestSource::Animation;
    }

    /// The ground speed (units per second) the movement animation is scaled to: with movement input the smoothed
    /// speed, but not below sMinimumAnimationSpeedFraction of the intended speed; without input the smoothed speed.
    constexpr float animationGroundSpeed(float smoothedSpeed, float intendedSpeed, bool hasMovementInput)
    {
        const float floorSpeed = sMinimumAnimationSpeedFraction * intendedSpeed;
        return hasMovementInput && floorSpeed > smoothedSpeed ? floorSpeed : smoothedSpeed;
    }

    /// The movement state "start" clip (WalkStart, RunStart) is only chosen for an actor that moves slower than this
    /// (units per second); from then on the cycle plays. The value is the one the first version used.
    inline constexpr float sAuthoredStartMaxSpeed = 20.f;

    /// Whether the authored start clip may be chosen this frame. Evaluated at the top of the frame, before the
    /// physics step that first moves the actor, so it must not wait for LocomotionPhase::Starting: that phase
    /// exists only from the next frame on, and by then the actor's movement state is already the walk cycle (the
    /// first version asked for it and so could never choose a start clip; docs/research/0052).
    /// @param clipExists the actor's animation sources contain the group (Morrowind's data has none)
    /// @param standingOrTurningInPlace the actor's movement state is 'none' or a turn in place
    constexpr bool shouldPlayAuthoredStart(bool inertialActive, bool authoredEnabled, bool clipExists,
        bool hasMovementInput, bool standingOrTurningInPlace, float smoothedSpeed)
    {
        return inertialActive && authoredEnabled && clipExists && hasMovementInput && standingOrTurningInPlace
            && smoothedSpeed < sAuthoredStartMaxSpeed;
    }

    /// Converts a preset enum to its standard parameter set
    LocomotionParameters getPresetParameters(Preset preset);

    /// Parses string ("off", "low", "medium", "high", "custom") to a Preset
    Preset parsePreset(std::string_view name);

    /**
     * @brief Manages 2D horizontal inertial locomotion state for a ground actor.
     * Smooths instantaneous requested velocity into weighted momentum with realistic
     * acceleration, deceleration, and turn arcs.
     */
    class LocomotionState
    {
    public:
        LocomotionState() = default;

        /**
         * @brief Updates horizontal velocity towards requested velocity over timestep dt.
         * @param requestedVelocity Target horizontal velocity in world units/second.
         * @param dt Elapsed timestep in seconds.
         * @param params Locomotion tuning parameters.
         * @param maxSpeed Actor's maximum speed attribute (scales acceleration curves).
         * @return The smoothed velocity in world units/second.
         */
        vsg::vec2 update(
            const vsg::vec2& requestedVelocity, float dt, const LocomotionParameters& params, float maxSpeed = 300.0f);

        /**
         * @brief Smooths actor body heading towards travel trajectory.
         * @param currentYaw Current actor yaw angle in radians.
         * @param dt Elapsed timestep in seconds.
         * @param params Locomotion tuning parameters.
         * @param strafeOrAim True if strafing, in combat stance, or aiming (maintains facing).
         * @return The smoothed yaw angle in radians.
         */
        float updateYaw(float currentYaw, float dt, const LocomotionParameters& params, bool strafeOrAim = false);

        /**
         * @brief Resets the locomotion state (e.g. on landing, swimming, teleportation, paralysis).
         */
        void reset(const vsg::vec2& seedVelocity = vsg::vec2(0.f, 0.f), float currentYaw = 0.f);

        [[nodiscard]] const vsg::vec2& getSmoothedVelocity() const noexcept { return mSmoothedVelocity; }
        [[nodiscard]] float getSmoothedSpeed() const noexcept { return mSmoothedSpeed; }
        [[nodiscard]] float getSmoothedYaw() const noexcept { return mSmoothedYaw; }
        [[nodiscard]] float getLinearAcceleration() const noexcept { return mLinearAcceleration; }
        [[nodiscard]] float getAngularVelocity() const noexcept { return mAngularVelocity; }
        [[nodiscard]] LocomotionPhase getPhase() const noexcept { return mPhase; }

    private:
        vsg::vec2 mSmoothedVelocity{ 0.f, 0.f };
        float mSmoothedSpeed = 0.f;
        float mSmoothedYaw = 0.f;
        float mLastSpeed = 0.f;
        float mLinearAcceleration = 0.f;
        float mAngularVelocity = 0.f;
        LocomotionPhase mPhase = LocomotionPhase::Idle;
    };
}

#endif
