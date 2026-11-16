#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

// Task 0238: the effects that change how fast a vital regenerates (Fortify and Damage Health, Magicka and Stamina Rate:
// the actor values 27 to 29 and 155 to 157, magic_status.hpp). The base rate of an actor is its race's (percent of the
// maximum a second, RACE DATA +84, +88, +92); the running effects change it with effectiveRegenRate. Nothing here
// allocates: one pass over the effect list per regeneration step, only when such an effect is running.
namespace game_skyrimse
{
    namespace
    {
        /// The least bonus health (units) sent to the engine for the player in one report; smaller amounts add up.
        constexpr float kPlayerRegenFlush = 0.25f;

        std::string num(float value)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
            return buffer;
        }

        std::string who(const openeden::sdk::game::CombatSubject& subject)
        {
            return subject.player ? std::string("player") : "actor " + std::to_string(subject.actor.reference);
        }
    }

    bool SkyrimCombat::anyRegenEffect() const
    {
        for (const ActiveEffect& e : mEffects.effects())
            if (e.mTiming == EffectTiming::Timed && isRegenRateValue(e.mValue))
                return true;
        return false;
    }

    float SkyrimCombat::regenRateFor(const Fighter& fighter, int vital, float baseRate, bool anyRunning) const
    {
        // task 0241: a held ability's or blessing's rates (the player's) join the running effects'
        const bool held = fighter.mHasAbilities && fighter.mAbilities.hasRegen();
        if (!anyRunning && !held)
            return baseRate;
        float rate = held ? fighter.mAbilities.mRegenRate[vital] : 0.f;
        float mult = held ? fighter.mAbilities.mRegenMult[vital] : 0.f;
        // A spell cast again on the same target replaces its earlier effect: newest first, and an (spell, effect) pair
        // that was already counted is skipped (the remembered pairs are few; past the table nothing is skipped).
        std::array<std::pair<FormId, FormId>, 16> counted{};
        std::size_t countedSize = 0;
        const std::vector<ActiveEffect>& list = mEffects.effects();
        for (std::size_t i = anyRunning ? list.size() : 0; i-- > 0;)
        {
            const ActiveEffect& e = list[i];
            if (e.mTiming != EffectTiming::Timed || !(e.mTarget == fighter.mSubject)
                || regenVital(e.mValue) != vital)
                continue;
            const std::pair<FormId, FormId> key{ e.mSpell, e.mEffect };
            if (std::find(counted.begin(), counted.begin() + static_cast<std::ptrdiff_t>(countedSize), key)
                != counted.begin() + static_cast<std::ptrdiff_t>(countedSize))
                continue;
            if (countedSize < counted.size())
                counted[countedSize++] = key;
            (isRegenMultValue(e.mValue) ? mult : rate) += e.mPerSecond; // signed: a damage effect is negative
        }
        return effectiveRegenRate(baseRate, rate, mult);
    }

    float SkyrimCombat::regenRatePercent(openeden::sdk::game::CombatSubject subject, int vital) const
    {
        const Fighter* f = find(subject);
        if (f == nullptr || !f->mHasProfile || vital < 0 || vital > 2)
            return -1.f;
        const float base = vital == 0 ? f->mProfile.mHealthRegenPercent
                                      : (vital == 1 ? f->mProfile.mMagickaRegenPercent : f->mProfile.mStaminaRegenPercent);
        return regenRateFor(*f, vital, base, anyRegenEffect());
    }

    void SkyrimCombat::traceRegenRates(bool anyRunning)
    {
        if (!mTrace || !anyRunning || mClock < mNextRegenTrace)
            return;
        mNextRegenTrace = mClock + 1.f;
        for (const Fighter& f : mFighters)
        {
            if (!f.mReady || !f.mHasProfile || f.mDead)
                continue;
            const float h = regenRateFor(f, 0, f.mProfile.mHealthRegenPercent, true);
            const float m = regenRateFor(f, 1, f.mProfile.mMagickaRegenPercent, true);
            const float s = regenRateFor(f, 2, f.mProfile.mStaminaRegenPercent, true);
            if (h == f.mProfile.mHealthRegenPercent && m == f.mProfile.mMagickaRegenPercent
                && s == f.mProfile.mStaminaRegenPercent)
                continue;
            trace("COMBAT regen " + who(f.mSubject) + " rates health " + num(h) + " magicka " + num(m) + " stamina "
                + num(s) + " percent a second (base " + num(f.mProfile.mHealthRegenPercent) + " "
                + num(f.mProfile.mMagickaRegenPercent) + " " + num(f.mProfile.mStaminaRegenPercent) + "), health "
                + num(f.mHealth) + " magicka " + num(f.mMagicka) + " stamina " + num(f.mStamina));
        }
    }

    void SkyrimCombat::addPlayerHealthRegen(float bonusUnits)
    {
        if (std::isfinite(bonusUnits) && bonusUnits > 0.f)
            mPlayerHealthBonus += bonusUnits;
    }

    void SkyrimCombat::flushPlayerHealthRegen(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (!(mPlayerHealthBonus >= kPlayerRegenFlush))
            return;
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !player->mReady || !player->mHasProfile || player->mDead)
        {
            mPlayerHealthBonus = 0.f;
            return;
        }
        openeden::sdk::game::SubjectState state;
        if (!writer.subjectState(openeden::sdk::game::CombatSubject::ofPlayer(), state) || !(state.healthMax > 0.f))
            return; // the engine has no health for the player yet: keep what was earned
        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = true;
        report.victimIsPlayer = true;
        report.amount = 0.f; // the same zero-damage report that carries a rest's and a level up's gain
        report.healthMax = std::max(state.healthMax, player->mProfile.mHealthMax);
        report.healthAfter = std::min(report.healthMax, std::max(state.health, 0.f) + mPlayerHealthBonus);
        mPlayerHealthBonus = 0.f;
        if (!(report.healthAfter > state.health))
            return;
        report.x = state.x;
        report.y = state.y;
        report.z = state.z;
        if (writer.reportDamage(report))
            player->mHealth = report.healthAfter;
    }
}
