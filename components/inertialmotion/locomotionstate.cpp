#include "locomotionstate.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace InertialMotion
{
    LocomotionParameters getPresetParameters(Preset preset)
    {
        LocomotionParameters params{};
        switch (preset)
        {
            case Preset::Off:
                params.mAccelTime = 0.0f;
                params.mStopTime = 0.0f;
                params.mSkidTurnTime = 0.0f;
                params.mTurnHalfLife = 0.0f;
                params.mTurnInPlaceThreshold = 90.0f;
                params.mMaxLeanDegrees = 0.0f;
                params.mEnableAuthoredTransitions = false;
                params.mProceduralLean = false;
                params.mLeanScale = 0.0f;
                break;

            case Preset::Low:
                params.mAccelTime = 0.12f;
                params.mStopTime = 0.08f;
                params.mSkidTurnTime = 0.10f;
                params.mTurnHalfLife = 0.05f;
                params.mTurnInPlaceThreshold = 70.0f;
                params.mMaxLeanDegrees = 3.5f;
                params.mEnableAuthoredTransitions = true;
                params.mProceduralLean = true;
                params.mLeanScale = 0.6f;
                break;

            case Preset::Medium:
            case Preset::Custom:
                params.mAccelTime = 0.25f;
                params.mStopTime = 0.18f;
                params.mSkidTurnTime = 0.25f;
                params.mTurnHalfLife = 0.12f;
                params.mTurnInPlaceThreshold = 50.0f;
                params.mMaxLeanDegrees = 7.0f;
                params.mEnableAuthoredTransitions = true;
                params.mProceduralLean = true;
                params.mLeanScale = 1.0f;
                break;

            case Preset::High:
                params.mAccelTime = 0.45f;
                params.mStopTime = 0.35f;
                params.mSkidTurnTime = 0.50f;
                params.mTurnHalfLife = 0.22f;
                params.mTurnInPlaceThreshold = 40.0f;
                params.mMaxLeanDegrees = 12.0f;
                params.mEnableAuthoredTransitions = true;
                params.mProceduralLean = true;
                params.mLeanScale = 1.4f;
                break;
        }
        return params;
    }

    Preset parsePreset(std::string_view name)
    {
        if (name.empty())
            return Preset::Off;

        std::string lower;
        lower.reserve(name.size());
        for (char c : name)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (lower == "off" || lower == "0" || lower == "false")
            return Preset::Off;
        if (lower == "low" || lower == "1")
            return Preset::Low;
        if (lower == "medium" || lower == "2" || lower == "true")
            return Preset::Medium;
        if (lower == "high" || lower == "3")
            return Preset::High;
        if (lower == "custom" || lower == "4")
            return Preset::Custom;

        return Preset::Off;
    }

    vsg::vec2 LocomotionState::update(
        const vsg::vec2& requestedVelocity, float dt, const LocomotionParameters& params, float maxSpeed)
    {
        if (dt <= 0.0f || !std::isfinite(dt))
            return mSmoothedVelocity;

        // If settings specify zero accel/stop time, bypass smoothing instantly (Vanilla mode)
        if (params.mAccelTime <= 0.0f && params.mStopTime <= 0.0f)
        {
            mSmoothedVelocity = requestedVelocity;
            mSmoothedSpeed = vsg::length(mSmoothedVelocity);
            mLinearAcceleration = 0.0f;
            mLastSpeed = mSmoothedSpeed;
            mPhase = (mSmoothedSpeed > 1e-3f) ? LocomotionPhase::Moving : LocomotionPhase::Idle;
            return mSmoothedVelocity;
        }

        const float effectiveMaxSpeed = std::max(50.0f, maxSpeed);
        const float accelRate = effectiveMaxSpeed / std::max(0.001f, params.mAccelTime);
        const float decelRate = effectiveMaxSpeed / std::max(0.001f, params.mStopTime);
        const float skidRate = effectiveMaxSpeed / std::max(0.001f, params.mSkidTurnTime);
        const float turnRate = (accelRate + decelRate) * 0.5f;

        const float targetSpeedSq = vsg::length2(requestedVelocity);
        const float currentSpeedSq = vsg::length2(mSmoothedVelocity);

        float maxRate = 0.0f;
        if (targetSpeedSq < 1e-4f)
        {
            // Stopping to zero
            maxRate = decelRate;
            mPhase = (mSmoothedSpeed > 5.0f) ? LocomotionPhase::Stopping : LocomotionPhase::Idle;
        }
        else
        {
            const float dot = vsg::dot(mSmoothedVelocity, requestedVelocity);
            if (dot < -0.1f * targetSpeedSq && currentSpeedSq > 25.0f)
            {
                // Abrupt directional reversal (180 skid)
                maxRate = skidRate;
                mPhase = LocomotionPhase::Skidding;
            }
            else if (currentSpeedSq < targetSpeedSq)
            {
                // Speeding up
                maxRate = accelRate;
                mPhase = (mSmoothedSpeed < 15.0f) ? LocomotionPhase::Starting : LocomotionPhase::Moving;
            }
            else
            {
                // Turning / cornering with momentum
                maxRate = turnRate;
                mPhase = LocomotionPhase::Moving;
            }
        }

        const vsg::vec2 delta = requestedVelocity - mSmoothedVelocity;
        const float deltaLen = vsg::length(delta);
        const float maxStep = maxRate * dt;

        if (deltaLen <= maxStep || deltaLen < 1e-6f)
        {
            mSmoothedVelocity = requestedVelocity;
        }
        else
        {
            mSmoothedVelocity += delta * (maxStep / deltaLen);
        }

        mSmoothedSpeed = vsg::length(mSmoothedVelocity);
        if (mSmoothedSpeed > effectiveMaxSpeed && effectiveMaxSpeed > 0.0f)
        {
            mSmoothedVelocity *= (effectiveMaxSpeed / mSmoothedSpeed);
            mSmoothedSpeed = effectiveMaxSpeed;
        }
        else if (mSmoothedSpeed < 1e-4f)
        {
            mSmoothedVelocity.set(0.f, 0.f);
            mSmoothedSpeed = 0.f;
            mPhase = LocomotionPhase::Idle;
        }

        mLinearAcceleration = (mSmoothedSpeed - mLastSpeed) / dt;
        mLastSpeed = mSmoothedSpeed;

        return mSmoothedVelocity;
    }

    float LocomotionState::updateYaw(float currentYaw, float dt, const LocomotionParameters& params,
        bool strafeOrAim)
    {
        if (dt <= 0.0f || !std::isfinite(dt) || params.mTurnHalfLife <= 0.0f || strafeOrAim)
        {
            mSmoothedYaw = currentYaw;
            mAngularVelocity = 0.0f;
            return mSmoothedYaw;
        }

        // When moving at sufficient speed, body heading turns along velocity vector
        if (mSmoothedSpeed < 5.0f)
        {
            mSmoothedYaw = currentYaw;
            mAngularVelocity = 0.0f;
            return mSmoothedYaw;
        }

        // Target angle from velocity vector (atan2(x, y))
        const float targetYaw = std::atan2(mSmoothedVelocity.x, mSmoothedVelocity.y);

        // Shortest angular distance [-pi, pi]
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kTwoPi = 2.0f * kPi;

        float diff = std::fmod(targetYaw - currentYaw + kPi, kTwoPi);
        if (diff < 0.0f)
            diff += kTwoPi;
        const float shortestDelta = diff - kPi;

        // Exponential decay tracking
        const float decay = std::exp(-std::numbers::ln2_v<float> * dt / std::max(0.001f, params.mTurnHalfLife));
        const float turnStep = shortestDelta * (1.0f - decay);

        mSmoothedYaw = currentYaw + turnStep;
        while (mSmoothedYaw > kPi)
            mSmoothedYaw -= kTwoPi;
        while (mSmoothedYaw < -kPi)
            mSmoothedYaw += kTwoPi;

        mAngularVelocity = turnStep / dt;
        return mSmoothedYaw;
    }

    void LocomotionState::reset(const vsg::vec2& seedVelocity, float currentYaw)
    {
        mSmoothedVelocity = seedVelocity;
        mSmoothedSpeed = vsg::length(seedVelocity);
        mSmoothedYaw = currentYaw;
        mLastSpeed = mSmoothedSpeed;
        mLinearAcceleration = 0.0f;
        mAngularVelocity = 0.0f;
        mPhase = (mSmoothedSpeed > 1e-4f) ? LocomotionPhase::Moving : LocomotionPhase::Idle;
    }
}
