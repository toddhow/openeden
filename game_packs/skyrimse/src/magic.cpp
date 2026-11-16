#include <game_skyrimse/magic.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }
    }

    bool isVitalValue(std::int32_t value)
    {
        return value >= 24 && value <= 26;
    }

    EffectTiming effectTiming(std::uint32_t spellCastType, const SpellEffect& effect, const MagicEffectRecord& record)
    {
        if (spellCastType == MagicCast::Concentration)
            return EffectTiming::PerSecond;
        if (effect.mDuration == 0)
            return EffectTiming::Instant;
        if (isVitalValue(record.mPrimaryValue) && (record.mFlags & MagicFlag::Recover) == 0)
            return EffectTiming::PerSecond;
        return EffectTiming::Timed;
    }

    float signedMagnitude(float magnitude, std::uint32_t mgefFlags)
    {
        const float value = finiteOr(magnitude, 0.f);
        return (mgefFlags & MagicFlag::Detrimental) != 0 ? -value : value;
    }

    float resistedMagnitude(float magnitude, float resistPercent)
    {
        const float value = finiteOr(magnitude, 0.f);
        const float resist = std::clamp(finiteOr(resistPercent, 0.f), 0.f, kMaxResistPercent);
        return value * (1.f - resist / 100.f);
    }

    float wardMaxPower(float peakMagnitude, const MagicSettings& settings, float skill)
    {
        const float peak = finiteOr(peakMagnitude, 0.f);
        if (peak <= 0.f)
            return 0.f;
        const float factor = finiteOr(settings.mWardPowerMaxBase, 0.f)
            + finiteOr(settings.mWardPowerMaxMult, 0.f) * finiteOr(skill, 0.f);
        return std::max(0.f, peak * factor);
    }

    float wardCharge(float power, float maxPower, float ratePerSecond, float seconds)
    {
        const float maximum = std::max(0.f, finiteOr(maxPower, 0.f));
        const float current = std::max(0.f, finiteOr(power, 0.f));
        const float rate = finiteOr(ratePerSecond, 0.f);
        const float dt = finiteOr(seconds, 0.f);
        if (rate <= 0.f || dt <= 0.f)
            return std::min(current, maximum);
        return std::min(maximum, current + rate * dt);
    }

    float wardAbsorb(float& power, float incoming)
    {
        if (!std::isfinite(incoming) || incoming <= 0.f)
            return 0.f;
        if (!std::isfinite(power) || power <= 0.f)
            return incoming;
        const float taken = std::min(power, incoming);
        power -= taken;
        return incoming - taken;
    }

    bool EffectList::add(ActiveEffect effect)
    {
        if (!(effect.mRemaining > 0.f) || !std::isfinite(effect.mRemaining) || mEffects.size() >= kMaxActiveEffects)
        {
            ++mRefused;
            return false;
        }
        effect.mId = mNext++;
        mEffects.push_back(effect);
        return true;
    }

    std::size_t EffectList::removeTarget(const openeden::sdk::game::CombatSubject& target)
    {
        return std::erase_if(mEffects, [&](const ActiveEffect& e) { return e.mTarget == target; });
    }

    std::size_t EffectList::removeCaster(const openeden::sdk::game::CombatSubject& caster)
    {
        return std::erase_if(
            mEffects, [&](const ActiveEffect& e) { return e.mCaster == caster || e.mTarget == caster; });
    }

    bool deliveryIsCast(std::uint32_t delivery, bool beamsOn, bool deliveryOn)
    {
        switch (delivery)
        {
            case MagicDelivery::Self:
            case MagicDelivery::Aimed:
                return true;
            case MagicDelivery::Touch:
                return beamsOn;
            case MagicDelivery::TargetActor:
            case MagicDelivery::TargetLocation:
                return deliveryOn;
            default:
                return false;
        }
    }
}
