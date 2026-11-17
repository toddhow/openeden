// Task 0242: spell delivery. A target actor spell (Heal Other, Calm) goes to the actor in the caster's sights; a target
// location spell (the runes) flies as a projectile, is placed where it lands and goes off when an enemy comes within
// the projectile's trigger proximity; an effect with an area, or one whose projectile names an explosion, reaches
// everyone within that radius of where the shot ended. Behind `combat spell delivery` (off by default). Main thread
// only, in combatUpdate; nothing here reads a clock but the combat clock and nothing allocates per frame beyond the
// trace lines. See docs/tasks/0242-skyrim-spell-delivery.md.
#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
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

        /// Is any effect of the spell hostile (a heal on a friend does not start a fight).
        bool anyHostile(CombatData& data, const SpellRecord& spell)
        {
            for (const SpellEffect& effect : spell.mEffects)
            {
                const MagicEffectRecord* record = data.effectById(effect.mEffect);
                if (record != nullptr && (record->mFlags & MagicFlag::Hostile) != 0)
                    return true;
            }
            return false;
        }
    }

    float SkyrimCombat::effectRadius(const SpellRecord& spell, const SpellEffect& effect) const
    {
        (void)spell;
        if (effect.mArea > 0)
            return static_cast<float>(effect.mArea) * kUnitsPerFoot;
        const MagicEffectRecord* record = mData->effectById(effect.mEffect);
        if (record == nullptr || record->mProjectile == kNullFormId)
            return 0.f;
        const ProjectileRecord* projectile = mData->projectileById(record->mProjectile);
        if (projectile == nullptr || projectile->mExplosion == kNullFormId)
            return 0.f;
        const ExplosionRecord* explosion = mData->explosionById(projectile->mExplosion);
        return explosion != nullptr && explosion->mHasData && std::isfinite(explosion->mRadius) ? explosion->mRadius
                                                                                                : 0.f;
    }

    bool SkyrimCombat::hasAreaEffect(const SpellRecord& spell) const
    {
        for (const SpellEffect& effect : spell.mEffects)
            if (effectRadius(spell, effect) > 0.f)
                return true;
        return false;
    }

    void SkyrimCombat::targetActorCast(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell)
    {
        const std::string name = spellName(spell);
        if (anyHostile(*mData, spell))
            caster.mCombatUntil = mClock + kCombatLingerSeconds;
        RayRequest ray;
        RayHit hit;
        if (!aimRay(writer, caster, beamRange(spell), ray) || !writer.castRay(caster.mSubject, ray, hit))
        {
            trace("COMBAT target " + name + " " + who(caster.mSubject) + " found no actor in the sights (no ray)");
            return;
        }
        Fighter* victim = nullptr;
        SweepHit where;
        where.x = hit.x;
        where.y = hit.y;
        where.z = hit.z;
        where.distance = hit.distance;
        if (hit.fate == RayFate::Actor)
        {
            victim = find(CombatSubject::of(hit.actor));
            where.target = SweepTarget::Actor;
            where.actor = hit.actor;
        }
        else if (hit.fate == RayFate::Player)
        {
            victim = find(CombatSubject::ofPlayer());
            where.target = SweepTarget::Player;
        }
        if (victim == nullptr || victim == &caster || victim->mDead || !ready(*victim))
        {
            trace("COMBAT target " + name + " " + who(caster.mSubject) + " found no actor in the sights");
            return;
        }
        trace("COMBAT target " + name + " " + who(caster.mSubject) + " -> " + who(victim->mSubject) + " at "
            + num(hit.distance));
        applySpellEffects(writer, caster, *victim, spell, &where);
    }

    bool SkyrimCombat::deliverAtEnd(
        CombatRequestWriter& writer, const ProjectileResult& result, Fighter& caster, const SpellRecord& spell)
    {
        const bool onActor = result.fate == ProjectileFate::HitActor || result.fate == ProjectileFate::HitPlayer;
        if (!onActor && result.fate != ProjectileFate::HitWorld)
            return false; // it ran out of range: nothing lands
        if (spell.mDelivery == MagicDelivery::TargetLocation)
        {
            placeRune(writer, caster, spell, result.x, result.y, result.z, result.token);
            return true;
        }
        if (spell.mDelivery != MagicDelivery::Aimed || !hasAreaEffect(spell))
            return false;
        caster.mCombatUntil = mClock + kCombatLingerSeconds;
        SweepHit direct;
        direct.target = result.fate == ProjectileFate::HitPlayer ? SweepTarget::Player
                                                                 : (onActor ? SweepTarget::Actor : SweepTarget::World);
        direct.actor = result.actor;
        direct.x = result.x;
        direct.y = result.y;
        direct.z = result.z;
        direct.distance = result.distance;
        explodeAt(writer, caster, spell, result.x, result.y, result.z, onActor ? &direct : nullptr);
        return true;
    }

    void SkyrimCombat::explodeAt(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell, double x,
        double y, double z, const SweepHit* direct)
    {
        ++mExplosionsDone;
        trace("COMBAT explosion " + spellName(spell) + " by " + who(caster.mSubject) + " at "
            + num(static_cast<float>(x)) + " " + num(static_cast<float>(y)) + " " + num(static_cast<float>(z)));
        Fighter* directVictim = nullptr;
        if (direct != nullptr)
            directVictim = direct->target == SweepTarget::Player ? find(CombatSubject::ofPlayer())
                                                                 : find(CombatSubject::of(direct->actor));
        std::size_t reached = 0;
        for (std::size_t i = 0; i < mFighters.size(); ++i)
        {
            Fighter& other = mFighters[i];
            if (&other == &caster || other.mDead || !ready(other))
                continue;
            const bool isDirect = &other == directVictim;
            if (!isDirect && !mayHit(caster, other))
                continue;
            double distanceSquared = std::numeric_limits<double>::infinity();
            SubjectState there;
            if (writer.subjectState(other.mSubject, there))
            {
                const double dx = there.x - x;
                const double dy = there.y - y;
                const double dz = there.z - z;
                distanceSquared = dx * dx + dy * dy + dz * dz;
            }
            bool blocked = false;
            bool touched = false;
            for (const SpellEffect& effect : spell.mEffects)
            {
                if (blocked)
                    break;
                const MagicEffectRecord* record = mData->effectById(effect.mEffect);
                if (record == nullptr || !record->mHasData)
                    continue;
                const float radius = effectRadius(spell, effect);
                // The actor the shot hit gets every effect (the impact is on its body, which may be farther than a
                // tiny radius); an effect with a radius also reaches everyone inside it.
                const bool reaches = isDirect
                    || (radius > 0.f && distanceSquared <= static_cast<double>(radius) * static_cast<double>(radius));
                if (!reaches)
                    continue;
                if (!effectConditionsAllow(caster, other, effect, *record))
                    continue;
                touched = true;
                applyOneEffect(writer, caster, other, spell, effect, *record, blocked, isDirect ? direct : nullptr);
                if (other.mDead)
                    break;
            }
            if (touched)
                ++reached;
            if (blocked)
                trace("COMBAT spell " + spellName(spell) + " blocked by the ward of " + who(other.mSubject));
        }
        trace("COMBAT explosion " + spellName(spell) + " reached " + std::to_string(reached));
    }

    void SkyrimCombat::placeRune(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell, double x,
        double y, double z, std::uint32_t token)
    {
        const std::string name = spellName(spell);
        if (mRunes.size() >= kMaxRunes)
        {
            trace("COMBAT rune " + name + " replaced the oldest of " + std::to_string(mRunes.size()));
            (void)writer.cancelProjectile(mRunes.front().mToken); // task 0262: its glyph goes with it
            mRunes.erase(mRunes.begin());
        }
        Rune rune;
        rune.mCaster = caster.mSubject;
        rune.mSpell = &spell;
        rune.mX = x;
        rune.mY = y;
        rune.mZ = z;
        rune.mTrigger = kDefaultRuneProximity;
        rune.mToken = token;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            const ProjectileRecord* projectile = record != nullptr && record->mProjectile != kNullFormId
                ? mData->projectileById(record->mProjectile)
                : nullptr;
            if (projectile != nullptr && projectile->mProximity > 0.f)
            {
                rune.mTrigger = projectile->mProximity;
                break;
            }
        }
        rune.mExpiresAt = mClock + kRuneLifetimeSeconds;
        mRunes.push_back(rune);
        trace("COMBAT rune " + name + " placed by " + who(caster.mSubject) + " at " + num(static_cast<float>(x)) + " "
            + num(static_cast<float>(y)) + " " + num(static_cast<float>(z)) + " trigger " + num(rune.mTrigger));
    }

    bool SkyrimCombat::runeTriggeredBy(const Fighter& caster, const Fighter& other) const
    {
        if (relationOf(caster, other) == ActorRelation::Enemy)
            return true;
        if (other.mSubject.player)
            return false;
        const AiActor* engaged = aiFind(other.mSubject.actor);
        return engaged != nullptr && engaged->mTarget == caster.mSubject;
    }

    void SkyrimCombat::tickRunes(CombatRequestWriter& writer)
    {
        for (std::size_t i = 0; i < mRunes.size();)
        {
            const Rune rune = mRunes[i];
            const SpellRecord& spell = *rune.mSpell;
            Fighter* caster = find(rune.mCaster);
            if (caster == nullptr)
            {
                (void)writer.cancelProjectile(rune.mToken);
                mRunes.erase(mRunes.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (mClock >= rune.mExpiresAt)
            {
                trace("COMBAT rune " + spellName(spell) + " expired (of " + who(rune.mCaster) + ")");
                (void)writer.cancelProjectile(rune.mToken); // task 0262
                mRunes.erase(mRunes.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            const Fighter* trigger = nullptr;
            const double reach = static_cast<double>(rune.mTrigger);
            for (std::size_t j = 0; j < mFighters.size() && trigger == nullptr; ++j)
            {
                Fighter& other = mFighters[j];
                if (&other == caster || other.mDead || !ready(other) || !runeTriggeredBy(*caster, other))
                    continue;
                SubjectState there;
                if (!writer.subjectState(other.mSubject, there))
                    continue;
                const double dx = there.x - rune.mX;
                const double dy = there.y - rune.mY;
                const double dz = there.z - rune.mZ;
                if (dx * dx + dy * dy + dz * dz <= reach * reach)
                    trigger = &other;
            }
            if (trigger == nullptr)
            {
                ++i;
                continue;
            }
            const CombatSubject by = trigger->mSubject;
            mRunes.erase(mRunes.begin() + static_cast<std::ptrdiff_t>(i));
            ++mRunesSetOff;
            (void)writer.cancelProjectile(rune.mToken); // task 0262: the glyph goes when the rune does
            trace("COMBAT rune " + spellName(spell) + " set off by " + who(by) + " (of " + who(rune.mCaster) + ")");
            explodeAt(writer, *caster, spell, rune.mX, rune.mY, rune.mZ, nullptr);
        }
    }
}
