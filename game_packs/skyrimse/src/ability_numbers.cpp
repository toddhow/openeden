// Task 0241: see game_skyrimse/ability_numbers.hpp.
#include "game_skyrimse/ability_numbers.hpp"

#include "game_skyrimse/magic.hpp"
#include "game_skyrimse/magic_status.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace game_skyrimse
{
    namespace
    {
        constexpr std::int32_t kHealthValue = 24, kMagickaValue = 25, kStaminaValue = 26, kCarryValue = 32,
                               kAbsorbValue = 83;

        float cut(float v)
        {
            return std::isfinite(v) ? std::clamp(v, -kMaxAbilityNumber, kMaxAbilityNumber) : 0.f;
        }

        bool differs(float a, float b)
        {
            return !(a == b);
        }

        // A resistance's net share in percent, 0 .. kMaxResistPercent; a negative net is a weakness, not a resistance.
        float positive(const AbilityNumbers& n, int index)
        {
            return std::clamp(n.mResist[index], 0.f, kMaxResistPercent);
        }

        float negative(const AbilityNumbers& n, int index)
        {
            return n.mResist[index] < 0.f ? -n.mResist[index] : 0.f;
        }

        // Does the magic resistance (value 44) also apply to an effect with this resist value? Not to poison and
        // disease (RECALLED-VERIFY), and not twice to magic itself.
        bool magicApplies(std::int32_t resistValue)
        {
            return resistValue != ResistValue::Poison && resistValue != ResistValue::Disease
                && resistValue != ResistValue::Magic;
        }
    }

    bool AbilityNumbers::any() const
    {
        if (mCarry != 0.f || mAbsorb != 0.f)
            return true;
        for (int i = 0; i < 3; ++i)
            if (mVital[i] != 0.f || mRegenRate[i] != 0.f || mRegenMult[i] != 0.f)
                return true;
        for (const float r : mResist)
            if (r != 0.f)
                return true;
        return false;
    }

    bool AbilityNumbers::hasRegen() const
    {
        for (int i = 0; i < 3; ++i)
            if (mRegenRate[i] != 0.f || mRegenMult[i] != 0.f)
                return true;
        return false;
    }

    bool AbilityNumbers::operator==(const AbilityNumbers& o) const
    {
        if (differs(mCarry, o.mCarry) || differs(mAbsorb, o.mAbsorb))
            return false;
        for (int i = 0; i < 3; ++i)
            if (differs(mVital[i], o.mVital[i]) || differs(mRegenRate[i], o.mRegenRate[i])
                || differs(mRegenMult[i], o.mRegenMult[i]))
                return false;
        for (int i = 0; i < 6; ++i)
            if (differs(mResist[i], o.mResist[i]))
                return false;
        return true;
    }

    AbilityNumbers sumAbilityNumbers(const PerkLibrary& library, std::span<const FormId> spells)
    {
        // sorted and unique: the sums (floats) come out the same whatever order the caller lists them in
        std::vector<FormId> ids(spells.begin(), spells.end());
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        AbilityNumbers n;
        for (const FormId id : ids)
        {
            const SpellRecord* spell = library.spell(id);
            if (spell == nullptr || !spell->mHasData)
                continue;
            const bool ability = spell->isAbility();
            for (const SpellEffect& effect : spell->mEffects)
            {
                const MagicEffectRecord* record = library.magicEffect(effect.mEffect);
                if (record == nullptr || !record->modifiesAValue())
                    continue;
                if (!ability
                    && (record->mArchetype != MagicArchetype::PeakValueModifier || effect.mDuration == 0
                        || (record->mFlags & MagicFlag::Hostile) != 0))
                    continue; // only a timed Fortify counts for a spell that is no ability
                const float v = signedMagnitude(effect.mMagnitude, record->mFlags);
                switch (record->mPrimaryValue)
                {
                    case kHealthValue:
                        n.mVital[0] += v;
                        break;
                    case kMagickaValue:
                        n.mVital[1] += v;
                        break;
                    case kStaminaValue:
                        n.mVital[2] += v;
                        break;
                    case kCarryValue:
                        n.mCarry += v;
                        break;
                    case kAbsorbValue:
                        n.mAbsorb += v;
                        break;
                    default:
                    {
                        const std::int32_t value = record->mPrimaryValue;
                        if (isResistValue(value))
                            n.mResist[value - ResistValue::Poison] += v;
                        else if (regenVital(value) >= 0)
                            (isRegenMultValue(value) ? n.mRegenMult : n.mRegenRate)[regenVital(value)] += v;
                        break;
                    }
                }
            }
        }
        for (float& v : n.mVital)
            v = cut(v);
        for (float& v : n.mRegenRate)
            v = cut(v);
        for (float& v : n.mRegenMult)
            v = cut(v);
        for (float& v : n.mResist)
            v = cut(v);
        n.mCarry = cut(n.mCarry);
        n.mAbsorb = cut(n.mAbsorb);
        return n;
    }

    float abilityResistPercent(const AbilityNumbers& numbers, std::int32_t resistValue)
    {
        const float own = isResistValue(resistValue) ? positive(numbers, resistValue - ResistValue::Poison) : 0.f;
        const float magic
            = magicApplies(resistValue) ? positive(numbers, ResistValue::Magic - ResistValue::Poison) : 0.f;
        const float combined = 100.f * (1.f - (1.f - own / 100.f) * (1.f - magic / 100.f));
        return std::clamp(combined, 0.f, kMaxResistPercent);
    }

    float abilityWeaknessPercent(const AbilityNumbers& numbers, std::int32_t resistValue)
    {
        float sum = isResistValue(resistValue) ? negative(numbers, resistValue - ResistValue::Poison) : 0.f;
        if (magicApplies(resistValue))
            sum += negative(numbers, ResistValue::Magic - ResistValue::Poison);
        return weaknessPercent(-sum);
    }
}
