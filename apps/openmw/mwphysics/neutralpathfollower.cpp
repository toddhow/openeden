#include "neutralpathfollower.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics::NeutralPath
{
    namespace
    {
        float groundDistance(const vsg::vec3& a, const vsg::vec3& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        bool finite(const vsg::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    bool Follower::setGoal(const vsg::vec3& target, float speed)
    {
        if (!finite(target) || !std::isfinite(speed) || speed <= 0.f)
            return false;
        mTarget = target;
        mSpeed = speed;
        mState = State::NeedPath;
        mPath.clear();
        mPath.reserve(kReservedWaypoints); // no-op once reserved
        mIndex = 0;
        mRetryWait = 0.f;
        mNotReadyAttempts = 0;
        mRepaths = 0;
        mStallTime = 0.f;
        return true;
    }

    void Follower::cancel()
    {
        mState = State::Idle;
        mPath.clear();
        mIndex = 0;
    }

    StepResult Follower::finish(Outcome outcome)
    {
        mState = State::Idle;
        mPath.clear();
        mIndex = 0;
        StepResult result;
        result.mSetVelocity = true; // the actor stops where it ended
        result.mOutcome = outcome;
        return result;
    }

    StepResult Follower::step(const vsg::vec3& feet, float dt, const PathFinder& finder)
    {
        if (mState == State::Idle || !std::isfinite(dt) || dt <= 0.f || !finite(feet))
            return {};

        if (mState == State::NeedPath)
        {
            mRetryWait -= dt;
            if (mRetryWait > 0.f)
            {
                StepResult wait;
                wait.mSetVelocity = true; // stand still while there is no path
                return wait;
            }
            mPath.clear();
            const FindStatus status = finder.find(feet, mTarget, mPath);
            switch (status)
            {
                case FindStatus::NotReady:
                    if (++mNotReadyAttempts >= kMaxNotReadyAttempts)
                        return finish(Outcome::NoPath);
                    mRetryWait = kNotReadyInterval;
                    {
                        StepResult wait;
                        wait.mSetVelocity = true;
                        return wait;
                    }
                case FindStatus::NoPath:
                    return finish(Outcome::NoPath);
                case FindStatus::Partial:
                    // A path that stops short of the goal is no way to it, and neither is one that ends under or over it
                    // on another floor (task 0158: Lucia's upstairs bed, the path ended on the cellar floor below it).
                    if (mPath.empty() || groundDistance(mPath.back(), mTarget) > kArrivalRadius
                        || std::fabs(mPath.back().z - mTarget.z) > kArrivalHeight)
                        return finish(Outcome::NoPath);
                    break;
                case FindStatus::Success:
                    if (mPath.empty())
                    {
                        return groundDistance(feet, mTarget) <= kArrivalRadius ? finish(Outcome::Arrived)
                                                                              : finish(Outcome::NoPath);
                    }
                    break;
            }
            mNotReadyAttempts = 0;
            mIndex = 0;
            mState = State::Following;
            mStallFrom = feet;
            mStallTime = 0.f;
        }
        return follow(feet, dt);
    }

    StepResult Follower::follow(const vsg::vec3& feet, float dt)
    {
        // Close enough to the goal itself (and on its floor): done, whatever waypoints are left.
        if (groundDistance(feet, mTarget) <= kArrivalRadius && std::fabs(feet.z - mTarget.z) <= kArrivalHeight)
            return finish(Outcome::Arrived);
        while (mIndex < mPath.size() && groundDistance(feet, mPath[mIndex]) <= kWaypointRadius)
            ++mIndex;
        if (mIndex >= mPath.size())
            return finish(Outcome::Arrived);

        mStallTime += dt;
        if (mStallTime >= kStallWindow)
        {
            const float moved = groundDistance(feet, mStallFrom);
            if (moved < kStallProgress * mSpeed * mStallTime)
            {
                if (++mRepaths > kMaxRepaths)
                    return finish(Outcome::Blocked);
                mState = State::NeedPath;
                mRetryWait = 0.f;
                StepResult stop;
                stop.mSetVelocity = true;
                return stop;
            }
            mStallFrom = feet;
            mStallTime = 0.f;
        }

        const vsg::vec3& waypoint = mPath[mIndex];
        const float dx = waypoint.x - feet.x;
        const float dy = waypoint.y - feet.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        StepResult result;
        result.mSetVelocity = true;
        if (distance > 1e-4f)
        {
            const float speed = std::min(mSpeed, distance / dt); // never past the waypoint in one frame
            result.mVelX = dx / distance * speed;
            result.mVelY = dy / distance * speed;
        }
        return result;
    }

    vsg::vec3 agentHalfExtents(float capsuleRadius, const vsg::vec3& sessionAgent)
    {
        if (!std::isfinite(capsuleRadius) || capsuleRadius <= 0.f)
            return sessionAgent;
        return vsg::vec3(
            std::max(capsuleRadius, sessionAgent.x), std::max(capsuleRadius, sessionAgent.y), sessionAgent.z);
    }
}
