#ifndef OPENEDEN_GAME_SKYRIMSE_RACE_EFFECTS_H
#define OPENEDEN_GAME_SKYRIMSE_RACE_EFFECTS_H

// Task 0248: the pure rules of the magic effects that tasks 0228, 0233, 0240 and 0241 left without a rule: the Script
// effects of the race powers (Orc Berserker Rage, Khajiit Night Eye, Wood Elf Command Animal), the Cloak (Dark Elf
// Ancestor's Wrath), Calm (Imperial Voice of the Emperor), spell absorb (Breton Dragonskin, the Atronach stone), the
// skill fortifies of the shrine blessings and the shout timer of the Talos blessing.
//
// VERIFIED on the five Skyrim SE masters (tools/skyrimse_effects_survey/survey.py; MGEF DATA +64 archetype, +8
// associated item, +136 perk to apply; PERK PRKE/EPFD):
//  - Berserker Rage (RaceOrcBerserk, 60 s) is one effect, RaceOrcBerserkEffect, archetype 1 (Script) with the perk
//    PowerOrcBerserk to apply (MGEF DATA +136). That perk has two entry points: 35 with function 3 (Multiply) and
//    EPFD 2.0 (ModAttackDamage: damage dealt twice) and 36 with function 3 and EPFD 0.5 (ModIncomingDamage: damage
//    taken half). So "double dealt, half taken" is the data's: the perk joins the caster's perk list while the effect
//    runs and the perk engine (task 0179) does the rest;
//  - Ancestor's Wrath (PowerDarkElfFlameCloak, 60 s): archetype 35 (Cloak), magnitude 10, associated item the spell
//    PowerDarkELfFlameCloakDmg (a held fire damage, 8 a second, duration 1, one condition: function 719
//    IsHostileToActor). The magnitude as a radius in feet is RECALLED-VERIFY;
//  - Voice of the Emperor (PowerImperialPacify): archetype 6 (Calm), magnitude 99 (the level gate), area 75 feet, 60 s,
//    no Hostile flag, one condition (HasKeyword);
//  - Command Animal (PowerWoodElfCommandAnimal): archetype 1, magnitude 99, area 75 feet, 60 s, five conditions
//    (IsCommandedActor == 0, GetPlayerTeammate == 0, HasKeyword 0x13798 == 1, HasKeyword 0x13796 == 0, IsUndead == 0);
//  - Night Eye (PowerKhajiitNightEye, lesser power, 60 s): archetype 1, magnitude 0 (the effect is its script);
//  - Dragonskin (PowerBretonAbsorbSpell): Peak on actor value 83 (spell absorb), magnitude 50, 60 s; the Atronach stone
//    is the same value, 50;
//  - the blessings: skills are actor values 6 + slot (Dibella 17 = Speechcraft, Nocturnal 15 = Sneak: points), the
//    "skill modifier" values are 96 + slot (Mara 112 Restoration, Stendarr 99 Block, Zenithar 107 Speechcraft: a
//    percent, task 0240), Talos is actor value 86 (the shout timer) with magnitude 0.2.
// What an effect named by its editor id does is the data's script; the rules below (what a percent skill modifier
// scales, an absorb roll, the shout timer scale, the cloak's radius) are OURS (RECALLED-VERIFY, named and tunable).
// Pure: nothing here allocates, throws or reads a clock.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace game_skyrimse::race_effects
{
    inline constexpr std::uint32_t kScriptArchetype = 1;
    inline constexpr std::uint32_t kCalmArchetype = 6;
    inline constexpr std::uint32_t kCloakArchetype = 35;
    /// The actor value of spell absorb and of the shout timer (VERIFIED: Dragonskin, the Atronach stone, the Talos
    /// blessing).
    inline constexpr std::int32_t kAbsorbValue = 83;
    inline constexpr std::int32_t kShoutTimerValue = 86;

    /// What a Script (archetype 1) effect is, by the editor id of its magic effect (VERIFIED names).
    enum class ScriptKind
    {
        None,
        Berserk, ///< RaceOrcBerserkEffect: the perk to apply does the work
        NightEye, ///< NightEyeEffect: a vision status
        CommandAnimal, ///< RaceWoodElfCommandAnimal: an animal fights on the caster's side
    };

    /// `archetype` must be 1; the editor id is compared without regard to case. Anything else is None.
    [[nodiscard]] ScriptKind scriptKindOf(std::uint32_t archetype, std::string_view editorId);

    /// A skill's actor value is 6 + slot (6 .. 23) and its percent modifier 96 + slot (96 .. 113); -1 for neither.
    [[nodiscard]] int skillPointsSlot(std::int32_t actorValue);
    [[nodiscard]] int skillPowerSlot(std::int32_t actorValue);

    /// The effective value of a skill: (base + points) scaled by 1 + power / 100, never below 0 and at most 255. A
    /// number that is not finite counts as 0. OURS: a percent skill modifier makes the skill's effect stronger.
    [[nodiscard]] float effectiveSkill(float base, float points, float powerPercent);

    /// The chance in percent (0 .. 100) that a hostile spell is absorbed, from the held ability's sum and the timed
    /// effects' sum on actor value 83. A number that is not finite counts as 0.
    [[nodiscard]] float absorbChancePercent(float heldPercent, float timedPercent);
    /// A roll in [0, 100) absorbs when it is below the chance; a chance of 0 never does (and the caller draws no roll).
    [[nodiscard]] bool absorbs(float chancePercent, float roll);

    /// The factor on a shout's recovery time from the sum of the shout timer effects (0.2 shortens it by 20 percent): 1
    /// - sum, kept between kMinShoutRecoveryScale and kMaxShoutRecoveryScale. OURS.
    inline constexpr float kMinShoutRecoveryScale = 0.1f;
    inline constexpr float kMaxShoutRecoveryScale = 2.f;
    [[nodiscard]] float shoutRecoveryScale(float timerSum);

    /// A cloak's radius in game units from the effect's magnitude read as feet (21.33 units a foot, as an area); 0 for
    /// a magnitude that is not above 0 or not finite, at most kMaxCloakRadius.
    inline constexpr float kMaxCloakRadius = 10000.f;
    [[nodiscard]] float cloakRadiusUnits(float magnitudeFeet);

    /// What a cloak's held damage does in `seconds`: perSecond x seconds, 0 for anything not finite or not above 0.
    [[nodiscard]] float cloakDamage(float perSecond, float seconds);
}

#endif
