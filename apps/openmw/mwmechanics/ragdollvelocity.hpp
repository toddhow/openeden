#ifndef OPENEDEN_MWMECHANICS_RAGDOLLVELOCITY_H
#define OPENEDEN_MWMECHANICS_RAGDOLLVELOCITY_H

#include <cmath>

namespace MWMechanics
{
    /// Task 0106, second slice (1). The velocity a dead actor's ragdoll starts with: how the actor moved over the last
    /// frames. The physics inertia (Actor::getInertialForce) is zero for an actor walking on the ground, so a guard
    /// killed at a run would stop dead. Plain doubles, no allocation, main thread.
    ///
    /// update() is called once per frame while the actor lives with its position and the frame's duration. A jump of
    /// more than cTeleportUnitsPerFrame in one frame (a teleport, a cell change) is not a velocity and clears it.
    constexpr double cTeleportUnitsPerFrame = 700.0;
    constexpr double cVelocitySmoothing = 0.5; // the share of the newest frame's velocity in the running value
    /// A ragdoll never starts faster than this (metres per second): a rope-length of a fall must not become a launch.
    constexpr double cMaxStartSpeedMetresPerSecond = 12.0;

    class ActorVelocityTracker
    {
    public:
        void update(double x, double y, double z, double dt)
        {
            if (!(dt > 0.0) || !std::isfinite(dt))
                return;
            if (mHasLast)
            {
                const double dx = x - mLast[0];
                const double dy = y - mLast[1];
                const double dz = z - mLast[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > cTeleportUnitsPerFrame)
                {
                    mVelocity[0] = mVelocity[1] = mVelocity[2] = 0.0;
                }
                else
                {
                    mVelocity[0] += (dx / dt - mVelocity[0]) * cVelocitySmoothing;
                    mVelocity[1] += (dy / dt - mVelocity[1]) * cVelocitySmoothing;
                    mVelocity[2] += (dz / dt - mVelocity[2]) * cVelocitySmoothing;
                }
            }
            mLast[0] = x;
            mLast[1] = y;
            mLast[2] = z;
            mHasLast = true;
        }

        void reset()
        {
            mHasLast = false;
            mVelocity[0] = mVelocity[1] = mVelocity[2] = 0.0;
        }

        /// Units per second.
        void velocity(double out[3]) const
        {
            for (int i = 0; i < 3; ++i)
                out[i] = mVelocity[i];
        }

    private:
        double mLast[3] = { 0.0, 0.0, 0.0 };
        double mVelocity[3] = { 0.0, 0.0, 0.0 };
        bool mHasLast = false;
    };

    /// The velocity cut to at most maxSpeed (units per second), keeping its direction; a value that is not finite
    /// becomes zero.
    inline void clampSpeed(double v[3], double maxSpeed)
    {
        const double speedSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!std::isfinite(speedSq))
        {
            v[0] = v[1] = v[2] = 0.0;
            return;
        }
        if (speedSq <= maxSpeed * maxSpeed)
            return;
        const double scale = maxSpeed / std::sqrt(speedSq);
        for (int i = 0; i < 3; ++i)
            v[i] *= scale;
    }
}

#endif
