// The combat AI of SkyrimCombat (task 0190, stage S5-9): engagement, the per-frame think of every engaged NPC and the
// translation of its decision into what the core can do (clips and sweeps through the combat writer, movement through
// intents the cell source drains). The decision itself is the pure aiThink of combat_ai.hpp.
//
// Cost: an unengaged actor that is not hostile on sight costs nothing (it is looked at once, by the scan). A watcher
// costs one state lookup every kAiSightPeriod. An engaged actor costs two state lookups, one aiThink and, at most
// every kAiRetargetPeriod, one goal request (the path search is the core's).
#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ActorSpeed;
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::GoalResult;
        using openeden::sdk::game::SubjectState;

        constexpr SlotTable::Entry sGuard[] = {
            { ClipSlot::BlockStart, kWeaponAnim1HSword, "1HM_BlockIdle" },
        };

        /// Fighters looked at per frame for hostility on sight.
        constexpr std::size_t kAiScanPerFrame = 4;
        /// The most NPCs engaged at once; the rest wait for a place.
        constexpr std::size_t kMaxAiActors = 256;
        /// How often a standing engaged NPC is turned to the target.
        constexpr float kAiFacePeriod = 0.25f;
        /// The length assumed for the guard clip when its sidecar is not known.
        constexpr float kAiGuardClipFallback = 1.0f;
        /// fFleeDistanceInterior of Skyrim.esm (VERIFIED value; its use here is RECALLED-VERIFY).
        constexpr float kAiFleeDistance = 3000.f;

        std::string fmtf(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(value));
            return buffer;
        }

        std::string subjectName(const CombatSubject& subject)
        {
            if (subject.player)
                return "player";
            return "actor" + std::to_string(subject.actor.reference) + "/" + std::to_string(subject.actor.session);
        }

        const char* modeName(AiMode mode)
        {
            switch (mode)
            {
                case AiMode::Calm:
                    return "Calm";
                case AiMode::Advance:
                    return "Advance";
                case AiMode::Duel:
                    return "Duel";
                case AiMode::Block:
                    return "Block";
                case AiMode::Circle:
                    return "Circle";
                case AiMode::Fallback:
                    return "Fallback";
                case AiMode::Retreat:
                    return "Retreat";
            }
            return "?";
        }
    }

    SlotTable SlotTable::guard()
    {
        SlotTable table;
        table.mEntries = sGuard;
        table.mCount = std::size(sGuard);
        return table;
    }

    void SkyrimCombat::setCombatAi(bool on)
    {
        mAiOn = on;
        if (on)
        {
            mGuardSlots = SlotTable::guard();
            mAiMoves.reserve(64);
            mAiActors.reserve(32);
            mAiWatchers.reserve(64);
        }
    }

    void SkyrimCombat::drainAiMoves(std::vector<AiMoveIntent>& out)
    {
        out.insert(out.end(), mAiMoves.begin(), mAiMoves.end());
        mAiMoves.clear();
    }

    SkyrimCombat::AiActor* SkyrimCombat::aiFind(ActorHandle handle)
    {
        for (AiActor& a : mAiActors)
            if (a.mHandle == handle)
                return &a;
        return nullptr;
    }

    const SkyrimCombat::AiActor* SkyrimCombat::aiFind(ActorHandle handle) const
    {
        for (const AiActor& a : mAiActors)
            if (a.mHandle == handle)
                return &a;
        return nullptr;
    }

    AiMode SkyrimCombat::aiModeOf(ActorHandle handle) const
    {
        const AiActor* a = aiFind(handle);
        return a != nullptr ? a->mState.mMode : AiMode::Calm;
    }

    bool SkyrimCombat::aiGuardUp(ActorHandle handle) const
    {
        const AiActor* a = aiFind(handle);
        return a != nullptr && a->mState.mGuardUp;
    }

    bool SkyrimCombat::setFighterHealth(CombatSubject subject, float health)
    {
        Fighter* f = find(subject);
        if (f == nullptr || !ready(*f))
            return false;
        f->mHealth = std::clamp(std::isfinite(health) ? health : 0.f, 0.f, f->mProfile.mHealthMax);
        if (f->mHealth <= 0.f)
            f->mDead = true;
        return true;
    }

    void SkyrimCombat::aiForget(ActorHandle handle)
    {
        std::erase_if(mAiActors, [&](const AiActor& a) { return a.mHandle == handle; });
        std::erase_if(mAiWatchers, [&](const AiWatcher& w) { return w.mHandle == handle; });
    }

    bool SkyrimCombat::aiBegin(Fighter& fighter, const char* reason)
    {
        if (fighter.mSubject.player || fighter.mDead || aiFind(fighter.mSubject.actor) != nullptr
            || mAiActors.size() >= kMaxAiActors || !ready(fighter))
            return false;
        // An actor that gave up the chase rests; a hit by the player still wakes it.
        if (std::strcmp(reason, "provoked") != 0)
        {
            const auto rest = mAiCooldown.find(fighter.mSubject.actor.reference);
            if (rest != mAiCooldown.end() && mClock < rest->second)
                return false;
        }
        AiActor actor;
        actor.mHandle = fighter.mSubject.actor;
        const std::uint32_t engagement = ++mAiEngagements[actor.mHandle.reference];
        game_skyrimse::aiEngage(actor.mState, aiSeed(actor.mHandle.reference, engagement), mClock);
        mAiActors.push_back(actor);
        AiMoveIntent intent;
        intent.mKind = AiMoveIntent::Kind::Suspend;
        intent.mActor = actor.mHandle;
        mAiMoves.push_back(intent);
        fighter.mCombatUntil = mClock + kCombatLingerSeconds;
        if (mTrace)
        {
            std::string id;
            for (const auto& n : mActorNames)
                if (n.first == actor.mHandle)
                    id = n.second;
            const AiProfile& ai = fighter.mProfile.mAi;
            trace("COMBAT ai engage " + id + " " + subjectName(fighter.mSubject) + " (" + reason + ") aggression "
                + std::to_string(ai.mAggression) + " confidence " + std::to_string(ai.mConfidence) + " style "
                + (ai.mStyle.mCommon.mEditorId.empty() ? std::string("default") : ai.mStyle.mCommon.mEditorId)
                + " engagement " + std::to_string(engagement));
        }
        return true;
    }

    void SkyrimCombat::aiRelease(std::size_t index, const char* why)
    {
        if (index >= mAiActors.size())
            return;
        const AiActor actor = mAiActors[index];
        mAiActors.erase(mAiActors.begin() + static_cast<std::ptrdiff_t>(index));
        AiMoveIntent intent;
        intent.mActor = actor.mHandle;
        if (actor.mMoving)
        {
            intent.mKind = AiMoveIntent::Kind::Stop;
            mAiMoves.push_back(intent);
        }
        intent.mKind = AiMoveIntent::Kind::Resume;
        mAiMoves.push_back(intent);
        if (mTrace)
        {
            std::string id;
            for (const auto& n : mActorNames)
                if (n.first == actor.mHandle)
                    id = n.second;
            trace("COMBAT ai lost " + id + " " + subjectName(CombatSubject::of(actor.mHandle)) + " (" + why + ")");
        }
    }

    void SkyrimCombat::aiNoteDamage(const openeden::sdk::game::DamageReport& report)
    {
        if (!mAiOn || !report.attackerIsPlayer || report.victimIsPlayer || !(report.amount > 0.f)
            || !(report.healthAfter > 0.f))
            return;
        Fighter* victim = find(CombatSubject::of(report.victim));
        if (victim != nullptr)
            (void)aiBegin(*victim, "provoked");
    }

    void SkyrimCombat::aiProvoke(const Fighter& attacker, Fighter& victim)
    {
        if (!mAiOn || !attacker.mSubject.player || victim.mSubject.player || victim.mDead || victim.mHealth <= 0.f)
            return;
        (void)aiBegin(victim, "provoked");
    }

    void SkyrimCombat::aiGoalFinished(ActorHandle handle, GoalResult result)
    {
        AiActor* a = aiFind(handle);
        if (a == nullptr)
            return;
        a->mMoving = false;
        a->mGoalAt = mClock; // the next goal waits for the retarget period, a failure included
        if (a->mState.mMode != AiMode::Retreat)
        {
            if (result == GoalResult::Arrived)
                a->mGoalFails = 0;
            else if (++a->mGoalFails >= kAiMaxGoalFails)
                a->mGiveUp = true;
            return;
        }
        if (!a->mFleeGoalOut)
            return;
        a->mFleeGoalOut = false;
        if (result == GoalResult::Arrived)
            a->mState.mFleeTry = 0; // got there: runs on from the new place
        else if (++a->mState.mFleeTry >= kAiFleeAttempts)
        {
            // Every way is blocked: it waits where it is while the target is far and fights when it comes near.
            a->mState.mFleeStuck = true;
            a->mState.mFleeTry = 0;
            if (mTrace)
                trace("COMBAT ai flee ran out of ways " + subjectName(CombatSubject::of(handle)));
        }
    }

    // The scan: a few fighters a frame get their profile read (the first time they are looked at) and, if they would
    // attack the player on sight, become watchers.
    void SkyrimCombat::aiScan()
    {
        const std::size_t count = mFighters.size();
        if (count == 0)
            return;
        std::size_t looked = 0;
        for (std::size_t step = 0; step < count && looked < kAiScanPerFrame; ++step)
        {
            const std::size_t index = (mAiScanCursor + step) % count;
            Fighter& f = mFighters[index];
            if (f.mSubject.player || f.mAiScanned)
                continue;
            ++looked;
            f.mAiScanned = true;
            if (!ready(f) || !f.mProfile.mAi.mHostileOnSight)
                continue;
            AiWatcher watcher;
            watcher.mHandle = f.mSubject.actor;
            watcher.mNextSight = mClock + 0.1f * static_cast<float>(f.mSubject.actor.reference % 5);
            mAiWatchers.push_back(watcher);
        }
        mAiScanCursor = (mAiScanCursor + 1) % count;
    }

    void SkyrimCombat::aiPickSpell(Fighter& fighter, AiActor& actor)
    {
        if (actor.mSpellLooked || mData == nullptr)
            return;
        actor.mSpellLooked = true;
        // The first spell of the NPC (form id order) that it can cast at a target: aimed, fire and forget, a cost, an
        // effect, a hostile first effect.
        for (const FormId id : fighter.mProfile.mSpells)
        {
            const SpellRecord* spell = mData->spellById(id);
            if (spell == nullptr || !spell->mHasData || spell->mType != 0 || spell->mDelivery != MagicDelivery::Aimed
                || spell->mCastType != MagicCast::FireAndForget || spell->mCost == 0 || spell->mEffects.empty())
                continue;
            const MagicEffectRecord* effect = mData->effectById(spell->mEffects.front().mEffect);
            if (effect == nullptr || !effect->mHasData || (effect->mFlags & 1u) == 0) // 1: hostile
                continue;
            actor.mSpell = spell;
            return;
        }
    }

    void SkyrimCombat::aiPushGoTo(AiActor& actor, double x, double y, double z, ActorSpeed speed)
    {
        AiMoveIntent intent;
        intent.mKind = AiMoveIntent::Kind::GoTo;
        intent.mActor = actor.mHandle;
        intent.mX = x;
        intent.mY = y;
        intent.mZ = z;
        intent.mSpeed = speed;
        mAiMoves.push_back(intent);
        actor.mMoving = true;
        actor.mGoalX = x;
        actor.mGoalY = y;
        actor.mGoalAt = mClock;
    }

    void SkyrimCombat::aiRaiseGuard(CombatRequestWriter& writer, AiActor& actor, Fighter& fighter)
    {
        const std::string_view clip = mGuardSlots.clipFor(ClipSlot::BlockStart, kWeaponAnim1HSword);
        if (clip.empty())
            return;
        openeden::sdk::game::ClipRequest request;
        request.slot = ClipSlot::BlockStart;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.1f;
        request.token = mNextToken++;
        if (!writer.playClip(fighter.mSubject, request))
            return;
        openeden::sdk::game::ClipTiming timing;
        const float length = (writer.clipTiming(clip, false, timing) && timing.length > 0.f) ? timing.length
                                                                                            : kAiGuardClipFallback;
        actor.mGuardToken = request.token;
        actor.mGuardClipEnds = mClock + length;
    }

    void SkyrimCombat::aiMove(CombatRequestWriter& writer, AiActor& actor, Fighter& fighter, AiMove move,
        const SubjectState& self, const SubjectState& target, float distance, float reach)
    {
        const AiPoint s{ self.x, self.y };
        const AiPoint t{ target.x, target.y };
        auto due = [&](const AiPoint& p) {
            if (mClock - actor.mGoalAt < kAiRetargetPeriod)
                return false;
            return !actor.mMoving || std::hypot(p.x - actor.mGoalX, p.y - actor.mGoalY) >= kAiRetargetDistance;
        };
        switch (move)
        {
            case AiMove::None:
                if (actor.mMoving)
                {
                    AiMoveIntent stop;
                    stop.mKind = AiMoveIntent::Kind::Stop;
                    stop.mActor = actor.mHandle;
                    mAiMoves.push_back(stop);
                    actor.mMoving = false;
                    actor.mFleeGoalOut = false;
                }
                if (mClock - actor.mFacedAt >= kAiFacePeriod)
                {
                    actor.mFacedAt = mClock;
                    (void)writer.faceHeading(
                        fighter.mSubject, static_cast<float>(std::atan2(target.x - self.x, target.y - self.y)));
                }
                break;
            case AiMove::Approach:
            {
                const double standOff = std::max<double>(kAiMinStandOff, kAiStandOffFraction * reach);
                const AiPoint p = aiApproachPoint(s, t, standOff);
                if (due(p))
                    aiPushGoTo(actor, p.x, p.y, target.z,
                        distance > kAiRunDistance ? ActorSpeed::Run : ActorSpeed::Walk);
                break;
            }
            case AiMove::Circle:
            {
                const double radius = std::clamp<double>(distance, kAiCircleMinRadius, std::max(reach, 100.f));
                const AiPoint p = aiCirclePoint(s, t, radius, actor.mState.mCircleDir, kAiCircleStep);
                if (due(p))
                    aiPushGoTo(actor, p.x, p.y, self.z, ActorSpeed::Walk);
                break;
            }
            case AiMove::Away:
            {
                const AiPoint p = aiAwayPoint(s, t, kAiFallbackStep, 0);
                if (due(p))
                    aiPushGoTo(actor, p.x, p.y, self.z, ActorSpeed::Walk);
                break;
            }
            case AiMove::Flee:
                if (!actor.mFleeGoalOut && mClock - actor.mGoalAt >= kAiRetargetPeriod)
                {
                    const AiPoint p = aiAwayPoint(s, t, kAiFleeStep, actor.mState.mFleeTry);
                    aiPushGoTo(actor, p.x, p.y, self.z, ActorSpeed::Run);
                    actor.mFleeGoalOut = true;
                }
                break;
        }
    }

    void SkyrimCombat::runAi(CombatRequestWriter& writer)
    {
        if (!mAiOn)
            return;
        aiScan();
        Fighter* player = find(CombatSubject::ofPlayer());
        if (player == nullptr)
            return;
        SubjectState playerState;
        bool havePlayer = false;
        bool triedPlayer = false;
        auto getPlayer = [&]() {
            if (!triedPlayer)
            {
                triedPlayer = true;
                havePlayer = writer.subjectState(CombatSubject::ofPlayer(), playerState);
            }
            return havePlayer;
        };
        const bool playerAlive = !player->mDead;

        // 1. The named test hostiles engage on their own; one that was lost engages again only in sight.
        if (playerAlive)
            for (const ActorHandle& handle : mHostiles)
            {
                if (aiFind(handle) != nullptr)
                    continue;
                Fighter* f = find(CombatSubject::of(handle));
                if (f == nullptr || f->mDead || !ready(*f))
                    continue;
                const bool again = mAiEngagements.count(handle.reference) != 0;
                if (again)
                {
                    SubjectState self;
                    if (!getPlayer() || !writer.subjectState(f->mSubject, self)
                        || std::hypot(playerState.x - self.x, playerState.y - self.y) > kAiSightDistance)
                        continue;
                }
                (void)aiBegin(*f, "test hostile");
            }

        // 2. Hostile on sight: a look twice a second.
        if (playerAlive)
            for (AiWatcher& w : mAiWatchers)
            {
                if (mClock < w.mNextSight || aiFind(w.mHandle) != nullptr)
                    continue;
                w.mNextSight = mClock + kAiSightPeriod;
                Fighter* f = find(CombatSubject::of(w.mHandle));
                SubjectState self;
                if (f == nullptr || f->mDead || !getPlayer() || !writer.subjectState(f->mSubject, self))
                    continue;
                if (std::hypot(playerState.x - self.x, playerState.y - self.y) <= kAiSightDistance
                    && std::abs(playerState.z - self.z) <= kAiSightHeight)
                    (void)aiBegin(*f, "on sight");
            }

        // 3. The engaged.
        for (std::size_t i = 0; i < mAiActors.size();)
        {
            AiActor& a = mAiActors[i];
            Fighter* f = find(CombatSubject::of(a.mHandle));
            if (f == nullptr || f->mDead)
            {
                aiRelease(i, "dead");
                continue;
            }
            if (!playerAlive)
            {
                aiRelease(i, "target dead");
                continue;
            }
            if (a.mGiveUp)
            {
                mAiCooldown[a.mHandle.reference] = mClock + kAiGiveUpCooldown;
                aiRelease(i, "no way to the target");
                continue;
            }
            SubjectState self;
            if (!getPlayer() || !writer.subjectState(f->mSubject, self))
            {
                ++i;
                continue;
            }
            const float distance = static_cast<float>(std::hypot(playerState.x - self.x, playerState.y - self.y));
            const WeaponRecord* weapon = mWeapon;
            const bool bow = weapon != nullptr && weapon->mAnimType == kWeaponAnimBow && bowReady();
            const float rawReach = meleeReachUnits(weapon, f->mProfile.mUnarmedReach, mCombatSettings);
            AiView view;
            view.mNow = mClock;
            view.mDistance = distance;
            view.mReach = rawReach + kBodyRadius - 4.f;
            view.mRange = bow ? (weapon->mMaxRange > 0.f ? weapon->mMaxRange : kBowDefaultRange) : 0.f;
            view.mHealthFraction
                = f->mProfile.mHealthMax > 0.f ? std::clamp(f->mHealth / f->mProfile.mHealthMax, 0.f, 1.f) : 1.f;
            view.mStaminaFraction
                = f->mProfile.mStaminaMax > 0.f ? std::clamp(f->mStamina / f->mProfile.mStaminaMax, 0.f, 1.f) : 1.f;
            view.mFleeDistance = kAiFleeDistance;
            view.mTargetAlive = playerAlive;
            view.mTargetSwinging = mPlayerSwingActive;
            view.mTargetBlocking = false; // the player has no block input
            view.mBusy = f->mSwinging || f->mStaggered || f->mBow != Fighter::Bow::None
                || isBusyCasting(f->mSubject);
            aiPickSpell(*f, a);
            view.mCanCast = a.mSpell != nullptr && f->mMagicka >= static_cast<float>(a.mSpell->mCost);
            const AiMode before = a.mState.mMode;
            const AiOutput out = aiThink(a.mState, view, f->mProfile.mAi);
            if (out.mBecameCornered && mTrace)
                trace("COMBAT ai cornered " + subjectName(f->mSubject) + " distance " + fmtf(distance));
            if (out.mRecovered && mTrace)
                trace("COMBAT ai recovered " + subjectName(f->mSubject) + " health " + fmtf(view.mHealthFraction));
            if (out.mModeChanged && mTrace)
                trace("COMBAT ai mode " + subjectName(f->mSubject) + " " + modeName(before) + " -> "
                    + modeName(a.mState.mMode) + " distance " + fmtf(distance) + " health "
                    + fmtf(view.mHealthFraction) + " stamina " + fmtf(view.mStaminaFraction));

            switch (out.mAction)
            {
                case AiAction::None:
                    break;
                case AiAction::LowerGuard:
                    a.mGuardClipEnds = -1.f;
                    if (mTrace)
                        trace("COMBAT ai guard down " + subjectName(f->mSubject));
                    break;
                case AiAction::RaiseGuard:
                    aiRaiseGuard(writer, a, *f);
                    if (mTrace)
                        trace("COMBAT ai guard up " + subjectName(f->mSubject) + " hold " + fmtf(out.mGuardHold));
                    break;
                case AiAction::Cast:
                    if (a.mSpell != nullptr)
                    {
                        (void)writer.faceHeading(f->mSubject,
                            static_cast<float>(std::atan2(playerState.x - self.x, playerState.y - self.y)));
                        (void)beginCast(writer, *f, *a.mSpell, false, 0.f);
                    }
                    break;
                case AiAction::Attack:
                case AiAction::PowerAttack:
                case AiAction::Bash:
                {
                    AttackKind kind = out.mAction == AiAction::Bash ? AttackKind::Bash
                        : out.mAction == AiAction::PowerAttack      ? AttackKind::Power
                                                                    : AttackKind::Normal;
                    ++f->mAttackCount;
                    if (mPowerEvery > 0 || mBashEvery > 0)
                    {
                        // The test settings fix the kind: every Mth a bash, else every Nth a power attack, else normal.
                        kind = AttackKind::Normal;
                        if (mBashEvery > 0 && f->mAttackCount % static_cast<std::uint32_t>(mBashEvery) == 0)
                            kind = AttackKind::Bash;
                        else if (mPowerEvery > 0 && f->mAttackCount % static_cast<std::uint32_t>(mPowerEvery) == 0)
                            kind = AttackKind::Power;
                    }
                    if (mTrace)
                        trace("COMBAT ai attack " + subjectName(f->mSubject)
                            + (kind == AttackKind::Bash        ? " bash"
                                    : kind == AttackKind::Power ? " power"
                                                                : " normal"));
                    (void)writer.faceHeading(f->mSubject,
                        static_cast<float>(std::atan2(playerState.x - self.x, playerState.y - self.y)));
                    if (bow)
                        beginBowDraw(writer, *f);
                    else
                        beginAttack(writer, *f, kind);
                    break;
                }
            }
            // A guard that is up keeps its clip going.
            if (a.mState.mGuardUp && mClock >= a.mGuardClipEnds && !f->mSwinging && !f->mStaggered
                && out.mAction != AiAction::RaiseGuard)
                aiRaiseGuard(writer, a, *f);

            if (out.mLost)
            {
                aiRelease(i, "flee over or target gone");
                continue;
            }
            const float moveReach = bow ? view.mRange * 0.7f / kAiStandOffFraction : rawReach;
            aiMove(writer, a, *f, out.mMove, self, playerState, distance, moveReach);
            ++i;
        }
    }
}
