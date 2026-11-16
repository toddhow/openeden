// Task 0233: see game_skyrimse/magic_status.hpp.
#include "game_skyrimse/magic_status.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    bool isResistValue(std::int32_t value)
    {
        return value >= ResistValue::Poison && value <= ResistValue::Disease;
    }

    int regenVital(std::int32_t value)
    {
        switch (value)
        {
            case RegenValue::HealthRate:
            case RegenValue::HealthRateMult:
                return 0;
            case RegenValue::MagickaRate:
            case RegenValue::MagickaRateMult:
                return 1;
            case RegenValue::StaminaRate:
            case RegenValue::StaminaRateMult:
                return 2;
            default:
                return -1;
        }
    }

    bool isRegenMultValue(std::int32_t value)
    {
        return value >= RegenValue::HealthRateMult && value <= RegenValue::StaminaRateMult;
    }

    bool isRegenRateValue(std::int32_t value)
    {
        return regenVital(value) >= 0;
    }

    float effectiveRegenRate(float baseRate, float rateSum, float multPercentSum)
    {
        if (!std::isfinite(baseRate) || !std::isfinite(rateSum) || !std::isfinite(multPercentSum))
            return 0.f;
        const float add = std::clamp(rateSum, -kMaxRegenRate, kMaxRegenRate);
        const float mult = std::clamp(multPercentSum, -100.f, kMaxRegenRate);
        return std::max(0.f, (baseRate + add) * (1.f + mult / 100.f));
    }

    bool levelGateAllows(std::uint32_t level, float magnitude)
    {
        return std::isfinite(magnitude) && static_cast<float>(level) <= magnitude;
    }

    float weaknessPercent(float signedModifierSum)
    {
        if (!std::isfinite(signedModifierSum) || !(signedModifierSum < 0.f))
            return 0.f;
        return std::min(-signedModifierSum, kMaxWeaknessPercent);
    }

    float weakened(float magnitude, float weaknessPercent)
    {
        if (!std::isfinite(magnitude))
            return 0.f;
        const float w = std::isfinite(weaknessPercent) ? std::clamp(weaknessPercent, 0.f, kMaxWeaknessPercent) : 0.f;
        return magnitude * (1.f + w / 100.f);
    }

    float extendStatus(float until, float now, float seconds)
    {
        if (!std::isfinite(seconds) || !(seconds > 0.f) || !std::isfinite(now))
            return until;
        const float end = now + std::min(seconds, kMaxStatusSeconds);
        return until < 0.f ? end : std::max(until, end);
    }

    bool statusActive(float until, float now)
    {
        return until >= 0.f && now < until;
    }
}
