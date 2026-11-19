#ifndef VSGOPENMW_MWANIMATION_LIGHTFLICKER_H
#define VSGOPENMW_MWANIMATION_LIGHTFLICKER_H

#include <cstdint>
#include <optional>

// OpenEden (task 0355 stage h): a Skyrim light's own flicker and pulse.
//
// A Skyrim LIGH record carries a flicker period (seconds), an intensity amplitude (a fraction of the brightness) and
// a movement amplitude (units, not used). The engine's one light rule (light.cpp, Morrowind's) ignored them. A neutral
// light whose record has a usable period and amplitude carries them in the engine's ESM::Light record (mData.mTime
// packed with packTimeField, mData.mValue the seed, flag ESM::Light::NeutralFlicker) and MWAnim::addLight then runs
// Walk instead of the Morrowind rule. Pure maths, no clock, no global random generator: the same seed and the same
// steps always give the same brightness.

namespace MWAnim::NeutralFlicker
{
    inline constexpr float kMinPeriod = 0.02f; ///< seconds
    inline constexpr float kMaxPeriod = 10.f;
    /// The deepest dip: the brightness never goes below 1 - kMaxAmplitude (the data has amplitudes up to 5).
    inline constexpr float kMaxAmplitude = 0.95f;

    struct Params
    {
        float mPeriod = 0.f; ///< seconds between new targets (a flicker) or for one full cycle (a pulse)
        float mAmplitude = 0.f; ///< fraction of the brightness the light dips
        std::uint32_t mSeed = 0;

        friend bool operator==(const Params&, const Params&) = default;
    };

    /// Period in milliseconds (low 16 bits) and amplitude in thousandths (above them). 0 when the pair cannot be used
    /// (period or amplitude not finite or not above 0, amplitude under a thousandth): the light then keeps the
    /// Morrowind rule. Period and amplitude are clamped to their limits.
    [[nodiscard]] std::int32_t packTimeField(float periodSeconds, float amplitude);

    /// The parameters packTimeField stored, with the seed; nothing for a field that holds no usable pair.
    [[nodiscard]] std::optional<Params> unpack(std::int32_t timeField, std::int32_t seedField);

    /// The brightness over time, in [1 - amplitude, 1]. A flicker picks a new seeded target every period and moves
    /// towards it in a straight line; a pulse moves between the two ends in half a period each, eased.
    class Walk
    {
    public:
        Walk(const Params& params, bool pulse);

        /// Advances by `dt` seconds (a negative or non-finite step is 0, one over a second is cut to a second) and
        /// returns the brightness.
        float step(float dt);

        [[nodiscard]] float brightness() const { return mBrightness; }
        [[nodiscard]] float low() const { return mLow; }

    private:
        std::uint32_t mSeed;
        std::uint64_t mState;
        float mPeriod;
        float mLow;
        bool mPulse;
        float mBrightness = 1.f;
        float mFrom = 1.f;
        float mTarget = 1.f;
        float mLegTime = 0.f;
        float mLegLength = 1.f;
        bool mGoingUp = false;
    };
}

#endif
