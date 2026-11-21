#ifndef OPENEDEN_RENDER_AUTOEXPOSUREMATH_H
#define OPENEDEN_RENDER_AUTOEXPOSUREMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include <components/render/hdrmath.hpp>

namespace Render::AutoExposure
{
    /*
     * The pure parts of automatic exposure (task 0364a, stage A1 of the Community Shaders parity plan; ideas only, no
     * code of any other project). A compute pass (files/shaders/post/autoexposure.slang) puts a sample of the HDR scene
     * into a histogram of log2 luminance and reduces it to a trimmed mean; the CPU adapts the exposure to it over time
     * (by the frame's dt, never a wall clock) and gives it to the tonemap as an exposure change in stops. This file is
     * the C++ twin of the shader's binning and trimmed mean (the constants are read back from the shader by a test) and
     * holds everything that needs no Vulkan.
     */

    // The histogram: bin 0 holds samples too dark to count (black, negative or NaN: darkness lights, the sky box's
    // edge, unlit corners); bins 1 to 63 split log2 luminance from minLog2 to maxLog2 into equal steps, and a sample
    // above the range joins the last bin.
    constexpr int bins = 64;
    constexpr int firstBin = 1;
    constexpr int measuredBins = bins - firstBin;
    constexpr float minLog2 = -8.f;
    constexpr float maxLog2 = 8.f;
    constexpr float binWidth = (maxLog2 - minLog2) / static_cast<float>(measuredBins);
    // The histogram buffer is the 64 bins, then the fixed-point sum of the clamped luminance, then the sample count.
    constexpr int lumaSumIndex = bins;
    constexpr int sampleCountIndex = bins + 1;
    constexpr int histogramEntries = bins + 2;
    // The arithmetic mean of luminance (for the colour grade's contrast pivot) clamps each sample so a sun disc does
    // not own it, and is summed as an integer: value * meanLumaScale. 4096 per sample at most, so 2^19 samples fit 32
    // bits.
    constexpr float meanLumaClamp = 16.f;
    constexpr float meanLumaScale = 256.f;
    constexpr std::uint32_t maxSamples = 1u << 19;

    // The results ring the GPU writes and the CPU reads: one slot per frame (serial modulo slots), each four 32-bit
    // words (the mean log2 luminance, the arithmetic mean luminance, the weight, the serial: the first three as
    // float bits) and four more of which the first is the highlight. A frame's slot is read at least `latency` frames
    // later: the frame fence ring is three long, so a frame that old has finished when the CPU asks.
    constexpr int slots = 8;
    // A slot is two 16-byte entries: the first as above, the second holds the highlight (log2 luminance of the
    // brightest share's lower edge, as float bits) in its first word.
    constexpr int wordsPerSlot = 8;
    constexpr int latency = 4;
    constexpr std::uint32_t serialMask = 0xFFFFFFu; // serial counts 1 .. 2^24, so that 0 means "never written"

