#include "neutralprocess.hpp"

#include <cmath>

namespace MWPhysics::NeutralProcess
{
    Level classify(float distanceSquared, const Config& config)
    {
        if (!std::isfinite(distanceSquared) || distanceSquared < 0.f)
            return Level::Low;
        if (distanceSquared <= config.mHighRadius * config.mHighRadius)
            return Level::High;
        if (distanceSquared <= config.mMiddleRadius * config.mMiddleRadius)
            return Level::Middle;
        return Level::Low;
    }

    Level reclassify(Level previous, float distanceSquared, const Config& config)
    {
        const Level plain = classify(distanceSquared, config);
        if (static_cast<int>(plain) <= static_cast<int>(previous))
            return plain; // towards a nearer level: at the radius itself
        // Towards a farther level: only once clearly beyond the rings, which are widened by the hysteresis.
        const float widen = 1.f + (config.mHysteresis > 0.f ? config.mHysteresis : 0.f);
        Config wide = config;
        wide.mHighRadius *= widen;
        wide.mMiddleRadius *= widen;
        const Level wideLevel = classify(distanceSquared, wide);
        return static_cast<int>(wideLevel) > static_cast<int>(previous) ? wideLevel : previous;
    }

    float periodOf(Level level, const Config& config)
    {
        switch (level)
        {
            case Level::Middle:
                return config.mMiddlePeriod > 0.f ? config.mMiddlePeriod : 0.f;
            case Level::Low:
                return config.mLowPeriod > 0.f ? config.mLowPeriod : 0.f;
            case Level::High:
                break;
        }
        return 0.f;
    }

    void Schedule::seed(float phase, Level level, const Config& config)
    {
        if (!std::isfinite(phase) || phase < 0.f || phase >= 1.f)
            phase = 0.f;
        mPending = phase * periodOf(level, config);
    }

    bool Schedule::advance(Level level, float dt, const Config& config, float& stepSeconds)
    {
        if (!std::isfinite(dt) || dt < 0.f)
            dt = 0.f;
        mPending += dt;
        const float period = periodOf(level, config);
        if (mPending < period || (period == 0.f && mPending <= 0.f))
        {
            stepSeconds = 0.f;
            return false;
        }
        stepSeconds = mPending;
        mPending = 0.f;
        return true;
    }
}
