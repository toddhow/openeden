#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

// Task 0233: the effects that change what an actor can do, and weakness. Paralysis holds an NPC (it falls, acts no more
// and keeps its pose) for the effect's duration; frenzy sets it on the nearest actor (the AI's own engagement with a
// new target, so every blow, arrow and spell it makes goes through the ordinary hit paths); fear makes it flee its
// caster (the AI's own flight, with `AiView::mFeared`). Weakness is a modifier on a resistance that the damage path of
// the magic runtime reads back. Everything here ends by itself: runStatuses is called once an update and restores each
// actor when its time is up. The player is never held, frightened or frenzied: the engine owns his body.
//
// Paralysis keeps no body of its own in the core: the clip that makes the fall is a Death-slot clip (the core holds the
// last pose of such a clip; the combat contract has no "hold" request and none is added), and the actor gets up with a
// Stagger-slot clip. VERIFIED archetype numbers: magic_status.hpp. The level gate of fear and frenzy is
// RECALLED-VERIFY.
namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ClipRequest;
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::SubjectState;

        /// How often a frenzied actor looks for its nearest target again (the combat clock, seconds).
        constexpr float kFrenzyRetargetPeriod = 0.5f;

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
    }

    bool SkyrimCombat::isParalyzed(const Fighter& fighter) const
    {
        return statusActive(fighter.mStatus.mParalyzedUntil, mClock);
    }

    bool SkyrimCombat::isFeared(const Fighter& fighter) const
    {
        return statusActive(fighter.mStatus.mFearUntil, mClock);
    }

    bool SkyrimCombat::paralyzed(CombatSubject subject) const
    {
        const Fighter* f = find(subject);
        return f != nullptr && isParalyzed(*f);
    }

    bool SkyrimCombat::feared(CombatSubject subject) const
    {
        const Fighter* f = find(subject);
        return f != nullptr && isFeared(*f);
    }

    bool SkyrimCombat::frenzied(CombatSubject subject) const
    {
        const Fighter* f = find(subject);
        return f != nullptr && statusActive(f->mStatus.mFrenzyUntil, mClock);
    }

    float SkyrimCombat::weaknessOf(const Fighter& target, std::int32_t resistValue) const
    {
        float sum = 0.f;
        for (const ActiveEffect& e : mEffects.effects())
        {
            if (e.mTiming != EffectTiming::Timed || !(e.mTarget == target.mSubject)
                || (e.mArchetype != MagicArchetype::PeakValueModifier && e.mArchetype != MagicArchetype::ValueModifier)
                || !isResistValue(e.mValue))
                continue;
            if (e.mValue == resistValue || e.mValue == ResistValue::Magic)
                sum += e.mPerSecond; // a weakness is stored negative (the effect is Detrimental)
        }
        float percent = weaknessPercent(sum);
        if (target.mSubject.player && target.mHasAbilities) // task 0241: a held ability's weakness (the Apprentice's)
            percent += abilityWeaknessPercent(target.mAbilities, resistValue);
        return std::min(percent, kMaxWeaknessPercent);
    }

    void SkyrimCombat::holdActor(ActorHandle handle)
    {
        crimeHold(handle); // the package brain lets go and the goal is cancelled
        if (AiActor* engaged = aiFind(handle))
            engaged->mMoving = false;
    }

    bool SkyrimCombat::applyStatusEffect(
        CombatRequestWriter& writer, Fighter& caster, Fighter& target, const SpellEffect& effect,
        const MagicEffectRecord& record)
    {
        const float seconds = static_cast<float>(effect.mDuration);
        const std::uint32_t archetype = record.mArchetype;
        if (archetype != StatusArchetype::Paralysis && archetype != StatusArchetype::Frenzy
            && archetype != StatusArchetype::Demoralize)
            return false;
        const char* const name = archetype == StatusArchetype::Paralysis ? "paralysis"
            : archetype == StatusArchetype::Frenzy                        ? "frenzy"
                                                                          : "fear";
        if (target.mSubject.player)
        {
            trace(std::string("COMBAT effect ") + name + " on the player is not applied (the engine owns his body)");
            return true;
        }
        if (target.mDead || !ready(target))
            return true;
        if (!(seconds > 0.f))
        {
            trace(std::string("COMBAT effect ") + name + " has no duration");
            return true;
        }
        ActorStatus& status = target.mStatus;
        if (archetype == StatusArchetype::Paralysis)
        {
            const bool fresh = !isParalyzed(target);
            status.mParalyzedUntil = extendStatus(status.mParalyzedUntil, mClock, seconds);
            ++mStatusesApplied;
            ++mEffectsApplied;
            trace("COMBAT effect paralysis on " + who(target.mSubject) + " for " + num(seconds) + " s");
            if (!fresh)
                return true;
            target.mSwinging = false;
            target.mStaggered = false;
            target.mBow = Fighter::Bow::None;
            cancelCastsOf(target.mSubject);
            holdActor(target.mSubject.actor);
            // The fall: the stagger-back clip as a Death-slot clip, which the core does not stop at its end.
            const std::string_view clip = mSlots.clipFor(ClipSlot::Stagger, kWeaponAnim1HSword);
            if (!clip.empty())
            {
                ClipRequest request;
                request.slot = ClipSlot::Death;
                request.clip = clip;
                request.blendSeconds = 0.05f;
                request.token = mNextToken++;
                if (writer.playClip(target.mSubject, request))
                    status.mParalysisClipHeld = true;
            }
            return true;
        }
        // Fear and frenzy are the combat AI's: without it there is nothing to drive.
        if (!mAiOn)
        {
            trace(std::string("COMBAT effect ") + name + " needs `combat ai`");
            return true;
        }
        if (!levelGateAllows(target.mProfile.mLevel, effect.mMagnitude))
        {
            trace(std::string("COMBAT effect ") + name + " on " + who(target.mSubject) + " resisted: its level "
                + std::to_string(target.mProfile.mLevel) + " is above the magnitude " + num(effect.mMagnitude));
            return true;
        }
        const ActorHandle handle = target.mSubject.actor;
        if (archetype == StatusArchetype::Frenzy)
        {
            status.mFrenzyUntil = extendStatus(status.mFrenzyUntil, mClock, seconds);
            status.mFrenzyRetargetAt = mClock;
            ++mStatusesApplied;
            ++mEffectsApplied;
            trace("COMBAT effect frenzy on " + who(target.mSubject) + " for " + num(seconds) + " s");
            return true;
        }
        // Fear: the caster is what it runs from.
        status.mFearUntil = extendStatus(status.mFearUntil, mClock, seconds);
        ++mStatusesApplied;
        trace("COMBAT effect fear on " + who(target.mSubject) + " for " + num(seconds) + " s");
        if (aiFind(handle) == nullptr)
        {
            mAiCooldown.erase(handle.reference);
            if (aiBegin(target, "fear", caster.mSubject, true))
                if (AiActor* engaged = aiFind(handle))
                    engaged->mByStatus = true;
        }
        (void)writer;
        return true;
    }

    void SkyrimCombat::frenzyRetarget(CombatRequestWriter& writer, Fighter& fighter)
    {
        if (!mAiOn || !ready(fighter))
            return;
        SubjectState self;
        if (!writer.subjectState(fighter.mSubject, self))
            return;
        CombatSubject best;
        double bestDistance = kAiSightDistance;
        bool found = false;
        for (Fighter& other : mFighters)
        {
            if (&other == &fighter || other.mDead)
                continue;
            if (!other.mSubject.player && !(other.mReady && other.mHasProfile))
                continue;
            SubjectState state;
            if (!writer.subjectState(other.mSubject, state) || std::abs(state.z - self.z) > kAiSightHeight)
                continue;
            const double distance = std::hypot(state.x - self.x, state.y - self.y);
            if (distance < bestDistance) // strict: the first of equals in the table order wins, so a run repeats
            {
                best = other.mSubject;
                bestDistance = distance;
                found = true;
            }
        }
        if (!found)
            return;
        const ActorHandle handle = fighter.mSubject.actor;
        AiActor* engaged = aiFind(handle);
        if (engaged == nullptr)
        {
            mAiCooldown.erase(handle.reference);
            if (aiBegin(fighter, "frenzy", best, true))
                if (AiActor* started = aiFind(handle))
                    started->mByStatus = true;
            return;
        }
        if (engaged->mTarget == best)
            return;
        engaged->mTarget = best;
        fighter.mAimTarget = best;
        game_skyrimse::aiEngage(engaged->mState, aiSeed(handle.reference, mAiEngagements[handle.reference]), mClock);
        trace("COMBAT effect frenzy: " + who(fighter.mSubject) + " now attacks " + who(best));
    }

    void SkyrimCombat::endParalysis(CombatRequestWriter& writer, Fighter& fighter)
    {
        ActorStatus& status = fighter.mStatus;
        status.mParalyzedUntil = -1.f;
        trace("COMBAT effect paralysis ended on " + who(fighter.mSubject));
        const ActorHandle handle = fighter.mSubject.actor;
        if (status.mParalysisClipHeld)
        {
            status.mParalysisClipHeld = false;
            const std::string_view clip = mSlots.clipFor(ClipSlot::Stagger, kWeaponAnim1HSword);
            if (!clip.empty())
            {
                ClipRequest request;
                request.slot = ClipSlot::Stagger;
                request.clip = clip;
                request.blendSeconds = 0.2f;
                request.token = mNextToken++;
                if (writer.playClip(fighter.mSubject, request))
                {
                    fighter.mStaggered = true; // it ends with the clip
                    fighter.mStaggerToken = request.token;
                }
            }
        }
        if (aiFind(handle) != nullptr)
        {
            AiMoveIntent stop;
            stop.mKind = AiMoveIntent::Kind::Stop;
            stop.mActor = handle;
            mAiMoves.push_back(stop); // the AI keeps the actor: only the goal is cancelled
        }
        else
            crimeRelease(handle); // the package brain takes its package up again
    }

    void SkyrimCombat::statusNoteLost(Fighter& fighter)
    {
        if (fighter.mStatus.mFearUntil >= 0.f)
        {
            fighter.mStatus.mFearUntil = -1.f;
            trace("COMBAT effect fear over: " + who(fighter.mSubject) + " got far enough away");
        }
    }

    void SkyrimCombat::runStatuses(CombatRequestWriter& writer)
    {
        const auto letGo = [this](const Fighter& fighter) {
            for (std::size_t i = 0; i < mAiActors.size(); ++i)
                if (mAiActors[i].mHandle == fighter.mSubject.actor && mAiActors[i].mByStatus)
                {
                    aiRelease(i, "status over");
                    return;
                }
        };
        for (Fighter& f : mFighters)
        {
            if (f.mSubject.player)
                continue;
            ActorStatus& s = f.mStatus;
            if (s.mParalyzedUntil < 0.f && s.mFrenzyUntil < 0.f && s.mFearUntil < 0.f)
                continue;
            if (f.mDead)
            {
                s = ActorStatus(); // it died held or afraid: the death clip is its pose now
                continue;
            }
            if (s.mParalyzedUntil >= 0.f && !statusActive(s.mParalyzedUntil, mClock))
                endParalysis(writer, f);
            if (s.mFrenzyUntil >= 0.f)
            {
                if (!statusActive(s.mFrenzyUntil, mClock))
                {
                    s.mFrenzyUntil = -1.f;
                    trace("COMBAT effect frenzy ended on " + who(f.mSubject));
                    letGo(f);
                }
                else if (!isParalyzed(f) && mClock >= s.mFrenzyRetargetAt)
                {
                    s.mFrenzyRetargetAt = mClock + kFrenzyRetargetPeriod;
                    frenzyRetarget(writer, f);
                }
            }
            if (s.mFearUntil >= 0.f && !statusActive(s.mFearUntil, mClock))
            {
                s.mFearUntil = -1.f;
                trace("COMBAT effect fear ended on " + who(f.mSubject));
                letGo(f);
            }
        }
    }
}
