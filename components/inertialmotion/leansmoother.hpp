#pragma once

#ifndef OPENMW_COMPONENTS_INERTIALMOTION_LEANSMOOTHER_H
#define OPENMW_COMPONENTS_INERTIALMOTION_LEANSMOOTHER_H

#include <algorithm>
#include <cmath>

namespace InertialMotion
{
    /// Tuning for LeanSmoother. The defaults are chosen for a body lean angle in radians.
    struct LeanSmootherParameters
    {
        /// Seconds. The lean closes about 60 percent of the gap in this time and settles in about ten times it.
        /// Must be above zero.
        float mSmoothTime = 0.15f;
        /// Largest change of the value per second (radians per second for a lean). A hard cap, so even a huge step
        /// in the target cannot make the value jump.
        float mMaxRate = 1.5f;
    };

    /**
     * @brief A critically damped spring with a speed limit, for ramping one number (a lean angle) toward a target.
     *
     * Why it exists: the procedural lean used to follow the player's acceleration directly. Inertial locomotion
     * accelerates at a constant rate, so the acceleration steps from zero to its full value on the first frame of a
     * walk and the spine popped by several degrees in one frame. This class turns any target, however abruptly it
     * changes, into a value that starts moving slowly, speeds up, and eases in without overshooting.
     *
     * The formula is the standard critically damped spring in closed form (Game Programming Gems 4, "Critically
     * Damped Ease-In/Ease-Out Smoothing"), which stays stable for any frame time.
     *
     * Ownership: a plain value type, one per smoothed number, no allocation. Not thread safe; the caller updates it
     * from one thread.
     */
    class LeanSmoother
    {
    public:
        /// Advances by dt seconds toward target and returns the new value. A non-positive or non-finite dt or a
        /// non-finite target changes nothing.
        float update(float target, float dt, const LeanSmootherParameters& params) noexcept
        {
            if (!(dt > 0.f) || !std::isfinite(dt) || !std::isfinite(target))
                return mValue;

            const float smoothTime = std::max(1e-4f, params.mSmoothTime);
            const float maxRate = std::max(0.f, params.mMaxRate);
            const float omega = 2.f / smoothTime;
            const float x = omega * dt;
            // Polynomial approximation of exp(-x), accurate for the whole range and never above 1.
            const float decay = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);

            // Limit how far the spring may think the target is, which limits its top speed.
            const float maxGap = maxRate * smoothTime;
            const float gap = std::clamp(mValue - target, -maxGap, maxGap);
            const float reachableTarget = mValue - gap;

            const float carry = (mVelocity + omega * gap) * dt;
            float velocity = (mVelocity - omega * carry) * decay;
            float value = reachableTarget + (gap + carry) * decay;

            // Never pass the real target.
            if ((target - mValue > 0.f) == (value > target))
            {
                value = target;
                velocity = 0.f;
            }

            // Hard cap on the change in one step.
            const float maxStep = maxRate * dt;
            const float step = value - mValue;
            if (std::abs(step) > maxStep)
            {
                value = mValue + std::copysign(maxStep, step);
                velocity = std::copysign(maxRate, step);
            }

            mValue = value;
            mVelocity = velocity;
            return mValue;
        }

        [[nodiscard]] float getValue() const noexcept { return mValue; }
        [[nodiscard]] float getVelocity() const noexcept { return mVelocity; }

        void reset() noexcept
        {
            mValue = 0.f;
            mVelocity = 0.f;
        }

    private:
        float mValue = 0.f;
        float mVelocity = 0.f;
    };
}

#endif
