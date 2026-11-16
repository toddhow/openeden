#include <game_skyrimse/combat.hpp>

#include <algorithm>

// Task 0236: the conditions on a magic effect, evaluated for the actor it lands on (Subject) and its caster (run-on 1).
// The effect's own list (the CTDAs after its EFID/EFIT in the SPEL) and the MGEF's list must both hold. Everything the
// record can say about "who" (race keywords, perks, level) is what actorParticipant already gives the perk engine; the
// running effects, the vitals and the combat state are read from the fighter here. A list the pack cannot answer fails
// closed: the effect does nothing and the function is traced once (magic_conditions.hpp has the list of what is answered).
namespace game_skyrimse
{
    namespace
    {
        std::string who(const openeden::sdk::game::CombatSubject& subject)
        {
            return subject.player ? std::string("player") : "actor " + std::to_string(subject.actor.reference);
        }

        std::uint64_t mix(std::uint64_t h, std::uint64_t v)
        {
            h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        }
    }

    effectconditions::Participant SkyrimCombat::effectParticipant(
        const Fighter& fighter, std::vector<FormId>& active) const
    {
        effectconditions::Participant out;
        out.mPerk = actorParticipant(fighter, false);
        out.mDead = fighter.mDead;
        out.mInCombat = fighter.mCombatUntil > mClock;
        const FighterProfile& p = fighter.mProfile;
        const auto fraction = [](float value, float maximum) {
            return maximum > 0.f ? std::clamp(value / maximum, 0.f, 1.f) : 1.f;
        };
        out.mHealthFraction = fraction(fighter.mHealth, p.mHealthMax);
        out.mMagickaFraction = fraction(fighter.mMagicka, p.mMagickaMax);
        out.mStaminaFraction = fraction(fighter.mStamina, p.mStaminaMax);
        active.clear();
        for (const ActiveEffect& e : mEffects.effects())
            if (e.mTarget == fighter.mSubject)
                active.push_back(e.mEffect);
        out.mActiveEffects = active;
        return out;
    }

    bool SkyrimCombat::effectConditionsAllow(
        const Fighter& caster, const Fighter& target, const SpellEffect& effect, const MagicEffectRecord& record)
    {
        const std::uint32_t conditions = effect.mConditions + record.mConditions;
        if (conditions == 0 || mApplyEffectConditions)
            return true;
        const bool listed = effect.mConditionList.size() == effect.mConditions
            && record.mConditionList.size() == record.mConditions;
        if (!listed)
        {
            if (mTrace)
                trace("COMBAT effect " + record.mCommon.mEditorId + " skipped: " + std::to_string(conditions)
                    + " conditions the pack cannot evaluate");
            return false;
        }
        if (!mEffectConditions)
        {
            mEffectConditions = std::make_unique<effectconditions::Evaluator>();
            mEffectConditions->setEffectLookup([this](FormId id) -> const MagicEffectRecord* {
                return mData != nullptr ? mData->effectById(id) : nullptr;
            });
            mEffectConditions->setLog([this](const std::string& line) { trace("COMBAT " + line); });
        }
        std::uint64_t seed = mix(0x0236, ++mEffectConditionChecks);
        seed = mix(seed, record.mCommon.mId);
        seed = mix(seed, caster.mSubject.player ? 0 : caster.mSubject.actor.reference);
        seed = mix(seed, target.mSubject.player ? 0 : target.mSubject.actor.reference);
        mEffectConditions->setSeed(seed);

        const effectconditions::Participant targetSide = effectParticipant(target, mActiveOfTarget);
        const effectconditions::Participant casterSide = effectParticipant(caster, mActiveOfCaster);
        const Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        effectconditions::Participant playerSide;
        std::vector<FormId> playerActive; // empty: the player's running effects are never asked (run-on 2 asks level)
        if (player != nullptr)
            playerSide = effectParticipant(*player, playerActive);

        using effectconditions::Verdict;
        Verdict verdict = Verdict::Pass;
        for (const std::vector<survey::Ctda>* list : { &effect.mConditionList, &record.mConditionList })
        {
            if (list->empty())
                continue;
            const Verdict v = mEffectConditions->evaluate(
                *list, &targetSide, &casterSide, player != nullptr ? &playerSide : nullptr);
            if (v == Verdict::Unanswered)
                verdict = Verdict::Unanswered;
            else if (v == Verdict::Fail && verdict == Verdict::Pass)
                verdict = Verdict::Fail;
        }
        switch (verdict)
        {
            case Verdict::Pass:
                ++mEffectConditionsPassed;
                if (mTrace)
                    trace("COMBAT effect " + record.mCommon.mEditorId + " conditions hold on " + who(target.mSubject)
                        + " (caster " + who(caster.mSubject) + ")");
                return true;
            case Verdict::Fail:
                ++mEffectConditionsFailed;
                if (mTrace)
                    trace("COMBAT effect " + record.mCommon.mEditorId + " refused: its conditions fail on "
                        + who(target.mSubject) + " (caster " + who(caster.mSubject) + ")");
                return false;
            case Verdict::Unanswered:
                break;
        }
        ++mEffectConditionsUnanswered;
        if (mTrace)
            trace("COMBAT effect " + record.mCommon.mEditorId + " refused: a condition cannot be answered here, on "
                + who(target.mSubject));
        return false;
    }
}
