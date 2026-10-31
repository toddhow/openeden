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
        float mAccelTime = 0.25f;            // Seconds from rest to full speed
        float mStopTime = 0.18f;             // Seconds from full speed to zero
        float mSkidTurnTime = 0.25f;         // Seconds for 180-deg reversal skid
        float mTurnHalfLife = 0.12f;         // Half-life in seconds for body yaw heading tracking
        float mTurnInPlaceThreshold = 50.0f; // Look error threshold in degrees to trigger stationary pivot
        float mMaxLeanDegrees = 7.0f;        // Maximum torso lean/bank tilt in degrees
        bool mEnableAuthoredTransitions = true;
        bool mProceduralLean = true;
        float mLeanScale = 1.0f;
    };

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
