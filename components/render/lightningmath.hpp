#ifndef OPENEDEN_RENDER_LIGHTNINGMATH_H
#define OPENEDEN_RENDER_LIGHTNINGMATH_H

// OpenEden (task 0040, stage a): the maths of lightning as light. No game types, no shader twin: the weather manager
// runs it once per frame and hands the renderer a brightness and a direction.
//
// In plain words. A thunderstorm strike (the weather manager's `lightningAndThunder`) starts a flash. The flash is a
// short brightness curve with the double flicker real lightning has: a quick first stroke that dies away, then a
// second, weaker stroke a fraction of a second later. The brightness is added to the sun, ambient and fog colours as a
// cold white-blue (the vanilla game added plain white), and the sun's light is tilted towards a direction in the clouds
// for the length of the flash (the light that reaches the ground is the sun's plus the flash's, which this file merges
// into one light whose direction is the average weighted by brightness).
//
// SAFETY (photosensitivity). Flashing light can trigger seizures. Two hard limits hold whatever the settings say, and
// both are tested (apps/components_tests/render/testlightningmath.cpp):
//  - the peak: no flash adds more than `peakCap` to the colours, which is the brightest single strike the vanilla game
//    ever drew (a strike at distance 0 adds exactly 1). The `lightning strength` setting scales the flash but cannot
//    pass the cap;
//  - the interval: a strike that starts less than `minInterval` seconds after the start of the last flash that was
//    shown does not start a flash (the thunder still sounds). With two strokes per flash this keeps any one second of
//    play to at most two bright flashes of the screen, under the limit of three in W3C's WCAG 2.2 success criterion
//    2.3.1 "Three Flashes or Below Threshold" (a window of one second).
//
// The direction of a flash comes from a hash of a fixed seed and the number of strikes so far (no clock, no random
// generator state), so two runs with the same strikes light the world from the same directions.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <vsg/maths/vec3.h>

namespace Render::Lightning
{
    constexpr float pi = 3.14159265358979f;

    // The most any flash adds to the sun, ambient and fog colours (display-encoded units, as the vanilla flash). 1 is
    // what the closest vanilla strike added at its peak; it is also the hard limit for every setting and every strike.
    constexpr float peakCap = 1.f;
    // The shortest time, in seconds, between the starts of two flashes that are shown. See the safety note above.
    constexpr float minInterval = 1.5f;

    // The shape of the flash, in seconds. Chosen by eye to read like a real stroke pair on film (a bright first stroke
    // of about a tenth of a second, a weaker return a few tenths later); no source.
    constexpr float riseTime = 0.03f;
    constexpr float firstDecay = 0.12f; // e-folding time of the first stroke
    constexpr float secondStart = 0.22f;
    constexpr float secondAmount = 0.6f; // the second stroke, as a share of the first
    constexpr float secondDecay = 0.18f;
    // The flash is gone by `duration`; it eases to nothing over the last `fadeOutTime` of it.
    constexpr float duration = 1.f;
    constexpr float fadeOutTime = 0.25f;
    // A flash's colour as an add to display-encoded colours: a cold white with a touch of blue. Chosen by eye; Morrowind's
    // weather records hold no lightning colour (the Weather_* fallback keys have a thunder frequency, threshold and
    // flash decrement only), so the colour is OpenEden's.
    constexpr vsg::vec3 colour{ 0.80f, 0.90f, 1.0f };
    // The part of the sky a flash comes from: the direction's height above the horizon is between these.
    constexpr float minElevation = 25.f * pi / 180.f;
    constexpr float maxElevation = 75.f * pi / 180.f;
    // The default seed of the direction generator.
    constexpr std::uint32_t defaultSeed = 0x0E0D0E0Au;

    namespace Detail
    {
        inline float saturate(float v)
        {
            // NaN compares false and falls through to 0.
            return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
        }

        inline float smooth(float v)
        {
            const float t = saturate(v);
            return t * t * (3.f - 2.f * t);
        }

        // One stroke: a short smooth rise, then an exponential decay.
        inline float stroke(float age, float start, float amount, float rise, float decay)
        {
            if (!(age > start))
                return 0.f;
            const float t = age - start;
            return amount * smooth(t / rise) * std::exp(-t / decay);
        }

        // A 32-bit integer hash (Chris Wellons' "lowbias32"); all integer arithmetic, so every platform agrees.
        inline std::uint32_t hash(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        // 0 to just under 1 from the top 24 bits (exactly representable in a float).
        inline float unit(std::uint32_t h)
        {
            return static_cast<float>(h >> 8) / 16777216.f;
        }
    }

