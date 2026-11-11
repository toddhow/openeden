#pragma once

#ifndef OPENMW_COMPONENTS_INERTIALMOTION_FOOTLOCK_H
#define OPENMW_COMPONENTS_INERTIALMOTION_FOOTLOCK_H

#include <vsg/maths/vec2.h>

#include <algorithm>
#include <cmath>

namespace InertialMotion
{
    /// Tuning for FootLock. Distances are in world units (Morrowind units, about 1.4 cm), speeds in units per second.
    /// The values were chosen against the stop measurements of docs/research/0052 (task 0107): with the
    /// CreativeMotion transition into idle the stance foot slid back by up to 15 units at up to 76 units per second.
    struct FootLockParameters
    {
        /// Body ground speed below which a foot that is on the ground is held.
        float mStillSpeed = 8.f;
        /// How fast the held position may drift toward where the animation wants the foot while the body is still.
        /// 8 units per second is about 11 cm per second: the settle into the idle stance takes one to two seconds
        /// instead of a quarter of a second, which reads as a weight shift, not as skating.
        float mSlideSpeed = 8.f;
        /// Extra drift allowed per unit of body speed, so a hold lets go of a body that starts to move.
        float mSlideSpeedPerBodySpeed = 2.f;
        /// The held position is never farther than this from where the animation wants the foot (the two-bone
        /// solver cannot reach farther, and a turn in place would twist the leg).
        float mMaxDistance = 16.f;
        /// Per second: how fast the hold takes over (weight toward one) and lets go (weight toward zero).
        float mEngageRate = 30.f;
        float mReleaseRate = 10.f;
    };

    /// The position to ask the leg solver for, and how much of the way from the animated position to take.
    struct FootLockResult
    {
        vsg::vec2 mTarget; ///< the held position (the animated one while nothing is held)
        float mWeight = 0.f; ///< 0 to 1: ankle = animated + (target - animated) * weight
    };

    /**
     * @brief Keeps one foot where it is while the body is standing still and the animation moves it.
     *
     * Why it exists: when an actor stops, the walk or run pose is blended into the idle pose (CreativeMotion's
     * inertialized transition, or a cut in vanilla). The body no longer moves, so the foot that stands on the
     * ground glides across it to the idle position: measured at up to 76 units per second, the "sliding" of
     * inertial locomotion. Holding the foot where it was drawn and letting the hold drift slowly toward the
     * animated position turns that glide into a slow settle.
     *
     * It is a pure state machine on a horizontal (x, y) position: no bones, no world, no allocation. The leg solver
     * in MWRender::ActorIkController turns the result into an ankle target.
     *
     * Ownership: a plain value type, one per foot. Not thread safe.
     */
    class FootLock
    {
    public:
        /// @param animated where this frame's animation puts the ankle
        /// @param lastShown where the ankle was drawn last frame (the hold starts there, so nothing pops)
        /// @param planted the animated foot is on the ground
        /// @param bodySpeed the actor's horizontal ground speed
        /// An invalid dt or non-finite position changes nothing and returns the last result.
        FootLockResult update(const vsg::vec2& animated, const vsg::vec2& lastShown, bool planted, float bodySpeed,
            float dt, const FootLockParameters& params) noexcept
        {
            if (!(dt > 0.f) || !std::isfinite(dt) || !finite(animated))
                return current(animated);

            const bool want = planted && bodySpeed <= params.mStillSpeed;
            if (want && !mEngaged)
            {
                mEngaged = true;
                // A position drawn last frame that is far from the animated one (a teleport, a changed pose, a foot
                // that was not solved) is not a place to hold: start at the animated position.
                mTarget = finite(lastShown) && distanceBetween(lastShown, animated) <= 2.f * params.mMaxDistance
                    ? lastShown
                    : animated;
                mWeight = 0.f;
            }
            if (!mEngaged)
                return { animated, 0.f };

            const float speed = want ? params.mSlideSpeed
                                     : params.mSlideSpeed + params.mSlideSpeedPerBodySpeed * std::max(0.f, bodySpeed);
            moveToward(animated, speed * dt);
            clampDistance(animated, params.mMaxDistance);

            if (want)
                mWeight += (1.f - mWeight) * std::clamp(1.f - std::exp(-params.mEngageRate * dt), 0.f, 1.f);
            else
                mWeight = std::max(0.f, mWeight - params.mReleaseRate * dt);
            mWeight = std::clamp(mWeight, 0.f, 1.f);

            if (!want && mWeight <= 0.f)
            {
                mEngaged = false;
                return { animated, 0.f };
            }
            return { mTarget, mWeight };
        }

        void reset() noexcept
        {
            mEngaged = false;
            mWeight = 0.f;
            mTarget = vsg::vec2(0.f, 0.f);
        }

        [[nodiscard]] bool isEngaged() const noexcept { return mEngaged; }

    private:
        static bool finite(const vsg::vec2& v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y); }
        static float distanceBetween(const vsg::vec2& a, const vsg::vec2& b) noexcept
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        FootLockResult current(const vsg::vec2& animated) const noexcept
        {
            return mEngaged ? FootLockResult{ mTarget, mWeight } : FootLockResult{ animated, 0.f };
        }

        void moveToward(const vsg::vec2& goal, float step) noexcept
        {
            const float dx = goal.x - mTarget.x;
            const float dy = goal.y - mTarget.y;
            const float length = std::hypot(dx, dy);
            if (length <= step || length < 1e-6f)
            {
                mTarget = goal;
                return;
            }
            mTarget.x += dx * (step / length);
            mTarget.y += dy * (step / length);
        }

        void clampDistance(const vsg::vec2& animated, float maxDistance) noexcept
        {
            const float dx = mTarget.x - animated.x;
            const float dy = mTarget.y - animated.y;
            const float length = std::hypot(dx, dy);
            if (length > maxDistance && length > 1e-6f)
            {
                mTarget.x = animated.x + dx * (maxDistance / length);
                mTarget.y = animated.y + dy * (maxDistance / length);
            }
        }

        bool mEngaged = false;
        float mWeight = 0.f;
        vsg::vec2 mTarget{ 0.f, 0.f };
    };
}

#endif
