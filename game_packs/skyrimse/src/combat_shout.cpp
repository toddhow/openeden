#include <game_skyrimse/combat.hpp>

#include <cmath>
#include <cstdio>

// Task 0205 (milestone M-S7): the player's shout, the rest of SkyrimCombat's magic runtime. The control goes down
// (onPlayerShoutEdge), the hold counts the words reached (shouts::wordsReached), the last unlocked word goes by itself,
// a release speaks the words reached so far. The word's spell is cast through the same cast path as a player spell
// (the aimed cast clip's timing, fireCast, launchSpell, applySpellEffects): a shout costs no magicka, trains no skill
// and starts the recovery cooldown of its word when it goes. Every rule about the hold is OURS (docs/tasks/0205).
namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;

        std::string num(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(value));
            return buffer;
        }

        constexpr float kDefaultShoutHold = 1.f;
    }

    void SkyrimCombat::onPlayerShoutEdge(bool pressed)
    {
        if (mShoutEdges.size() < 64)
            mShoutEdges.push_back(pressed);
    }

    bool SkyrimCombat::setTestPlayerShout(std::string_view editorId)
    {
        mTestShoutLooked = false;
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestPlayerShout.clear();
            return false;
        }
        mTestPlayerShout = std::string(editorId);
        return true;
    }

    void SkyrimCombat::setTestShoutEvery(float seconds)
    {
        mShoutEvery = std::isfinite(seconds) && seconds > 0.f ? seconds : 0.f;
        mNextShout = mShoutEvery;
    }

    void SkyrimCombat::setTestShoutHold(float seconds)
    {
        mShoutHold = std::isfinite(seconds) && seconds > 0.f ? seconds : kDefaultShoutHold;
    }

    bool SkyrimCombat::beginShoutCast(
        CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell, float recovery)
    {
        if (!caster.mSubject.player || caster.mDead || !ready(caster) || isBusyCasting(caster.mSubject)
            || mCasts.size() >= kMaxCasts || spell.mEffects.empty()
            || spell.mCastType == MagicCast::Concentration)
            return false;
        const std::uint8_t column = spell.mDelivery == MagicDelivery::Self ? kMagicSelf : kMagicAimed;
        const std::string_view clip = mMagicSlots.clipFor(ClipSlot::Cast, column);
        if (clip.empty())
            return false;
        float time = 0.f;
        // No first-person arms: the spell goes after the time the cast clip's own event gives.
        if (!writer.clipEventTime(clip, false, openeden::sdk::game::ClipEventKind::SpellFire, time) || !(time >= 0.f))
            return false; // the sidecar is still being read: asked again next frame
        Cast cast;
        cast.mCaster = caster.mSubject;
        cast.mSpell = &spell;
        cast.mStarted = mClock;
        cast.mFireAt = mClock + time;
        cast.mShout = true;
        cast.mRecovery = recovery;
        mCasts.push_back(cast);
        return true;
    }

    void SkyrimCombat::runPlayerShout(CombatRequestWriter& writer, float step)
    {
        if (mShouts == nullptr || mData == nullptr || mFighters.empty())
        {
            mShoutEdges.clear();
            return;
        }
        mShouts->tick(step);
        if (!mHaveShoutSettings)
        {
            mShoutSettings = mData->shoutSettings();
            mHaveShoutSettings = true;
        }
        Fighter& player = mFighters.front();
        if (!ready(player))
        {
            mShoutEdges.clear();
            return;
        }
        // The test setting equips its shout once the records can answer.
        if (!mTestPlayerShout.empty() && !mTestShoutLooked)
        {
            mTestShoutLooked = true;
            const ShoutRecord* found = mData->shoutByEditorId(mTestPlayerShout);
            trace("COMBAT player shout '" + mTestPlayerShout + "' " + (found != nullptr ? "found" : "NOT found"));
            if (found != nullptr)
                mShouts->equip(found->mCommon.mId);
        }
        // The automatic test: press every period and release after the hold.
        if (mShoutEvery > 0.f && !mShoutHeld && !mShoutWanted && mClock >= mNextShout)
        {
            mShoutEdges.push_back(true);
            mNextShout = mClock + mShoutEvery;
            mShoutAutoReleaseAt = mClock + mShoutHold;
        }
        if (mShoutAutoReleaseAt >= 0.f && mClock >= mShoutAutoReleaseAt)
        {
            mShoutEdges.push_back(false);
            mShoutAutoReleaseAt = -1.f;
        }
        const ShoutRecord* shout = mShouts->equipped() == kNullFormId ? nullptr : mData->shoutById(mShouts->equipped());
        const auto choose = [&](std::size_t words) {
            mShoutWanted = true;
            mShoutWordsChosen = words;
            mShoutWantedAt = mClock;
        };
        for (const bool pressed : mShoutEdges)
        {
            if (pressed)
            {
                if (mShoutHeld || mShoutWanted)
                    continue;
                if (shout == nullptr || !shout->mHasData)
                {
                    trace("COMBAT shout refused: no shout equipped");
                    continue;
                }
                const std::size_t available = mShouts->availableWords(*shout);
                if (available == 0)
                {
                    trace("COMBAT shout " + shout->mCommon.mEditorId + " refused: no word unlocked");
                    continue;
                }
                if (!mShouts->ready())
                {
                    trace("COMBAT shout " + shout->mCommon.mEditorId + " refused: the cooldown has "
                        + num(mShouts->cooldown()) + " s to run");
                    continue;
                }
                if (isBusyCasting(player.mSubject))
                    continue;
                mShoutHeld = true;
                mShoutAutoFired = false;
                mShoutHeldFor = 0.f;
                mShoutWordsTold = 0;
                mShoutAvail = available;
                continue;
            }
            if (!mShoutHeld)
                continue;
            mShoutHeld = false;
            if (mShoutAutoFired)
                continue;
            const std::size_t words = shouts::wordsReached(mShoutHeldFor, mShoutAvail, mShoutSettings);
            if (words == 0)
            {
                trace("COMBAT shout cancelled: held " + num(mShoutHeldFor) + " s, under the minimum");
                continue;
            }
            choose(words);
        }
        mShoutEdges.clear();
        if (mShoutHeld)
        {
            mShoutHeldFor += step;
            const std::size_t words = shouts::wordsReached(mShoutHeldFor, mShoutAvail, mShoutSettings);
            if (words > mShoutWordsTold)
            {
                mShoutWordsTold = words;
                trace("COMBAT shout word " + std::to_string(words) + " reached after " + num(mShoutHeldFor) + " s");
            }
            if (words >= mShoutAvail && words > 0 && !mShoutWanted && !mShoutAutoFired)
            {
                // the last word the player has goes without waiting for the release
                mShoutAutoFired = true;
                choose(words);
            }
        }
        if (!mShoutWanted)
            return;
        if (shout == nullptr || mShoutWordsChosen == 0 || mShoutWordsChosen > shout->mWordCount)
        {
            mShoutWanted = false;
            return;
        }
        if (mClock - mShoutWantedAt > kCastEventTimeoutSeconds)
        {
            mShoutWanted = false;
            trace("COMBAT shout gave up: the cast clip's timing never came or the spell cannot be shouted");
            return;
        }
        const ShoutWordSlot& slot = shout->mWords[mShoutWordsChosen - 1];
        const SpellRecord* spell = mData->spellById(slot.mSpell);
        if (spell == nullptr)
        {
            mShoutWanted = false;
            trace("COMBAT shout " + shout->mCommon.mEditorId + " word " + std::to_string(mShoutWordsChosen)
                + ": no spell");
            return;
        }
        mShoutTraceName = shout->mCommon.mEditorId;
        if (beginShoutCast(writer, player, *spell, slot.mRecovery))
            mShoutWanted = false;
    }
}
