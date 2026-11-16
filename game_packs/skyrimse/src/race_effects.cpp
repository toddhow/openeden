// Task 0248: see game_skyrimse/race_effects.hpp.
#include "game_skyrimse/race_effects.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse::race_effects
{
    namespace
    {
        constexpr float kUnitsPerFoot = 21.33f;
        constexpr float kMaxSkillValue = 255.f;

        bool sameText(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
                const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
                if (x != y)
                    return false;
            }
            return true;
        }

        float finite(float v)
        {
            return std::isfinite(v) ? v : 0.f;
        }
    }

    ScriptKind scriptKindOf(std::uint32_t archetype, std::string_view editorId)
    {
        if (archetype != kScriptArchetype)
            return ScriptKind::None;
        if (sameText(editorId, "RaceOrcBerserkEffect"))
            return ScriptKind::Berserk;
        if (sameText(editorId, "NightEyeEffect"))
            return ScriptKind::NightEye;
        if (sameText(editorId, "RaceWoodElfCommandAnimal"))
            return ScriptKind::CommandAnimal;
        return ScriptKind::None;
    }

    int skillPointsSlot(std::int32_t actorValue)
    {
        return actorValue >= 6 && actorValue <= 23 ? actorValue - 6 : -1;
    }

    int skillPowerSlot(std::int32_t actorValue)
    {
        return actorValue >= 96 && actorValue <= 113 ? actorValue - 96 : -1;
    }

    float effectiveSkill(float base, float points, float powerPercent)
    {
        const float sum = finite(base) + finite(points);
        const float scale = std::max(0.f, 1.f + finite(powerPercent) / 100.f);
        return std::clamp(sum * scale, 0.f, kMaxSkillValue);
    }

    float absorbChancePercent(float heldPercent, float timedPercent)
    {
        return std::clamp(finite(heldPercent) + finite(timedPercent), 0.f, 100.f);
    }

    bool absorbs(float chancePercent, float roll)
    {
        return std::isfinite(chancePercent) && std::isfinite(roll) && chancePercent > 0.f && roll < chancePercent;
    }

    float shoutRecoveryScale(float timerSum)
    {
        return std::clamp(1.f - finite(timerSum), kMinShoutRecoveryScale, kMaxShoutRecoveryScale);
    }

    float cloakRadiusUnits(float magnitudeFeet)
    {
        if (!std::isfinite(magnitudeFeet) || !(magnitudeFeet > 0.f))
            return 0.f;
        return std::min(magnitudeFeet * kUnitsPerFoot, kMaxCloakRadius);
    }

    float cloakDamage(float perSecond, float seconds)
    {
        if (!std::isfinite(perSecond) || !std::isfinite(seconds) || !(perSecond > 0.f) || !(seconds > 0.f))
            return 0.f;
        return perSecond * seconds;
    }
}
