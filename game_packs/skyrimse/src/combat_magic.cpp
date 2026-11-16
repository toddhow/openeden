#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

// Task 0186 (milestone M-S5, stage S5-8): the magic runtime of SkyrimCombat. A spell is cast by the pack: an NPC plays
// the cast clip and lets the spell go on the clip's SpellFire event; the player (no first-person arms) lets it go after
// the time the sidecar of the same clip gives. Fire and forget spells cost their magicka when they go; a concentration
// spell costs it every second while held. An effect changes health, magicka or stamina at once, every second for its
// duration, or is a status for its duration; a ward charges while its spell is held and absorbs the magnitude of the
// hostile spells that hit its caster. The Firebolt damage effect carries a script in the data; it is handled here as
// plain fire damage (toddhow's decision of 2026-11-12). Every formula is RECALLED-VERIFY.
namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::SubjectState;
        using openeden::sdk::game::SweepHit;
        using openeden::sdk::game::SweepTarget;

        constexpr std::int32_t kHealth = 24, kMagicka = 25, kStamina = 26;

        std::string lowerText(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        std::string trimmed(std::string_view text)
        {
            std::size_t b = 0;
            std::size_t e = text.size();
            while (b < e && (text[b] == ' ' || text[b] == '\t'))
                ++b;
            while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t'))
                --e;
            return std::string(text.substr(b, e - b));
        }

        std::string num(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(value));
            return buffer;
        }

        std::string who(const CombatSubject& subject)
        {
            if (subject.player)
                return "player";
            return "actor " + std::to_string(subject.actor.reference);
        }

        std::string spellName(const SpellRecord& spell)
        {
            return spell.mCommon.mEditorId.empty() ? std::to_string(spell.mCommon.mId) : spell.mCommon.mEditorId;
        }

        // The ward spell is the one with an AccumulateMagnitude effect; its Peak effect is the ward's strength.
        bool isWard(CombatData& data, const SpellRecord& spell)
        {
            for (const SpellEffect& e : spell.mEffects)
            {
                const MagicEffectRecord* record = data.effectById(e.mEffect);
                if (record != nullptr && record->mArchetype == MagicArchetype::AccumulateMagnitude)
                    return true;
            }
            return false;
        }

        // The spell restores health and does nothing else (a self heal): casting it at full health is pointless.
        bool isPlainHeal(CombatData& data, const SpellRecord& spell)
        {
            if (spell.mDelivery != MagicDelivery::Self || spell.mEffects.empty())
                return false;
            for (const SpellEffect& e : spell.mEffects)
            {
                const MagicEffectRecord* record = data.effectById(e.mEffect);
                if (record == nullptr || record->mPrimaryValue != kHealth
                    || (record->mFlags & MagicFlag::Detrimental) != 0
                    || record->mArchetype != MagicArchetype::ValueModifier)
                    return false;
            }
            return true;
        }
    }

    SlotTable SlotTable::magic()
    {
        // Names from the listing of Skyrim - Animations.bsa (meshes/actors/character/animations/mrh_release.hkx and
        // mrh_selfrelease.hkx); the events from animationdata/defaultmale.txt: MRh_Release has MRh_SpellFire_Event at
        // 0.167, MRh_SelfRelease at 0.367 playback seconds.
        static constexpr Entry sMagic[] = {
            { ClipSlot::Cast, kMagicAimed, "MRh_Release" },
            { ClipSlot::Cast, kMagicSelf, "MRh_SelfRelease" },
        };
        SlotTable table;
        table.mEntries = sMagic;
        table.mCount = sizeof sMagic / sizeof sMagic[0];
        return table;
    }

    // ---- settings --------------------------------------------------------------------------------------------------

    bool SkyrimCombat::setTestPlayerSpell(std::string_view editorId)
    {
        mPlayerSpell = nullptr;
        mPlayerSpellLooked = false;
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestPlayerSpell.clear();
            return false;
        }
        mTestPlayerSpell = lowerText(editorId);
        return true;
    }

    void SkyrimCombat::setTestPlayerCastEvery(float seconds)
    {
        mPlayerCastEvery = (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
        mNextPlayerCast = mPlayerCastEvery > 0.f ? mPlayerCastEvery : 1.f; // the first press comes after one period
    }

    void SkyrimCombat::setTestPlayerCastHold(float seconds)
    {
        mPlayerCastHold = (std::isfinite(seconds) && seconds > 0.f) ? seconds : kDefaultCastHold;
    }

    bool SkyrimCombat::setTestCasts(std::string_view list)
    {
        if (list.size() > kMaxTestCastListBytes)
            return false;
        std::vector<TestCaster> parsed;
        std::size_t begin = 0;
        while (begin <= list.size() && !trimmed(list).empty())
        {
            std::size_t end = list.find(',', begin);
            if (end == std::string_view::npos)
                end = list.size();
            const std::string entry = trimmed(list.substr(begin, end - begin));
            const std::size_t colon = entry.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= entry.size())
                return false;
            if (parsed.size() >= kMaxTestCasters)
                return false;
            TestCaster caster;
            caster.mActor = lowerText(trimmed(entry.substr(0, colon)));
            caster.mSpellName = lowerText(trimmed(entry.substr(colon + 1)));
            if (caster.mActor.empty() || caster.mSpellName.empty())
                return false;
            caster.mNext = -1.f;
            for (const auto& named : mActorNames)
                if (named.second == caster.mActor)
                {
                    caster.mHandle = named.first;
                    break;
                }
            parsed.push_back(std::move(caster));
            begin = end + 1;
            if (end >= list.size())
                break;
        }
        mTestCasters = std::move(parsed);
        return true;
    }

    void SkyrimCombat::setTestCastEvery(float seconds)
    {
        mCastPeriod = (std::isfinite(seconds) && seconds > 0.f) ? seconds : kDefaultCastPeriod;
    }

    void SkyrimCombat::setTestCastHold(float seconds)
    {
        mCastHold = (std::isfinite(seconds) && seconds > 0.f) ? seconds : kDefaultCastHold;
    }

    // ---- the pieces of a cast --------------------------------------------------------------------------------------

    bool SkyrimCombat::isBusyCasting(const CombatSubject& subject) const
    {
        for (const Cast& c : mCasts)
            if (c.mCaster == subject)
                return true;
        for (const Concentration& c : mConcentrations)
            if (c.mCaster == subject)
                return true;
        return false;
    }

    bool SkyrimCombat::isBusyConcentrating(const CombatSubject& subject) const
    {
        for (const Concentration& c : mConcentrations)
            if (c.mCaster == subject)
                return true;
        return false;
    }

    void SkyrimCombat::cancelCastsOf(const CombatSubject& subject)
    {
        std::erase_if(mCasts, [&](const Cast& c) { return c.mCaster == subject; });
        for (std::size_t i = mConcentrations.size(); i > 0; --i)
            if (mConcentrations[i - 1].mCaster == subject)
                endConcentration(i - 1, "caster gone");
        std::erase_if(mSpellShots, [&](const SpellShot& s) { return s.mOwner == subject; });
    }

    float SkyrimCombat::resistOf(const Fighter& fighter, std::int32_t resistValue)
    {
        if (resistValue < 0 || !mUsePerks || mData == nullptr || !fighter.mHasProfile)
            return 0.f;
        const PerkLibrary* library = mData->perkLibrary();
        if (library == nullptr)
            return 0.f;
        std::vector<FormId> spells = fighter.mProfile.mSpells;
        collectPerkAbilities(*library, fighter.mProfile.mPerks, spells);
        bool found = false;
        const float sum = abilityModifier(*library, spells, resistValue, found);
        return found ? sum : 0.f;
    }

    float SkyrimCombat::changeVital(CombatRequestWriter& writer, Fighter* source, Fighter& target, std::int32_t value,
        float delta, const SweepHit* where)
    {
        if (!std::isfinite(delta) || delta == 0.f || target.mDead)
            return 0.f;
        if (value == kMagicka)
        {
            const float before = target.mMagicka;
            target.mMagicka = std::clamp(before + delta, 0.f, std::max(0.f, target.mProfile.mMagickaMax));
            return target.mMagicka - before;
        }
        if (value == kStamina)
        {
            const float before = target.mStamina;
            target.mStamina = std::clamp(before + delta, 0.f, std::max(0.f, target.mProfile.mStaminaMax));
            return target.mStamina - before;
        }
        if (value != kHealth)
            return 0.f;
        float current = target.mHealth;
        float maximum = target.mProfile.mHealthMax;
        if (target.mSubject.player)
        {
            // The player's health is the engine's: read it when the engine has one.
            SubjectState state;
            if (writer.subjectState(target.mSubject, state) && state.healthMax > 0.f)
            {
                current = state.health;
                maximum = state.healthMax;
            }
        }
        const float after = std::clamp(current + delta, 0.f, std::max(0.f, maximum));
        const float changed = after - current;
        if (changed == 0.f)
            return 0.f;
        target.mHealth = after;
        const bool harm = changed < 0.f;
        if (harm)
        {
            target.mCombatUntil = mClock + kCombatLingerSeconds;
            if (source != nullptr)
                source->mCombatUntil = mClock + kCombatLingerSeconds;
        }
        if (harm || target.mSubject.player)
        {
            // A heal of the player is told to the engine too (its health is the engine's, through healthAfter).
            openeden::sdk::game::DamageReport report;
            report.attackerIsPlayer = source != nullptr ? source->mSubject.player : target.mSubject.player;
            report.attacker = source != nullptr ? source->mSubject.actor : target.mSubject.actor;
            report.victimIsPlayer = target.mSubject.player;
            report.victim = target.mSubject.actor;
            report.amount = harm ? -changed : 0.f;
            report.healthAfter = after;
            report.healthMax = maximum;
            if (where != nullptr)
            {
                report.x = where->x;
                report.y = where->y;
                report.z = where->z;
            }
            else
            {
                SubjectState at;
                if (writer.subjectState(target.mSubject, at))
                {
                    report.x = at.x;
                    report.y = at.y;
                    report.z = at.z;
                }
            }
            (void)writer.reportDamage(report);
        }
        if (harm)
            ++mHitsResolved;
        if (after > 0.f || target.mDead)
            return changed;
        target.mDead = true;
        target.mStaggered = false;
        trace("COMBAT death " + who(target.mSubject) + " (spell)");
        cancelCastsOf(target.mSubject);
        if (!target.mSubject.player)
        {
            const std::string_view clip = mSlots.clipFor(ClipSlot::Stagger, kWeaponAnim1HSword);
            if (!clip.empty())
            {
                openeden::sdk::game::ClipRequest request;
                request.slot = ClipSlot::Death;
                request.clip = clip;
                request.blendSeconds = 0.05f;
                request.token = mNextToken++;
                (void)writer.playClip(target.mSubject, request);
            }
            target.mSwinging = false;
            (void)writer.reportDeath(target.mSubject.actor);
        }
        return changed;
    }

    // ---- effects ---------------------------------------------------------------------------------------------------

    void SkyrimCombat::applyOneEffect(CombatRequestWriter& writer, Fighter& caster, Fighter& target,
        const SpellRecord& spell, const SpellEffect& effect, const MagicEffectRecord& record, bool& blocked,
        const SweepHit* where)
    {
        const bool hostile = (record.mFlags & MagicFlag::Hostile) != 0 && &caster != &target;
        const std::uint32_t archetype = record.mArchetype;
        const bool valueEffect = archetype == MagicArchetype::ValueModifier || archetype == MagicArchetype::Dual
            || archetype == MagicArchetype::Absorb;
        float magnitude = signedMagnitude(effect.mMagnitude, record.mFlags);
        if (hostile && valueEffect)
            magnitude = (magnitude < 0.f ? -1.f : 1.f)
                * resistedMagnitude(std::abs(magnitude), resistOf(target, record.mResistValue));
        const EffectTiming timing = effectTiming(spell.mCastType, effect, record);
        // The ward takes what it can of a hostile spell's magnitude; a spell it holds entirely does nothing at all.
        if (hostile && target.mWardPower > 0.f)
        {
            if (valueEffect && timing != EffectTiming::PerSecond)
            {
                const float incoming = std::abs(magnitude);
                const float before = target.mWardPower;
                const float left = wardAbsorb(target.mWardPower, incoming);
                trace("COMBAT ward " + who(target.mSubject) + " takes " + num(before - target.mWardPower) + " of "
                    + num(incoming) + ", power " + num(before) + " -> " + num(target.mWardPower));
                if (left <= 0.f)
                {
                    blocked = true;
                    return;
                }
                magnitude = (magnitude < 0.f ? -1.f : 1.f) * left;
            }
            else if (!valueEffect)
            {
                trace("COMBAT ward " + who(target.mSubject) + " stops effect archetype " + std::to_string(archetype));
                return;
            }
        }
        const float seconds = static_cast<float>(effect.mDuration);
        auto active = [&](float perSecond, float secondPerSecond, bool absorb) {
            ActiveEffect e;
            e.mCaster = caster.mSubject;
            e.mTarget = target.mSubject;
            e.mSpell = spell.mCommon.mId;
            e.mEffect = record.mCommon.mId;
            e.mArchetype = archetype;
            e.mTiming = timing;
            e.mValue = record.mPrimaryValue;
            e.mSecondValue = record.mSecondaryValue;
            e.mPerSecond = perSecond;
            e.mSecondPerSecond = secondPerSecond;
            e.mAbsorb = absorb;
            e.mRemaining = seconds;
            return mEffects.add(e);
        };
        const float secondWeight = record.mSecondaryWeight;
        switch (archetype)
        {
            case MagicArchetype::ValueModifier:
            case MagicArchetype::PeakValueModifier:
            case MagicArchetype::Dual:
            case MagicArchetype::Absorb:
            {
                const bool dual = archetype == MagicArchetype::Dual;
                const bool absorb = archetype == MagicArchetype::Absorb;
                if (!isVitalValue(record.mPrimaryValue))
                {
                    // A status on a value the pack does not track (a skill, a resistance): kept for its duration.
                    if (seconds > 0.f)
                    {
                        (void)active(0.f, 0.f, false);
                        ++mEffectsApplied;
                    }
                    trace("COMBAT effect " + record.mCommon.mEditorId + " on value "
                        + std::to_string(record.mPrimaryValue) + " is a status only");
                    return;
                }
                const float second = dual ? magnitude * secondWeight : 0.f;
                if (timing == EffectTiming::Instant)
                {
                    float taken = changeVital(writer, &caster, target, record.mPrimaryValue, magnitude, where);
                    if (dual && isVitalValue(record.mSecondaryValue))
                        (void)changeVital(writer, &caster, target, record.mSecondaryValue, second, where);
                    if (absorb && taken != 0.f)
                        (void)changeVital(writer, nullptr, caster, record.mPrimaryValue, -taken, nullptr);
                }
                else if (timing == EffectTiming::PerSecond)
                {
                    if (!active(magnitude, second, absorb))
                        trace("COMBAT effect list is full");
                }
                else
                    (void)active(0.f, 0.f, false);
                ++mEffectsApplied;
                trace("COMBAT effect " + record.mCommon.mEditorId + " " + who(caster.mSubject) + " -> "
                    + who(target.mSubject) + " magnitude " + num(magnitude)
                    + (timing == EffectTiming::Instant ? " instant"
                                                       : timing == EffectTiming::PerSecond ? " a second for " : " for ")
                    + (timing == EffectTiming::Instant ? std::string() : num(seconds) + " s"));
                return;
            }
            case MagicArchetype::Stagger:
            {
                ++mEffectsApplied;
                if (target.mSubject.player || target.mDead)
                {
                    trace("COMBAT effect stagger on " + who(target.mSubject) + " (nothing to play)");
                    return;
                }
                const std::string_view clip = mSlots.clipFor(ClipSlot::Stagger, kWeaponAnim1HSword);
                if (clip.empty())
                    return;
                openeden::sdk::game::ClipRequest request;
                request.slot = ClipSlot::Stagger;
                request.clip = clip;
                request.blendSeconds = 0.05f;
                request.token = mNextToken++;
                if (writer.playClip(target.mSubject, request))
                {
                    target.mSwinging = false;
                    target.mStaggered = true;
                    target.mStaggerToken = request.token;
                    trace("COMBAT effect stagger on " + who(target.mSubject) + " clip " + std::string(clip));
                }
                return;
            }
            case MagicArchetype::Demoralize:
            {
                ++mEffectsApplied;
                if (seconds > 0.f && active(0.f, 0.f, false))
                    target.mDemoralizedUntil = mClock + seconds;
                trace("COMBAT effect demoralize on " + who(target.mSubject) + " for " + num(seconds) + " s");
                return;
            }
            case MagicArchetype::AccumulateMagnitude:
                return; // a ward is charged by its concentration (tickConcentrations)
            default:
                trace("COMBAT effect " + record.mCommon.mEditorId + ": archetype " + std::to_string(archetype)
                    + " is not supported");
                return;
        }
    }

    void SkyrimCombat::applySpellEffects(
        CombatRequestWriter& writer, Fighter& caster, Fighter& target, const SpellRecord& spell, const SweepHit* where)
    {
        bool blocked = false;
        for (const SpellEffect& effect : spell.mEffects)
        {
            if (blocked)
                break;
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr || !record->mHasData)
            {
                trace("COMBAT effect of " + spellName(spell) + ": no record");
                continue;
            }
            const std::uint32_t conditions = effect.mConditions + record->mConditions;
            if (conditions > 0 && !mApplyEffectConditions)
            {
                trace("COMBAT effect " + record->mCommon.mEditorId + " skipped: " + std::to_string(conditions)
                    + " conditions the pack cannot evaluate");
                continue;
            }
            applyOneEffect(writer, caster, target, spell, effect, *record, blocked, where);
            if (target.mDead)
                break;
        }
        if (blocked)
            trace("COMBAT spell " + spellName(spell) + " blocked by the ward of " + who(target.mSubject));
    }

    void SkyrimCombat::tickEffects(CombatRequestWriter& writer, float step)
    {
        mEffects.advance(
            step,
            [&](ActiveEffect& e, float used) {
                if (e.mTiming != EffectTiming::PerSecond)
                    return;
                Fighter* target = find(e.mTarget);
                if (target == nullptr || target->mDead)
                    return;
                Fighter* caster = find(e.mCaster);
                const float taken = changeVital(writer, caster, *target, e.mValue, e.mPerSecond * used, nullptr);
                if (isVitalValue(e.mSecondValue) && e.mSecondPerSecond != 0.f)
                    (void)changeVital(writer, caster, *target, e.mSecondValue, e.mSecondPerSecond * used, nullptr);
                if (e.mAbsorb && taken != 0.f && caster != nullptr)
                    (void)changeVital(writer, nullptr, *caster, e.mValue, -taken, nullptr);
            },
            [&](const ActiveEffect& e) {
                ++mEffectsEnded;
                if (e.mArchetype == MagicArchetype::Demoralize)
                    if (Fighter* target = find(e.mTarget))
                        target->mDemoralizedUntil = -1.f;
                trace("COMBAT effect ended " + std::to_string(e.mEffect) + " on " + who(e.mTarget) + " after "
                    + num(e.mElapsed) + " s");
            });
    }

    // ---- concentration ---------------------------------------------------------------------------------------------

    void SkyrimCombat::endConcentration(std::size_t index, const char* why)
    {
        if (index >= mConcentrations.size())
            return;
        const Concentration c = mConcentrations[index];
        mConcentrations.erase(mConcentrations.begin() + static_cast<std::ptrdiff_t>(index));
        if (c.mEffectToken != 0)
            mPendingHides.push_back(c.mEffectToken);
        if (Fighter* f = find(c.mCaster))
            if (c.mSpell != nullptr && f->mWardMax > 0.f && isWard(*mData, *c.mSpell))
            {
                f->mWardPower = 0.f;
                f->mWardMax = 0.f;
            }
        trace("COMBAT concentration ended " + (c.mSpell != nullptr ? spellName(*c.mSpell) : std::string("?")) + " "
            + who(c.mCaster) + " (" + why + ")");
    }

    void SkyrimCombat::tickConcentrations(CombatRequestWriter& writer, float step)
    {
        for (std::size_t i = 0; i < mConcentrations.size();)
        {
            Concentration& c = mConcentrations[i];
            Fighter* f = find(c.mCaster);
            if (f == nullptr || f->mDead || c.mSpell == nullptr)
            {
                endConcentration(i, "caster gone");
                continue;
            }
            if (!c.mHeldByKey && c.mEndAt >= 0.f && mClock >= c.mEndAt)
            {
                endConcentration(i, "time");
                continue;
            }
            const SpellRecord& spell = *c.mSpell;
            const float wanted = static_cast<float>(spell.mCost) * step;
            const float spend = std::min(wanted, std::max(0.f, f->mMagicka));
            const float fraction = wanted > 0.f ? spend / wanted : 1.f;
            f->mMagicka -= spend;
            f->mMagickaRegenAt = mClock + mMagicSettings.mDamagedMagickaRegenDelay;
            const float seconds = step * fraction;
            if (c.mBeam)
                tickBeam(writer, c, *f, spell, seconds);
            for (const SpellEffect& effect : spell.mEffects)
            {
                if (c.mBeam)
                    break; // a beam's effects go to what its ray meets (tickBeam), not to the caster
                const MagicEffectRecord* record = mData->effectById(effect.mEffect);
                if (record == nullptr || !record->mHasData)
                    continue;
                if (effect.mConditions + record->mConditions > 0 && !mApplyEffectConditions)
                    continue;
                if (record->mArchetype == MagicArchetype::AccumulateMagnitude)
                {
                    f->mWardPower = wardCharge(f->mWardPower, f->mWardMax, effect.mMagnitude, seconds);
                    continue;
                }
                if (record->mArchetype != MagicArchetype::ValueModifier || !isVitalValue(record->mPrimaryValue))
                    continue;
                (void)changeVital(writer, f, *f, record->mPrimaryValue,
                    signedMagnitude(effect.mMagnitude, record->mFlags) * seconds, nullptr);
            }
            if (mClock >= c.mTraceAt)
            {
                c.mTraceAt = mClock + kConcentrationTraceSeconds;
                trace("COMBAT concentration " + spellName(spell) + " " + who(c.mCaster) + " health " + num(f->mHealth)
                    + " magicka " + num(f->mMagicka) + " ward " + num(f->mWardPower));
            }
            if (f->mMagicka <= 0.f)
            {
                endConcentration(i, "out of magicka");
                continue;
            }
            ++i;
        }
    }

    // ---- casting ---------------------------------------------------------------------------------------------------

    bool SkyrimCombat::beginCast(
        CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell, bool heldByKey, float holdSeconds)
    {
        if (caster.mDead || !ready(caster) || isBusyCasting(caster.mSubject) || mCasts.size() >= kMaxCasts)
            return false;
        const std::string name = spellName(spell);
        if (spell.mEffects.empty())
        {
            trace("COMBAT cast " + name + " refused: the spell has no effects");
            return false;
        }
        const bool concentration = spell.mCastType == MagicCast::Concentration;
        const float needed = concentration ? 0.f : static_cast<float>(spell.mCost);
        if (caster.mMagicka <= needed && (concentration || caster.mMagicka < needed))
        {
            trace("COMBAT cast " + name + " refused: not enough magicka (" + num(caster.mMagicka) + " of "
                + num(static_cast<float>(spell.mCost)) + ")");
            return false;
        }
        const std::uint8_t column = spell.mDelivery == MagicDelivery::Self ? kMagicSelf : kMagicAimed;
        const std::string_view clip = mMagicSlots.clipFor(ClipSlot::Cast, column);
        if (clip.empty())
            return false;
        Cast cast;
        cast.mCaster = caster.mSubject;
        cast.mSpell = &spell;
        cast.mStarted = mClock;
        cast.mHold = holdSeconds;
        cast.mHeldByKey = heldByKey;
        if (caster.mSubject.player)
        {
            // No first-person arms: the spell goes after the time the clip's own event gives.
            float time = 0.f;
            if (!writer.clipEventTime(clip, false, openeden::sdk::game::ClipEventKind::SpellFire, time)
                || !(time >= 0.f))
                return false; // the sidecar is still being read: asked again next frame
            cast.mFireAt = mClock + time;
        }
        else
        {
            openeden::sdk::game::ClipRequest request;
            request.slot = ClipSlot::Cast;
            request.clip = clip;
            request.clipPerSex = false;
            request.blendSeconds = 0.1f;
            request.token = mNextToken++;
            if (!writer.playClip(caster.mSubject, request))
                return false;
            cast.mToken = request.token;
            caster.mSwinging = false;
        }
        mCasts.push_back(cast);
        trace("COMBAT cast begin " + name + " by " + who(caster.mSubject) + " clip " + std::string(clip) + " token "
            + std::to_string(cast.mToken));
        return true;
    }

    bool SkyrimCombat::launchSpell(
        CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell, const SubjectState* target)
    {
        const ProjectileRecord* projectile = nullptr;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record != nullptr && record->mProjectile != kNullFormId)
            {
                projectile = mData->projectileById(record->mProjectile);
                if (projectile != nullptr)
                    break;
            }
        }
        SubjectState self;
        if (projectile == nullptr || !projectile->mHasData || !writer.subjectState(caster.mSubject, self))
        {
            trace("COMBAT cast " + spellName(spell) + ": no projectile to launch");
            return false;
        }
        const float speed = projectile->mSpeed;
        const float gravity = std::max(0.f, projectile->mGravity) * kProjectileGravityUnits;
        openeden::sdk::game::ProjectileRequest request;
        request.token = mNextToken++;
        if (target != nullptr)
        {
            const double dx = target->x - self.x;
            const double dy = target->y - self.y;
            const double h0 = std::atan2(dx, dy);
            const double ox = self.x + std::sin(h0) * static_cast<double>(kBowMuzzleForward);
            const double oy = self.y + std::cos(h0) * static_cast<double>(kBowMuzzleForward);
            const double oz = self.z + static_cast<double>(kBowReleaseHeight);
            const double ax = target->x - ox;
            const double ay = target->y - oy;
            const double horizontal = std::sqrt(ax * ax + ay * ay);
            const double rise = target->z + static_cast<double>(kBowAimHeight) - oz;
            request.x = ox;
            request.y = oy;
            request.z = oz;
            request.heading = static_cast<float>(std::atan2(ax, ay));
            request.pitch = solveLaunchPitch(static_cast<float>(horizontal), static_cast<float>(rise), speed, gravity)
                                .mPitch;
        }
        else
        {
            request.x = self.x + std::sin(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            request.y = self.y + std::cos(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            request.z = self.z + static_cast<double>(kPlayerEyeHeight);
            request.heading = self.heading;
            request.pitch = self.pitch;
        }
        request.speed = speed;
        request.gravity = gravity;
        request.range = projectile->mRange;
        request.stickDepth = 0.f;
        request.stickSeconds = 0.f;
        const std::string model = projectileModelPath(projectile->mModel);
        request.model = model;
        if (!writer.launchProjectile(caster.mSubject, request))
            return false;
        if (mSpellShots.size() >= kMaxSpellShots)
            mSpellShots.erase(mSpellShots.begin());
        mSpellShots.push_back({ request.token, caster.mSubject, &spell });
        trace("COMBAT spell launch " + spellName(spell) + " " + who(caster.mSubject) + " token "
            + std::to_string(request.token) + " heading " + num(request.heading) + " pitch " + num(request.pitch)
            + " speed " + num(request.speed));
        return true;
    }

    void SkyrimCombat::fireCast(CombatRequestWriter& writer, const Cast& cast)
    {
        Fighter* f = find(cast.mCaster);
        if (f == nullptr || f->mDead || cast.mSpell == nullptr)
            return;
        const SpellRecord& spell = *cast.mSpell;
        const std::string name = spellName(spell);
        if (spell.mCastType == MagicCast::Concentration)
        {
            const bool beam = spell.mDelivery == MagicDelivery::Aimed || spell.mDelivery == MagicDelivery::Touch;
            if (spell.mDelivery != MagicDelivery::Self && !(beam && mSpellBeams))
            {
                trace("COMBAT cast " + name + ": a held spell that is not cast on its caster is not supported");
                return;
            }
            if (f->mMagicka <= 0.f || mConcentrations.size() >= kMaxConcentrations)
                return;
            Concentration c;
            c.mCaster = cast.mCaster;
            c.mSpell = &spell;
            c.mHeldByKey = cast.mHeldByKey;
            c.mBeam = spell.mDelivery != MagicDelivery::Self;
            c.mNextVisual = mClock;
            if (isWard(*mData, spell))
                c.mEffectToken = showWardBody(writer, *f, spell);
            c.mEndAt = cast.mHeldByKey ? -1.f : mClock + cast.mHold;
            c.mTraceAt = mClock;
            if (isWard(*mData, spell))
            {
                float peak = 0.f;
                for (const SpellEffect& e : spell.mEffects)
                {
                    const MagicEffectRecord* record = mData->effectById(e.mEffect);
                    if (record != nullptr && record->mArchetype == MagicArchetype::PeakValueModifier)
                        peak = std::max(peak, e.mMagnitude);
                }
                f->mWardMax = wardMaxPower(peak, mMagicSettings, 0.f);
                f->mWardPower = 0.f;
            }
            mConcentrations.push_back(c);
            ++mSpellsCast;
            trace("COMBAT cast " + name + " by " + who(cast.mCaster) + " (held) magicka " + num(f->mMagicka));
            return;
        }
        const float cost = static_cast<float>(spell.mCost);
        if (f->mMagicka < cost)
        {
            trace("COMBAT cast " + name + " refused: not enough magicka (" + num(f->mMagicka) + " of " + num(cost)
                + ")");
            return;
        }
        f->mMagicka -= cost;
        f->mMagickaRegenAt = mClock + mMagicSettings.mDamagedMagickaRegenDelay;
        ++mSpellsCast;
        trace("COMBAT cast " + name + " by " + who(cast.mCaster) + " cost " + num(cost) + " magicka " + num(f->mMagicka)
            + " of " + num(f->mProfile.mMagickaMax));
        if (spell.mDelivery == MagicDelivery::Self)
        {
            applySpellEffects(writer, *f, *f, spell, nullptr);
            return;
        }
        if (spell.mDelivery == MagicDelivery::Touch && mSpellBeams)
        {
            touchCast(writer, *f, spell);
            return;
        }
        if (spell.mDelivery != MagicDelivery::Aimed)
        {
            trace("COMBAT cast " + name + ": delivery " + std::to_string(spell.mDelivery) + " is not supported");
            return;
        }
        f->mCombatUntil = mClock + kCombatLingerSeconds;
        if (f->mSubject.player)
        {
            (void)launchSpell(writer, *f, spell, nullptr);
            return;
        }
        SubjectState player;
        if (writer.subjectState(CombatSubject::ofPlayer(), player))
            (void)launchSpell(writer, *f, spell, &player);
    }

    bool SkyrimCombat::resolveSpellShot(
        CombatRequestWriter& writer, const openeden::sdk::game::ProjectileResult& result)
    {
        const auto shot = std::find_if(mSpellShots.begin(), mSpellShots.end(),
            [&](const SpellShot& s) { return s.mToken == result.token && s.mOwner == result.owner; });
        if (shot == mSpellShots.end())
            return false;
        const SpellShot launched = *shot;
        mSpellShots.erase(shot);
        trace("COMBAT spell ended " + spellName(*launched.mSpell) + " " + who(result.owner) + " token "
            + std::to_string(result.token) + " fate " + std::to_string(static_cast<int>(result.fate)) + " after "
            + num(result.flightSeconds) + " s");
        if (result.fate != openeden::sdk::game::ProjectileFate::HitActor
            && result.fate != openeden::sdk::game::ProjectileFate::HitPlayer)
            return true;
        Fighter* caster = find(result.owner);
        Fighter* victim = result.fate == openeden::sdk::game::ProjectileFate::HitPlayer
            ? find(CombatSubject::ofPlayer())
            : find(CombatSubject::of(result.actor));
        if (caster == nullptr || victim == nullptr || caster == victim || victim->mDead || !ready(*victim))
            return true;
        SweepHit where;
        where.target = result.fate == openeden::sdk::game::ProjectileFate::HitPlayer ? SweepTarget::Player
                                                                                     : SweepTarget::Actor;
        where.actor = result.actor;
        where.x = result.x;
        where.y = result.y;
        where.z = result.z;
        where.distance = result.distance;
        caster->mCombatUntil = mClock + kCombatLingerSeconds;
        applySpellEffects(writer, *caster, *victim, *launched.mSpell, &where);
        return true;
    }

    // ---- task 0191: beams, touch and what is drawn ---------------------------------------------------------------

    bool SkyrimCombat::aimRay(
        CombatRequestWriter& writer, const Fighter& caster, float range, openeden::sdk::game::RayRequest& out)
    {
        SubjectState self;
        if (!writer.subjectState(caster.mSubject, self))
            return false;
        out = {};
        out.range = range;
        if (caster.mSubject.player)
        {
            out.x = self.x + std::sin(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            out.y = self.y + std::cos(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            out.z = self.z + static_cast<double>(kPlayerEyeHeight);
            out.heading = self.heading;
            out.pitch = self.pitch;
            return true;
        }
        SubjectState player;
        if (!writer.subjectState(CombatSubject::ofPlayer(), player))
            return false;
        const double dx = player.x - self.x;
        const double dy = player.y - self.y;
        const double heading = std::atan2(dx, dy);
        out.x = self.x + std::sin(heading) * static_cast<double>(kBowMuzzleForward);
        out.y = self.y + std::cos(heading) * static_cast<double>(kBowMuzzleForward);
        out.z = self.z + static_cast<double>(kBowReleaseHeight);
        const double ax = player.x - out.x;
        const double ay = player.y - out.y;
        const double rise = player.z + static_cast<double>(kBowAimHeight) - out.z;
        out.heading = static_cast<float>(std::atan2(ax, ay));
        out.pitch = static_cast<float>(std::atan2(rise, std::sqrt(ax * ax + ay * ay)));
        return true;
    }

    float SkyrimCombat::beamRange(const SpellRecord& spell) const
    {
        if (spell.mDelivery == MagicDelivery::Touch)
            return kTouchReach;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr || record->mProjectile == kNullFormId)
                continue;
            const ProjectileRecord* projectile = mData->projectileById(record->mProjectile);
            if (projectile != nullptr && projectile->mHasData && projectile->mRange > 0.f)
                return projectile->mRange;
        }
        return spell.mRange > 0.f ? spell.mRange : kDefaultBeamRange;
    }

    void SkyrimCombat::launchBeamVisual(CombatRequestWriter& writer, const Fighter& caster, const SpellRecord& spell,
        const openeden::sdk::game::RayRequest& ray, float length)
    {
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr || record->mProjectile == kNullFormId)
                continue;
            const ProjectileRecord* projectile = mData->projectileById(record->mProjectile);
            if (projectile == nullptr || !projectile->mHasData)
                continue;
            const std::string model = projectileModelPath(projectile->mModel);
            if (model.empty())
                return;
            openeden::sdk::game::ProjectileRequest request;
            request.token = mNextToken++;
            request.x = ray.x;
            request.y = ray.y;
            request.z = ray.z;
            request.heading = ray.heading;
            request.pitch = ray.pitch;
            request.speed = projectile->mSpeed > 0.f ? projectile->mSpeed : 1000.f;
            request.gravity = 0.f;
            request.range = std::max(1.f, length);
            request.model = model;
            request.visualOnly = true;
            (void)writer.launchProjectile(caster.mSubject, request);
            return;
        }
    }

    void SkyrimCombat::tickBeam(
        CombatRequestWriter& writer, Concentration& beam, Fighter& caster, const SpellRecord& spell, float paidSeconds)
    {
        openeden::sdk::game::RayRequest ray;
        if (!aimRay(writer, caster, beamRange(spell), ray))
            return;
        openeden::sdk::game::RayHit hit;
        if (!writer.castRay(caster.mSubject, ray, hit))
            return;
        if (mSpellVisuals && mClock >= beam.mNextVisual)
        {
            beam.mNextVisual = mClock + kBeamVisualInterval;
            const bool missed = hit.fate == openeden::sdk::game::RayFate::Nothing;
            launchBeamVisual(writer, caster, spell, ray, missed ? ray.range : hit.distance);
        }
        Fighter* victim = nullptr;
        SweepHit where;
        where.x = hit.x;
        where.y = hit.y;
        where.z = hit.z;
        where.distance = hit.distance;
        if (hit.fate == openeden::sdk::game::RayFate::Actor)
        {
            victim = find(CombatSubject::of(hit.actor));
            where.target = SweepTarget::Actor;
            where.actor = hit.actor;
        }
        else if (hit.fate == openeden::sdk::game::RayFate::Player)
        {
            victim = find(CombatSubject::ofPlayer());
            where.target = SweepTarget::Player;
        }
        if (victim == nullptr || victim == &caster || victim->mDead || !ready(*victim) || !(paidSeconds > 0.f))
            return;
        caster.mCombatUntil = mClock + kCombatLingerSeconds;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr || !record->mHasData)
                continue;
            if (effect.mConditions + record->mConditions > 0 && !mApplyEffectConditions)
                continue;
            const std::uint32_t archetype = record->mArchetype;
            const bool value = archetype == MagicArchetype::ValueModifier || archetype == MagicArchetype::Dual
                || archetype == MagicArchetype::Absorb;
            if (!value || !isVitalValue(record->mPrimaryValue))
                continue;
            const bool hostile = (record->mFlags & MagicFlag::Hostile) != 0;
            float magnitude = signedMagnitude(effect.mMagnitude, record->mFlags);
            if (hostile)
                magnitude = (magnitude < 0.f ? -1.f : 1.f)
                    * resistedMagnitude(std::abs(magnitude), resistOf(*victim, record->mResistValue));
            const float taken
                = changeVital(writer, &caster, *victim, record->mPrimaryValue, magnitude * paidSeconds, &where);
            if (archetype == MagicArchetype::Dual && isVitalValue(record->mSecondaryValue) && !victim->mDead)
                (void)changeVital(writer, &caster, *victim, record->mSecondaryValue,
                    magnitude * record->mSecondaryWeight * paidSeconds, &where);
            if (archetype == MagicArchetype::Absorb && taken != 0.f)
                (void)changeVital(writer, nullptr, caster, record->mPrimaryValue, -taken, nullptr);
            if (victim->mDead)
                break;
        }
        if (mClock >= beam.mTraceAt)
            trace("COMBAT beam " + spellName(spell) + " " + who(caster.mSubject) + " -> " + who(victim->mSubject)
                + " distance " + num(hit.distance) + " health " + num(victim->mHealth));
    }

    void SkyrimCombat::touchCast(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell)
    {
        caster.mCombatUntil = mClock + kCombatLingerSeconds;
        openeden::sdk::game::RayRequest ray;
        openeden::sdk::game::RayHit hit;
        if (!aimRay(writer, caster, kTouchReach, ray) || !writer.castRay(caster.mSubject, ray, hit))
            return;
        Fighter* victim = nullptr;
        SweepHit where;
        where.x = hit.x;
        where.y = hit.y;
        where.z = hit.z;
        where.distance = hit.distance;
        if (hit.fate == openeden::sdk::game::RayFate::Actor)
        {
            victim = find(CombatSubject::of(hit.actor));
            where.target = SweepTarget::Actor;
            where.actor = hit.actor;
        }
        else if (hit.fate == openeden::sdk::game::RayFate::Player)
        {
            victim = find(CombatSubject::ofPlayer());
            where.target = SweepTarget::Player;
        }
        if (victim == nullptr || victim == &caster || victim->mDead || !ready(*victim))
        {
            trace("COMBAT touch " + spellName(spell) + " " + who(caster.mSubject) + " found nothing in reach");
            return;
        }
        applySpellEffects(writer, caster, *victim, spell, &where);
    }

    std::uint32_t SkyrimCombat::showWardBody(
        CombatRequestWriter& writer, const Fighter& caster, const SpellRecord& spell)
    {
        if (!mSpellVisuals)
            return 0;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr || record->mHitEffectArt == kNullFormId)
                continue;
            const std::string model = projectileModelPath(std::string(mData->artModelById(record->mHitEffectArt)));
            if (model.empty())
            {
                trace("COMBAT ward body of " + spellName(spell) + ": the hit art has no model");
                continue;
            }
            openeden::sdk::game::EffectRequest request;
            request.token = mNextToken++;
            request.heightOffset = kWardBodyHeight;
            request.model = model;
            const bool shown = writer.showEffect(caster.mSubject, request);
            trace("COMBAT ward body " + model + (shown ? " shown on " : " refused on ") + who(caster.mSubject));
            return shown ? request.token : 0;
        }
        trace("COMBAT ward body of " + spellName(spell) + ": no effect has hit art");
        return 0;
    }

    // ---- who casts when --------------------------------------------------------------------------------------------

    void SkyrimCombat::runPlayerCast(CombatRequestWriter& writer)
    {
        if (mTestPlayerSpell.empty())
        {
            mCastEdges.clear();
            return;
        }
        Fighter& player = mFighters.front();
        if (!ready(player))
        {
            mCastEdges.clear();
            return;
        }
        if (!mPlayerSpellLooked)
        {
            mPlayerSpellLooked = true;
            mPlayerSpell = mData->spellByEditorId(mTestPlayerSpell);
            trace("COMBAT player spell '" + mTestPlayerSpell + "' "
                + (mPlayerSpell != nullptr ? "found" : "NOT found"));
        }
        if (mPlayerSpell == nullptr)
        {
            mCastEdges.clear();
            return;
        }
        const bool concentration = mPlayerSpell->mCastType == MagicCast::Concentration;
        // The automatic test: press every period, and for a held spell release after the hold.
        if (mPlayerCastEvery > 0.f && !mPlayerCastHeld && !mPlayerCastWanted && mClock >= mNextPlayerCast)
        {
            mCastEdges.push_back(true);
            mNextPlayerCast = mClock + mPlayerCastEvery;
            mAutoCastReleaseAt = concentration ? mClock + mPlayerCastHold : -1.f;
        }
        if (mAutoCastReleaseAt >= 0.f && mClock >= mAutoCastReleaseAt)
        {
            mCastEdges.push_back(false);
            mAutoCastReleaseAt = -1.f;
        }
        for (const bool pressed : mCastEdges)
        {
            if (pressed)
            {
                if (mPlayerCastHeld || mPlayerCastWanted || isBusyCasting(player.mSubject))
                    continue; // a press while a cast runs is dropped
                const float needed = concentration ? 0.f : static_cast<float>(mPlayerSpell->mCost);
                if (player.mMagicka <= needed && (concentration || player.mMagicka < needed))
                {
                    trace("COMBAT cast " + spellName(*mPlayerSpell) + " refused: not enough magicka ("
                        + num(player.mMagicka) + " of " + num(static_cast<float>(mPlayerSpell->mCost)) + ")");
                    continue; // a press with no magicka to pay for it is dropped, not retried every frame
                }
                mPlayerCastHeld = true;
                mPlayerCastWanted = true;
                mPlayerCastWantedAt = mClock;
                continue;
            }
            mPlayerCastHeld = false;
            mPlayerCastWanted = false;
            // A held spell that is let go of before it goes never starts (a fire and forget spell still goes: a tap).
            std::erase_if(mCasts, [](const Cast& c) {
                return c.mCaster.player && c.mHeldByKey && c.mSpell != nullptr
                    && c.mSpell->mCastType == MagicCast::Concentration;
            });
            for (std::size_t i = mConcentrations.size(); i > 0; --i)
                if (mConcentrations[i - 1].mCaster.player && mConcentrations[i - 1].mHeldByKey)
                    endConcentration(i - 1, "released");
        }
        mCastEdges.clear();
        if (!mPlayerCastWanted)
            return;
        if (mClock - mPlayerCastWantedAt > kCastEventTimeoutSeconds)
        {
            mPlayerCastWanted = false;
            trace("COMBAT player cast gave up: the cast clip's timing never came");
            return;
        }
        if (beginCast(writer, player, *mPlayerSpell, true, mPlayerCastHold))
        {
            mPlayerCastWanted = false;
            if (!concentration)
                mPlayerCastHeld = false; // nothing is held for a spell that goes at once
        }
    }

    void SkyrimCombat::startTestCasts(CombatRequestWriter& writer)
    {
        for (TestCaster& tc : mTestCasters)
        {
            if (!tc.mLooked)
            {
                tc.mLooked = true;
                tc.mSpell = mData->spellByEditorId(tc.mSpellName);
                trace("COMBAT test caster '" + tc.mActor + "' spell '" + tc.mSpellName + "' "
                    + (tc.mSpell != nullptr ? "found" : "NOT found"));
            }
            if (tc.mSpell == nullptr || !tc.mHandle.valid())
                continue;
            if (tc.mNext < 0.f)
                tc.mNext = mCastPeriod; // the first cast comes when the combat clock reaches one period
            if (mClock < tc.mNext)
                continue;
            Fighter* f = find(CombatSubject::of(tc.mHandle));
            if (f == nullptr || f->mDead || f->mStaggered || isBusyCasting(f->mSubject) || !ready(*f))
                continue;
            tc.mNext = mClock + mCastPeriod;
            if (isPlainHeal(*mData, *tc.mSpell) && f->mHealth >= f->mProfile.mHealthMax)
                continue; // nothing to heal
            if (tc.mSpell->mDelivery == MagicDelivery::Aimed)
            {
                SubjectState player;
                SubjectState self;
                if (!writer.subjectState(CombatSubject::ofPlayer(), player) || !writer.subjectState(f->mSubject, self))
                    continue;
                (void)writer.faceHeading(
                    f->mSubject, static_cast<float>(std::atan2(player.x - self.x, player.y - self.y)));
            }
            (void)beginCast(writer, *f, *tc.mSpell, false, mCastHold);
        }
    }

    void SkyrimCombat::updateMagic(CombatRequestWriter& writer, float step)
    {
        if (!mHaveMagicSettings)
        {
            mMagicSettings = mData->magicSettings();
            mHaveMagicSettings = true;
        }
        if (mPendingHides.capacity() == 0)
            mPendingHides.reserve(kMaxConcentrations);
        for (const std::uint32_t token : mPendingHides)
            (void)writer.hideEffect(token);
        mPendingHides.clear();
        tickEffects(writer, step);
        tickConcentrations(writer, step);
        for (std::size_t i = 0; i < mCasts.size();)
        {
            const Cast cast = mCasts[i];
            const Fighter* f = find(cast.mCaster);
            if (f == nullptr || f->mDead)
            {
                mCasts.erase(mCasts.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            bool fire = false;
            if (cast.mToken != 0)
            {
                fire = cast.mFireSeen;
                if (!fire && mClock - cast.mStarted > kCastEventTimeoutSeconds)
                {
                    trace("COMBAT cast " + spellName(*cast.mSpell) + " gave up: no SpellFire event");
                    mCasts.erase(mCasts.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            }
            else
                fire = mClock >= cast.mFireAt;
            if (!fire)
            {
                ++i;
                continue;
            }
            mCasts.erase(mCasts.begin() + static_cast<std::ptrdiff_t>(i));
            fireCast(writer, cast);
        }
        runPlayerCast(writer);
        startTestCasts(writer);
    }
}
