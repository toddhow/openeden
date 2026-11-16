#include "neutralfade.hpp"

#include <algorithm>
#include <cmath>

namespace MWWorld
{
    namespace
    {
        float alphaOf(float from, float to, float seconds, float elapsed)
        {
            const float t = std::clamp(elapsed / seconds, 0.f, 1.f);
            return from + (to - from) * t;
        }
    }

    bool NeutralFades::start(std::uint32_t placement, bool fadeIn, float seconds)
    {
        if (!std::isfinite(seconds) || !(seconds > 0.f))
            return false;
        seconds = std::min(seconds, kMaxSeconds);
        for (Fade& fade : mFades)
        {
            if (fade.mPlacement != placement)
                continue;
            // replaced in place (it keeps its position in the order), from where the picture is now
            const float now = alphaOf(fade.mFrom, fade.mTo, fade.mSeconds, fade.mElapsed);
            fade = { placement, fadeIn, now, fadeIn ? 1.f : 0.f, seconds, 0.f };
            return true;
        }
        if (mFades.size() >= kMaxFades)
            return false;
        mFades.push_back({ placement, fadeIn, fadeIn ? 0.f : 1.f, fadeIn ? 1.f : 0.f, seconds, 0.f });
        return true;
    }

    void NeutralFades::advance(float dt, std::vector<Update>& out)
    {
        if (!std::isfinite(dt) || dt < 0.f)
            return;
        for (Fade& fade : mFades)
        {
            fade.mElapsed += dt;
            const bool finished = fade.mElapsed >= fade.mSeconds;
            Update update;
            update.mPlacement = fade.mPlacement;
            update.mFadeIn = fade.mFadeIn;
            update.mFinished = finished;
            update.mAlpha = finished ? fade.mTo : alphaOf(fade.mFrom, fade.mTo, fade.mSeconds, fade.mElapsed);
            out.push_back(update);
        }
        // the finished ones go; the others keep their order
        std::erase_if(mFades, [](const Fade& fade) { return fade.mElapsed >= fade.mSeconds; });
    }

    bool NeutralFades::cancel(std::uint32_t placement)
    {
        return std::erase_if(mFades, [&](const Fade& fade) { return fade.mPlacement == placement; }) != 0;
    }

    bool NeutralFades::fading(std::uint32_t placement) const
    {
        return std::any_of(
            mFades.begin(), mFades.end(), [&](const Fade& fade) { return fade.mPlacement == placement; });
    }
}
