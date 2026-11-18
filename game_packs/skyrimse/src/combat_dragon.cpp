// Task 0253 (dragons, first slice): the dragon in the combat rules. Behind the pack setting `dragons` (off by default).
//  - The flight (dragons.hpp `Flight`) is stepped here once an update and written as intents (AiMoveIntent::Fly and
//    Land) that the cell source turns into the actor requests useFurniture / placeFurniture / leaveFurniture; an
//    airborne dragon (or one that is breathing) is skipped by the combat AI (`dragonHolds`).
//  - The breath is the first spell of the shout the dragon's NPC_ lists (`DragonFireBreathShout01`...): a concentration
//    spell. It is NOT cast through the player's shout runtime or `beginCast` (those pay magicka and wait for a
//    SpellFire event the dragon's clip does not send under that name): the breath clip is played, the spell goes
//    kBreathFireDelay seconds later and each update during its cast duration every fighter inside a cone from the mouth
//    to the target takes the spell's per-second effects. No allocation per update beyond the trace lines.
//  - A dead dragon's soul goes to the player (ShoutState::addSouls) once the player is near enough, a delay after
//  death.
// Main thread only, in combatUpdate. No clock but the combat clock.
#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

        std::string spellLabel(const SpellRecord& spell)
        {
            return spell.mCommon.mEditorId.empty() ? std::to_string(spell.mCommon.mId) : spell.mCommon.mEditorId;
        }

        const char* phaseName(dragons::Phase phase)
        {
            switch (phase)
            {
                case dragons::Phase::Ground:
                    return "ground";
                case dragons::Phase::Takeoff:
                    return "takeoff";
                case dragons::Phase::Cruise:
                    return "cruise";
                case dragons::Phase::Approach:
                    return "approach";
                case dragons::Phase::Touchdown:
                    return "touchdown";
            }
            return "?";
        }
    }

    dragons::Phase SkyrimCombat::dragonPhase(ActorHandle handle) const
    {
        for (const dragons::DragonState& state : mDragonStates)
            if (state.mHandle == handle)
                return state.mFlight.phase();
        return dragons::Phase::Ground;
    }

    std::uint32_t SkyrimCombat::dragonBreaths() const
    {
        std::uint32_t total = 0;
        for (const dragons::DragonState& state : mDragonStates)
            total += state.mBreaths;
        return total;
    }

    std::vector<dragons::DragonSave> SkyrimCombat::savedDragons() const
    {
        std::vector<dragons::DragonSave> saves;
        if (!mDragonSettings.mOn)
            return saves;
        for (const dragons::DragonState& state : mDragonStates)
        {
            if (!state.mFlight.airborne() || state.mHandle.reference == 0)
                continue;
            dragons::DragonSave save;
            save.mPlacement = state.mHandle.reference;
            save.mX = state.mFlight.lastX();
            save.mY = state.mFlight.lastY();
            save.mZ = state.mFlight.lastZ();
            save.mHeading = state.mFlight.heading();
            saves.push_back(save);
        }
        std::sort(saves.begin(), saves.end(),
            [](const dragons::DragonSave& a, const dragons::DragonSave& b) { return a.mPlacement < b.mPlacement; });
        return saves;
    }

    void SkyrimCombat::restoreDragons(std::vector<dragons::DragonSave> saves)
    {
        mDragonRestores.clear();
        if (!mDragonSettings.mOn)
            return;
        std::sort(saves.begin(), saves.end(),
            [](const dragons::DragonSave& a, const dragons::DragonSave& b) { return a.mPlacement < b.mPlacement; });
        for (const dragons::DragonSave& save : saves)
        {
            if (mDragonRestores.size() >= dragons::kMaxDragonSaves)
                break;
            if (save.mPlacement == 0
                || (!mDragonRestores.empty() && mDragonRestores.back().mPlacement == save.mPlacement))
                continue;
            mDragonRestores.push_back(save);
        }
    }

    bool SkyrimCombat::dragonHolds(const CombatSubject& subject) const
    {
        if (!mDragonSettings.mOn || subject.player)
            return false;
        for (const dragons::DragonState& state : mDragonStates)
            if (state.mHandle == subject.actor)
                return state.mFlight.airborne() || state.mBreathing;
        return false;
    }

    bool SkyrimCombat::dragonWalksDirect(ActorHandle handle) const
    {
        if (!mDragonSettings.mOn)
            return false;
        const Fighter* f = find(CombatSubject::of(handle));
        if (f == nullptr || !f->mHasProfile || !dragons::isDragon(f->mProfile.mCreature))
            return false;
        if (!mDragonSettings.mNavmeshWalk)
            return true; // the first slice: always straight
        // Task 0280: the navigator first; straight only for a while after it found no path.
        for (const dragons::DragonState& state : mDragonStates)
            if (state.mHandle == handle)
                return mClock < state.mDirectUntil;
        return false;
    }

    void SkyrimCombat::dragonNoPath(ActorHandle handle)
    {
        if (!mDragonSettings.mOn || !mDragonSettings.mNavmeshWalk)
            return;
        for (dragons::DragonState& state : mDragonStates)
            if (state.mHandle == handle)
            {
                state.mDirectUntil = mClock + dragons::kDirectFallbackSeconds;
                if (mTrace)
                    trace("COMBAT dragon actor " + std::to_string(handle.reference)
                        + " found no path: it walks straight");
                return;
            }
    }

    void SkyrimCombat::updateDragons(CombatRequestWriter& writer, float step)
    {
        if (!mDragonSettings.mOn || mData == nullptr)
            return;
        // Forget dragons that are gone, learn the new ones (a fighter whose profile was read and names the dragon row).
        std::erase_if(mDragonStates,
            [&](const dragons::DragonState& state) { return find(CombatSubject::of(state.mHandle)) == nullptr; });
        for (const Fighter& f : mFighters)
        {
            if (f.mSubject.player || !f.mReady || !f.mHasProfile || !dragons::isDragon(f.mProfile.mCreature))
                continue;
            const bool known = std::any_of(mDragonStates.begin(), mDragonStates.end(),
                [&](const dragons::DragonState& s) { return s.mHandle == f.mSubject.actor; });
            if (known)
                continue;
            mDragonStates.emplace_back(mDragonSettings.mFlight);
            mDragonStates.back().mHandle = f.mSubject.actor;
            // Task 0280: it was in the air when the game was saved.
            const auto restore = std::find_if(mDragonRestores.begin(), mDragonRestores.end(),
                [&](const dragons::DragonSave& r) { return r.mPlacement == f.mSubject.actor.reference; });
            if (restore != mDragonRestores.end())
            {
                mDragonStates.back().mResumePending = true;
                mDragonStates.back().mResume = *restore;
                mDragonRestores.erase(restore);
            }
            if (mTrace)
                trace("COMBAT dragon " + who(f.mSubject) + " is a dragon");
            if (mDragonSettings.mTestHealth > 0.f && std::isfinite(mDragonSettings.mTestHealth))
            {
                // A developer aid: it starts weak enough to be killed in a run.
                (void)setFighterHealth(f.mSubject, mDragonSettings.mTestHealth);
                if (mTrace)
                    trace(
                        "COMBAT dragon " + who(f.mSubject) + " starts with health " + num(mDragonSettings.mTestHealth));
            }
        }
        if (mDragonStates.empty())
            return;
        SubjectState player;
        const bool havePlayer = writer.subjectState(CombatSubject::ofPlayer(), player);

        for (dragons::DragonState& state : mDragonStates)
        {
            Fighter* f = find(CombatSubject::of(state.mHandle));
            SubjectState self;
            if (f == nullptr || !havePlayer || !writer.subjectState(f->mSubject, self))
                continue;
            AiActor* ai = aiFind(state.mHandle);
            if (mTrace && mClock >= state.mNextTrace)
            {
                state.mNextTrace = mClock + 1.f;
                trace("COMBAT dragon " + who(f->mSubject) + " at " + num(static_cast<float>(self.x)) + " "
                    + num(static_cast<float>(self.y)) + " " + num(static_cast<float>(self.z)) + " "
                    + phaseName(state.mFlight.phase()) + " health " + num(f->mHealth));
            }

            if (mDragonSettings.mTestKillAt > 0.f && !f->mDead && mClock >= mDragonSettings.mTestKillAt)
            {
                // A developer aid: the dragon dies now (the way the combat does: health 0, dead, reported).
                f->mHealth = 0.f;
                f->mDead = true;
                f->mSwinging = false;
                (void)writer.reportDeath(state.mHandle);
                if (mTrace)
                    trace("COMBAT dragon " + who(f->mSubject) + " is killed by the test setting");
            }
            // ---- death: the dragon leaves its pose where it is, the soul goes to the player
            if (f->mDead)
            {
                if (!state.mDeathClipPlayed)
                {
                    // Its own death clip (a death by a spell plays a humanoid stagger clip in combat_magic.cpp, which a
                    // dragon's skeleton has not).
                    state.mDeathClipPlayed = true;
                    const std::string_view death
                        = f->mProfile.mCreature != nullptr ? f->mProfile.mCreature->mDeath : std::string_view();
                    if (!death.empty())
                    {
                        ClipRequest request;
                        request.slot = ClipSlot::Death;
                        request.clip = death;
                        request.clipPerSex = false;
                        request.blendSeconds = 0.1f;
                        request.token = mNextToken++;
                        (void)writer.playClip(f->mSubject, request);
                    }
                }
                if (!state.mDeathSeen)
                {
                    state.mDeathSeen = true;
                    state.mDiedAt = mClock;
                    state.mBreathing = false;
                    if (mTrace)
                        trace("COMBAT dragon death " + who(f->mSubject) + " " + phaseName(state.mFlight.phase()));
                }
                if (state.mFlight.airborne())
                {
                    dragons::FlightInput in;
                    in.mDt = step;
                    in.mX = self.x;
                    in.mY = self.y;
                    in.mZ = self.z;
                    in.mHeading = self.heading;
                    in.mPlayerX = player.x;
                    in.mPlayerY = player.y;
                    in.mPlayerZ = player.z;
                    in.mDead = true;
                    const dragons::FlightOutput out = state.mFlight.step(in);
                    if (out.mLand)
                    {
                        AiMoveIntent land;
                        land.mKind = AiMoveIntent::Kind::Land;
                        land.mActor = state.mHandle;
                        land.mX = out.mX;
                        land.mY = out.mY;
                        land.mZ = out.mZ;
                        mAiMoves.push_back(land);
                        if (mTrace)
                            trace("COMBAT dragon " + who(f->mSubject) + " falls from its flight");
                    }
                }
                if (!state.mSoulTaken
                    && dragons::soulAbsorbs(mClock - state.mDiedAt, std::hypot(player.x - self.x, player.y - self.y)))
                {
                    state.mSoulTaken = true;
                    if (mSoulByScript && mSoulByScript(state.mHandle.reference))
                    {
                        // Task 0313: the dragon's own script absorbs it (Actor.ModActorValue("dragonsouls"))
                        if (mTrace)
                            trace("COMBAT dragon soul of " + who(f->mSubject) + " is left to its death script");
                    }
                    else if (mShouts != nullptr)
                    {
                        mShouts->addSouls(1);
                        mShouts->announce(std::string(dragons::kSoulMessage));
                        ++mDragonSouls;
                        if (mTrace)
                            trace("COMBAT dragon soul absorbed from " + who(f->mSubject) + ", souls "
                                + std::to_string(mShouts->souls()));
                    }
                    else if (mTrace)
                        trace("COMBAT dragon soul of " + who(f->mSubject) + " is lost: no shout state to keep it");
                }
                continue;
            }

            const bool engaged = ai != nullptr;
            const CombatSubject targetSubject = engaged ? ai->mTarget : CombatSubject::ofPlayer();
            SubjectState target;
            bool haveTarget = false;
            if (targetSubject.player)
            {
                target = player;
                haveTarget = true;
            }
            else
                haveTarget = writer.subjectState(targetSubject, target);

            // ---- the flight
            if (state.mResumePending)
            {
                // A load put this dragon in the air (task 0280): the cruise goes on from the saved place.
                state.mResumePending = false;
                state.mFlight.resume(state.mResume.mX, state.mResume.mY, state.mResume.mZ, state.mResume.mHeading,
                    player.x, player.y);
                if (mTrace)
                    trace("COMBAT dragon " + who(f->mSubject) + " resumes its flight at "
                        + num(static_cast<float>(state.mResume.mX)) + " " + num(static_cast<float>(state.mResume.mY))
                        + " " + num(static_cast<float>(state.mResume.mZ)));
            }
            const bool breathing = state.mBreathing;
            dragons::FlightInput in;
            in.mDt = step;
            in.mX = self.x;
            in.mY = self.y;
            in.mZ = self.z;
            in.mHeading = self.heading;
            in.mPlayerX = player.x;
            in.mPlayerY = player.y;
            in.mPlayerZ = player.z;
            in.mEngaged = engaged;
            in.mBusy = f->mSwinging || f->mStaggered || breathing;
            in.mFaceTarget = breathing && state.mBreathAir;
            const dragons::Phase before = state.mFlight.phase();
            const dragons::FlightOutput out = state.mFlight.step(in);
            if (out.mPlace)
            {
                AiMoveIntent fly;
                fly.mKind = AiMoveIntent::Kind::Fly;
                fly.mActor = state.mHandle;
                fly.mX = out.mX;
                fly.mY = out.mY;
                fly.mZ = out.mZ;
                fly.mHeading = out.mHeading;
                fly.mClip = out.mClip;
                fly.mStart = out.mStartPose;
                mAiMoves.push_back(fly);
                if (out.mStartPose)
                {
                    if (ai != nullptr)
                        ai->mMoving = false;
                    if (mTrace)
                        trace(std::string("COMBAT dragon ") + who(f->mSubject) + " flight " + phaseName(out.mPhase)
                            + " clip " + std::string(out.mClip));
                }
            }
            if (out.mLand)
            {
                AiMoveIntent land;
                land.mKind = AiMoveIntent::Kind::Land;
                land.mActor = state.mHandle;
                land.mX = out.mX;
                land.mY = out.mY;
                land.mZ = out.mZ;
                land.mHeading = out.mHeading;
                mAiMoves.push_back(land);
                if (ai != nullptr)
                    ai->mMoving = false;
                if (mTrace)
                    trace("COMBAT dragon " + who(f->mSubject) + " lands (was " + phaseName(before) + ")");
            }

            // ---- the breath
            const dragons::Phase phase = state.mFlight.phase();
            const bool air = phase == dragons::Phase::Cruise;
            if (!state.mBreathLooked)
            {
                state.mBreathLooked = true;
                for (const FormId id : f->mProfile.mSpells)
                {
                    const ShoutRecord* shout = mData->shoutById(id);
                    if (shout == nullptr || !shout->mHasData || shout->mWordCount == 0)
                        continue;
                    const SpellRecord* spell = mData->spellById(shout->mWords[0].mSpell);
                    if (spell == nullptr || !spell->mHasData || spell->mCastType != MagicCast::Concentration
                        || spell->mDelivery != MagicDelivery::Aimed || spell->mEffects.empty())
                        continue;
                    state.mBreathSpellId = spell->mCommon.mId;
                    state.mBreathRecovery = shout->mWords[0].mRecovery;
                    if (mTrace)
                        trace("COMBAT dragon " + who(f->mSubject) + " breathes " + spellLabel(*spell) + " of shout "
                            + (shout->mCommon.mEditorId.empty() ? std::to_string(shout->mCommon.mId)
                                                                : shout->mCommon.mEditorId)
                            + ", " + std::to_string(f->mProfile.mSpells.size()) + " spells in its list");
                    break;
                }
            }
            const SpellRecord* breath = state.mBreathSpellId != 0 ? mData->spellById(state.mBreathSpellId) : nullptr;
            if (breath == nullptr || !haveTarget)
                continue;
            const float aimHeight = kBowAimHeight;
            const double heading = static_cast<double>(self.heading);
            const double mouthX = self.x + std::sin(heading) * static_cast<double>(dragons::kBreathMouthForward);
            const double mouthY = self.y + std::cos(heading) * static_cast<double>(dragons::kBreathMouthForward);
            const double mouthZ = self.z
                + static_cast<double>(air ? dragons::kBreathMouthHeightAir : dragons::kBreathMouthHeightGround);
            const double aimX = target.x;
            const double aimY = target.y;
            const double aimZ = target.z + static_cast<double>(aimHeight);

            if (!state.mBreathing)
            {
                if (!engaged || mClock < state.mBreathReadyAt || f->mSwinging || f->mStaggered || isParalyzed(*f)
                    || (phase != dragons::Phase::Ground && !air))
                    continue;
                const float range = beamRange(*breath);
                const double flat = std::hypot(target.x - self.x, target.y - self.y);
                const double distance = air ? std::hypot(flat, target.z - self.z) : flat;
                const float reach
                    = meleeReachUnits(weaponFor(*f), f->mProfile.mUnarmedReach, mCombatSettings) + kBodyRadius - 4.f;
                const double least = air ? 0.0 : static_cast<double>(reach + dragons::kBreathMinExtra);
                if (!(distance > least) || distance > static_cast<double>(range))
                    continue;
                ClipRequest request;
                request.slot = ClipSlot::Cast;
                request.clip = air ? dragons::kClipBreathAir : dragons::kClipBreathGround;
                request.clipPerSex = false;
                request.blendSeconds = 0.1f;
                request.token = mNextToken++;
                if (!writer.playClip(f->mSubject, request))
                {
                    state.mBreathReadyAt = mClock + 1.f; // refused: try again in a second
                    continue;
                }
                if (!air)
                    (void)writer.faceHeading(
                        f->mSubject, static_cast<float>(std::atan2(target.x - self.x, target.y - self.y)));
                if (ai != nullptr)
                {
                    ai->mMoving = false;
                    AiMoveIntent stop;
                    stop.mKind = AiMoveIntent::Kind::Stop;
                    stop.mActor = state.mHandle;
                    mAiMoves.push_back(stop);
                }
                f->mSwinging = false;
                state.mBreathing = true;
                state.mBreathAir = air;
                state.mBreathFired = false;
                state.mBreathStarted = mClock;
                state.mBreathSeconds = breath->mCastDuration > 0.f && std::isfinite(breath->mCastDuration)
                    ? breath->mCastDuration
                    : dragons::kBreathDefaultSeconds;
                ++state.mBreaths;
                if (mTrace)
                    trace("COMBAT dragon breath begin " + spellLabel(*breath) + " " + who(f->mSubject)
                        + (air ? " air" : " ground") + " distance " + num(static_cast<float>(distance)));
                continue;
            }

            // The breath in progress: the spell goes after the clip's event, then the cone burns for its cast duration.
            const float elapsed = mClock - state.mBreathStarted;
            const float fireEnd = dragons::kBreathFireDelay + state.mBreathSeconds;
            const float seconds
                = std::max(0.f, std::min(elapsed, fireEnd) - std::max(elapsed - step, dragons::kBreathFireDelay));
            if (seconds > 0.f)
            {
                if (!state.mBreathFired)
                {
                    state.mBreathFired = true;
                    if (mTrace)
                        trace("COMBAT dragon breath fires " + spellLabel(*breath) + " " + who(f->mSubject));
                }
                const float range = beamRange(*breath);
                const dragons::Cone cone
                    = dragons::breathCone(mouthX, mouthY, mouthZ, aimX, aimY, aimZ, range, dragons::kBreathHalfAngle);
                if (mSpellVisuals && mClock >= state.mNextVisual)
                {
                    state.mNextVisual = mClock + kBeamVisualInterval;
                    RayRequest ray;
                    ray.x = mouthX;
                    ray.y = mouthY;
                    ray.z = mouthZ;
                    ray.heading = static_cast<float>(std::atan2(aimX - mouthX, aimY - mouthY));
                    ray.pitch = static_cast<float>(std::atan2(aimZ - mouthZ, std::hypot(aimX - mouthX, aimY - mouthY)));
                    ray.range = range;
                    launchBeamVisual(writer, *f, *breath, ray, range);
                }
                f->mCombatUntil = mClock + kCombatLingerSeconds;
                for (std::size_t i = 0; i < mFighters.size(); ++i)
                {
                    Fighter& victim = mFighters[i];
                    if (&victim == f || victim.mDead || !ready(victim) || !victim.mHasProfile || !mayHit(*f, victim))
                        continue;
                    SubjectState at;
                    if (!writer.subjectState(victim.mSubject, at))
                        continue;
                    if (!dragons::coneContains(cone, at.x, at.y, at.z + static_cast<double>(aimHeight)))
                        continue;
                    SweepHit where;
                    where.x = at.x;
                    where.y = at.y;
                    where.z = at.z + static_cast<double>(aimHeight);
                    where.distance = static_cast<float>(std::hypot(at.x - mouthX, at.y - mouthY));
                    if (victim.mSubject.player)
                        where.target = SweepTarget::Player;
                    else
                    {
                        where.target = SweepTarget::Actor;
                        where.actor = victim.mSubject.actor;
                    }
                    for (const SpellEffect& effect : breath->mEffects)
                    {
                        const MagicEffectRecord* record = mData->effectById(effect.mEffect);
                        if (record == nullptr || !record->mHasData)
                            continue;
                        if (!effectConditionsAllow(*f, victim, effect, *record))
                            continue;
                        const std::uint32_t archetype = record->mArchetype;
                        const bool value = archetype == MagicArchetype::ValueModifier
                            || archetype == MagicArchetype::Dual || archetype == MagicArchetype::Absorb;
                        if (!value || !isVitalValue(record->mPrimaryValue))
                            continue;
                        const bool hostile = (record->mFlags & MagicFlag::Hostile) != 0;
                        float magnitude = signedMagnitude(effect.mMagnitude, record->mFlags);
                        if (hostile)
                            magnitude = (magnitude < 0.f ? -1.f : 1.f)
                                * weakened(
                                    resistedMagnitude(std::abs(magnitude), resistOf(victim, record->mResistValue)),
                                    weaknessOf(victim, record->mResistValue));
                        (void)changeVital(writer, f, victim, record->mPrimaryValue,
                            magnitude * seconds * mDragonSettings.mTestDamageScale, &where);
                        if (victim.mDead)
                            break;
                    }
                }
            }
            if (elapsed >= dragons::kBreathClipSeconds)
            {
                state.mBreathing = false;
                state.mBreathReadyAt = mClock + std::max(state.mBreathRecovery, dragons::kBreathMinGap);
                if (mTrace)
                    trace("COMBAT dragon breath ends " + who(f->mSubject));
            }
        }
    }
}
