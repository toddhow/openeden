#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/race_effects.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

// Task 0248: the effects tasks 0228, 0233, 0240 and 0241 left without a rule (race_effects.hpp has the data). Berserker
// Rage is a Script effect whose perk (PowerOrcBerserk: ModAttackDamage x2, ModIncomingDamage x0.5, VERIFIED) joins the
// caster's perk list while the effect runs; Voice of the Emperor is Calm (no new fight until it ends or a blow lands);
// Ancestor's Wrath is a Cloak (the associated spell's damage to the caster's enemies in range, every update); Command
// Animal puts an animal on the player's side; Night Eye is a status; Dragonskin and the Atronach sign absorb a hostile
// spell by a seeded roll (one roll per spell that lands); skill fortifies read by the progression and the consumers
// (skillPowerOf). Main thread, no allocation per update beyond what the older effect code does.
namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::SubjectState;

        /// The absorb rolls' seed (ours): a roll is rollPercent(kAbsorbSeed + counter), the counter running per
        /// session.
        constexpr std::uint64_t kAbsorbSeed = 0x0AB50B0ull;
        /// A spell that lands again on the same target within this many seconds is the same hit (a beam, the second
        /// effect of a spell): it keeps the first roll.
        constexpr float kAbsorbMemoSeconds = 0.3f;

        std::string num(float v)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(v));
            return buffer;
        }

        std::string who(const CombatSubject& subject)
        {
            return subject.player ? std::string("player") : "actor " + std::to_string(subject.actor.reference);
        }
    }

    // ---- read side -------------------------------------------------------------------------------------------------

    SkyrimCombat::SkillModifiers SkyrimCombat::playerSkillModifiers() const
    {
        SkillModifiers out;
        const AbilityNumbers numbers = playerAbilityNumbers();
        for (std::size_t s = 0; s < out.mPoints.size(); ++s)
        {
            out.mPoints[s] = numbers.mSkillPoints[s];
            out.mPower[s] = numbers.mSkillPower[s] + mValueMods.mSkillPercent[s];
        }
        return out;
    }

    float SkyrimCombat::skillPowerOf(std::size_t slot) const
    {
        if (slot >= 18)
            return 0.f;
        float power = mValueMods.mSkillPercent[slot];
        const Fighter* player = find(CombatSubject::ofPlayer());
        if (player != nullptr && player->mHasAbilities)
            power += player->mAbilities.mSkillPower[slot];
        return std::isfinite(power) ? power : 0.f;
    }

    float SkyrimCombat::playerCarryBonus() const
    {
        return playerAbilityNumbers().mCarry + mValueMods.mCarry;
    }

    float SkyrimCombat::playerShoutRecoveryScale() const
    {
        return race_effects::shoutRecoveryScale(playerAbilityNumbers().mShoutTimer);
    }

    bool SkyrimCombat::playerNightEye() const
    {
        const Fighter* player = find(CombatSubject::ofPlayer());
        return player != nullptr && statusActive(player->mStatus.mNightEyeUntil, mClock);
    }

    bool SkyrimCombat::playerImageSpace(openeden::sdk::game::PlayerImageSpaceState& out)
    {
        out = openeden::sdk::game::PlayerImageSpaceState();
        if (!mNightEyeView)
            return false;
        if (!mNightEyeRecordsTried && mData != nullptr && mData->combatReady())
        {
            // The game's own records: the script of NightEyeEffect names them as IntroFX, MainFX and OutroFX (VERIFIED
            // in the VMAD); the held state is the last key of the intro (docs/tasks/0257-skyrim-night-eye.md).
            mNightEyeRecordsTried = true;
            mNightEyeTracker.setNeutralContrast(true); // see NightEyeTracker::setNeutralContrast
            mNightEyeTracker.setRecords(mData->imageModifierByEditorId("NightEyeImodIntro"),
                mData->imageModifierByEditorId("NightEyeImodOutro"));
            if (!mNightEyeTracker.hasRecords())
                trace("COMBAT night eye: the image space modifiers NightEyeImodIntro and Outro were not found");
        }
        if (!mNightEyeTracker.hasRecords())
            return false;
        out = mNightEyeTracker.update(playerNightEye(), mClock);
        return out.active;
    }

    bool SkyrimCombat::calmed(CombatSubject subject) const
    {
        const Fighter* f = find(subject);
        return f != nullptr && statusActive(f->mStatus.mCalmUntil, mClock);
    }

    bool SkyrimCombat::commanded(CombatSubject subject) const
    {
        const Fighter* f = find(subject);
        return f != nullptr && statusActive(f->mStatus.mCommandedUntil, mClock);
    }

    bool SkyrimCombat::holdsPerk(CombatSubject subject, FormId perk) const
    {
        const Fighter* f = find(subject);
        return f != nullptr
            && std::find(f->mProfile.mPerks.begin(), f->mProfile.mPerks.end(), perk) != f->mProfile.mPerks.end();
    }

    // ---- timed perks -----------------------------------------------------------------------------------------------

    void SkyrimCombat::addTimedPerk(Fighter& fighter, FormId perk)
    {
        if (perk == kNullFormId)
            return;
        fighter.mTimedPerks.push_back(perk);
        std::vector<FormId>& perks = fighter.mProfile.mPerks;
        if (std::find(perks.begin(), perks.end(), perk) == perks.end())
        {
            perks.push_back(perk);
            std::sort(perks.begin(), perks.end());
        }
    }

    void SkyrimCombat::removeTimedPerk(Fighter& fighter, FormId perk)
    {
        const auto one = std::find(fighter.mTimedPerks.begin(), fighter.mTimedPerks.end(), perk);
        if (one == fighter.mTimedPerks.end())
            return;
        fighter.mTimedPerks.erase(one);
        if (std::find(fighter.mTimedPerks.begin(), fighter.mTimedPerks.end(), perk) == fighter.mTimedPerks.end())
        {
            std::vector<FormId>& perks = fighter.mProfile.mPerks;
            perks.erase(std::remove(perks.begin(), perks.end(), perk), perks.end());
        }
    }

    // ---- spell absorb ----------------------------------------------------------------------------------------------

    float SkyrimCombat::absorbChanceOf(const Fighter& target) const
    {
        const float held = target.mSubject.player && target.mHasAbilities ? target.mAbilities.mAbsorb : 0.f;
        float timed = 0.f;
        for (const ActiveEffect& e : mEffects.effects())
            if (e.mTiming == EffectTiming::Timed && e.mTarget == target.mSubject
                && e.mValue == race_effects::kAbsorbValue
                && (e.mArchetype == MagicArchetype::PeakValueModifier || e.mArchetype == MagicArchetype::ValueModifier))
                timed += e.mPerSecond;
        return race_effects::absorbChancePercent(held, timed);
    }

    bool SkyrimCombat::spellAbsorbed(Fighter& caster, Fighter& target, const SpellRecord& spell)
    {
        const float chance = absorbChanceOf(target);
        if (!(chance > 0.f))
            return false; // no roll is drawn: a run with no absorb is the run before
        AbsorbMemo& memo = mAbsorbMemo;
        if (memo.mValid && memo.mSpell == spell.mCommon.mId && memo.mCaster == caster.mSubject
            && memo.mTarget == target.mSubject && mClock - memo.mAt < kAbsorbMemoSeconds)
        {
            memo.mAt = mClock;
            return memo.mAbsorbed;
        }
        const float roll = rollPercent(kAbsorbSeed + mAbsorbRolls++);
        const bool absorbed = race_effects::absorbs(chance, roll);
        memo = { true, spell.mCommon.mId, caster.mSubject, target.mSubject, mClock, absorbed };
        if (absorbed)
            target.mMagicka = std::min(target.mProfile.mMagickaMax, target.mMagicka + static_cast<float>(spell.mCost));
        if (mTrace)
            trace("COMBAT spell absorb " + who(target.mSubject) + " chance " + num(chance) + " roll " + num(roll)
                + (absorbed ? " absorbed (magicka " + num(target.mMagicka) + ")" : " not absorbed"));
        return absorbed;
    }

    // ---- Script, Calm and Cloak effects ----------------------------------------------------------------------------

    bool SkyrimCombat::applyRaceEffect(CombatRequestWriter& writer, Fighter& caster, Fighter& target,
        const SpellRecord& spell, const SpellEffect& effect, const MagicEffectRecord& record, float seconds,
        EffectTiming timing)
    {
        (void)writer;
        const std::uint32_t archetype = record.mArchetype;
        const race_effects::ScriptKind kind = race_effects::scriptKindOf(archetype, record.mCommon.mEditorId);
        const bool cloak = archetype == race_effects::kCloakArchetype;
        const bool calm = archetype == race_effects::kCalmArchetype;
        if (kind == race_effects::ScriptKind::None && !cloak && !calm)
            return false;
        ++mEffectsApplied;
        if (!(seconds > 0.f))
        {
            trace("COMBAT effect " + record.mCommon.mEditorId + " has no duration");
            return true;
        }
        const auto running = [&](FormId perk, float radiusFeet) {
            ActiveEffect e;
            e.mCaster = caster.mSubject;
            e.mTarget = target.mSubject;
            e.mSpell = spell.mCommon.mId;
            e.mEffect = record.mCommon.mId;
            e.mArchetype = archetype;
            e.mTiming = timing == EffectTiming::Instant ? EffectTiming::Timed : timing;
            e.mValue = record.mPrimaryValue;
            e.mPerSecond = radiusFeet; // a cloak keeps its radius in feet here
            e.mRemaining = seconds;
            e.mPerk = perk;
            return mEffects.add(e);
        };
        if (cloak)
        {
            if (!running(kNullFormId, effect.mMagnitude))
                trace("COMBAT effect list is full");
            else
                trace("COMBAT effect cloak " + record.mCommon.mEditorId + " on " + who(target.mSubject) + " radius "
                    + num(race_effects::cloakRadiusUnits(effect.mMagnitude)) + " units for " + num(seconds) + " s");
            return true;
        }
        if (calm)
        {
            if (target.mSubject.player || &caster == &target)
            {
                trace("COMBAT effect calm on " + who(target.mSubject) + " is not applied");
                return true;
            }
            if (target.mDead || !ready(target))
                return true;
            if (!levelGateAllows(target.mProfile.mLevel, effect.mMagnitude))
            {
                trace("COMBAT effect calm on " + who(target.mSubject) + " resisted: its level "
                    + std::to_string(target.mProfile.mLevel) + " is above the magnitude " + num(effect.mMagnitude));
                return true;
            }
            target.mStatus.mCalmUntil = extendStatus(target.mStatus.mCalmUntil, mClock, seconds);
            ++mStatusesApplied;
            trace("COMBAT effect calm on " + who(target.mSubject) + " for " + num(seconds) + " s");
            for (std::size_t i = 0; i < mAiActors.size(); ++i)
                if (mAiActors[i].mHandle == target.mSubject.actor)
                {
                    aiRelease(i, "calm");
                    break;
                }
            return true;
        }
        switch (kind)
        {
            case race_effects::ScriptKind::Berserk:
            {
                if (record.mPerkToApply == kNullFormId || !running(record.mPerkToApply, 0.f))
                {
                    trace("COMBAT effect berserk has no perk or no room");
                    return true;
                }
                addTimedPerk(target, record.mPerkToApply);
                trace("COMBAT effect berserk on " + who(target.mSubject) + " for " + num(seconds) + " s: perk "
                    + std::to_string(record.mPerkToApply) + " joins (damage dealt and taken follow its entry points)");
                return true;
            }
            case race_effects::ScriptKind::NightEye:
                (void)running(kNullFormId, 0.f);
                target.mStatus.mNightEyeUntil = extendStatus(target.mStatus.mNightEyeUntil, mClock, seconds);
                trace("COMBAT effect night eye on " + who(target.mSubject) + " for " + num(seconds)
                    + (mNightEyeView ? " s (the view: the image space modifier)"
                                     : " s (a status only: nothing draws a brighter view)"));
                return true;
            case race_effects::ScriptKind::CommandAnimal:
            {
                if (target.mSubject.player || &caster == &target || target.mDead || !ready(target))
                    return true;
                if (!levelGateAllows(target.mProfile.mLevel, effect.mMagnitude))
                {
                    trace("COMBAT effect command animal on " + who(target.mSubject) + " resisted: its level "
                        + std::to_string(target.mProfile.mLevel) + " is above the magnitude " + num(effect.mMagnitude));
                    return true;
                }
                target.mStatus.mCommandedUntil = extendStatus(target.mStatus.mCommandedUntil, mClock, seconds);
                ++mStatusesApplied;
                trace("COMBAT effect command animal on " + who(target.mSubject) + " for " + num(seconds) + " s");
                for (std::size_t i = 0; i < mAiActors.size(); ++i)
                    if (mAiActors[i].mHandle == target.mSubject.actor)
                    {
                        aiRelease(i, "commanded");
                        break;
                    }
                return true;
            }
            case race_effects::ScriptKind::None:
                break;
        }
        return true;
    }

    void SkyrimCombat::tickCloak(CombatRequestWriter& writer, const ActiveEffect& effect, float used)
    {
        Fighter* caster = find(effect.mCaster);
        const MagicEffectRecord* cloakRecord = mData != nullptr ? mData->effectById(effect.mEffect) : nullptr;
        if (caster == nullptr || caster->mDead || cloakRecord == nullptr || cloakRecord->mAssociatedItem == kNullFormId)
            return;
        const SpellRecord* damageSpell = mData->spellById(cloakRecord->mAssociatedItem);
        const float radius = race_effects::cloakRadiusUnits(effect.mPerSecond);
        SubjectState me;
        if (damageSpell == nullptr || !(radius > 0.f) || !writer.subjectState(caster->mSubject, me))
            return;
        for (std::size_t i = 0; i < mFighters.size(); ++i)
        {
            Fighter& other = mFighters[i];
            if (&other == caster || other.mDead || !ready(other))
                continue;
            SubjectState there;
            if (!writer.subjectState(other.mSubject, there))
                continue;
            const double dx = there.x - me.x;
            const double dy = there.y - me.y;
            const double dz = there.z - me.z;
            if (dx * dx + dy * dy + dz * dz > static_cast<double>(radius) * static_cast<double>(radius))
                continue;
            for (const SpellEffect& held : damageSpell->mEffects)
            {
                const MagicEffectRecord* record = mData->effectById(held.mEffect);
                if (record == nullptr || !record->mHasData || (record->mFlags & MagicFlag::Hostile) == 0
                    || record->mArchetype != MagicArchetype::ValueModifier || !isVitalValue(record->mPrimaryValue))
                    continue;
                // the record's own condition (IsHostileToActor) keeps the cloak off the caster's friends
                if (!effectConditionsAllow(*caster, other, held, *record, true))
                    continue;
                const float signedMag = signedMagnitude(held.mMagnitude, record->mFlags);
                const float shaped
                    = weakened(resistedMagnitude(std::abs(signedMag), resistOf(other, record->mResistValue)),
                        weaknessOf(other, record->mResistValue));
                const float amount = race_effects::cloakDamage(shaped, used);
                if (amount > 0.f)
                    (void)changeVital(
                        writer, caster, other, record->mPrimaryValue, signedMag < 0.f ? -amount : amount, nullptr);
            }
        }
    }
}
