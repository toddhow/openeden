#include "facekeys.hpp"

#include <algorithm>
#include <cmath>

namespace face_editor
{
    using openeden::editor::CurveKey;

    namespace
    {
        /// The ONE evaluation rule, used by sparsify's exactness check and by resample, so they can not disagree.
        double between(const CurveKey& a, const CurveKey& b, double t)
        {
            if (b.timeSeconds <= a.timeSeconds)
                return b.value;
            const double f = (t - a.timeSeconds) / (b.timeSeconds - a.timeSeconds);
            return a.value + (b.value - a.value) * f;
        }

        int quantise(double value, int lo, int hi)
        {
            const double q = std::floor(value * kQuantScale + 0.5);
            return static_cast<int>(std::clamp(q, static_cast<double>(lo), static_cast<double>(hi)));
        }

        double timeOfFrame(std::size_t frame, std::uint32_t frameRate)
        {
            return static_cast<double>(frame) / static_cast<double>(frameRate);
        }
    }

    std::vector<CurveKey> sparsify(const std::vector<std::int16_t>& frames, std::uint32_t frameRate)
    {
        std::vector<CurveKey> keys;
        if (frames.empty() || frameRate == 0)
            return keys;
        auto keyOf = [&](std::size_t i) { return CurveKey{ timeOfFrame(i, frameRate), frames[i] / kQuantScale }; };
        // Quantised range of the data, for the exactness check (a signed channel reaches below 0).
        const int lo = -static_cast<int>(kQuantScale);
        const int hi = static_cast<int>(kQuantScale);

        std::size_t anchor = 0;
        keys.push_back(keyOf(0));
        while (anchor + 1 < frames.size())
        {
            std::size_t best = anchor + 1;
            const std::size_t limit = std::min(frames.size() - 1, anchor + kMaxSegmentFrames);
            for (std::size_t k = anchor + 2; k <= limit; ++k)
            {
                const CurveKey a = keyOf(anchor);
                const CurveKey b = keyOf(k);
                bool exact = true;
                for (std::size_t j = anchor + 1; j < k && exact; ++j)
                    exact = quantise(between(a, b, timeOfFrame(j, frameRate)), lo, hi) == frames[j];
                if (!exact)
                    break;
                best = k;
            }
            keys.push_back(keyOf(best));
            anchor = best;
        }
        return keys;
    }

    std::vector<std::int16_t> resample(
        const std::vector<CurveKey>& input, std::uint32_t frameRate, std::uint32_t frameCount, int lo, int hi)
    {
        std::vector<std::int16_t> out(frameCount, 0);
        std::vector<CurveKey> keys;
        keys.reserve(input.size());
        for (const CurveKey& k : input)
            if (std::isfinite(k.timeSeconds) && std::isfinite(k.value))
                keys.push_back(k);
        if (keys.empty() || frameRate == 0)
            return out;
        for (std::uint32_t j = 0; j < frameCount; ++j)
        {
            const double t = timeOfFrame(j, frameRate);
            auto next = std::upper_bound(
                keys.begin(), keys.end(), t, [](double time, const CurveKey& key) { return time < key.timeSeconds; });
            double value;
            if (next == keys.begin())
                value = keys.front().value;
            else if (next == keys.end())
                value = keys.back().value;
            else
                value = between(*(next - 1), *next, t);
            out[j] = static_cast<std::int16_t>(quantise(value, lo, hi));
        }
        return out;
    }

    std::size_t insertKey(std::vector<CurveKey>& keys, const CurveKey& key)
    {
        auto at = std::upper_bound(keys.begin(), keys.end(), key.timeSeconds,
            [](double time, const CurveKey& k) { return time < k.timeSeconds; });
        const std::size_t index = static_cast<std::size_t>(at - keys.begin());
        keys.insert(at, key);
        return index;
    }
}
