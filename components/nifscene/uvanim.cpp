#include "uvanim.hpp"

#include <algorithm>
#include <cmath>

namespace NifScene::UvAnim
{
    namespace
    {
        double positiveMod(double value, double period)
        {
            const double r = std::fmod(value, period);
            return r < 0.0 ? r + period : r;
        }

        // The controller's time mapped into its [start, stop] window by the extrapolation mode.
        double windowTime(const FloatTrack& track, double controllerTime)
        {
            const double start = track.mStart;
            const double duration = static_cast<double>(track.mStop) - start;
            if (!(duration > 0.0))
                return start;
            switch (track.mCycle)
            {
                case 0:
                    return start + positiveMod(controllerTime - start, duration);
                case 1:
                {
                    const double p = positiveMod(controllerTime - start, 2.0 * duration);
                    return start + (p > duration ? 2.0 * duration - p : p);
                }
                default:
                    return std::clamp(controllerTime, start, start + duration);
            }
        }
    }

    float sample(const FloatTrack& track, double clock)
    {
        if (track.mKeys.empty() || !std::isfinite(clock))
            return 0.f;
        const double controllerTime = clock * static_cast<double>(track.mFrequency) + static_cast<double>(track.mPhase);
        if (!std::isfinite(controllerTime))
            return 0.f;
        const double t = windowTime(track, controllerTime);
        const std::vector<FloatKey>& keys = track.mKeys;
        if (keys.size() == 1 || t <= keys.front().mTime)
            return keys.front().mValue;
        if (t >= keys.back().mTime)
            return keys.back().mValue;
        // the first key after t (keys are sorted by time)
        const auto next = std::upper_bound(
            keys.begin(), keys.end(), t, [](double time, const FloatKey& key) { return time < key.mTime; });
        const FloatKey& b = *next;
        const FloatKey& a = *(next - 1);
        const double h = static_cast<double>(b.mTime) - a.mTime;
        if (!(h > 0.0) || track.mInterpolation == 5)
            return a.mValue;
        const double u = (t - a.mTime) / h;
        if (track.mInterpolation == 1)
            return static_cast<float>(a.mValue + (static_cast<double>(b.mValue) - a.mValue) * u);
        // Hermite: the file's tangents are slopes per second
        const double u2 = u * u;
        const double u3 = u2 * u;
        const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
        const double h10 = u3 - 2.0 * u2 + u;
        const double h01 = -2.0 * u3 + 3.0 * u2;
        const double h11 = u3 - u2;
        return static_cast<float>(
            h00 * a.mValue + h10 * h * a.mOutTan + h01 * b.mValue + h11 * h * b.mInTan);
    }

    std::array<float, 16> matrixAt(const UvAnimation& animation, double clock)
    {
        const auto pick = [&](const FloatTrack& track, float base) { return track.empty() ? base : sample(track, clock); };
        std::array<float, 16> m{};
        m[0] = pick(animation.mScale[0], animation.mBaseScale[0]);
        m[5] = pick(animation.mScale[1], animation.mBaseScale[1]);
        m[10] = 1.f;
        m[15] = 1.f;
        m[12] = pick(animation.mOffset[0], animation.mBaseOffset[0]);
        m[13] = pick(animation.mOffset[1], animation.mBaseOffset[1]);
        return m;
    }
}
