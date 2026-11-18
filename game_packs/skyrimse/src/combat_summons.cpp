// Task 0250: summons and raise dead, the combat side (members of SkyrimCombat). A Summon Creature effect (MGEF
// archetype 18) asks the world, through SummonHooks, for a new actor of the effect's associated NPC_ 150 units ahead of
// the caster; a Reanimate effect (22) does the same at a corpse's place and hides the corpse. The new actor is an ally
// of the caster (a follower of the player for the player's summons), lives for the effect's duration and is then taken
// out of the world (a summon) or falls dead where it stands (a raised corpse). Main thread only, from combatUpdate and
// the cast; nothing here reads a clock but the combat clock. Behind `combat spell delivery` plus the summons switch
// (off by default). See docs/tasks/0250-skyrim-summons-and-raise-dead.md.
#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
        constexpr std::int32_t kHealthValue = 24;

        std::string subjectText(const CombatSubject& subject)
        {
            return subject.player ? std::string("player") : "actor " + std::to_string(subject.actor.reference);
        }

        std::string spellText(const SpellRecord& spell)
        {
            return spell.mCommon.mEditorId.empty() ? std::to_string(spell.mCommon.mId) : spell.mCommon.mEditorId;
        }

        std::string numText(double value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.1f", value);
            return buffer;
        }
    }

    bool SkyrimCombat::spellReanimates(const SpellRecord& spell) const
    {
        if (!mSummonsOn || !mSpellDelivery || mData == nullptr)
            return false;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record != nullptr && record->mHasData && record->mArchetype == SummonArchetype::Reanimate)
                return true;
        }
        return false;
    }

    bool SkyrimCombat::spellSummons(const SpellRecord& spell) const
    {
        if (!mSummonsOn || !mSpellDelivery || mData == nullptr)
            return false;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record != nullptr && record->mHasData && record->mArchetype == SummonArchetype::SummonCreature)
                return true;
        }
        return false;
    }

    bool SkyrimCombat::applySummonEffect(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell,
        const SpellEffect& effect, const MagicEffectRecord& record)
    {
        if (!mSummonsOn || record.mArchetype != SummonArchetype::SummonCreature)
            return false;
        const std::string name = spellText(spell);
        if (!mSummonHooks.place || !mSummonHooks.resolveBase)
        {
            if (mTrace)
                trace("COMBAT summon failed: " + name + " (the world gives no hooks)");
            return true;
        }
        const SummonFormId base
            = mSummonHooks.resolveBase(record.mAssociatedItem, caster.mProfile.mLevel, mSummonCount);
        SubjectState me;
        if (base == 0 || !writer.subjectState(caster.mSubject, me))
        {
            if (mTrace)
                trace("COMBAT summon failed: " + name
                    + (base == 0 ? " (no actor for its associated form)" : " (no caster state)"));
            return true;
        }
        const SummonSpot spot = summonSpawnSpot(
            me.x, me.y, me.z, static_cast<double>(me.heading), kSummonSpawnDistance, kSummonSpawnRise);
        const WorldId id = mSummonHooks.place(base, spot.x, spot.y, spot.z, me.heading);
        if (id == kNoWorldId)
        {
            if (mTrace)
                trace("COMBAT summon failed: " + name + " (the world refused the actor)");
            return true;
        }
        while (mSummons.countOf(caster.mSubject) >= mMaxSummons)
        {
            const SummonEntry* oldest = mSummons.oldestOf(caster.mSubject);
            if (oldest == nullptr)
                break;
            dismissSummon(&writer, oldest->mReference, "replaced");
        }
        SummonEntry entry;
        entry.mReference = id;
        entry.mMaster = caster.mSubject;
        entry.mSpell = spell.mCommon.mId;
        entry.mEffect = record.mCommon.mId;
        entry.mBase = base;
        entry.mKind = SummonKind::Summoned;
        const float seconds = summonSeconds(effect.mDuration);
        entry.mExpiresAt = mClock + seconds;
        if (!mSummons.add(entry))
        {
            if (mSummonHooks.remove)
                mSummonHooks.remove(id);
            return true;
        }
        ++mSummonCount;
        if (mTrace)
            trace("COMBAT summon " + subjectText(caster.mSubject) + " conjures " + name + " as placement "
                + std::to_string(id) + " base " + std::to_string(base) + " at " + numText(spot.x) + ","
                + numText(spot.y) + " for " + numText(seconds) + " s");
        return true;
    }

    bool SkyrimCombat::reanimateInSights(CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell)
    {
        if (!mSummonsOn)
            return false;
        const SpellEffect* effect = nullptr;
        for (const SpellEffect& e : spell.mEffects)
        {
            const MagicEffectRecord* record = mData != nullptr ? mData->effectById(e.mEffect) : nullptr;
            if (record != nullptr && record->mHasData && record->mArchetype == SummonArchetype::Reanimate)
            {
                effect = &e;
                break;
            }
        }
        if (effect == nullptr)
            return false;
        const std::string name = spellText(spell);
        SubjectState me;
        if (!mSummonHooks.place || !writer.subjectState(caster.mSubject, me))
        {
            if (mTrace)
                trace("COMBAT reanimate failed: " + name + " (no hooks or no caster state)");
            return true;
        }
        Fighter* best = nullptr;
        SubjectState bestState;
        double bestDistance = std::numeric_limits<double>::max();
        for (Fighter& f : mFighters)
        {
            if (!f.mDead || f.mSubject.player || !f.mSubject.actor.valid() || &f == &caster)
                continue;
            SubjectState s;
            if (!writer.subjectState(f.mSubject, s))
                continue;
            if (!corpseInSights(
                    me.x, me.y, static_cast<double>(me.heading), s.x, s.y, kReanimateReach, kReanimateMinDot))
                continue;
            const double d = std::hypot(s.x - me.x, s.y - me.y);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = &f;
                bestState = s;
            }
        }
        if (best == nullptr)
        {
            if (mTrace)
                trace(
                    "COMBAT reanimate " + name + " " + subjectText(caster.mSubject) + " found no corpse in the sights");
            return true;
        }
        if (!ready(*best) || !reanimateLevelAllowed(best->mProfile.mLevel, effect->mMagnitude))
        {
            if (mTrace)
                trace("COMBAT reanimate " + name + ": " + subjectText(best->mSubject) + " is level "
                    + std::to_string(best->mProfile.mLevel) + ", above " + numText(effect->mMagnitude));
            return true;
        }
        const SummonFormId base = static_cast<SummonFormId>(best->mBase);
        const WorldId corpse = best->mSubject.actor.reference;
        const WorldId id = mSummonHooks.place(base, bestState.x, bestState.y, bestState.z, bestState.heading);
        if (id == kNoWorldId)
        {
            if (mTrace)
                trace("COMBAT reanimate failed: " + name + " (the world refused the actor)");
            return true;
        }
        if (mSummonHooks.remove)
            mSummonHooks.remove(corpse); // the corpse is hidden while the raised actor stands in its place
        if (mCorpseRaised)
            mCorpseRaised(corpse); // task 0254
        while (mSummons.countOf(caster.mSubject) >= mMaxSummons)
        {
            const SummonEntry* oldest = mSummons.oldestOf(caster.mSubject);
            if (oldest == nullptr)
                break;
            dismissSummon(&writer, oldest->mReference, "replaced");
        }
        SummonEntry entry;
        entry.mReference = id;
        entry.mMaster = caster.mSubject;
        entry.mSpell = spell.mCommon.mId;
        entry.mEffect = effect->mEffect;
        entry.mBase = base;
        entry.mKind = SummonKind::Reanimated;
        entry.mCorpse = corpse;
        const float seconds = summonSeconds(effect->mDuration);
        entry.mExpiresAt = mClock + seconds;
        if (!mSummons.add(entry))
            return true;
        ++mSummonCount;
        if (mTrace)
            trace("COMBAT reanimate " + subjectText(caster.mSubject) + " raises " + subjectText(best->mSubject)
                + " as placement " + std::to_string(id) + " for " + numText(seconds) + " s");
        return true;
    }

    void SkyrimCombat::dismissSummon(CombatRequestWriter* writer, WorldId reference, const char* why)
    {
        const SummonEntry* found = mSummons.find(reference);
        if (found == nullptr)
            return;
        const SummonEntry entry = *found;
        mSummons.erase(reference);
        if (mTrace)
            trace("COMBAT summon ended placement " + std::to_string(reference) + " (" + why + ")");
        if (entry.mArrived && mSummonHooks.follow)
            mSummonHooks.follow(reference, false);
        if (entry.mKind == SummonKind::Summoned || !entry.mArrived)
        {
            if (mSummonHooks.remove)
                mSummonHooks.remove(reference);
            return;
        }
        // A raised corpse falls dead where it stands and stays as a corpse.
        Fighter* f = find(CombatSubject::of(entry.mHandle));
        if (f != nullptr && !f->mDead && writer != nullptr && ready(*f))
            (void)changeVital(
                *writer, nullptr, *f, kHealthValue, -(f->mProfile.mHealthMax + f->mHealth + 1.f), nullptr);
    }

    bool testKillMatches(std::string_view spec, std::string_view actor, bool& multi)
    {
        multi = spec.find(',') != std::string_view::npos || (!spec.empty() && spec.back() == '*');
        while (!spec.empty())
        {
            const std::size_t comma = spec.find(',');
            std::string_view name = spec.substr(0, comma);
            spec = comma == std::string_view::npos ? std::string_view() : spec.substr(comma + 1);
            if (name.empty())
                continue;
            if (name.back() == '*')
            {
                multi = true;
                name.remove_suffix(1);
                if (actor.substr(0, name.size()) == name && !actor.empty())
                    return true;
            }
            else if (name == actor && !actor.empty())
                return true;
        }
        return false;
    }

    void SkyrimCombat::setTestKill(std::string_view editorId)
    {
        mTestKill.assign(editorId);
        for (char& c : mTestKill)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    void SkyrimCombat::tickSummons(CombatRequestWriter& writer)
    {
        // Task 0306: `summons test kill = <editor id>@<seconds>` kills it no earlier than that combat second (the cultist
        // of the ambush is created long after the data was read, and must talk first)
        const std::size_t killAt = mTestKill.find('@');
        const std::string killName = killAt == std::string::npos ? mTestKill : mTestKill.substr(0, killAt);
        const float killAfter = killAt == std::string::npos
            ? kSummonTestKillSeconds
            : std::max(kSummonTestKillSeconds, static_cast<float>(std::atof(mTestKill.c_str() + killAt + 1)));
        if (!mTestKill.empty() && mClock >= killAfter)
        {
            for (const auto& named : mActorNames)
            {
                bool multi = false;
                if (!testKillMatches(killName, named.second, multi))
                    continue;
                Fighter* victim = find(CombatSubject::of(named.first));
                const bool alive = victim != nullptr && !victim->mDead && ready(*victim);
                if (alive)
                {
                    (void)changeVital(writer, nullptr, *victim, kHealthValue,
                        -(victim->mProfile.mHealthMax + victim->mHealth + 1.f), nullptr);
                    if (mTrace)
                        trace("COMBAT test kill " + subjectText(victim->mSubject));
                    if (!multi)
                        mTestKill.clear();
                }
                // a list kills one actor an update (a stable order); a single name stops at its first match
                if (alive || !multi)
                    break;
            }
        }
        if (!mSummonsOn || mSummons.size() == 0)
            return;
        const float now = mClock;
        // Work on the references: dismissing changes the ledger.
        std::vector<WorldId> references;
        references.reserve(mSummons.size());
        for (const SummonEntry& e : mSummons.entries())
            references.push_back(e.mReference);
        for (const WorldId reference : references)
        {
            SummonEntry* entry = mSummons.find(reference);
            if (entry == nullptr)
                continue;
            const Fighter* me = entry->mArrived ? find(CombatSubject::of(entry->mHandle)) : nullptr;
            if (me != nullptr && me->mDead)
            {
                if (entry->mKind == SummonKind::Reanimated)
                {
                    // it is a corpse again: nothing to take out
                    if (mSummonHooks.follow)
                        mSummonHooks.follow(reference, false);
                    mSummons.erase(reference);
                    if (mTrace)
                        trace("COMBAT summon ended placement " + std::to_string(reference) + " (killed)");
                    continue;
                }
                if (entry->mDeadAt < 0.f)
                    entry->mDeadAt = now;
                else if (now - entry->mDeadAt >= kSummonDeadLinger)
                    dismissSummon(&writer, reference, "killed");
                continue;
            }
            if (!entry->mMaster.player)
            {
                const Fighter* master = find(entry->mMaster);
                if (master == nullptr || master->mDead)
                {
                    dismissSummon(&writer, reference, "its master is gone");
                    continue;
                }
            }
            if (now >= entry->mExpiresAt)
                dismissSummon(&writer, reference, "its time is up");
        }
    }

    void SkyrimCombat::summonArrived(const ActorInfo& info)
    {
        SummonEntry* entry = mSummons.find(info.handle.reference);
        if (entry == nullptr)
            return;
        entry->mArrived = true;
        entry->mHandle = info.handle;
        const bool playerSide
            = entry->mMaster.player || (mFollowerQuery && mFollowerQuery(entry->mMaster.actor.reference));
        if (playerSide && mSummonHooks.follow)
            mSummonHooks.follow(info.handle.reference, true);
        if (mTrace)
            trace("COMBAT summon arrived placement " + std::to_string(info.handle.reference));
    }

    void SkyrimCombat::summonGone(ActorHandle handle)
    {
        const SummonEntry* entry = mSummons.findByHandle(handle);
        if (entry == nullptr)
            return;
        const WorldId reference = entry->mReference;
        mSummons.erase(reference);
        if (mSummonHooks.follow)
            mSummonHooks.follow(reference, false);
        if (mTrace)
            trace("COMBAT summon ended placement " + std::to_string(reference) + " (the core removed it)");
    }

    const SkyrimCombat::Fighter* SkyrimCombat::summonMasterFighter(const Fighter& fighter) const
    {
        if (!mSummonsOn || fighter.mSubject.player)
            return nullptr;
        const SummonEntry* entry = mSummons.find(fighter.mSubject.actor.reference);
        if (entry == nullptr || entry->mMaster.player)
            return nullptr; // a player's summon is on the player's side through the follower flag
        return find(entry->mMaster);
    }
}