    // The flash's brightness `age` seconds after it started, 0 to 1: the double flicker. 0 before the start and from
    // `duration` on; a bad age (NaN, negative) is 0.
    inline float envelope(float age)
    {
        using namespace Detail;
        if (!(age > 0.f) || age >= duration)
            return 0.f;
        const float sum = stroke(age, 0.f, 1.f, riseTime, firstDecay)
            + stroke(age, secondStart, secondAmount, riseTime, secondDecay);
        const float fade = 1.f - smooth((age - (duration - fadeOutTime)) / fadeOutTime);
        return std::min(sum, 1.f) * fade;
    }

    // How strong a vanilla strike is: `lightningAndThunder` adds 1 - 0.25 * distance to the flash, for the thunder
    // sound number 0 (nearest) to 3 (farthest).
    inline float strikeStrength(unsigned distance)
    {
        return 1.f - 0.25f * static_cast<float>(std::min(distance, 3u));
    }

    // The most a flash of this strike adds at its peak. `setting` is `lightning strength`. Never above `peakCap`,
    // never negative, whatever the inputs (a NaN is 0).
    inline float peak(float setting, float strike)
    {
        const float p = setting * strike;
        if (!(p > 0.f))
            return 0.f;
        return p < peakCap ? p : peakCap;
    }

    // The unit direction of the flash for strike number `index`, as the direction to the light in world space (Z up):
    // a hash of the seed and the index picks the compass direction and the height (between minElevation and
    // maxElevation).
    inline vsg::vec3 direction(std::uint32_t seed, std::uint32_t index)
    {
        using namespace Detail;
        const std::uint32_t base = hash(seed ^ (index * 0x9E3779B9U));
        const float azimuth = 2.f * pi * unit(hash(base + 1u));
        const float elevation = minElevation + (maxElevation - minElevation) * unit(hash(base + 2u));
        const float flat = std::cos(elevation);
        return vsg::vec3(flat * std::cos(azimuth), flat * std::sin(azimuth), std::sin(elevation));
    }

    // The colour a flash of brightness `amount` adds (display-encoded, added to sun, ambient and fog).
    inline vsg::vec3 colourAdd(float amount)
    {
        return colour * amount;
    }

    // The state of the flashes: when the last shown flash started, how strong it is, which strike it was, and how many
    // strikes there have been. `now` is the caller's accumulated game seconds (never a wall clock). No allocation.
    class Flasher
    {
    public:
        // A strike happens at `now`. Returns true if it starts a flash, false if it came too soon after the last one
        // (the strike is still counted, so the direction sequence does not depend on the limiter).
        bool strike(float now, float strength)
        {
            const std::uint32_t index = mCount++;
            if (!(strength > 0.f))
                return false;
            if (mHasFlash && now - mStart < minInterval)
                return false;
            mHasFlash = true;
            mStart = now;
            mStrength = strength;
            mIndex = index;
            return true;
        }

        // The brightness to add at `now`, for the `lightning strength` setting `setting`. At most peakCap.
        float intensity(float now, float setting) const
        {
            if (!mHasFlash)
                return 0.f;
            return peak(setting, mStrength) * envelope(now - mStart);
        }

        // Direction to the light of the current flash (the last one shown).
        vsg::vec3 flashDirection(std::uint32_t seed = defaultSeed) const { return direction(seed, mIndex); }

        // Strikes counted since the start (shown or not).
        std::uint32_t count() const { return mCount; }
        bool hasFlash() const { return mHasFlash; }
        float start() const { return mStart; }

        // Moves the clock back by `seconds`: the caller's accumulated seconds are reduced by the same amount now and
        // then, so that a float keeps its precision in a long game.
        void shift(float seconds) { mStart -= seconds; }

        void reset() { *this = Flasher(); }

    private:
        std::uint32_t mCount = 0;
        std::uint32_t mIndex = 0;
        bool mHasFlash = false;
        float mStart = 0.f;
        float mStrength = 0.f;
    };

    // One light from two: the sun's (`sunDirection`, brightness `sunWeight`) and the flash's (`flashDirection`,
    // `flashWeight`), as the direction of their weighted average. The result has the length of `sunDirection`, so a
    // caller that keeps the light as a position keeps its distance. With no flash it is `sunDirection` itself. Weights
    // that are negative, NaN or both zero count as no flash.
    inline vsg::vec3 mergedDirection(
        const vsg::vec3& sunDirection, float sunWeight, const vsg::vec3& flashDirection, float flashWeight)
    {
        const float ws = sunWeight > 0.f ? sunWeight : 0.f;
        const float wf = flashWeight > 0.f ? flashWeight : 0.f;
        const float length
            = std::sqrt(sunDirection.x * sunDirection.x + sunDirection.y * sunDirection.y + sunDirection.z * sunDirection.z);
        if (!(wf > 0.f) || !(length > 0.f))
            return sunDirection;
        const vsg::vec3 unitSun = sunDirection / length;
        const vsg::vec3 sum = unitSun * ws + flashDirection * wf;
        const float sumLength = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
        if (!(sumLength > 1e-6f))
            return sunDirection;
        return sum * (length / sumLength);
    }
}

#endif
