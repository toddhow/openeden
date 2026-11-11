#include "facesample.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace Face
{
    namespace
    {
        constexpr float sQuantum = 255.f; // facecurves.hpp: value / 255

        int findChannel(const Curves& curves, std::string_view name)
        {
            for (std::size_t i = 0; i < curves.mChannels.size(); ++i)
                if (curves.mChannels[i].mName == name)
                    return static_cast<int>(i);
            return -1;
        }

        // Rhubarb shapes A..H and X (faceimport.hpp) as how far a stock head's jaw opens. INITIAL VALUES, chosen by me
        // from the shapes' descriptions (A closed lips, B slightly open, C open, D wide open, E rounded, F puckered,
        // G teeth on lip, H tongue up, X rest), not measured on a real head.
        constexpr std::array<const char*, 9> sVisemeNames = { "viseme_a", "viseme_b", "viseme_c", "viseme_d",
            "viseme_e", "viseme_f", "viseme_g", "viseme_h", "viseme_x" };
        constexpr std::array<float, 9> sVisemeOpenness = { 0.f, 0.25f, 0.6f, 1.f, 0.55f, 0.35f, 0.15f, 0.4f, 0.f };

        float valueAt(const Curves& curves, int channel, std::uint32_t index, double fraction)
        {
            if (channel < 0)
                return 0.f;
            const auto& values = curves.mChannels[static_cast<std::size_t>(channel)].mValues;
            const double a = values[index];
            const double b = index + 1 < values.size() ? values[index + 1] : a;
            return static_cast<float>((a + (b - a) * fraction) / sQuantum);
        }
    }

    bool sampleAt(const Curves& curves, double seconds, float* out, std::size_t outCount)
    {
        if (out == nullptr || outCount < curves.mChannels.size() || curves.mFrameRate == 0 || curves.mFrameCount == 0)
            return false;
        if (!std::isfinite(seconds) || seconds < 0.0)
            return false;
        const double position = seconds * static_cast<double>(curves.mFrameRate);
        if (position >= static_cast<double>(curves.mFrameCount))
            return false;
        const double whole = std::floor(position);
        const std::uint32_t index = static_cast<std::uint32_t>(whole);
        const double fraction = position - whole;
        for (std::size_t i = 0; i < curves.mChannels.size(); ++i)
        {
            if (curves.mChannels[i].mValues.size() != curves.mFrameCount)
                return false;
            out[i] = valueAt(curves, static_cast<int>(i), index, fraction);
        }
        return true;
    }

    Driver::Driver(const Curves& curves, StockHeadTuning tuning)
        : mCurves(&curves)
        , mTuning(tuning)
        , mJaw(findChannel(curves, "jawopen"))
        , mFunnel(findChannel(curves, "mouthfunnel"))
        , mPucker(findChannel(curves, "mouthpucker"))
        , mBlinkLeft(findChannel(curves, "eyeblinkleft"))
        , mBlinkRight(findChannel(curves, "eyeblinkright"))
    {
        if (mJaw < 0 && mFunnel < 0 && mPucker < 0)
            for (std::size_t i = 0; i < sVisemeNames.size(); ++i)
                mVisemes[i] = findChannel(curves, sVisemeNames[i]);
    }

    std::optional<Reduction> Driver::update(double streamSeconds) const
    {
        const Curves& c = *mCurves;
        if (c.mFrameRate == 0 || c.mFrameCount == 0 || !std::isfinite(streamSeconds) || streamSeconds < 0.0)
            return std::nullopt;
        const double position = streamSeconds * static_cast<double>(c.mFrameRate);
        if (position >= static_cast<double>(c.mFrameCount))
            return std::nullopt;
        const double whole = std::floor(position);
        const std::uint32_t index = static_cast<std::uint32_t>(whole);
        const double fraction = position - whole;
        auto at = [&](int channel) {
            if (channel >= 0 && c.mChannels[static_cast<std::size_t>(channel)].mValues.size() != c.mFrameCount)
                return 0.f;
            return valueAt(c, channel, index, fraction);
        };
        Reduction r;
        r.mTalk = std::clamp(
            at(mJaw) * mTuning.mJawOpen + at(mFunnel) * mTuning.mMouthFunnel + at(mPucker) * mTuning.mMouthPucker, 0.f,
            1.f);
        if (mJaw < 0 && mFunnel < 0 && mPucker < 0)
        {
            float visemeTalk = 0.f;
            for (std::size_t i = 0; i < mVisemes.size(); ++i)
                visemeTalk += at(mVisemes[i]) * sVisemeOpenness[i];
            r.mTalk = std::clamp(visemeTalk, 0.f, 1.f);
        }
        r.mBlink = std::clamp(std::max(at(mBlinkLeft), at(mBlinkRight)), 0.f, 1.f);
        return r;
    }
}
