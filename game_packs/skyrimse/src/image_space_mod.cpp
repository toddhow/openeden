#include "game_skyrimse/image_space_mod.hpp"

#include "exterior_common.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        constexpr std::size_t kDnamBytes = 244; // VERIFIED: every IMAD of the five masters
        constexpr std::size_t kKeyBytes = 8; // time, value
        constexpr std::size_t kTintKeyBytes = 20; // time, r, g, b, amount
        constexpr std::uint32_t kAnimatableBit = 1;

        bool allFinite(const std::byte* p, std::size_t floats)
        {
            for (std::size_t i = 0; i < floats; ++i)
                if (!std::isfinite(readLe<float>(p + i * 4)))
                    return false;
            return true;
        }

        // Key times must not go back (a track is sampled by walking it).
        template <class K>
        bool ascending(const std::vector<K>& keys)
        {
            for (std::size_t i = 1; i < keys.size(); ++i)
                if (keys[i].mTime < keys[i - 1].mTime)
                    return false;
            return true;
        }

        bool readKeys(std::span<const std::byte> data, std::vector<imagespace::Key>& out)
        {
            if (data.empty() || data.size() % kKeyBytes != 0 || data.size() / kKeyBytes > imagespace::kMaxKeysPerTrack
                || !allFinite(data.data(), data.size() / 4))
                return false;
            std::vector<imagespace::Key> keys;
            keys.reserve(data.size() / kKeyBytes);
            for (std::size_t i = 0; i < data.size(); i += kKeyBytes)
                keys.push_back({ readLe<float>(data.data() + i), readLe<float>(data.data() + i + 4) });
            if (!ascending(keys))
                return false;
            out = std::move(keys);
            return true;
        }

        bool readTintKeys(std::span<const std::byte> data, std::vector<imagespace::TintKey>& out)
        {
            if (data.empty() || data.size() % kTintKeyBytes != 0
                || data.size() / kTintKeyBytes > imagespace::kMaxKeysPerTrack
                || !allFinite(data.data(), data.size() / 4))
                return false;
            std::vector<imagespace::TintKey> keys;
            keys.reserve(data.size() / kTintKeyBytes);
            for (std::size_t i = 0; i < data.size(); i += kTintKeyBytes)
            {
                imagespace::TintKey k;
                k.mTime = readLe<float>(data.data() + i);
                for (std::size_t c = 0; c < 3; ++c)
                    k.mRgb[c] = readLe<float>(data.data() + i + 4 + c * 4);
                k.mAmount = readLe<float>(data.data() + i + 16);
                keys.push_back(k);
            }
            if (!ascending(keys))
                return false;
            out = std::move(keys);
            return true;
        }

        // The value of a keyed track at `t`: linear between keys, the end keys held outside, `neutral` with no keys.
        template <class K, class Get>
        float trackAt(const std::vector<K>& keys, float t, float neutral, Get get)
        {
            if (keys.empty())
                return neutral;
            if (t <= keys.front().mTime)
                return get(keys.front());
            if (t >= keys.back().mTime)
                return get(keys.back());
            for (std::size_t i = 1; i < keys.size(); ++i)
            {
                if (t > keys[i].mTime)
                    continue;
                const float span = keys[i].mTime - keys[i - 1].mTime;
                if (span <= 0.f)
                    return get(keys[i]);
                const float f = (t - keys[i - 1].mTime) / span;
                return get(keys[i - 1]) + (get(keys[i]) - get(keys[i - 1])) * f;
            }
            return get(keys.back());
        }

        float finiteShare(float f)
        {
            return std::isfinite(f) ? std::clamp(f, 0.f, 1.f) : 0.f;
        }
    }

    bool decodeImageModifier(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ImageModifierRecord& out, std::string& why)
    {
        const Fields fields{ context, "IMAD", why };
        out = ImageModifierRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("DNAM"):
                    if (data.size() == kDnamBytes && std::isfinite(readLe<float>(data.data() + 4)))
                    {
                        out.mHasData = true;
                        out.mAnimatable = (readLe<std::uint32_t>(data.data()) & kAnimatableBit) != 0;
                        out.mDuration = readLe<float>(data.data() + 4);
                    }
                    else
                        fields.odd(type);
                    break;
                case Content::fourCC("TNAM"):
                    if (!readTintKeys(data, out.mTint))
                        fields.odd(type);
                    break;
                // The pairs of tracks: a multiply (0x11 to 0x13 in the first byte) and its add ('Q' to 'S').
                // Which pair is which value is RECALLED-VERIFY (see the header).
                case Content::fourCC("\x11IAD"):
                    if (!readKeys(data, out.mSaturationMult))
                        fields.odd(type);
                    break;
                case Content::fourCC("QIAD"):
                    if (!readKeys(data, out.mSaturationAdd))
                        fields.odd(type);
                    break;
                case Content::fourCC("\x12IAD"):
                    if (!readKeys(data, out.mBrightnessMult))
                        fields.odd(type);
                    break;
                case Content::fourCC("RIAD"):
                    if (!readKeys(data, out.mBrightnessAdd))
                        fields.odd(type);
                    break;
                case Content::fourCC("\x13IAD"):
                    if (!readKeys(data, out.mContrastMult))
                        fields.odd(type);
                    break;
                case Content::fourCC("SIAD"):
                    if (!readKeys(data, out.mContrastAdd))
                        fields.odd(type);
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    namespace imagespace
    {
        State sample(const ImageModifierRecord& record, float t)
        {
            const float at = std::isfinite(t) ? std::clamp(t, 0.f, 1.f) : 0.f;
            const auto value = [](const Key& k) { return k.mValue; };
            State s;
            s.active = true;
            s.saturationMult = trackAt(record.mSaturationMult, at, 1.f, value);
            s.saturationAdd = trackAt(record.mSaturationAdd, at, 0.f, value);
            s.brightnessMult = trackAt(record.mBrightnessMult, at, 1.f, value);
            s.brightnessAdd = trackAt(record.mBrightnessAdd, at, 0.f, value);
            s.contrastMult = trackAt(record.mContrastMult, at, 1.f, value);
            s.contrastAdd = trackAt(record.mContrastAdd, at, 0.f, value);
            s.tintAmount = trackAt(record.mTint, at, 0.f, [](const TintKey& k) { return k.mAmount; });
            for (std::size_t c = 0; c < 3; ++c)
                s.tint[c] = trackAt(record.mTint, at, 1.f, [c](const TintKey& k) { return k.mRgb[c]; });
            return s;
        }

        State lerp(const State& a, const State& b, float f)
        {
            const float w = finiteShare(f);
            const auto mix = [w](float x, float y) { return x + (y - x) * w; };
            State s;
            s.active = a.active || b.active;
            s.saturationMult = mix(a.saturationMult, b.saturationMult);
            s.saturationAdd = mix(a.saturationAdd, b.saturationAdd);
            s.brightnessMult = mix(a.brightnessMult, b.brightnessMult);
            s.brightnessAdd = mix(a.brightnessAdd, b.brightnessAdd);
            s.contrastMult = mix(a.contrastMult, b.contrastMult);
            s.contrastAdd = mix(a.contrastAdd, b.contrastAdd);
            s.tintAmount = mix(a.tintAmount, b.tintAmount);
            for (std::size_t c = 0; c < 3; ++c)
                s.tint[c] = mix(a.tint[c], b.tint[c]);
            return s;
        }

        void NightEyeTracker::setRecords(const ImageModifierRecord* intro, const ImageModifierRecord* outro)
        {
            mIntro = intro;
            mOutro = outro;
            if (mIntro == nullptr)
                mPhase = Phase::Idle;
        }

        State NightEyeTracker::update(bool running, float now)
        {
            State s = step(running, now);
            if (mNeutralContrast && s.active)
            {
                s.contrastMult = 1.f;
                s.contrastAdd = 0.f;
            }
            return s;
        }

        State NightEyeTracker::step(bool running, float now)
        {
            if (mIntro == nullptr)
                return State();
            if (std::isfinite(now))
            {
                // A clock that goes back counts as no time passing.
                if (mHaveNow && now < mLastNow)
                    now = mLastNow;
                mLastNow = now;
                mHaveNow = true;
            }
            else if (!mHaveNow)
                return State();
            else
                now = mLastNow; // a time that is not a number: the state of the last good one

            const float introLength = mIntro->mDuration;
            const auto introShareAt = [&](float time) {
                if (!(introLength > 0.f))
                    return 1.f;
                return std::clamp((time - mStart) / introLength, 0.f, 1.f);
            };

            if (running)
            {
                if (mPhase == Phase::Idle || mPhase == Phase::Out)
                {
                    mPhase = Phase::In;
                    mStart = now;
                }
                if (mPhase == Phase::In && introShareAt(now) >= 1.f)
                    mPhase = Phase::Held;
            }
            else if (mPhase == Phase::In || mPhase == Phase::Held)
            {
                mShare = mPhase == Phase::Held ? 1.f : introShareAt(now);
                if (mOutro == nullptr)
                {
                    mPhase = Phase::Idle;
                    return State();
                }
                mPhase = Phase::Out;
                mStart = now;
            }

            switch (mPhase)
            {
                case Phase::Idle:
                    return State();
                case Phase::In:
                    return sample(*mIntro, introShareAt(now));
                case Phase::Held:
                    return sample(*mIntro, 1.f);
                case Phase::Out:
                {
                    const float length = mOutro->mDuration;
                    const float u = length > 0.f ? (now - mStart) / length : 1.f;
                    if (u >= 1.f)
                    {
                        mPhase = Phase::Idle;
                        return State();
                    }
                    return lerp(State(), sample(*mOutro, u), mShare);
                }
            }
            return State();
        }
    }
}
