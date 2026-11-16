#include "game_skyrimse/werewolf.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse::werewolf
{
    namespace
    {
        constexpr float kMaxStep = 1.f; ///< a frame longer than this is clamped

        // The row (docs/tasks/0259-skyrim-werewolf.md section 1): walk and run strides are the root motion of
        // ww_walkforward and ww_runforward, the height is the skin's z extent; the radius is OURS.
        const CreatureProfile kBare = { "werewolf", "actors/werewolfbeast/character assets/skeleton.nif",
            "werewolfbeastproject", "ww_idlebase", "ww_walkforward", "ww_runforward", 93.4308f / 1.33333f,
            222.219f / 0.733333f, 93.4308f, 222.219f, 139.6f, "ww_attackrightfast", "ww_attackrightpowercombo",
            "ww_staggersmalll", "ww_staggermedium", "ww_deathtemp", 30.f };
        const CreatureProfile kPlayer = { "werewolf", "actors/werewolfbeast/character assets/skeleton.nif",
            "werewolfbeastproject", "ww_idlebase", "ww_walkforward", "ww_runforward", 93.4308f / 1.33333f,
            222.219f / 0.733333f, 93.4308f, 222.219f, 139.6f, "werewolfbeast::ww_attackrightfast",
            "werewolfbeast::ww_attackrightpowercombo", "ww_staggersmalll", "ww_staggermedium", "ww_deathtemp", 30.f };
    }

    FormId levelAbility(std::uint32_t level)
    {
        struct Step
        {
            std::uint32_t mUpTo;
            FormId mAbility;
        };
        static constexpr Step kSteps[] = { { 10, 0x10C6E8 }, { 15, 0x10F186 }, { 20, 0x10C6E9 }, { 25, 0x10F187 },
            { 30, 0x10C6EA }, { 35, 0x10F188 }, { 40, 0x10F189 }, { 45, 0x10F18A } };
        const std::uint32_t l = std::max<std::uint32_t>(level, 1);
        for (const Step& step : kSteps)
            if (l <= step.mUpTo)
                return step.mAbility;
        return 0x10C6EB;
    }

    void BeastClock::start(float seconds)
    {
        mActive = true;
        mRemaining = std::isfinite(seconds) && seconds > 0.f ? std::min(seconds, kMaxSeconds) : kStandardSeconds;
        mWarned = mRemaining <= kWarnSeconds;
    }

    void BeastClock::stop()
    {
        mActive = false;
        mRemaining = 0.f;
        mWarned = false;
    }

    BeastClock::Event BeastClock::tick(float dt)
    {
        if (!mActive)
            return Event::None;
        const float step = std::isfinite(dt) && dt > 0.f ? std::min(dt, kMaxStep) : 0.f;
        mRemaining -= step;
        if (mRemaining <= 0.f)
        {
            stop();
            return Event::Ended;
        }
        if (!mWarned && mRemaining <= kWarnSeconds)
        {
            mWarned = true;
            return Event::Warned;
        }
        return Event::None;
    }

    bool BeastClock::feed(bool victimIsPerson)
    {
        if (!mActive)
            return false;
        mRemaining
            = std::min(kMaxSeconds, mRemaining + kFeedSeconds / 2.f + (victimIsPerson ? kFeedSeconds / 2.f : 0.f));
        if (mRemaining > kWarnSeconds)
            mWarned = false; // the warning comes again when it runs low again
        return true;
    }

    std::string_view bareClip(std::string_view clip)
    {
        const std::size_t at = clip.find(kClipSeparator);
        return at == std::string_view::npos ? clip : clip.substr(at + kClipSeparator.size());
    }

    std::string_view clipFolder(std::string_view clip)
    {
        const std::size_t at = clip.find(kClipSeparator);
        return at == std::string_view::npos ? std::string_view() : clip.substr(0, at);
    }

    const CreatureProfile& playerProfile()
    {
        return kPlayer;
    }

    const CreatureProfile& bareProfile()
    {
        return kBare;
    }
}