    inline float luminance(float r, float g, float b)
    {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    // The bin of a luminance; 0 for a value that is not a usable brightness (NaN, zero, negative, below 2^minLog2).
    inline int binOf(float luma)
    {
        if (!(luma >= std::exp2(minLog2)))
            return 0;
        const float position = (std::log2(luma) - minLog2) / binWidth;
        return firstBin + std::clamp(static_cast<int>(std::floor(position)), 0, measuredBins - 1);
    }

    inline float binCentreLog2(int bin)
    {
        return minLog2 + (static_cast<float>(bin - firstBin) + 0.5f) * binWidth;
    }

    struct Measurement
    {
        float meanLog2 = 0.f; // the trimmed mean of log2 luminance over the counted samples
        float meanLuma = 0.f; // the arithmetic mean of the clamped luminance over every finite sample (pivot)
        float weight = 0.f; // the number of samples that counted; 0: nothing to measure
        float highLog2 = 0.f; // log2 luminance above which the brightest (1 - highlight percentile) of the samples lie
        std::uint32_t serial = 0; // the frame the pass measured (set by the pass; serialOf)
        bool valid() const { return weight >= 1.f && std::isfinite(meanLog2); }
    };

    // The mean of the log2 luminance between two percentiles of the bright-enough samples (bins 1 to 63): the darkest
    // `low` share and the brightest `1 - high` share of the counted samples are left out, so a few light sources or a
    // black frame edge cannot move the exposure. Bin weights at the cuts count in part. `luma` is the arithmetic part
    // (the fixed-point sum and the sample count that the buffer carries after the bins).
    inline Measurement trimmedMean(const std::array<std::uint32_t, bins>& histogram, std::uint32_t lumaSum,
        std::uint32_t sampleCount, float low, float high)
    {
        Measurement result;
        float total = 0.f;
        for (int i = firstBin; i < bins; ++i)
            total += static_cast<float>(histogram[i]);
        if (sampleCount > 0)
            result.meanLuma = static_cast<float>(lumaSum) / meanLumaScale / static_cast<float>(sampleCount);
        if (!(total >= 1.f))
            return result;
        low = std::clamp(low, 0.f, 1.f);
        high = std::clamp(high, low, 1.f);
        const float cutLow = total * low;
        const float cutHigh = total * high;
        float passed = 0.f;
        float sum = 0.f;
        float weight = 0.f;
        for (int i = firstBin; i < bins; ++i)
        {
            const float count = static_cast<float>(histogram[i]);
            const float begin = passed;
            const float end = passed + count;
            passed = end;
            const float kept = std::max(0.f, std::min(end, cutHigh) - std::max(begin, cutLow));
            sum += kept * binCentreLog2(i);
            weight += kept;
        }
        if (!(weight > 0.f))
        {
            // The cuts met inside one bin (a high of 0, or one sample): take the whole histogram's middle.
            result.meanLog2 = binCentreLog2(firstBin);
            return result;
        }
        result.meanLog2 = sum / weight;
        result.weight = weight;
        return result;
    }

    // The log2 luminance at a percentile of the counted samples (bins 1 to 63; interpolated inside the bin): the level
    // below which `percentile` of them lie. Used for the highlights, which a trimmed mean cannot see.
    inline float percentileLog2(const std::array<std::uint32_t, bins>& histogram, float percentile)
    {
        float total = 0.f;
        for (int i = firstBin; i < bins; ++i)
            total += static_cast<float>(histogram[i]);
        if (!(total >= 1.f))
            return minLog2;
        const float cut = total * std::clamp(percentile, 0.f, 1.f);
        float passed = 0.f;
        for (int i = firstBin; i < bins; ++i)
        {
            const float count = static_cast<float>(histogram[i]);
            if (passed + count >= cut && count > 0.f)
                return minLog2 + (static_cast<float>(i - firstBin) + (cut - passed) / count) * binWidth;
            passed += count;
        }
        return maxLog2;
    }

    struct Settings
    {
        // The geometric mean luminance the exposure aims for (linear, before the tonemap): a clear Morrowind noon
        // measures about 0.11.
        float key = 0.11f;
        float minStops = -4.f; // the exposure change auto exposure may make, in stops
        float maxStops = 4.f;
        float speedUp = 1.f; // per second, when the exposure rises (the scene went dark)
        float speedDown = 2.f; // per second, when the exposure falls (the scene went bright)
        float lowPercentile = 0.1f;
        float highPercentile = 0.9f;
        // Highlight protection: brightening stops where the brightest `highlightPercentile` of the scene would pass
        // `highlightWhite` (linear, 1 is the display's white; 0 percentile: off). Without it a bright sky clips when
        // the dark ground asks for more exposure.
        float highlightPercentile = 0.98f;
        float highlightWhite = 1.f;
    };

    // The exposure change, in stops, that brings a scene of this mean log2 luminance to the key.
    inline float targetStops(float meanLog2, const Settings& settings)
    {
        const float lo = std::min(settings.minStops, settings.maxStops);
        const float hi = std::max(settings.minStops, settings.maxStops);
        return std::clamp(std::log2(std::max(settings.key, 1e-6f)) - meanLog2, lo, hi);
    }

    // The same with the highlights kept below the white: the change that puts the highlight level at the white is the
    // most that is allowed (it may still be negative: a scene whose highlights are far over the white is darkened only
    // by the key, never by this, so a dark room with a lamp is not made darker).
    inline float targetStops(float meanLog2, float highLog2, const Settings& settings)
    {
        float wanted = std::log2(std::max(settings.key, 1e-6f)) - meanLog2;
        if (settings.highlightPercentile > 0.f)
        {
            const float headroom = std::log2(std::max(settings.highlightWhite, 1e-6f)) - highLog2;
            wanted = std::min(wanted, std::max(headroom, 0.f));
        }
        const float lo = std::min(settings.minStops, settings.maxStops);
        const float hi = std::max(settings.minStops, settings.maxStops);
        return std::clamp(wanted, lo, hi);
    }

    // The longest step of time one adaptation takes: a frame after a loading screen or a freeze is not a long time.
    constexpr float maxStep = 0.5f;

    // One step of the eye's adaptation: exponential, so that the answer does not depend on the frame rate (two steps of
    // dt equal one of 2 dt). A rate of 0 does not move.
    inline float adapt(float current, float target, float dt, const Settings& settings)
    {
        const float rate = target > current ? settings.speedUp : settings.speedDown;
        const float step = std::clamp(dt, 0.f, maxStep);
        if (!(rate > 0.f) || !(step > 0.f))
            return current;
        return current + (target - current) * (1.f - std::exp(-rate * step));
    }

    // The adaptation's memory: a state of its own, so that the first measurement is taken as it is (a game that starts
    // in a dark cellar does not fade in from the open air's exposure) and a measurement that is not valid changes
    // nothing.
    struct State
    {
        float stops = 0.f;
        bool started = false;

        void reset() { *this = State(); }

        // Returns the stops to apply after this frame.
        float update(const Measurement& measurement, float dt, const Settings& settings)
        {
            if (!measurement.valid())
                return stops;
            const float target = targetStops(measurement.meanLog2, measurement.highLog2, settings);
            stops = started ? adapt(stops, target, dt, settings) : target;
            started = true;
            return stops;
        }
    };

    // Which of the ring's slots to read: the newest that is at least `latency` frames old. `serials` holds each slot's
    // serial (0: never written); `counter` is this frame's serial. The serial counts modulo 2^24, so ages are
    // compared in that ring. Nothing if no slot qualifies.
    inline std::optional<int> pickSlot(const std::array<std::uint32_t, slots>& serials, std::uint32_t counter)
    {
        std::optional<int> best;
        std::uint32_t bestAge = 0;
        for (int i = 0; i < slots; ++i)
        {
            if (serials[i] == 0)
                continue;
            const std::uint32_t age = (counter - serials[i]) & serialMask;
            // A serial from "the future" (a slot written by a frame the counter has not reached, or a wrapped value)
            // has a huge age and is skipped.
            if (age < static_cast<std::uint32_t>(latency) || age > serialMask / 2)
                continue;
            if (!best || age < bestAge)
            {
                best = i;
                bestAge = age;
            }
        }
        return best;
    }

    // The serial of the n-th frame (counter 0 is serial 1): 1 .. 2^24, then round again, never 0.
    inline std::uint32_t serialOf(std::uint64_t frame)
    {
        return static_cast<std::uint32_t>(frame % serialMask) + 1u;
    }

    // The colour grade's contrast pivot (the tonemapped, encoded luma of the scene's mean brightness), from the mean
    // luminance the pass measured instead of the tonemap shader's own 36 taps: the same steps as the shader's
    // gradePivot (exposure, the curve, the clip, the encode, the clamp to 0.02 .. 0.8) over a grey of the mean. 0 means
    // "not measured".
    inline float pivotFromMeanLuma(float meanLuma, float exposureStops, Hdr::Tonemap curve, bool encode)
    {
        if (!(meanLuma > 0.f))
            return 0.f;
        float c = std::clamp(meanLuma * Hdr::exposureScale(exposureStops), 0.f, Hdr::maxTargetValue);
        c = Hdr::tonemapChannel(curve, c);
        c = std::clamp(c, 0.f, 1.f);
        if (encode)
            c = std::pow(c, 1.f / Hdr::displayGamma);
        return std::clamp(c, 0.02f, 0.8f);
    }
}

#endif
