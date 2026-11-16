// Poisons on weapons: the pure part (task 0229). See poison.hpp.
#include "game_skyrimse/poison.hpp"

#include <algorithm>

namespace game_skyrimse::crafting
{
    CoatStatus PoisonCoatings::apply(FormId weapon, WeaponPoison poison)
    {
        if (weapon == kNullFormId || poison.mEffects.empty() || poison.mEffects.size() > kMaxEffects)
            return CoatStatus::BadArgument;
        if (poison.mCharges <= 0)
            return CoatStatus::NoCharges;
        if (find(weapon) != nullptr)
            return CoatStatus::AlreadyPoisoned;
        if (mCoats.size() >= kMax)
            return CoatStatus::BadArgument;
        mCoats.push_back({ weapon, std::move(poison) });
        return CoatStatus::Ok;
    }

    const WeaponPoison* PoisonCoatings::find(FormId weapon) const
    {
        for (const Entry& e : mCoats)
            if (e.mWeapon == weapon)
                return &e.mPoison;
        return nullptr;
    }

    int PoisonCoatings::spend(FormId weapon)
    {
        for (auto it = mCoats.begin(); it != mCoats.end(); ++it)
        {
            if (it->mWeapon != weapon)
                continue;
            const int left = std::max(0, it->mPoison.mCharges - 1);
            if (left == 0)
                mCoats.erase(it);
            else
                it->mPoison.mCharges = left;
            return left;
        }
        return -1;
    }

    SpellRecord poisonSpell(const WeaponPoison& poison)
    {
        SpellRecord spell;
        spell.mCommon.mId = poison.mPoison;
        spell.mCommon.mEditorId = poison.mName;
        spell.mHasData = true;
        spell.mCastType = 1; // MagicCast::FireAndForget (SPIT +16)
        spell.mDelivery = 1; // MagicDelivery::Touch (SPIT +20)
        for (const EffectEntry& e : poison.mEffects)
        {
            SpellEffect effect;
            effect.mEffect = e.mEffect;
            effect.mMagnitude = e.mMagnitude;
            effect.mArea = e.mArea;
            effect.mDuration = e.mDuration;
            spell.mEffects.push_back(effect);
        }
        return spell;
    }
}
