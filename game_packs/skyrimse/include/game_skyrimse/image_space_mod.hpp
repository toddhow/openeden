#ifndef OPENEDEN_GAME_SKYRIMSE_IMAGE_SPACE_MOD_H
#define OPENEDEN_GAME_SKYRIMSE_IMAGE_SPACE_MOD_H

// Skyrim SE image space modifiers (IMAD) and the Night Eye view (task 0257). An IMAD is a set of keyed tracks that
// change the picture while an effect runs. This file is the pure half: the record decoder (only the tracks the core can
// draw: saturation, brightness, contrast and the tint), the sampling of a record at a time, and the tracker that plays
// Night Eye's intro, holds its last key and plays its outro. No engine, renderer, script-language or clock type: the
// caller hands in the combat clock. The layout is in docs/tasks/0257-skyrim-night-eye.md; what the data settles is
// VERIFIED there, what it does not is RECALLED-VERIFY here and in image_space_mod.cpp.
//
// Layout (IMAD, 225 records in the five masters, VERIFIED unless said): EDID; DNAM 244 bytes (flags u32, bit 0 =
// animatable; duration f32 seconds; then key counts); thirteen named tracks and 21 pairs of "?IAD" tracks (a multiply
// and an add), each a list of keys. A key is 8 bytes (time f32, value f32) or, for the tint (TNAM) and the fade colour
// (NAM3), 20 bytes (time, r, g, b, amount). Key times run 0 to 1 over the duration (VERIFIED: every key time of the
// Night Eye records is in 0 to 1). Which pair is which value is RECALLED-VERIFY, supported by the data: the pair 0x11
// goes 1 -> 0.5 while the tint comes in (a desaturation), 0x12's add goes 0 -> 1.1 (a brightening), 0x13's add 0 ->
// 0.2 (contrast); every other pair stays at 1 and 0. The tint amount is the fifth float of a TNAM key.
//
// Hostile input: a track size that is not a whole number of keys, a key that is not finite, a track over the key cap
// are counted as odd subrecords and the track is left out; nothing throws.

#include "game_skyrimse/land.hpp"
#include "game_skyrimse/records.hpp"

#include <openeden/game/player_image_space.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace imagespace
    {
        inline constexpr std::size_t kMaxKeysPerTrack = 64;

        struct Key
        {
            float mTime = 0.f;
            float mValue = 0.f;
        };

        struct TintKey
        {
            float mTime = 0.f;
            std::array<float, 3> mRgb{ 1.f, 1.f, 1.f };
            float mAmount = 0.f;
        };
    }

    struct ImageModifierRecord
    {
        RecordCommon mCommon;
        bool mHasData = false; ///< a DNAM of the right size was read
        bool mAnimatable = false; ///< DNAM flags bit 0: plays over its duration (else a held state)
        float mDuration = 0.f; ///< seconds the keys' 0 to 1 time spans
        std::vector<imagespace::Key> mSaturationMult, mSaturationAdd;
        std::vector<imagespace::Key> mBrightnessMult, mBrightnessAdd;
        std::vector<imagespace::Key> mContrastMult, mContrastAdd;
        std::vector<imagespace::TintKey> mTint;
    };

    [[nodiscard]] bool decodeImageModifier(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ImageModifierRecord& out, std::string& why);

    namespace imagespace
    {
        using State = openeden::sdk::game::PlayerImageSpaceState;

        /// The record's values at normalised time `t` (clamped to 0 to 1; linear between keys, the first or last key
        /// outside them). A track with no key is neutral (multiply 1, add 0, no tint). `active` is true.
        [[nodiscard]] State sample(const ImageModifierRecord& record, float t);

        /// Componentwise linear blend from `a` to `b` by `f` (clamped to 0 to 1); the result is active when either is.
        [[nodiscard]] State lerp(const State& a, const State& b, float f);

        /// Night Eye's view over time: the intro plays while the effect runs, the last key of the intro is held, and
        /// the outro plays when the effect ends (from the share of the intro that had played). The records belong to
        /// the caller and must outlive the tracker. No clock: `now` is the caller's seconds; a time that is not
        /// finite is ignored, a time that goes back counts as no time passed.
        class NightEyeTracker
        {
        public:
            void setRecords(const ImageModifierRecord* intro, const ImageModifierRecord* outro);
            [[nodiscard]] bool hasRecords() const { return mIntro != nullptr; }
            /// True: the result never changes the contrast (multiply 1, add 0). The core pivots contrast on mid grey, the
            /// game on the scene's average luma, so a contrast boost would only darken a night view.
            void setNeutralContrast(bool on) { mNeutralContrast = on; }
            /// The state to draw now. `active` false in the result means no change at all.
            [[nodiscard]] State update(bool running, float now);

        private:
            [[nodiscard]] State step(bool running, float now);

        private:
            enum class Phase
            {
                Idle,
                In,
                Held,
                Out
            };
            const ImageModifierRecord* mIntro = nullptr;
            const ImageModifierRecord* mOutro = nullptr;
            Phase mPhase = Phase::Idle;
            float mStart = 0.f; ///< the clock when the intro (or the outro) began
            float mShare = 0.f; ///< 0 to 1: how much of the intro had played when the outro began
            float mLastNow = 0.f;
            bool mHaveNow = false;
            bool mNeutralContrast = false;
        };
    }
}

#endif
