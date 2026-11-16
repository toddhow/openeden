// The combat AI of SkyrimCombat (task 0190, stage S5-9): engagement, the per-frame think of every engaged NPC and the
// translation of its decision into what the core can do (clips and sweeps through the combat writer, movement through
// intents the cell source drains). The decision itself is the pure aiThink of combat_ai.hpp.
//
// Cost: an unengaged actor that is not hostile on sight costs nothing (it is looked at once, by the scan). A watcher
// costs one state lookup every kAiSightPeriod. An engaged actor costs two state lookups, one aiThink and, at most
// every kAiRetargetPeriod, one goal request (the path search is the core's).
#include "game_skyrimse/combat.hpp"
#include "game_skyrimse/crime.hpp"

#include <algorithm>
#include <cctype>
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

    // ---- task 0204: what the crime rules read and do --------------------------------------------------------------

    namespace
    {
        void fillCrimeActor(crime::CrimeActor& out, ActorHandle handle, FormId base, const FighterProfile& profile,
            bool dead, bool engagedAgainstPlayer, std::string name)
        {
            out.mHandle = handle;
            out.mBase = base;
            out.mCrimeFaction = profile.mAi.mCrimeFaction;
            out.mGuard = profile.mAi.mGuard;
            out.mEnemyOfPlayer = static_cast<openeden::sdk::game::ActorRelation>(profile.mAi.mRelationToPlayer)
                == openeden::sdk::game::ActorRelation::Enemy;
            out.mEngagedAgainstPlayer = engagedAgainstPlayer;
            out.mDead = dead;
            out.mName = std::move(name);
        }
    }

    bool SkyrimCombat::freshlyProvoked(ActorHandle handle) const
    {
        for (const auto& e : mFreshProvoked)
            if (e.first == handle && e.second == mUpdates)
                return true;
        return false;
    }

    void SkyrimCombat::crimeActors(std::vector<crime::CrimeActor>& out)
    {
        for (Fighter& f : mFighters)
        {
            if (f.mSubject.player || !ready(f))
                continue;
            const AiActor* ai = aiFind(f.mSubject.actor);
            std::string name;
            for (const auto& n : mActorNames)
                if (n.first == f.mSubject.actor)
                    name = n.second;
            crime::CrimeActor a;
            const bool testGuard = !mCrimeTestGuard.empty() && name == mCrimeTestGuard;
            fillCrimeActor(a, f.mSubject.actor, f.mBase, f.mProfile, f.mDead,
                ai != nullptr && ai->mTarget.player && !freshlyProvoked(f.mSubject.actor), std::move(name));
            a.mGuard = a.mGuard || testGuard;
            out.push_back(std::move(a));
        }
    }

    bool SkyrimCombat::crimeActor(ActorHandle handle, crime::CrimeActor& out)
    {
        Fighter* f = find(CombatSubject::of(handle));
        if (f == nullptr || !ready(*f))
            return false;
        const AiActor* ai = aiFind(handle);
        std::string name;
        for (const auto& n : mActorNames)
            if (n.first == handle)
                name = n.second;
        const bool testGuard = !mCrimeTestGuard.empty() && name == mCrimeTestGuard;
        fillCrimeActor(out, handle, f->mBase, f->mProfile, f->mDead,
            ai != nullptr && ai->mTarget.player && !freshlyProvoked(handle), std::move(name));
        out.mGuard = out.mGuard || testGuard;
        return true;
    }

    void SkyrimCombat::setCrimeTestGuard(std::string_view editorId)
    {
        mCrimeTestGuard.assign(editorId);
        for (char& c : mCrimeTestGuard)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    void SkyrimCombat::crimeGoTo(ActorHandle handle, double x, double y, double z, openeden::sdk::game::ActorSpeed speed)
    {
        AiMoveIntent intent;
        intent.mKind = AiMoveIntent::Kind::GoTo;
        intent.mActor = handle;
        intent.mX = x;
        intent.mY = y;
        intent.mZ = z;
        intent.mSpeed = speed;
        mAiMoves.push_back(intent);
    }

    void SkyrimCombat::crimeHold(ActorHandle handle)
    {
        AiMoveIntent intent;
        intent.mActor = handle;
        intent.mKind = AiMoveIntent::Kind::Suspend;
        mAiMoves.push_back(intent);
        intent.mKind = AiMoveIntent::Kind::Stop;
        mAiMoves.push_back(intent);
    }

    void SkyrimCombat::crimeRelease(ActorHandle handle)
    {
        AiMoveIntent intent;
        intent.mActor = handle;
        intent.mKind = AiMoveIntent::Kind::Stop;
        mAiMoves.push_back(intent);
        intent.mKind = AiMoveIntent::Kind::Resume;
        mAiMoves.push_back(intent);
    }

    void SkyrimCombat::crimeAttack(ActorHandle handle)
    {
        if (!mAiOn)
            return;
        Fighter* f = find(CombatSubject::of(handle));
        if (f == nullptr)
            return;
        // A guard that was held for the talk is let go first so that the AI can take it over.
        crimeRelease(handle);
        (void)aiBegin(*f, "provoked", CombatSubject::ofPlayer());
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

    bool SkyrimCombat::playerInCombat() const
    {
        for (const AiActor& a : mAiActors)
            if (a.mTarget.player && !a.mGiveUp)
                return true;
        return false;
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

    bool SkyrimCombat::aiBegin(Fighter& fighter, const char* reason, CombatSubject target, bool assisted)
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
        actor.mTarget = target;
        actor.mAssisted = assisted;
        fighter.mAimTarget = target;
        const std::uint32_t engagement = ++mAiEngagements[actor.mHandle.reference];
        game_skyrimse::aiEngage(actor.mState, aiSeed(actor.mHandle.reference, engagement), mClock);
        mAiActors.push_back(actor);
        if (target.player && std::strcmp(reason, "provoked") == 0)
        {
            std::erase_if(mFreshProvoked, [this](const auto& e) { return e.second != mUpdates; });
            if (mFreshProvoked.size() < 64)
                mFreshProvoked.emplace_back(actor.mHandle, mUpdates); // task 0204
        }
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
                + " engagement " + std::to_string(engagement)
                + (target.player ? std::string() : " target " + subjectName(target))
                + (factions() ? " at " + fmtf(mClock) : std::string())); // task 0197
        }
        if (factions() && !assisted)
            mAiHelpFor.push_back(actor.mHandle);
        return true;
    }

    void SkyrimCombat::aiRelease(std::size_t index, const char* why)
    {
        if (index >= mAiActors.size())
            return;
        const AiActor actor = mAiActors[index];
        if (Fighter* f = find(CombatSubject::of(actor.mHandle)))
            f->mAimTarget = CombatSubject::ofPlayer();
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
        if (!mAiOn || (!report.attackerIsPlayer && !mFactionsOn) || report.victimIsPlayer || !(report.amount > 0.f)
            || !(report.healthAfter > 0.f))
            return;
        Fighter* victim = find(CombatSubject::of(report.victim));
        if (victim == nullptr)
            return;
        // Task 0197: an NPC that is hurt by another NPC fights it (an ally's accident included: it is not refused).
        const CombatSubject by
            = report.attackerIsPlayer ? CombatSubject::ofPlayer() : CombatSubject::of(report.attacker);
        (void)aiBegin(*victim, "provoked", by);
    }

    void SkyrimCombat::aiProvoke(const Fighter& attacker, Fighter& victim)
    {
        if (!mAiOn || (!attacker.mSubject.player && !mFactionsOn) || victim.mSubject.player || victim.mDead
            || victim.mHealth <= 0.f || (!attacker.mSubject.player && attacker.mDead))
            return;
        (void)aiBegin(victim, "provoked", attacker.mSubject);
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
            if (!ready(f) || (!factions() && !f.mProfile.mAi.mHostileOnSight))
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
        if (factions())
            aiSnapshot(writer);
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
                CombatSubject target = CombatSubject::ofPlayer();
                if (factions())
                    for (const auto& pair : mOpponents)
                        if (pair.first == handle)
                            target = CombatSubject::of(pair.second);
                const bool again = mAiEngagements.count(handle.reference) != 0;
                if (again && target.player)
                {
                    SubjectState self;
                    if (!getPlayer() || !writer.subjectState(f->mSubject, self)
                        || std::hypot(playerState.x - self.x, playerState.y - self.y) > kAiSightDistance)
                        continue;
                }
                (void)aiBegin(*f, "test hostile", target);
            }

        // 2. Hostile on sight: a look twice a second. With the factions rules (task 0197) every fighter that can start
        // a fight looks at every valid target, not only the player.
        if (factions())
        {
            std::size_t looked = 0;
            const std::size_t count = mAiWatchers.size();
            for (std::size_t step = 0; mClock >= mFactionsDelay && step < count && looked < Factions::kScanBudget;
                 ++step)
            {
                const std::size_t index = (mWatchCursor + step) % count;
                AiWatcher& w = mAiWatchers[index];
                if (mClock < w.mNextSight || aiFind(w.mHandle) != nullptr)
                    continue;
                ++looked;
                w.mNextSight = mClock + kAiSightPeriod;
                aiLookForTarget(writer, w);
            }
            if (count != 0)
                mWatchCursor = (mWatchCursor + 1) % count;
            for (std::size_t i = 0; i < mAiHelpFor.size(); ++i)
                aiCallForHelp(writer, mAiHelpFor[i]);
            mAiHelpFor.clear();
        }
        else if (playerAlive)
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
            if (!playerAlive && a.mTarget.player)
            {
                aiRelease(i, "target dead");
                continue;
            }
            // Task 0197: the target of this fight (the player's state is read once for the whole update).
            Fighter* targetFighter = a.mTarget.player ? player : find(a.mTarget);
            SubjectState targetState;
            if (!a.mTarget.player)
            {
                if (targetFighter == nullptr || targetFighter->mDead || !writer.subjectState(a.mTarget, targetState))
                {
                    aiRelease(i, "target dead");
                    continue;
                }
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
            if (a.mTarget.player)
                targetState = playerState;
            else if (factions() && partyOf(*f).mFollower
                && std::hypot(playerState.x - self.x, playerState.y - self.y) > 1.5 * Factions::kFollowerLeash)
            {
                aiRelease(i, "left the player");
                continue;
            }
            const float distance = static_cast<float>(std::hypot(targetState.x - self.x, targetState.y - self.y));
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
            view.mTargetAlive = a.mTarget.player ? playerAlive : true;
            view.mTargetSwinging = a.mTarget.player ? mPlayerSwingActive : targetFighter->mSwinging;
            // the player has no block input
            view.mTargetBlocking = a.mTarget.player ? false : isBlocking(*targetFighter);
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
                            static_cast<float>(std::atan2(targetState.x - self.x, targetState.y - self.y)));
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
                        static_cast<float>(std::atan2(targetState.x - self.x, targetState.y - self.y)));
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
            aiMove(writer, a, *f, out.mMove, self, targetState, distance, moveReach);
            ++i;
        }
    }

    // ---- task 0197: targets from relations, followers, guards and sight ---------------------------------------------

    void SkyrimCombat::setCombatFactions(bool on)
    {
        mFactionsOn = on;
        if (on)
        {
            mAiSnap.reserve(256);
            mAiScratch.reserve(64);
            mAiHelpFor.reserve(16);
        }
    }

    void SkyrimCombat::setFollowerQuery(std::function<bool(openeden::sdk::game::WorldId)> query)
    {
        mFollowerQuery = std::move(query);
    }

    void SkyrimCombat::setTestAttacker(std::string_view editorId)
    {
        mTestAttacker.assign(editorId);
        for (char& c : mTestAttacker)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    CombatSubject SkyrimCombat::aiTargetOf(ActorHandle handle) const
    {
        const AiActor* a = aiFind(handle);
        return a != nullptr ? a->mTarget : CombatSubject::ofPlayer();
    }

    bool SkyrimCombat::isTestAttacker(ActorHandle handle) const
    {
        if (mTestAttacker.empty())
            return false;
        for (const auto& n : mActorNames)
            if (n.first == handle)
                return n.second == mTestAttacker;
        return false;
    }

    Factions::Party SkyrimCombat::partyOf(const Fighter& fighter) const
    {
        Factions::Party party;
        if (fighter.mSubject.player)
        {
            party.mPlayer = true;
            return party;
        }
        party.mFollower = mFollowerQuery && mFollowerQuery(fighter.mSubject.actor.reference);
        const AiProfile& ai = fighter.mProfile.mAi;
        party.mAggression = ai.mAggression;
        party.mAssistance = ai.mAssistance;
        party.mGuard = ai.mGuard;
        party.mHostileOnSight = ai.mHostileOnSight;
        party.mRelationToPlayer
            = static_cast<openeden::sdk::game::ActorRelation>(std::min<std::uint8_t>(ai.mRelationToPlayer, 3));
        // The test attacker turns on at the crowd test's delay (`combat test crowd delay`, default 4 s of combat clock)
        // so that a scenario can arrange the street first.
        if (mClock >= mCrowdDelay && isTestAttacker(fighter.mSubject.actor))
            party.mAggression = 3;
        return party;
    }

    openeden::sdk::game::ActorRelation SkyrimCombat::relationOf(const Fighter& a, const Fighter& b) const
    {
        using openeden::sdk::game::ActorRelation;
        if (a.mSubject == b.mSubject)
            return ActorRelation::Ally;
        // The test duels name their opponents.
        if (!a.mSubject.player && !b.mSubject.player)
            for (const auto& pair : mOpponents)
                if (pair.first == a.mSubject.actor && pair.second == b.mSubject.actor)
                    return ActorRelation::Enemy;
        ActorRelation base = ActorRelation::Neutral;
        if (a.mSubject.player || b.mSubject.player)
        {
            const Fighter& other = a.mSubject.player ? b : a;
            base = static_cast<ActorRelation>(std::min<std::uint8_t>(other.mProfile.mAi.mRelationToPlayer, 3));
        }
        else if (mData != nullptr)
        {
            const std::uint64_t lo = std::min<std::uint64_t>(a.mBase, b.mBase);
            const std::uint64_t hi = std::max<std::uint64_t>(a.mBase, b.mBase);
            const std::uint64_t key = (lo << 32) | hi;
            const auto found = mRelCache.find(key);
            if (found != mRelCache.end())
                base = static_cast<ActorRelation>(found->second);
            else
            {
                base = mData->factionRelation(a.mProfile, b.mProfile);
                if (mRelCache.size() >= 65536)
                    mRelCache.clear();
                mRelCache[key] = static_cast<std::uint8_t>(base);
            }
        }
        return Factions::sideRelation(partyOf(a), partyOf(b), base);
    }

    bool SkyrimCombat::aiTargetsPlayerSide(const Fighter& fighter) const
    {
        if (fighter.mSubject.player)
            return false;
        const AiActor* a = aiFind(fighter.mSubject.actor);
        if (a == nullptr)
            return false;
        if (a->mTarget.player)
            return true;
        return mFollowerQuery && mFollowerQuery(a->mTarget.actor.reference);
    }

    // Where every ready fighter stands, every kSnapshotPeriod. A look reads this and asks the core for nothing but
    // the rays of the targets it picked.
    void SkyrimCombat::aiSnapshot(CombatRequestWriter& writer)
    {
        if (mClock - mAiSnapAt < Factions::kSnapshotPeriod)
            return;
        mAiSnapAt = mClock;
        mAiSnap.clear();
        for (Fighter& f : mFighters)
        {
            if (f.mDead || (!f.mSubject.player && !(f.mReady && f.mHasProfile)))
                continue;
            SubjectState state;
            if (!writer.subjectState(f.mSubject, state))
                continue;
            AiSnap snap;
            snap.mSubject = f.mSubject;
            snap.mX = state.x;
            snap.mY = state.y;
            snap.mZ = state.z;
            mAiSnap.push_back(snap);
        }
    }

    // One ray from the eye of `from` to the target. A ray the core cannot answer leaves the target in sight (unknown
    // is not blocked).
    bool SkyrimCombat::aiHasSight(CombatRequestWriter& writer, const CombatSubject& from, const AiSnap& to)
    {
        SubjectState self;
        if (!writer.subjectState(from, self))
            return false;
        const Factions::SightRay ray = Factions::sightRay(self.x, self.y, self.z, to.mX, to.mY, to.mZ);
        if (!ray.mValid)
            return true;
        openeden::sdk::game::RayHit hit;
        if (!writer.castRay(from, ray.mRay, hit))
            return true;
        return Factions::sightClear(hit.fate);
    }

    void SkyrimCombat::aiLookForTarget(CombatRequestWriter& writer, AiWatcher& watcher)
    {
        Fighter* f = find(CombatSubject::of(watcher.mHandle));
        if (f == nullptr || f->mDead || !ready(*f) || aiFind(watcher.mHandle) != nullptr)
            return;
        const auto rest = mAiCooldown.find(watcher.mHandle.reference);
        if (rest != mAiCooldown.end() && mClock < rest->second)
            return;
        const Factions::Party self = partyOf(*f);
        if (self.mAggression == 0 && !self.mGuard && !self.mFollower)
            return;
        const AiSnap* me = nullptr;
        const AiSnap* playerSnap = nullptr;
        for (const AiSnap& snap : mAiSnap)
        {
            if (snap.mSubject == f->mSubject)
                me = &snap;
            if (snap.mSubject.player)
                playerSnap = &snap;
        }
        if (me == nullptr)
            return;
        // A follower fights near the player only.
        if (self.mFollower
            && (playerSnap == nullptr
                || std::hypot(playerSnap->mX - me->mX, playerSnap->mY - me->mY) > Factions::kFollowerLeash))
            return;
        mAiScratch.clear();
        for (std::size_t i = 0; i < mAiSnap.size(); ++i)
        {
            const AiSnap& snap = mAiSnap[i];
            if (snap.mSubject == f->mSubject)
                continue;
            const double flat = std::hypot(snap.mX - me->mX, snap.mY - me->mY);
            if (flat > kAiSightDistance || std::abs(snap.mZ - me->mZ) > kAiSightHeight)
                continue;
            const Fighter* other = find(snap.mSubject);
            if (other == nullptr || other->mDead)
                continue;
            const bool hostileToSide = aiTargetsPlayerSide(*other);
            if (!Factions::attacksOnSight(self, partyOf(*other), relationOf(*f, *other), hostileToSide))
                continue;
            mAiScratch.push_back({ static_cast<float>(flat), i });
        }
        std::sort(mAiScratch.begin(), mAiScratch.end());
        std::size_t rays = 0;
        for (const auto& candidate : mAiScratch)
        {
            if (++rays > 3)
                break; // at most three rays a look
            const AiSnap to = mAiSnap[candidate.second];
            if (!aiHasSight(writer, f->mSubject, to))
            {
                if (mTrace && mSightTraceAt[f->mSubject.actor.reference % 64] + 5.f <= mClock)
                {
                    mSightTraceAt[f->mSubject.actor.reference % 64] = mClock;
                    trace("COMBAT ai no sight " + subjectName(f->mSubject) + " -> " + subjectName(to.mSubject));
                }
                continue;
            }
            (void)aiBegin(*f, "on sight", to.mSubject);
            return;
        }
    }

    // The engaged fighter picked a target: those who would help the target against it join.
    void SkyrimCombat::aiCallForHelp(CombatRequestWriter& writer, ActorHandle engaged)
    {
        const AiActor* actor = aiFind(engaged);
        Fighter* attacker = find(CombatSubject::of(engaged));
        if (actor == nullptr || attacker == nullptr)
            return;
        const CombatSubject victimSubject = actor->mTarget;
        Fighter* victim = find(victimSubject);
        if (victim == nullptr)
            return;
        const AiSnap* victimSnap = nullptr;
        const AiSnap* attackerSnap = nullptr;
        for (const AiSnap& snap : mAiSnap)
        {
            if (snap.mSubject == victimSubject)
                victimSnap = &snap;
            if (snap.mSubject == attacker->mSubject)
                attackerSnap = &snap;
        }
        if (victimSnap == nullptr || attackerSnap == nullptr)
            return;
        const bool victimOnSide = Factions::playerSide(partyOf(*victim));
        for (std::size_t i = 0; i < mAiSnap.size(); ++i)
        {
            const AiSnap snap = mAiSnap[i];
            if (snap.mSubject.player || snap.mSubject == attacker->mSubject || snap.mSubject == victimSubject)
                continue;
            if (std::hypot(snap.mX - victimSnap->mX, snap.mY - victimSnap->mY) > Factions::kAssistRadius)
                continue;
            // it has to be able to see the attacker: within sight distance and height, and a clear line
            if (std::hypot(snap.mX - attackerSnap->mX, snap.mY - attackerSnap->mY) > kAiSightDistance
                || std::abs(snap.mZ - attackerSnap->mZ) > kAiSightHeight)
                continue;
            Fighter* helper = find(snap.mSubject);
            if (helper == nullptr || helper->mDead || aiFind(snap.mSubject.actor) != nullptr)
                continue;
            if (!Factions::assists(partyOf(*helper), relationOf(*helper, *victim), relationOf(*helper, *attacker),
                    victimOnSide))
                continue;
            if (!aiHasSight(writer, helper->mSubject, *attackerSnap))
                continue;
            (void)aiBegin(*helper, "assist", attacker->mSubject, true);
        }
    }
}
