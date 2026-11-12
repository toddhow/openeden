#include "locomotion.hpp"

#include <algorithm>
#include <cmath>

namespace HavokPack::Gait
{
    namespace
    {
        float clamp01(float v)
        {
            return std::clamp(v, 0.f, 1.f);
        }
    }

    Weights weights(float speed, const Params& p)
    {
        Weights w;
        if (!std::isfinite(speed) || speed <= p.mStopSpeed)
            return w; // only the idle
        // Fade the idle out, then mix the gaits by the speed between the walk's and the run's.
        const float move = clamp01((speed - p.mStopSpeed) / std::max(p.mFullSpeed - p.mStopSpeed, 1e-6f));
        const float run = clamp01((speed - p.mWalkSpeed) / std::max(p.mRunSpeed - p.mWalkSpeed, 1e-6f));
        w.mIdle = 1.f - move;
        w.mWalk = move * (1.f - run);
        w.mRun = move * run;
        const float stride = p.mWalkStride + (p.mRunStride - p.mWalkStride) * run;
        w.mCyclesPerSecond = stride > 0.f ? speed / stride : 0.f;
        return w;
    }

    float smoothSpeed(float current, float target, float dt, float tau)
    {
        if (!(dt > 0.f) || !std::isfinite(dt))
            return current;
        if (!(tau > 0.f))
            return target;
        return current + (target - current) * (1.f - std::exp(-dt / tau));
    }

    float advancePhase(float phase, float cyclesPerSecond, float dt)
    {
        if (!(cyclesPerSecond >= 0.f) || !(dt >= 0.f) || !std::isfinite(cyclesPerSecond) || !std::isfinite(dt))
            return phase;
        const float next = phase + cyclesPerSecond * dt;
        return next - std::floor(next);
    }

    float clipTime(float phase, float leftFoot, float duration)
    {
        if (!(duration > 0.f))
            return 0.f;
        const float v = phase + leftFoot;
        return (v - std::floor(v)) * duration;
    }

    Transform blend(const Transform& a, const Transform& b, float t)
    {
        t = clamp01(t);
        Transform r;
        for (int i = 0; i < 3; ++i)
        {
            r.mTranslation[i] = a.mTranslation[i] + (b.mTranslation[i] - a.mTranslation[i]) * t;
            r.mScale[i] = a.mScale[i] + (b.mScale[i] - a.mScale[i]) * t;
        }
        float dot = 0.f;
        for (int i = 0; i < 4; ++i)
            dot += a.mRotation[i] * b.mRotation[i];
        const float sign = dot < 0.f ? -1.f : 1.f;
        float len2 = 0.f;
        for (int i = 0; i < 4; ++i)
        {
            r.mRotation[i] = a.mRotation[i] * (1.f - t) + b.mRotation[i] * sign * t;
            len2 += r.mRotation[i] * r.mRotation[i];
        }
        if (len2 > 1e-12f)
        {
            const float inv = 1.f / std::sqrt(len2);
            for (float& v : r.mRotation)
                v *= inv;
        }
        else
            r.mRotation = a.mRotation;
        return r;
    }
}
