#include "lightflicker.hpp"

#include <algorithm>
#include <cmath>

namespace MWAnim::NeutralFlicker
{
    namespace
    {
        constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;
        // The longest step the walk takes in one call; a longer one is cut, so a stall never costs a long loop.
        constexpr float kMaxStep = 1.f;
        // The shortest leg, so the loop in Walk::step is bounded (a pulse leg is half the period).
        constexpr float kMinLeg = 0.01f;

        std::uint64_t splitmix(std::uint64_t& state)
        {
            state += kGolden;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        float unit(std::uint64_t& state)
        {
            return static_cast<float>(splitmix(state) >> 40) / 16777216.f; // 24 bits: [0, 1)
        }
    }

    std::int32_t packTimeField(float periodSeconds, float amplitude)
    {
        if (!std::isfinite(periodSeconds) || !std::isfinite(amplitude) || periodSeconds <= 0.f || amplitude <= 0.f)
            return 0;
        const float period = std::clamp(periodSeconds, kMinPeriod, kMaxPeriod);
        const float amp = std::min(amplitude, kMaxAmplitude);
        const long ms = std::lround(period * 1000.f);
        const long permille = std::lround(amp * 1000.f);
        if (permille < 1)
            return 0;
        return static_cast<std::int32_t>(ms | (permille << 16));
    }

    std::optional<Params> unpack(std::int32_t timeField, std::int32_t seedField)
    {
        if (timeField <= 0)
            return std::nullopt;
        const std::int32_t ms = timeField & 0xFFFF;
        const std::int32_t permille = (timeField >> 16) & 0x7FFF;
        if (ms < 20 || ms > 10000 || permille < 1 || permille > 950)
            return std::nullopt;
        return Params{ static_cast<float>(ms) / 1000.f, static_cast<float>(permille) / 1000.f,
            static_cast<std::uint32_t>(seedField) };
    }

    Walk::Walk(const Params& params, bool pulse)
        : mSeed(params.mSeed)
        , mState(static_cast<std::uint64_t>(params.mSeed) * kGolden + 0x1234567ull)
        , mPeriod(std::isfinite(params.mPeriod) ? std::clamp(params.mPeriod, kMinPeriod, kMaxPeriod) : kMinPeriod)
        , mLow(1.f
              - (std::isfinite(params.mAmplitude) ? std::clamp(params.mAmplitude, 0.f, kMaxAmplitude) : 0.f))
        , mPulse(pulse)
    {
        mBrightness = mLow + unit(mState) * (1.f - mLow);
        mFrom = mBrightness;
        if (mPulse)
        {
            mLegLength = std::max(mPeriod * 0.5f, kMinLeg);
            mGoingUp = mBrightness < 0.5f * (mLow + 1.f);
            mTarget = mGoingUp ? 1.f : mLow;
        }
        else
        {
            mLegLength = std::max(mPeriod, kMinLeg);
            mTarget = mLow + unit(mState) * (1.f - mLow);
        }
    }

    float Walk::step(float dt)
    {
        if (!std::isfinite(dt) || dt <= 0.f)
            return mBrightness;
        float remaining = std::min(dt, kMaxStep);
        while (remaining > 0.f)
        {
            const float t = std::min(remaining, mLegLength - mLegTime);
            mLegTime += t;
            remaining -= t;
            if (mLegTime >= mLegLength - 1e-6f)
            {
                mBrightness = mTarget;
                mFrom = mTarget;
                mLegTime = 0.f;
                if (mPulse)
                {
                    mGoingUp = !mGoingUp;
                    mTarget = mGoingUp ? 1.f : mLow;
                }
                else
                    mTarget = mLow + unit(mState) * (1.f - mLow);
            }
            else
            {
                float f = mLegTime / mLegLength;
                if (mPulse)
                    f = f * f * (3.f - 2.f * f); // eased, so a pulse turns smoothly at its ends
                mBrightness = mFrom + (mTarget - mFrom) * f;
            }
        }
        return mBrightness;
    }
}
