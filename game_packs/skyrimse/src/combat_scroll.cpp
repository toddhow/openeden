// Task 0234: the player's scrolls, the combat half. Using a scroll queues its spell (a SpellRecord the pack decoded
// from the SCRL, scroll.hpp: cost 0, fire and forget) and the next combat updates cast it through the same cast clip,
// delivery and effect rules as any spell (task 0186). No magicka is spent. Modelled on castPlayerPower (task 0228).
#include "game_skyrimse/combat.hpp"

#include <utility>

namespace game_skyrimse
{
    namespace
    {
        /// How long a queued scroll waits for the cast clip's timing (the sidecar is read on a worker) before it gives
        /// up and the scroll is given back.
        constexpr float kScrollWaitSeconds = 5.f;
    }

    void SkyrimCombat::setScrollOutcome(std::function<void(FormId scroll, bool cast)> sink)
    {
        mScrollOutcome = std::move(sink);
    }

    bool SkyrimCombat::castPlayerScroll(FormId scroll)
    {
        // Combat that has never run (the setting is off) can never cast it: refuse, so the scroll is not used up.
        if (mData == nullptr || mUpdates == 0 || mPendingScroll != kNullFormId)
            return false;
        const SpellRecord* record = mData->spellById(scroll);
        if (record == nullptr || !record->mScroll || !record->mHasData || record->mEffects.empty())
            return false;
        mPendingScroll = scroll;
        mPendingScrollAt = mClock;
        trace("COMBAT player scroll " + record->mCommon.mEditorId + " queued");
        return true;
    }

    void SkyrimCombat::runPlayerScroll(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (mPendingScroll == kNullFormId || mFighters.empty())
            return;
        const FormId scroll = mPendingScroll;
        const SpellRecord* spell = mData != nullptr ? mData->spellById(scroll) : nullptr;
        const auto finish = [&](bool cast) {
            mPendingScroll = kNullFormId;
            if (mScrollOutcome)
                mScrollOutcome(scroll, cast);
        };
        if (spell == nullptr)
        {
            finish(false);
            return;
        }
        Fighter& player = mFighters.front();
        const bool held = spell->mCastType == MagicCast::Concentration;
        if (ready(player) && beginCast(writer, player, *spell, false, held ? mPlayerCastHold : 0.f))
        {
            trace("COMBAT player scroll " + spell->mCommon.mEditorId + " cast begins");
            finish(true);
            return;
        }
        if (mClock - mPendingScrollAt > kScrollWaitSeconds)
        {
            trace("COMBAT player scroll " + spell->mCommon.mEditorId + " gave up: the cast did not start");
            finish(false);
        }
    }
}
