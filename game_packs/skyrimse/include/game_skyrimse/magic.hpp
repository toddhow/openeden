#ifndef OPENEDEN_GAME_SKYRIMSE_MAGIC_H
#define OPENEDEN_GAME_SKYRIMSE_MAGIC_H

// The pure part of the Skyrim SE magic runtime (task 0186, milestone M-S5 stage S5-8, decision 0018): the numbers of an
// effect (timing, sign, resistance), the ward, and the list of effects that are running. No engine, renderer, Jolt, Qt
// or OSG type and no allocation after construction: SkyrimCombat (combat.hpp) owns the casting and the targets and
// uses these.
//
// Everything about HOW the numbers combine is RECALLED-VERIFY (plausible and tunable): the archetype numbers are
// VERIFIED on the five masters by SkyrimMagicRealData (Healing, Firebolt, the wards, Frostbite, Sparks), the rest is
// the engine's own and not in the data.

#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/perks.hpp>

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace game_skyrimse
{
    /// MGEF DATA +64 (VERIFIED by the data: ValueModifier 0 on Firebolt's damage, Dual 5 on Frostbite and Sparks,
    /// Demoralize 7 on the Intense Flames effect, AccumulateMagnitude 32 on WardConcSelf0, Stagger 33, Peak 34 on
    /// ShieldConcSelf, and the counts of SkyrimMagicRealData over every spell effect equal the histogram of note 0056:
    /// 603 ValueModifier, 205 Dual, 139 Stagger, 127 Absorb, 500 Peak; what Absorb DOES is RECALLED-VERIFY).
    namespace MagicArchetype
    {
        inline constexpr std::uint32_t ValueModifier = 0, Absorb = 4, Dual = 5, Demoralize = 7,
                                       AccumulateMagnitude = 32, Stagger = 33, PeakValueModifier = 34;
    }

    /// MGEF DATA +0 flags (CommonLibSSE-NG; the data agrees: Firebolt's damage 0x10221805 has Hostile and Detrimental).
    namespace MagicFlag
    {
        inline constexpr std::uint32_t Hostile = 0x1, Recover = 0x2, Detrimental = 0x4;
    }

    /// SPIT +16 and MGEF +80: how a spell is cast.
    namespace MagicCast
    {
        inline constexpr std::uint32_t Constant = 0, FireAndForget = 1, Concentration = 2;
    }

    /// SPIT +20 and MGEF +84: where the effects go.
    namespace MagicDelivery
    {
        inline constexpr std::uint32_t Self = 0, Touch = 1, Aimed = 2, TargetActor = 3, TargetLocation = 4;
    }

    /// Task 0242: does the magic runtime cast a spell with this delivery? Self and aimed always; touch with the beams
    /// setting; target actor and target location (the runes) with the delivery setting. Pure.
    [[nodiscard]] bool deliveryIsCast(std::uint32_t delivery, bool beamsOn, bool deliveryOn);

    /// The most effects that run at once (further ones are refused and counted).
    inline constexpr std::size_t kMaxActiveEffects = 256;
    /// The most a resistance takes off (RECALLED-VERIFY: Skyrim caps resistances at 85 percent).
    inline constexpr float kMaxResistPercent = 85.f;

    /// How an effect changes its target over time.
    enum class EffectTiming : std::uint8_t
    {
        Instant, ///< applied once, now
        PerSecond, ///< magnitude every second for the effect's duration (or while a concentration is held)
        Timed, ///< applied once and kept for the duration (a modifier, a status), no tick
    };

    /// True for Health, Magicka and Stamina (actor values 24 to 26), the ones the pack tracks.
    [[nodiscard]] bool isVitalValue(std::int32_t value);

    /// The timing of one effect of a spell: a concentration spell's effects tick while it is held; a duration of 0 is
    /// instant; a duration on a vital without the Recover flag ticks per second (poison, a heal over time); anything
    /// else is a status for its duration.
    [[nodiscard]] EffectTiming effectTiming(
        std::uint32_t spellCastType, const SpellEffect& effect, const MagicEffectRecord& record);

    /// The effect's magnitude with its sign: negative when the MGEF is Detrimental. Not finite becomes 0.
    [[nodiscard]] float signedMagnitude(float magnitude, std::uint32_t mgefFlags);

    /// The magnitude after a resistance of `resistPercent` (clamped to 0 .. kMaxResistPercent); not finite becomes 0.
    [[nodiscard]] float resistedMagnitude(float magnitude, float resistPercent);

    /// How much power a ward can hold: the Peak magnitude times (fMagicWardPowerMaxBase plus fMagicWardPowerMaxMult
    /// times the skill) (RECALLED-VERIFY; the stand-in has no skill).
    [[nodiscard]] float wardMaxPower(float peakMagnitude, const MagicSettings& settings, float skill);

    /// The ward's power after `seconds` of charging at `ratePerSecond`, never above `maxPower`.
    [[nodiscard]] float wardCharge(float power, float maxPower, float ratePerSecond, float seconds);

    /// A ward takes up to `power` of an incoming magnitude: returns what is left of it (0 when the ward held all of
    /// it) and lowers `power` by what it took. An incoming magnitude that is zero, negative or not finite takes
    /// nothing from the ward and is returned as 0.
    [[nodiscard]] float wardAbsorb(float& power, float incoming);

    /// One effect that is running on a target.
    struct ActiveEffect
    {
        std::uint32_t mId = 0; ///< the list's own number, from 1
        openeden::sdk::game::CombatSubject mCaster;
        openeden::sdk::game::CombatSubject mTarget;
        FormId mSpell = kNullFormId;
        FormId mEffect = kNullFormId;
        std::uint32_t mArchetype = 0;
        EffectTiming mTiming = EffectTiming::Timed;
        std::int32_t mValue = -1; ///< primary actor value
        std::int32_t mSecondValue = -1;
        float mPerSecond = 0.f; ///< signed amount a second on mValue (PerSecond only)
        float mSecondPerSecond = 0.f; ///< signed amount a second on mSecondValue
        bool mAbsorb = false; ///< the caster gains what the target loses
        float mRemaining = 0.f; ///< seconds left, > 0
        float mElapsed = 0.f;
    };

    /// The effects that are running, in the order they were added; ticks and ends happen in that order, so a run is
    /// repeatable. Preallocated: adding and ticking allocate nothing.
    class EffectList
    {
    public:
        EffectList() { mEffects.reserve(kMaxActiveEffects); }

        /// Adds a copy (its mId is set). False (counted) when the list is full or mRemaining is not above 0.
        bool add(ActiveEffect effect);

        /// Advances every effect by `seconds` (not finite or not above 0: nothing happens). For each effect `tick`
        /// is called once with the seconds it was active in this step (the step, or what was left of it); an effect
        /// whose time is used up is then removed and `ended` is called with it. Both are called in list order.
        template <class Tick, class Ended>
        void advance(float seconds, Tick&& tick, Ended&& ended)
        {
            if (!(seconds > 0.f) || seconds > 3600.f)
                return;
            for (std::size_t i = 0; i < mEffects.size();)
            {
                ActiveEffect& effect = mEffects[i];
                const float used = seconds < effect.mRemaining ? seconds : effect.mRemaining;
                effect.mElapsed += used;
                effect.mRemaining -= used;
                tick(effect, used);
                if (effect.mRemaining > 0.f)
                {
                    ++i;
                    continue;
                }
                const ActiveEffect done = effect;
                mEffects.erase(mEffects.begin() + static_cast<std::ptrdiff_t>(i));
                ended(done);
            }
        }

        /// Removes the effects on `target` (an actor that has gone). Not reported as ended.
        std::size_t removeTarget(const openeden::sdk::game::CombatSubject& target);
        /// Removes the effects `caster` started and the ones on it (an actor that has gone).
        std::size_t removeCaster(const openeden::sdk::game::CombatSubject& caster);

        [[nodiscard]] std::size_t size() const { return mEffects.size(); }
        [[nodiscard]] std::size_t refused() const { return mRefused; }
        [[nodiscard]] const std::vector<ActiveEffect>& effects() const { return mEffects; }

    private:
        std::vector<ActiveEffect> mEffects;
        std::uint32_t mNext = 1;
        std::size_t mRefused = 0;
    };
}

#endif
