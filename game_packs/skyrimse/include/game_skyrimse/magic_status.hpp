#ifndef OPENEDEN_GAME_SKYRIMSE_MAGIC_STATUS_H
#define OPENEDEN_GAME_SKYRIMSE_MAGIC_STATUS_H

// Task 0233: the pure part of the effects that change what an actor can do (paralysis, frenzy, fear) and of weakness to
// an element. Nothing here allocates, throws or reads a clock: SkyrimCombat (combat_status.cpp) owns the actors and
// calls these with its own combat clock.
//
// VERIFIED on the five Skyrim SE masters (a read of every MGEF, DATA +64 archetype, +68 primary actor value):
//  - Paralysis is archetype 21 on actor value 53 (AlchParalysis, ParalysisFFContact, PerkDeepFreezeParalysis...);
//  - Frenzy is archetype 8 (AlchInfluenceAggUp: aggression up), Calm is 6 (AlchInfluenceAggDown);
//  - Fear is archetype 7 (Demoralize, "confidence down": AlchInfluenceConfDown, InfluenceConfDownFFAimed (the Fear
//    spell), HowlWerewolfFearEffect1..3; 21 effects); the MGEFs of Paralysis, Frenzy and Fear carry 2 to 6 CTDA
//    conditions each (functions 560, 244, 700, 715, 365: target immunities and states) that the magic
//    runtime evaluates since task 0236 (magic_conditions.hpp; before it skipped such an effect unless
//    `combat test spell conditions` was set, task 0186);
//  - a weakness is archetype 34 (Peak) with the Hostile, Recover and Detrimental flags (0x...07) on the resistance's
//    actor value: poison 40, fire 41, shock 42, frost 43, magic 44 (disease 45): AlchWeaknessFire, AlchWeaknessShock,
//    AlchWeaknessFrost, AlchWeaknessMagic, AlchWeaknessPoison.
// How the numbers combine (the level gate, the weakness cap and the way a weakness adds to a hit) is RECALLED-VERIFY
// and ours: plausible, named and tunable.

#include <cstdint>

namespace game_skyrimse
{
    /// MGEF DATA +64 of the effects that change an actor's behaviour (VERIFIED, see above).
    namespace StatusArchetype
    {
        inline constexpr std::uint32_t Calm = 6, Demoralize = 7, Frenzy = 8, Paralysis = 21;
    }

    /// Actor values of the resistances (MGEF DATA +68 of the weakness effects, VERIFIED).
    namespace ResistValue
    {
        inline constexpr std::int32_t Poison = 40, Fire = 41, Shock = 42, Frost = 43, Magic = 44, Disease = 45;
    }

    /// Task 0238: the actor values that change how fast a vital regenerates (MGEF DATA +68 of the Fortify and Damage
    /// "Rate" effects, VERIFIED by the five masters: the race powers Histskin (27), Highborn (28), Adrenaline Rush (29);
    /// the alchemy and enchanting Fortify Health, Magicka and Stamina Rate effects are on 155, 156 and 157, and the
    /// poisons that damage a rate are archetype 0 on the same values). The names (HealRate, MagickaRate, StaminaRate and
    /// the three Mult values) are RECALLED-VERIFY; which effect uses which value is the data's.
    namespace RegenValue
    {
        inline constexpr std::int32_t HealthRate = 27, MagickaRate = 28, StaminaRate = 29, HealthRateMult = 155,
                                      MagickaRateMult = 156, StaminaRateMult = 157;
    }

    /// 0 health, 1 magicka, 2 stamina; -1 when `value` is no regeneration value.
    [[nodiscard]] int regenVital(std::int32_t value);
    /// True for the three "Mult" values (155 to 157): their magnitude is a percentage of the rate, not an amount.
    [[nodiscard]] bool isRegenMultValue(std::int32_t value);
    [[nodiscard]] bool isRegenRateValue(std::int32_t value);

    /// The rate a vital regenerates at (percent of its maximum a second), OURS: (base + sum of the rate effects) x (1 +
    /// sum of the Mult effects / 100), never below 0 and 0 when anything is not finite. The race powers' numbers fit it
    /// (RECALLED-VERIFY, see the task doc): the base is 0.7, 3 and 5, Histskin adds 10 (14 times), Highborn 25 (9 times)
    /// and Adrenaline Rush 50 (11 times; the in-game text is recalled as "ten times faster").
    [[nodiscard]] float effectiveRegenRate(float baseRate, float rateSum, float multPercentSum);

    /// The most a regeneration rate effect is let to add in total (a bad record cannot make an actor heal at once).
    inline constexpr float kMaxRegenRate = 1000.f;

    /// The most a weakness adds to a hit, in percent (OURS: 100 doubles it).
    inline constexpr float kMaxWeaknessPercent = 100.f;
    /// The longest a status is kept, in seconds (a bad duration cannot hold an actor for ever).
    inline constexpr float kMaxStatusSeconds = 3600.f;

    /// True for the actor values 40 to 45 (the resistances a weakness changes).
    [[nodiscard]] bool isResistValue(std::int32_t value);

    /// Fear, frenzy and calm only work on a target whose level is at or below the effect's magnitude (RECALLED-VERIFY:
    /// the game's own rule). A magnitude that is not finite lets nothing through.
    [[nodiscard]] bool levelGateAllows(std::uint32_t level, float magnitude);

    /// The weakness in percent from the signed sum of the Peak modifiers on a resistance (a weakness is negative: the
    /// effect is Detrimental): 0 when the sum is not negative or not finite, at most kMaxWeaknessPercent.
    [[nodiscard]] float weaknessPercent(float signedModifierSum);

    /// A hit's magnitude with a weakness added: magnitude x (1 + weakness / 100). The weakness is clamped to 0 to
    /// kMaxWeaknessPercent; a magnitude that is not finite becomes 0.
    [[nodiscard]] float weakened(float magnitude, float weaknessPercent);

    /// The time a status ends at: the later of the one it has now and now + seconds. `until` below 0 means "not
    /// active". A duration that is not finite or not above 0 changes nothing; one above kMaxStatusSeconds is cut.
    [[nodiscard]] float extendStatus(float until, float now, float seconds);

    /// Is a status that ends at `until` still on at `now`?
    [[nodiscard]] bool statusActive(float until, float now);

    /// The statuses of one actor (all "not active" at first).
    struct ActorStatus
    {
        float mParalyzedUntil = -1.f;
        float mFrenzyUntil = -1.f;
        float mFearUntil = -1.f;
        float mFrenzyRetargetAt = 0.f; ///< the combat clock at which a frenzied actor looks again for a target
        bool mParalysisClipHeld = false; ///< the held fall clip is playing and must be ended by a recovery clip
    };
}

#endif
