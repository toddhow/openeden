#include "mouth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TriMorph
{
    float quantise(float weight)
    {
        if (!std::isfinite(weight))
            return 0.f;
        const float clamped = std::clamp(weight, 0.f, 1.f);
        return std::round(clamped * static_cast<float>(kMouthSteps)) / static_cast<float>(kMouthSteps);
    }

    MouthWeights MouthDrive::step(float loudness)
    {
        float target = 0.f;
        if (std::isfinite(loudness) && loudness > 0.f)
            target = std::min(1.f, loudness * kLoudnessGain);
        else if (loudness == std::numeric_limits<float>::infinity())
            target = 1.f;
        const float share = target > mOpen ? kAttack : kRelease;
        mOpen += (target - mOpen) * share;
        MouthWeights w;
        w.mAah = quantise(std::min(mOpen, kAahMax));
        const float wide = (mOpen - kBigAahFrom) / (1.f - kBigAahFrom);
        w.mBigAah = quantise(std::max(0.f, wide));
        return w;
    }
}
