// Crossbows, real bows and dual wielding in the combat runtime (task 0300): the kit a fighter shoots with, the dual
// wielder test, the crossbow's reload and the player's aim time. The pure rules are in combat_ranged.cpp.
#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
    }

    bool SkyrimCombat::setTestPlayerOffHand(std::string_view editorId)
    {
        mPlayerOffHand = nullptr;
        mPlayerOffHandLooked = false;
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestPlayerOffHand.clear();
            return false;
        }
        mTestPlayerOffHand.assign(editorId);
        for (char& c : mTestPlayerOffHand)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        return true;
    }

    SkyrimCombat::RangedKit SkyrimCombat::rangedKitFor(const Fighter& fighter) const
    {
        RangedKit kit;
        // The test weapon first: every run made before this task, and every test that names one, is unchanged.
        if (mWeapon != nullptr && isRangedAnimType(mWeapon->mAnimType))
        {
            kit.mBow = mWeapon;
            kit.mAmmo = mAmmo;
            kit.mProjectile = mProjectile;
            return kit;
        }
        if (!mRangedGear || mData == nullptr || mWeapon != nullptr)
            return kit;
        if (fighter.mSubject.player)
        {
            if (mEquip.mWeapon == nullptr || !isRangedAnimType(mEquip.mWeapon->mAnimType))
                return kit;
            kit.mBow = mEquip.mWeapon;
            if (mEquip.mAmmo != nullptr && ammoFitsWeapon(*mEquip.mWeapon, *mEquip.mAmmo))
            {
                kit.mAmmo = mEquip.mAmmo;
                kit.mAmmoItem = mEquip.mAmmoItem;
                kit.mProjectile = mData->projectileOf(*mEquip.mAmmo);
            }
            return kit;
        }
        if (!mRealGear || !fighter.mGear.mKnown || fighter.mGear.mBow == nullptr || fighter.mGear.mAmmo == nullptr)
            return kit;
        kit.mBow = fighter.mGear.mBow;
        kit.mAmmo = fighter.mGear.mAmmo;
        kit.mProjectile = mData->projectileOf(*fighter.mGear.mAmmo);
        return kit;
    }

    bool SkyrimCombat::isDualWielding(const Fighter& fighter) const
    {
        if (!mDualWield && mTestPlayerOffHand.empty())
            return false;
        if (fighter.mSubject.player)
        {
            // The player has no left-hand slot in the inventory yet: the test weapon is his left hand.
            if (mPlayerOffHand == nullptr || mBeast)
                return false;
            const WeaponRecord* main = mWeapon != nullptr ? mWeapon : mEquip.mWeapon;
            return main != nullptr && dualWieldAllowed(CombatStyleFlag::AllowDualWielding, main, mPlayerOffHand);
        }
        if (!mDualWield || !mRealGear || !fighter.mGear.mKnown || fighter.mGear.mOffHand == nullptr || mWeapon != nullptr)
            return false;
        return dualWieldAllowed(fighter.mProfile.mAi.mStyle.mFlags, fighter.mGear.mWeapon, fighter.mGear.mOffHand);
    }

    void SkyrimCombat::beginReload(CombatRequestWriter& writer, Fighter& fighter)
    {
        const RangedKit kit = rangedKitFor(fighter);
        fighter.mBow = Fighter::Bow::None;
        if (kit.mBow == nullptr || !kit.crossbow())
            return;
        const std::string_view clip = crossbowClipsFor(*kit.mBow).mReload;
        openeden::sdk::game::ClipRequest request;
        request.slot = ClipSlot::BowDraw;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.05f;
        request.token = mNextToken++;
        if (!writer.playClip(fighter.mSubject, request))
            return;
        fighter.mBow = Fighter::Bow::Reloading;
        fighter.mBowToken = request.token;
        if (mTrace)
            trace("COMBAT crossbow reload " + std::string(fighter.mSubject.player ? "player" : "actor")
                + (fighter.mSubject.player ? std::string() : std::to_string(fighter.mSubject.actor.reference))
                + " token " + std::to_string(request.token));
    }

    // The aim clip's length is the crossbow's draw time: it has no variable draw and no sidecar events in the five
    // masters (the DLC project files are not installed), so the clip's own length (from the decoded clip) is used. 0
    // until the engine has read it (asked again next frame).
    float SkyrimCombat::playerAimSeconds(CombatRequestWriter& writer, const RangedKit& kit)
    {
        if (kit.mBow == nullptr || !kit.crossbow())
            return 0.f;
        const std::string_view clip = crossbowClipsFor(*kit.mBow).mAim;
        if (mPlayerAimClip != clip)
        {
            mPlayerAimClip.assign(clip);
            mPlayerAimTime = 0.f;
        }
        openeden::sdk::game::ClipTiming timing;
        if (!(mPlayerAimTime > 0.f) && writer.clipTiming(clip, false, timing) && timing.length > 0.f)
            mPlayerAimTime = timing.length;
        // The engine reads a clip on a worker the first time it is asked about it: ask for the reload clip now, so its
        // length is known by the first shot.
        (void)writer.clipTiming(crossbowClipsFor(*kit.mBow).mReload, false, timing);
        return mPlayerAimTime;
    }
}
