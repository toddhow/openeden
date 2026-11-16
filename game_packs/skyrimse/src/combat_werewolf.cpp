#include "game_skyrimse/combat.hpp"

// Task 0259 (werewolf): the beast form in the combat rules. While it lasts the player fights with the race's claws (his
// profile's bare-hand numbers and sounds are the beast's, his creature row is `werewolf::playerProfile()`, so the swing
// and its timing are the werewolf's clips), and an equipped weapon is ignored (`weaponFor`). The form itself (the clock,
// the body, feeding) is the pack's glue; this file only changes how he fights.

namespace game_skyrimse
{
    void SkyrimCombat::applyBeastToProfile(Fighter& player)
    {
        if (!player.mHasProfile)
            return;
        FighterProfile& profile = player.mProfile;
        if (mBeast)
        {
            profile.mUnarmedDamage = mBeastForm.mUnarmedDamage;
            profile.mUnarmedReach = mBeastForm.mUnarmedReach;
            if (mBeastForm.mBodyMaterial != kNullFormId)
                profile.mBodyMaterial = mBeastForm.mBodyMaterial;
            if (mBeastForm.mUnarmedImpactSet != kNullFormId)
                profile.mUnarmedImpactSet = mBeastForm.mUnarmedImpactSet;
            profile.mCreature = &werewolf::playerProfile();
            return;
        }
        // Human again: the stand-in's own bare hands and body.
        if (mData != nullptr)
        {
            const FighterProfile human = mData->playerFighter();
            profile.mUnarmedDamage = human.mUnarmedDamage;
            profile.mUnarmedReach = human.mUnarmedReach;
            profile.mBodyMaterial = human.mBodyMaterial;
            profile.mUnarmedImpactSet = human.mUnarmedImpactSet;
        }
        profile.mCreature = nullptr;
    }

    void SkyrimCombat::setPlayerBeast(const BeastForm* form)
    {
        const bool was = mBeast;
        mBeast = form != nullptr;
        if (mBeast)
            mBeastForm = *form;
        // The swing timing is per clip: the old clips' numbers do not belong to the new ones.
        mHaveTiming[0] = mHaveTiming[1] = false;
        mPlayerTimingKicked = false;
        mPlayerTimingSince = -1.f;
        if (was != mBeast)
            trace(std::string("COMBAT player beast form ") + (mBeast ? "on" : "off"));
        if (Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer()))
            if (player->mReady)
                applyBeastToProfile(*player);
    }
}
