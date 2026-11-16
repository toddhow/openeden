#include "game_skyrimse/combat.hpp"

#include <cmath>

namespace game_skyrimse
{
    using openeden::sdk::game::ClipSlot;

    // Task 0249. The loops are first-person clips of `_1stperson/animations` (VERIFIED in Skyrim - Animations.bsa).
    std::string_view handsHoldClip(HandsHold hold, bool self, bool ward)
    {
        switch (hold)
        {
            case HandsHold::None:
                break;
            case HandsHold::Charging:
                return ward ? "MRh_WardCharge" : self ? "MRh_SelfChargeLoop" : "MRh_ChargeLoop";
            case HandsHold::Holding:
                return ward ? "MRh_WardLoop" : self ? "MRh_SelfConcentration" : "MRh_AimedConcentration";
        }
        return {};
    }

    void HandsHoldClock::update(HandsHold hold, float now)
    {
        if (hold == HandsHold::None)
        {
            mHold = HandsHold::None;
            mSince = 0.f;
            return;
        }
        if (mHold == HandsHold::None)
            mSince = now; // a charge that becomes the hold keeps its start: it is the same press
        mHold = hold;
    }

    float HandsHoldClock::seconds(float now) const
    {
        if (mHold == HandsHold::None || !std::isfinite(now) || now < mSince)
            return 0.f;
        return now - mSince;
    }

    PlayerHands SkyrimCombat::playerHands() const
    {
        const std::lock_guard lock(mHandsMutex);
        return mHands;
    }

    namespace
    {
        // The ward spell is the one with an AccumulateMagnitude effect (the rule of combat_magic.cpp).
        bool wardSpell(CombatData* data, const SpellRecord& spell)
        {
            if (data == nullptr)
                return false;
            for (const SpellEffect& e : spell.mEffects)
            {
                const MagicEffectRecord* record = data->effectById(e.mEffect);
                if (record != nullptr && record->mArchetype == MagicArchetype::AccumulateMagnitude)
                    return true;
            }
            return false;
        }
    }

    // Called once at the end of each combat update, on the combat thread. A spell the player HOLDS is a concentration
    // spell: charging while its cast is pending (the key is down, the spell has not gone), holding while it goes on.
    // A fire and forget spell is a tap and never a hold.
    void SkyrimCombat::trackPlayerHands()
    {
        HandsHold hold = HandsHold::None;
        const SpellRecord* spell = nullptr;
        for (const Cast& cast : mCasts)
            if (cast.mCaster.player && !cast.mShout && cast.mHeldByKey && cast.mSpell != nullptr
                && cast.mSpell->mCastType == MagicCast::Concentration)
            {
                hold = HandsHold::Charging;
                spell = cast.mSpell;
            }
        if (hold == HandsHold::None && mPlayerCastWanted && mPlayerSpell != nullptr
            && mPlayerSpell->mCastType == MagicCast::Concentration)
        {
            hold = HandsHold::Charging;
            spell = mPlayerSpell;
        }
        for (const Concentration& held : mConcentrations)
            if (held.mCaster.player && held.mSpell != nullptr)
            {
                hold = HandsHold::Holding;
                spell = held.mSpell;
            }
        mHandsClock.update(hold, mClock);

        PlayerHands hands;
        hands.bowDrawToken = mHandsBowDraws;
        hands.bowReleaseToken = mHandsBowReleases;
        hands.bowDrawing = mPlayerDrawing;
        if (mWeapon != nullptr && mWeapon->mAnimType == kWeaponAnimBow)
        {
            hands.bowDrawClip.assign(mBowSlots.clipFor(ClipSlot::BowDraw, kWeaponAnimBow));
            hands.bowReleaseClip.assign(mBowSlots.clipFor(ClipSlot::BowRelease, kWeaponAnimBow));
        }
        hands.castHold = static_cast<std::uint8_t>(hold);
        if (spell != nullptr && hold != HandsHold::None)
        {
            hands.castHoldSeconds = mHandsClock.seconds(mClock);
            hands.castHoldClip.assign(
                handsHoldClip(hold, spell->mDelivery == MagicDelivery::Self, wardSpell(mData, *spell)));
        }
        const std::lock_guard lock(mHandsMutex);
        mHands = std::move(hands);
    }
}
