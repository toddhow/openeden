// Poisons on weapons: the pure part (task 0229). See poison.hpp.
#include "game_skyrimse/poison.hpp"

#include <algorithm>
#include <cmath>

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

    // ---- the saved form (task 0233) ----------------------------------------------------------------------------

    void PoisonCoatings::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mCoats.size()));
        for (const Entry& e : mCoats)
        {
            writer.writeValue(e.mWeapon);
            writer.writeValue(e.mPoison.mPoison);
            writer.writeString(e.mPoison.mName);
            writer.writeValue(e.mPoison.mCharges);
            writer.writeValue(static_cast<std::uint8_t>(e.mPoison.mEffects.size()));
            for (const EffectEntry& effect : e.mPoison.mEffects)
            {
                writer.writeValue(effect.mEffect);
                writer.writeValue(effect.mMagnitude);
                writer.writeValue(effect.mArea);
                writer.writeValue(effect.mDuration);
            }
        }
    }

    bool PoisonCoatings::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        const auto bad = [&](const char* why) {
            error = why;
            return false;
        };
        std::uint32_t count = 0;
        if (!reader.readValue(count) || count > kMax)
            return bad("the coating count is damaged");
        std::vector<Entry> coats;
        coats.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Entry e;
            std::uint8_t effects = 0;
            if (!reader.readValue(e.mWeapon) || !reader.readValue(e.mPoison.mPoison)
                || !reader.readString(e.mPoison.mName) || !reader.readValue(e.mPoison.mCharges)
                || !reader.readValue(effects))
                return bad("a coating is cut short");
            if (e.mWeapon == kNullFormId || e.mPoison.mName.size() > kMaxName || e.mPoison.mCharges < 1
                || e.mPoison.mCharges > kMaxCharges || effects < 1 || effects > kMaxEffects)
                return bad("a coating is damaged");
            for (std::uint8_t k = 0; k < effects; ++k)
            {
                EffectEntry effect;
                if (!reader.readValue(effect.mEffect) || !reader.readValue(effect.mMagnitude)
                    || !reader.readValue(effect.mArea) || !reader.readValue(effect.mDuration))
                    return bad("an effect is cut short");
                if (!std::isfinite(effect.mMagnitude))
                    return bad("an effect is damaged");
                e.mPoison.mEffects.push_back(effect);
            }
            for (const Entry& other : coats)
                if (other.mWeapon == e.mWeapon)
                    return bad("a weapon is listed twice");
            coats.push_back(std::move(e));
        }
        if (reader.remaining() != 0)
            return bad("the chunk has trailing bytes");
        mCoats = std::move(coats);
        return true;
    }

    bool PoisonChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        bool ok = true;
        if (version != kVersion)
        {
            error = "the poisons chunk has version " + std::to_string(version) + ", this build reads "
                + std::to_string(kVersion);
            ok = false;
        }
        else
            ok = mCoatings.read(reader, error);
        if (!ok)
        {
            mRefusal = error;
            if (mSink)
                mSink(error);
            return false;
        }
        mRefusal.clear();
        if (mRestored)
            mRestored(mCoatings.size());
        return true;
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
