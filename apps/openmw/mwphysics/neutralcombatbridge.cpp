#include "neutralcombatbridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ClipEnd;
    using openeden::sdk::game::ClipEvent;
    using openeden::sdk::game::ClipEventKind;
    using openeden::sdk::game::ClipRequest;
    using openeden::sdk::game::ClipSlot;
    using openeden::sdk::game::CombatRules;
    using openeden::sdk::game::CombatSubject;

    namespace
    {
        // OPENEDEN_NEUTRAL_COMBAT_TRACE=1 (developer aid): one NCOMBAT line per clip start, event and end.
        bool traceOn()
        {
            static const bool on = std::getenv("OPENEDEN_NEUTRAL_COMBAT_TRACE") != nullptr;
            return on;
        }

        // OPENEDEN_NEUTRAL_ACTOR_COST=1 (task 0154's developer aid; task 0192 adds the pack's combatUpdate time).
        bool costTraceOn()
        {
            static const bool on = std::getenv("OPENEDEN_NEUTRAL_ACTOR_COST") != nullptr;
            return on;
        }

        bool equalsNoCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
                return lower(x) == lower(y);
            });
        }

        bool validClipName(std::string_view clip)
        {
            if (clip.empty() || clip.size() > openeden::sdk::game::kMaxCombatClipName)
                return false;
            return std::all_of(clip.begin(), clip.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }
    }

    ClipEventKind classifyClipEvent(std::string_view name)
    {
        struct Entry
        {
            std::string_view mName;
            ClipEventKind mKind;
        };
        static constexpr Entry sTable[] = {
            { "preHitFrame", ClipEventKind::PreHit },
            { "weaponSwing", ClipEventKind::WeaponSwing },
            { "HitFrame", ClipEventKind::Hit },
            { "attackStop", ClipEventKind::AttackStop },
            { "AttackWinStart", ClipEventKind::AttackWindowStart },
            { "AttackWinEnd", ClipEventKind::AttackWindowEnd },
            { "InitiateWinBegin", ClipEventKind::BlockWindowStart },
            { "InitiateWinEnd", ClipEventKind::BlockWindowEnd },
            { "blockHitStop", ClipEventKind::BlockHitStop },
            { "bashRelease", ClipEventKind::BashRelease },
            { "bowDrawStart", ClipEventKind::BowDrawStart },
            { "bowDrawn", ClipEventKind::BowDrawn },
            { "BowRelease", ClipEventKind::BowRelease },
            { "arrowRelease", ClipEventKind::BowRelease }, // the sidecar's own name (Bow_Release, task 0178)
            { "BowReleaseFast", ClipEventKind::BowRelease },
            { "staggerStop", ClipEventKind::StaggerStop },
            { "recoilStop", ClipEventKind::RecoilStop },
            { "MRh_SpellFire_Event", ClipEventKind::SpellFire }, // task 0186
            { "MLh_SpellFire_Event", ClipEventKind::SpellFire },
        };
        for (const Entry& entry : sTable)
            if (equalsNoCase(entry.mName, name))
                return entry.mKind;
        return ClipEventKind::Other;
    }

    bool clipTimingOf(const HavokPack::ClipMeta* meta, openeden::sdk::game::ClipTiming& out)
    {
        out = {};
        if (meta == nullptr)
            return false;
        const float length = meta->playbackDuration();
        if (!(length > 0.f))
            return false;
        out.length = length;
        for (const HavokPack::ClipMetaEvent& event : meta->mEvents)
        {
            if (classifyClipEvent(event.mName) == ClipEventKind::Hit && std::isfinite(event.mTime))
            {
                out.hasHit = true;
                out.hitTime = event.mTime;
                break;
            }
        }
        return true;
    }

    bool NeutralCombatBridge::Writer::doClipTiming(
        std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out)
    {
        out = {};
        if (mSink == nullptr || mWorld == nullptr || !validClipName(clip))
            return false;
        // The first call for a clip starts the world's read; the answer comes in a later frame.
        return clipTimingOf(mWorld->playerClipMeta(clip, perSex).get(), out);
    }

    NeutralCombatBridge::NeutralCombatBridge(const NeutralActorBridge& actors)
        : mActors(actors)
        , mWriter(*this)
    {
        // Nothing is reserved here: a session with no combat (Morrowind, or the setting off) never runs update() with
        // rules and so never pays for the tables (review fix of task 0171).
    }

    void NeutralCombatBridge::ensureStorage()
    {
        mPlaying.reserve(kMaxPlaying);
        mLate.reserve(2 * kMaxPlaying);
        mResults.reserve(kMaxPendingResults);
        mShots.reserve(kMaxProjectiles);
        mStuck.reserve(256);
        mEffects.reserve(openeden::sdk::game::kMaxShownEffects);
        mProjectileResults.reserve(kMaxProjectiles);
        mCandidates.reserve(kMaxCandidates);
        mDead.reserve(kMaxDead);
    }

    void NeutralCombatBridge::pruneDead()
    {
        std::erase_if(mDead, [&](const ActorHandle& h) { return mActors.keyOfHandle(h) == nullptr; });
    }

    void NeutralCombatBridge::stopSafely(NeutralClipSink& sink, const void* key)
    {
        try
        {
            sink.stopClip(key);
        }
        catch (...)
        {
            ++mStats.mWriterFailures;
        }
    }

    std::size_t NeutralCombatBridge::reservedBytes() const
    {
        return mPlaying.capacity() * sizeof(Playing) + mLate.capacity() * sizeof(Finished)
            + mResults.capacity() * sizeof(SweepResult) + mCandidates.capacity() * sizeof(NeutralSweep::Candidate)
            + mDead.capacity() * sizeof(ActorHandle);
    }

    void NeutralCombatBridge::clear()
    {
        mShots.clear();
        mStuck.clear();
        mEffects.clear();
        mProjectileResults.clear();
        mPlaying.clear();
        mLate.clear();
        mResults.clear();
        mDead.clear();
    }

    namespace
    {
        bool handleBefore(const ActorHandle& a, const ActorHandle& b)
        {
            return a.reference != b.reference ? a.reference < b.reference : a.session < b.session;
        }
    }

    bool NeutralCombatBridge::isDeadByKey(const void* key) const
    {
        const openeden::sdk::game::ActorHandle handle = mActors.handleOf(key);
        return handle.valid() && isDead(handle);
    }

    bool NeutralCombatBridge::isDead(ActorHandle actor) const
    {
        const auto it = std::lower_bound(mDead.begin(), mDead.end(), actor, handleBefore);
        return it != mDead.end() && *it == actor;
    }

    std::size_t NeutralCombatBridge::indexOf(CombatSubject subject) const
    {
        for (std::size_t i = 0; i < mPlaying.size(); ++i)
            if (mPlaying[i].mSubject == subject)
                return i;
        return mPlaying.size();
    }

    void NeutralCombatBridge::tellFinished(CombatRules& rules, const Finished& finished)
    {
        try
        {
            rules.onClipFinished(finished.mSubject, finished.mToken, finished.mHow);
        }
        catch (...)
        {
            ++mStats.mRulesFailures;
        }
    }

    bool NeutralCombatBridge::Writer::doPlayClip(CombatSubject subject, const ClipRequest& request)
    {
        NeutralCombatBridge& b = mBridge;
        if (mSink == nullptr || subject.player || !subject.valid() || request.slot == ClipSlot::None
            || !validClipName(request.clip) || !std::isfinite(request.blendSeconds) || request.blendSeconds < 0.f)
        {
            ++b.mStats.mRefused;
            return false;
        }
        const void* key = b.mActors.keyOfHandle(subject.actor);
        if (key == nullptr)
        {
            ++b.mStats.mRefused;
            return false;
        }
        // A new request replaces the running clip: its end is told as Interrupted at the start of the next update.
        const std::size_t existing = b.indexOf(subject);
        if (existing != b.mPlaying.size())
        {
            if (b.mLate.size() < b.mLate.capacity())
                b.mLate.push_back({ subject, b.mPlaying[existing].mToken, ClipEnd::Interrupted });
            ++b.mStats.mClipsInterrupted;
            b.mPlaying.erase(b.mPlaying.begin() + static_cast<std::ptrdiff_t>(existing));
        }
        if (b.mPlaying.size() >= kMaxPlaying)
        {
            ++b.mStats.mTableFull;
            return false;
        }
        Playing p;
        p.mSubject = subject;
        p.mKey = key;
        p.mSlot = request.slot;
        p.mToken = request.token;
        b.mPlaying.push_back(std::move(p));
        ++b.mStats.mClipsStarted;
        if (traceOn())
            Log(Debug::Info) << "NCOMBAT start token " << request.token << " slot " << static_cast<int>(request.slot)
                             << " clip " << request.clip;
        mSink->playClip(key, request.clip, request.clipPerSex);
        return true;
    }

    bool NeutralCombatBridge::Writer::doStopClip(CombatSubject subject)
    {
        NeutralCombatBridge& b = mBridge;
        if (mSink == nullptr || subject.player || !subject.valid() || b.mActors.keyOfHandle(subject.actor) == nullptr)
        {
            ++b.mStats.mRefused;
            return false;
        }
        const std::size_t existing = b.indexOf(subject);
        if (existing == b.mPlaying.size())
            return true;
        if (b.mLate.size() < b.mLate.capacity())
            b.mLate.push_back({ subject, b.mPlaying[existing].mToken, ClipEnd::Interrupted });
        ++b.mStats.mClipsInterrupted;
        mSink->stopClip(b.mPlaying[existing].mKey);
        b.mPlaying.erase(b.mPlaying.begin() + static_cast<std::ptrdiff_t>(existing));
        return true;
    }

    bool NeutralCombatBridge::Writer::doFaceHeading(CombatSubject subject, float heading)
    {
        const void* key = (subject.valid() && !subject.player) ? mBridge.mActors.keyOfHandle(subject.actor) : nullptr;
        if (mSink == nullptr || key == nullptr || !std::isfinite(heading))
        {
            ++mBridge.mStats.mRefused;
            return false;
        }
        mSink->setHeading(key, heading);
        return true;
    }

    bool NeutralCombatBridge::Writer::doSubjectState(CombatSubject subject, openeden::sdk::game::SubjectState& out)
    {
        out = {};
        if (mSink == nullptr || mWorld == nullptr || !subject.valid())
            return false;
        if (!subject.player && mBridge.mActors.keyOfHandle(subject.actor) == nullptr)
            return false;
        return mWorld->subjectState(subject, out);
    }

    bool NeutralCombatBridge::Writer::doRequestSweep(CombatSubject subject, const openeden::sdk::game::SweepRequest& r)
    {
        NeutralCombatBridge& b = mBridge;
        constexpr float kPi = 3.14159265358979f;
        const bool finite = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && std::isfinite(r.heading)
            && std::isfinite(r.reach) && std::isfinite(r.halfAngle) && std::isfinite(r.heightBelow)
            && std::isfinite(r.heightAbove);
        if (mSink == nullptr || mWorld == nullptr || !subject.valid() || !finite || !(r.reach > 0.f)
            || !(r.halfAngle > 0.f && r.halfAngle <= kPi) || r.heightBelow < 0.f || !(r.heightAbove > 0.f)
            || (!subject.player && b.mActors.keyOfHandle(subject.actor) == nullptr)
            || b.mResults.size() >= kMaxPendingResults)
        {
            ++b.mStats.mSweepsRefused;
            return false;
        }
        b.mCandidates.clear();
        mWorld->collectCandidates(b.mCandidates);
        std::erase_if(b.mCandidates, [&](const NeutralSweep::Candidate& c) {
            return c.mTarget == openeden::sdk::game::SweepTarget::Actor && b.isDead(c.mActor);
        });
        SweepResult result;
        result.mSubject = subject;
        result.mToken = r.token;
        std::size_t dropped = 0;
        result.mCount
            = NeutralSweep::arcSweep(r, { subject.player, subject.actor }, b.mCandidates, result.mHits, &dropped);
        // Stage S5-3b: a blow needs a clear line from the weapon to the target's capsule. The world's geometry (walls,
        // closed doors) blocks it; actors never do.
        const double eyeZ = r.z + static_cast<double>(kWeaponHeight);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < result.mCount; ++i)
        {
            const openeden::sdk::game::SweepHit& hit = result.mHits[i];
            double aimZ = eyeZ;
            for (const NeutralSweep::Candidate& c : b.mCandidates)
            {
                if (c.mTarget != hit.target
                    || (hit.target == openeden::sdk::game::SweepTarget::Actor && !(c.mActor == hit.actor)))
                    continue;
                const double low = c.mZ + static_cast<double>(kAimMargin);
                const double high = c.mZ + static_cast<double>(c.mHeight) - static_cast<double>(kAimMargin);
                aimZ = std::clamp(eyeZ, low, std::max(low, high));
                break;
            }
            if (mWorld->lineBlocked(r.x, r.y, eyeZ, hit.x, hit.y, aimZ))
            {
                ++b.mStats.mSweepHitsBlocked;
                if (traceOn())
                    Log(Debug::Info) << "NCOMBAT sweep token " << r.token << " blocked by geometry: "
                                     << (hit.target == openeden::sdk::game::SweepTarget::Player
                                                ? std::string("player")
                                                : std::to_string(hit.actor.reference))
                                     << " at distance " << hit.distance;
                continue;
            }
            result.mHits[kept++] = hit;
        }
        result.mCount = kept;
        ++b.mStats.mSweeps;
        b.mStats.mSweepHitsDropped += dropped;
        if (traceOn())
            Log(Debug::Info) << "NCOMBAT sweep token " << r.token << " from " << r.x << " " << r.y << " " << r.z
                             << " heading " << r.heading << " reach " << r.reach << " hits " << result.mCount;
        b.mResults.push_back(result);
        return true;
    }

    bool NeutralCombatBridge::Writer::doReportDamage(const openeden::sdk::game::DamageReport& d)
    {
        NeutralCombatBridge& b = mBridge;
        if (mSink == nullptr || !std::isfinite(d.amount) || d.amount < 0.f || !std::isfinite(d.healthAfter)
            || !std::isfinite(d.healthMax) || !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)
            || (!d.victimIsPlayer && b.mActors.keyOfHandle(d.victim) == nullptr)
            || (!d.attackerIsPlayer && b.mActors.keyOfHandle(d.attacker) == nullptr))
        {
            ++b.mStats.mDamageRefused;
            return false;
        }
        ++b.mStats.mDamageReports;
        if (traceOn())
            Log(Debug::Info) << "NCOMBAT damage " << (d.attackerIsPlayer ? "player" : "actor") << " -> "
                             << (d.victimIsPlayer ? "player" : "actor") << " amount " << d.amount << " health after "
                             << d.healthAfter << " of " << d.healthMax;
        if (d.victimIsPlayer && mWorld != nullptr)
        {
            // The engine's health is never written outside [0, max] (a pack's number is data).
            openeden::sdk::game::DamageReport clamped = d;
            clamped.healthAfter = std::max(0.f, d.healthAfter);
            if (d.healthMax > 0.f)
                clamped.healthAfter = std::min(clamped.healthAfter, d.healthMax);
            mWorld->applyPlayerDamage(clamped);
        }
        return true;
    }

    bool NeutralCombatBridge::Writer::doReportDeath(ActorHandle actor)
    {
        NeutralCombatBridge& b = mBridge;
        const void* key = b.mActors.keyOfHandle(actor);
        if (mSink == nullptr || key == nullptr)
        {
            ++b.mStats.mDamageRefused;
            return false;
        }
        if (b.isDead(actor))
            return true;
        if (b.mDead.size() >= kMaxDead)
            b.pruneDead(); // the corpses of cells that have unloaded make room
        if (b.mDead.size() >= kMaxDead)
        {
            ++b.mStats.mDeadTableFull;
            if (!b.mWarnedDeadFull)
            {
                b.mWarnedDeadFull = true;
                Log(Debug::Warning) << "Combat: " << kMaxDead << " dead actors are remembered already; a death is not";
            }
            return false;
        }
        b.mDead.insert(
            std::lower_bound(b.mDead.begin(), b.mDead.end(), actor, handleBefore), actor); // room: no allocation
        ++b.mStats.mDeaths;
        if (traceOn())
            Log(Debug::Info) << "NCOMBAT death actor " << actor.reference << "/" << actor.session;
        if (mWorld != nullptr)
            mWorld->actorDied(key, actor);
        return true;
    }

    template <class F>
    bool NeutralCombatBridge::Writer::guarded(F&& call)
    {
        try
        {
            return call();
        }
        catch (...)
        {
            ++mBridge.mStats.mWriterFailures;
            if (!mBridge.mWarnedWriter)
            {
                mBridge.mWarnedWriter = true;
                Log(Debug::Warning) << "Combat: a call into the engine threw; the request fails (reported once)";
            }
            return false;
        }
    }

    bool NeutralCombatBridge::Writer::playClip(CombatSubject subject, const ClipRequest& request)
    {
        return guarded([&] { return doPlayClip(subject, request); });
    }
    bool NeutralCombatBridge::Writer::stopClip(CombatSubject subject)
    {
        return guarded([&] { return doStopClip(subject); });
    }
    bool NeutralCombatBridge::Writer::faceHeading(CombatSubject subject, float heading)
    {
        return guarded([&] { return doFaceHeading(subject, heading); });
    }
    bool NeutralCombatBridge::Writer::requestSweep(CombatSubject subject, const openeden::sdk::game::SweepRequest& r)
    {
        return guarded([&] { return doRequestSweep(subject, r); });
    }
    bool NeutralCombatBridge::Writer::reportDamage(const openeden::sdk::game::DamageReport& d)
    {
        return guarded([&] { return doReportDamage(d); });
    }
    bool NeutralCombatBridge::Writer::reportDeath(ActorHandle actor)
    {
        return guarded([&] { return doReportDeath(actor); });
    }
    bool NeutralCombatBridge::Writer::subjectState(CombatSubject subject, openeden::sdk::game::SubjectState& out)
    {
        return guarded([&] { return doSubjectState(subject, out); });
    }
    bool NeutralCombatBridge::Writer::clipTiming(
        std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out)
    {
        return guarded([&] { return doClipTiming(clip, perSex, out); });
    }

    void NeutralCombatBridge::update(CombatRules* rules, float seconds, NeutralClipSink& sink,
        const NeutralClipMetaFn& metaFn, NeutralCombatWorld* world)
    {
        if (rules == nullptr)
        {
            // Only a clip whose actor is still registered under the same key is stopped: a stale key may name a freed
            // object (review fix of task 0171).
            for (const Playing& p : mPlaying)
                if (p.mSubject.valid() && mActors.keyOfHandle(p.mSubject.actor) == p.mKey)
                    stopSafely(sink, p.mKey);
            for (const NeutralProjectile::Shot& shot : mShots)
                sink.removeProjectile(shot.mId);
            for (const Stuck& stuck : mStuck)
                sink.removeProjectile(stuck.mId);
            for (const Effect& effect : mEffects)
                sink.removeProjectile(effect.mId);
            clear();
            return;
        }
        ensureStorage();
        ++mStats.mUpdates;
        if (!mDead.empty() && ++mUpdatesSincePrune >= kDeadPruneUpdates)
        {
            mUpdatesSincePrune = 0;
            pruneDead();
        }
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;
        for (const Finished& finished : mLate)
            tellFinished(*rules, finished);
        mLate.clear();

        // Sweeps cast in an earlier frame: told now, before this frame's events and combatUpdate.
        for (const SweepResult& result : mResults)
        {
            if (!result.mSubject.player && mActors.keyOfHandle(result.mSubject.actor) == nullptr)
            {
                ++mStats.mSweepResultsDropped;
                continue;
            }
            mStats.mSweepHits += result.mCount;
            try
            {
                rules->onSweepResult(result.mSubject, result.mToken, result.mHits, result.mCount);
            }
            catch (...)
            {
                ++mStats.mRulesFailures;
            }
        }
        mResults.clear();

        // Projectiles that ended in an earlier frame: told now, after the sweeps and before the events.
        for (const openeden::sdk::game::ProjectileResult& result : mProjectileResults)
        {
            if (!result.owner.player && mActors.keyOfHandle(result.owner.actor) == nullptr)
            {
                ++mStats.mProjectileResultsDropped;
                continue;
            }
            try
            {
                rules->onProjectileResult(result);
            }
            catch (...)
            {
                ++mStats.mRulesFailures;
            }
        }
        mProjectileResults.clear();
        // The shots in the air fly this frame's time; whatever ends is told at the start of the next update.
        stepProjectiles(seconds, sink, world);
        followEffects(sink, world);

        for (std::size_t i = 0; i < mPlaying.size();)
        {
            Playing& p = mPlaying[i];
            if (!mActors.keyOfHandle(p.mSubject.actor))
            {
                // The actor has gone (cell unloaded): nobody to play for, nothing to tell.
                mPlaying.erase(mPlaying.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (!p.mMeta)
            {
                HavokPack::ClipProgress progress;
                try
                {
                    if (metaFn)
                        progress = metaFn(p.mKey);
                    if (!progress.mMeta && mMetaWaiter && !p.mWaitedFor)
                    {
                        p.mWaitedFor = true;
                        progress = mMetaWaiter(p.mKey);
                    }
                }
                catch (...)
                {
                    ++mStats.mWriterFailures; // the clip keeps waiting; the timeout below still ends it
                }
                if (progress.mMeta)
                    p.mMeta = progress.mMeta;
                if (!p.mMeta)
                {
                    p.mWaited += seconds * p.mRate;
                    if (p.mWaited > kMetaTimeoutSeconds)
                    {
                        stopSafely(sink, p.mKey);
                        ++mStats.mClipsFailed;
                        tellFinished(*rules, { p.mSubject, p.mToken, ClipEnd::Failed });
                        mPlaying.erase(mPlaying.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }
                    ++i;
                    continue;
                }
                // The first frame the meta is visible. The clock counts from the request: it is the time of the frames
                // that passed while the meta was being read (0 when it was ready at once), so the frame on which an
                // event fires does not depend on how long the worker took, nor on where the renderer's own sampling
                // had got to (task 0171 S5-3b follow-up: both varied by a frame from run to run). Events that the
                // wait has passed fire now, in clip-time order.
                p.mPrevious = -1.f;
                p.mCurrent = p.mWaited;
            }
            else
            {
                p.mPrevious = p.mCurrent;
                p.mCurrent += seconds * p.mRate;
            }
            const CombatSubject subject = p.mSubject;
            const ClipSlot slot = p.mSlot;
            const std::uint32_t token = p.mToken;
            const bool finished = HavokPack::advanceClipEvents(
                *p.mMeta, p.mPrevious, p.mCurrent, false, [&](const HavokPack::ClipMetaEvent& event, float clipTime) {
                    ClipEvent out;
                    out.subject = subject;
                    out.slot = slot;
                    out.token = token;
                    out.kind = classifyClipEvent(event.mName);
                    out.name = event.mName;
                    out.clipTime = clipTime;
                    ++mStats.mEventsDelivered;
                    if (traceOn())
                        Log(Debug::Info) << "NCOMBAT event token " << token << " " << event.mName << " kind "
                                         << static_cast<int>(out.kind) << " at " << clipTime << " s (frame clock "
                                         << p.mCurrent << ")";
                    try
                    {
                        rules->onClipEvent(out);
                    }
                    catch (...)
                    {
                        ++mStats.mRulesFailures;
                    }
                });
            if (finished)
            {
                if (traceOn())
                    Log(Debug::Info) << "NCOMBAT end token " << token << " completed at clock " << p.mCurrent;
                // A death clip is not stopped when it ends: the body keeps its last pose (task 0171).
                if (slot != ClipSlot::Death)
                    stopSafely(sink, p.mKey);
                ++mStats.mClipsCompleted;
                tellFinished(*rules, { subject, token, ClipEnd::Completed });
                mPlaying.erase(mPlaying.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }

        mWriter.mSink = &sink;
        mWriter.mWorld = world;
        // OPENEDEN_NEUTRAL_COMBAT_TRACE: a pack update that blocks the main thread is visible here (review fix of task
        // 0171: the first one used to read every plugin). Not a NCOMBAT line: the milliseconds differ run to run.
        const bool costOn = costTraceOn();
        const auto updateStart
            = (traceOn() || costOn) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
        try
        {
            rules->combatUpdate(seconds, mWriter);
        }
        catch (...)
        {
            ++mStats.mRulesFailures;
        }
        if (costOn)
            mStats.mRulesNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - updateStart)
                    .count());
        if (traceOn())
        {
            const double ms
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
            if (ms > 1.0)
                Log(Debug::Info) << "TIMING combatUpdate " << ms << " ms at update " << mStats.mUpdates;
        }
        mWriter.mSink = nullptr;
        mWriter.mWorld = nullptr;
    }

    bool NeutralCombatBridge::playerAttack(openeden::sdk::game::CombatRules* rules, bool power)
    {
        if (rules == nullptr)
        {
            ++mStats.mPlayerAttackRefused;
            return false;
        }
        try
        {
            rules->onPlayerAttack(power);
        }
        catch (...)
        {
            ++mStats.mPlayerAttackRefused;
            ++mStats.mRulesFailures;
            return false;
        }
        ++mStats.mPlayerAttacks;
        return true;
    }

    // ---- task 0178 (stage S5-6): projectiles, the clip speed, the clip event time, the player's edges -------------

    namespace
    {
        constexpr float kHalfPi = 1.57079632679f;
        constexpr float kMaxProjectileSpeed = 1.0e6f;
        constexpr float kMaxProjectileGravity = 1.0e5f;
        constexpr float kMaxProjectileRange = 1.0e6f;
        constexpr float kMaxStickDepth = 1.0e4f;
        constexpr float kMaxStickSeconds = 1.0e3f;
        constexpr float kMinClipSpeed = 0.1f;
        constexpr float kMaxClipSpeed = 10.f;
        constexpr std::size_t kMaxStuckArrows = 256;

        bool validModelPath(std::string_view path)
        {
            if (path.size() > NeutralCombatBridge::kMaxProjectileModelPath)
                return false;
            if (path.find("..") != std::string_view::npos || path.find(':') != std::string_view::npos)
                return false;
            return path.empty() || (path.front() != '/' && path.front() != '\\');
        }

        MWPhysics::NeutralProjectilePose poseOf(const NeutralProjectile::Vec& at, const NeutralProjectile::Vec& dir)
        {
            MWPhysics::NeutralProjectilePose pose;
            pose.mX = at.x;
            pose.mY = at.y;
            pose.mZ = at.z;
            pose.mDirX = static_cast<float>(dir.x);
            pose.mDirY = static_cast<float>(dir.y);
            pose.mDirZ = static_cast<float>(dir.z);
            return pose;
        }

        class WorldRayAdapter final : public NeutralProjectile::WorldRay
        {
        public:
            explicit WorldRayAdapter(NeutralCombatWorld& world)
                : mWorld(world)
            {
            }
            bool cast(const NeutralProjectile::Vec& p0, const NeutralProjectile::Vec& p1, double& fraction) override
            {
                return mWorld.rayWorld(p0, p1, fraction);
            }

        private:
            NeutralCombatWorld& mWorld;
        };
    }

    bool NeutralCombatBridge::Writer::doLaunchProjectile(
        CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r)
    {
        NeutralCombatBridge& b = mBridge;
        const bool finite = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z)
            && std::isfinite(r.heading) && std::isfinite(r.pitch) && std::isfinite(r.speed)
            && std::isfinite(r.gravity) && std::isfinite(r.range) && std::isfinite(r.stickDepth)
            && std::isfinite(r.stickSeconds);
        if (mSink == nullptr || !owner.valid() || !finite || !(r.speed > 0.f && r.speed <= kMaxProjectileSpeed)
            || !(r.gravity >= 0.f && r.gravity <= kMaxProjectileGravity)
            || !(r.range > 0.f && r.range <= kMaxProjectileRange) || !(std::abs(r.pitch) < kHalfPi)
            || !(r.stickDepth >= 0.f && r.stickDepth <= kMaxStickDepth)
            || !(r.stickSeconds >= 0.f && r.stickSeconds <= kMaxStickSeconds) || !validModelPath(r.model)
            || (!owner.player && b.mActors.keyOfHandle(owner.actor) == nullptr)
            || b.mShots.size() >= kMaxProjectiles)
        {
            ++b.mStats.mProjectilesRefused;
            return false;
        }
        NeutralProjectile::Shot shot;
        shot.mId = b.mNextProjectileId++;
        if (b.mNextProjectileId == 0)
            b.mNextProjectileId = 1;
        shot.mOwner = owner;
        shot.mToken = r.token;
        shot.mPosition = { r.x, r.y, r.z };
        const double heading = static_cast<double>(r.heading);
        const double pitch = static_cast<double>(r.pitch);
        const double speed = static_cast<double>(r.speed);
        const NeutralProjectile::Vec direction{ std::sin(heading) * std::cos(pitch), std::cos(heading) * std::cos(pitch),
            std::sin(pitch) };
        shot.mVelocity = { direction.x * speed, direction.y * speed, direction.z * speed };
        shot.mGravity = r.gravity;
        shot.mRangeLeft = r.range;
        shot.mStickDepth = r.stickDepth;
        shot.mStickSeconds = r.visualOnly ? 0.f : r.stickSeconds;
        shot.mVisualOnly = r.visualOnly;
        b.mShots.push_back(shot); // room was checked above
        ++b.mStats.mProjectilesLaunched;
        if (r.visualOnly)
            ++b.mStats.mVisualShotsLaunched;
        if (!r.model.empty())
            mSink->spawnProjectile(shot.mId, r.model, poseOf(shot.mPosition, direction));
        if (traceOn())
            Log(Debug::Info) << "NCOMBAT projectile launch id " << shot.mId << " token " << r.token << " from " << r.x
                             << " " << r.y << " " << r.z << " heading " << r.heading << " pitch " << r.pitch
                             << " speed " << r.speed << " gravity " << r.gravity;
        return true;
    }

    bool NeutralCombatBridge::Writer::doSetClipSpeed(CombatSubject subject, float multiplier)
    {
        NeutralCombatBridge& b = mBridge;
        if (mSink == nullptr || subject.player || !subject.valid() || !std::isfinite(multiplier)
            || multiplier < kMinClipSpeed || multiplier > kMaxClipSpeed)
        {
            ++b.mStats.mRefused;
            return false;
        }
        const std::size_t index = b.indexOf(subject);
        if (index == b.mPlaying.size())
        {
            ++b.mStats.mRefused;
            return false;
        }
        b.mPlaying[index].mRate = multiplier;
        mSink->setClipSpeed(b.mPlaying[index].mKey, multiplier);
        return true;
    }

    bool NeutralCombatBridge::Writer::doPlaySound(const openeden::sdk::game::SoundRequest& request)
    {
        NeutralCombatBridge& b = mBridge;
        const void* key = (!request.subject.player && request.subject.valid())
            ? b.mActors.keyOfHandle(request.subject.actor)
            : nullptr;
        if (mSink == nullptr || !request.subject.valid() || (!request.subject.player && key == nullptr)
            || request.file.empty() || request.file.size() > openeden::sdk::game::kMaxCombatSoundPath
            || !std::isfinite(request.volume))
        {
            ++b.mStats.mSoundsRefused;
            return false;
        }
        mSink->playSound(key, request.file, request.name, std::clamp(request.volume, 0.f, 1.f));
        ++b.mStats.mSoundsPlayed;
        return true;
    }

    bool NeutralCombatBridge::Writer::doClipEventTime(
        std::string_view clip, bool perSex, ClipEventKind kind, float& out)
    {
        out = 0.f;
        if (mSink == nullptr || mWorld == nullptr || !validClipName(clip))
            return false;
        const std::shared_ptr<const HavokPack::ClipMeta> meta = mWorld->playerClipMeta(clip, perSex);
        if (!meta)
            return false;
        for (const HavokPack::ClipMetaEvent& event : meta->mEvents)
        {
            if (classifyClipEvent(event.mName) == kind && std::isfinite(event.mTime))
            {
                out = event.mTime;
                return true;
            }
        }
        return false;
    }

    bool NeutralCombatBridge::Writer::launchProjectile(
        CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r)
    {
        return guarded([&] { return doLaunchProjectile(owner, r); });
    }
    bool NeutralCombatBridge::Writer::setClipSpeed(CombatSubject subject, float multiplier)
    {
        return guarded([&] { return doSetClipSpeed(subject, multiplier); });
    }
    bool NeutralCombatBridge::Writer::playSound(const openeden::sdk::game::SoundRequest& request)
    {
        return guarded([&] { return doPlaySound(request); });
    }
    bool NeutralCombatBridge::Writer::clipEventTime(
        std::string_view clip, bool perSex, ClipEventKind kind, float& out)
    {
        return guarded([&] { return doClipEventTime(clip, perSex, kind, out); });
    }

    void NeutralCombatBridge::stepProjectiles(float seconds, NeutralClipSink& sink, NeutralCombatWorld* world)
    {
        // The arrows stuck in a wall go when their time is up.
        for (std::size_t i = 0; i < mStuck.size();)
        {
            mStuck[i].mSecondsLeft -= seconds;
            if (mStuck[i].mSecondsLeft <= 0.f)
            {
                sink.removeProjectile(mStuck[i].mId);
                mStuck.erase(mStuck.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        }
        if (mShots.empty())
            return;
        mCandidates.clear();
        if (world != nullptr)
            world->collectCandidates(mCandidates);
        std::erase_if(mCandidates, [&](const NeutralSweep::Candidate& c) {
            return c.mTarget == openeden::sdk::game::SweepTarget::Actor && isDead(c.mActor);
        });
        std::optional<WorldRayAdapter> ray;
        if (world != nullptr)
            ray.emplace(*world);
        for (std::size_t i = 0; i < mShots.size();)
        {
            NeutralProjectile::Shot& shot = mShots[i];
            NeutralProjectile::StepResult result;
            if (shot.mAge >= kMaxProjectileSeconds)
            {
                result.mOutcome = NeutralProjectile::Outcome::Expired;
                result.mAt = shot.mPosition;
            }
            else
                result = NeutralProjectile::step(shot, seconds, mCandidates, ray ? &*ray : nullptr);
            if (result.mOutcome == NeutralProjectile::Outcome::Flying)
            {
                sink.moveProjectile(shot.mId, poseOf(result.mAt, result.mDirection));
                if (traceOn())
                    Log(Debug::Info) << "NCOMBAT projectile step id " << shot.mId << " at " << result.mAt.x << " "
                                     << result.mAt.y << " " << result.mAt.z << " age " << shot.mAge;
                ++i;
                continue;
            }
            if (shot.mVisualOnly)
            {
                // Task 0191: a drawn-only shot ends without a word to the pack and sticks in nothing.
                sink.removeProjectile(shot.mId);
                ++mStats.mVisualShotsEnded;
                mShots.erase(mShots.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            using openeden::sdk::game::ProjectileFate;
            openeden::sdk::game::ProjectileResult told;
            told.owner = shot.mOwner;
            told.token = shot.mToken;
            told.x = result.mAt.x;
            told.y = result.mAt.y;
            told.z = result.mAt.z;
            told.flightSeconds = shot.mAge;
            told.distance = shot.mTravelled;
            told.speed = NeutralProjectile::speedOf(shot);
            switch (result.mOutcome)
            {
                case NeutralProjectile::Outcome::HitActor:
                    told.fate = ProjectileFate::HitActor;
                    told.actor = result.mActor;
                    ++mStats.mProjectileHitsActor;
                    break;
                case NeutralProjectile::Outcome::HitPlayer:
                    told.fate = ProjectileFate::HitPlayer;
                    ++mStats.mProjectileHitsPlayer;
                    break;
                case NeutralProjectile::Outcome::HitWorld:
                    told.fate = ProjectileFate::HitWorld;
                    ++mStats.mProjectileHitsWorld;
                    break;
                default:
                    told.fate = ProjectileFate::Expired;
                    ++mStats.mProjectilesExpired;
                    break;
            }
            if (traceOn())
                Log(Debug::Info) << "NCOMBAT projectile end id " << shot.mId << " token " << shot.mToken << " fate "
                                 << static_cast<int>(told.fate) << " at " << told.x << " " << told.y << " " << told.z
                                 << " flight " << told.flightSeconds << " s distance " << told.distance;
            if (mProjectileResults.size() < mProjectileResults.capacity())
                mProjectileResults.push_back(told);
            else
                ++mStats.mProjectileResultsDropped;
            if (told.fate == ProjectileFate::HitWorld && shot.mStickSeconds > 0.f)
            {
                // The arrow stays, drawn its stick depth into the surface along its flight.
                const NeutralProjectile::Vec in{ result.mAt.x + result.mDirection.x * shot.mStickDepth,
                    result.mAt.y + result.mDirection.y * shot.mStickDepth,
                    result.mAt.z + result.mDirection.z * shot.mStickDepth };
                sink.moveProjectile(shot.mId, poseOf(in, result.mDirection));
                if (mStuck.size() >= kMaxStuckArrows)
                {
                    sink.removeProjectile(mStuck.front().mId);
                    mStuck.erase(mStuck.begin());
                }
                mStuck.push_back({ shot.mId, shot.mStickSeconds });
            }
            else
                sink.removeProjectile(shot.mId);
            mShots.erase(mShots.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    // ---- task 0191: rays and shown effects -----------------------------------------------------------------------

    bool NeutralCombatBridge::Writer::doCastRay(
        CombatSubject from, const openeden::sdk::game::RayRequest& r, openeden::sdk::game::RayHit& out)
    {
        NeutralCombatBridge& b = mBridge;
        out = {};
        const bool finite = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z)
            && std::isfinite(r.heading) && std::isfinite(r.pitch) && std::isfinite(r.range);
        if (mSink == nullptr || mWorld == nullptr || !from.valid() || !finite
            || !(r.range > 0.f && r.range <= kMaxProjectileRange) || !(std::abs(r.pitch) < kHalfPi)
            || (!from.player && b.mActors.keyOfHandle(from.actor) == nullptr))
        {
            ++b.mStats.mRaysRefused;
            return false;
        }
        b.mCandidates.clear();
        mWorld->collectCandidates(b.mCandidates);
        std::erase_if(b.mCandidates, [&](const NeutralSweep::Candidate& c) {
            return c.mTarget == openeden::sdk::game::SweepTarget::Actor && b.isDead(c.mActor);
        });
        // A ray is one step of a gravity-free shot: speed = range over one second.
        const double heading = static_cast<double>(r.heading);
        const double pitch = static_cast<double>(r.pitch);
        const double range = static_cast<double>(r.range);
        NeutralProjectile::Shot shot;
        shot.mOwner = from;
        shot.mPosition = { r.x, r.y, r.z };
        shot.mVelocity = { std::sin(heading) * std::cos(pitch) * range, std::cos(heading) * std::cos(pitch) * range,
            std::sin(pitch) * range };
        shot.mRangeLeft = r.range;
        WorldRayAdapter ray(*mWorld);
        const NeutralProjectile::StepResult result = NeutralProjectile::step(shot, 1.f, b.mCandidates, &ray);
        using openeden::sdk::game::RayFate;
        switch (result.mOutcome)
        {
            case NeutralProjectile::Outcome::HitActor:
                out.fate = RayFate::Actor;
                out.actor = result.mActor;
                break;
            case NeutralProjectile::Outcome::HitPlayer:
                out.fate = RayFate::Player;
                break;
            case NeutralProjectile::Outcome::HitWorld:
                out.fate = RayFate::World;
                break;
            default:
                out.fate = RayFate::Nothing;
                break;
        }
        out.x = result.mAt.x;
        out.y = result.mAt.y;
        out.z = result.mAt.z;
        out.distance = shot.mTravelled;
        ++b.mStats.mRaysCast;
        return true;
    }

    bool NeutralCombatBridge::Writer::doShowEffect(CombatSubject subject, const openeden::sdk::game::EffectRequest& r)
    {
        NeutralCombatBridge& b = mBridge;
        openeden::sdk::game::SubjectState state;
        const bool replaces = std::any_of(
            b.mEffects.begin(), b.mEffects.end(), [&](const Effect& e) { return e.mToken == r.token; });
        if (mSink == nullptr || mWorld == nullptr || !subject.valid() || r.model.empty() || !validModelPath(r.model)
            || !std::isfinite(r.heightOffset) || (!subject.player && b.mActors.keyOfHandle(subject.actor) == nullptr)
            || (!replaces && b.mEffects.size() >= openeden::sdk::game::kMaxShownEffects)
            || !mWorld->subjectState(subject, state))
        {
            if (traceOn())
                Log(Debug::Info) << "NCOMBAT effect refused token " << r.token << " sink " << (mSink != nullptr)
                                 << " world " << (mWorld != nullptr) << " valid " << subject.valid() << " model '"
                                 << r.model << "' key "
                                 << (subject.player || b.mActors.keyOfHandle(subject.actor) != nullptr);
            ++b.mStats.mEffectsRefused;
            return false;
        }
        if (replaces)
        {
            for (std::size_t i = 0; i < b.mEffects.size(); ++i)
                if (b.mEffects[i].mToken == r.token)
                {
                    mSink->removeProjectile(b.mEffects[i].mId);
                    b.mEffects.erase(b.mEffects.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
        }
        Effect effect;
        effect.mId = b.mNextProjectileId++;
        if (b.mNextProjectileId == 0)
            b.mNextProjectileId = 1;
        effect.mToken = r.token;
        effect.mSubject = subject;
        effect.mHeight = r.heightOffset;
        b.mEffects.push_back(effect);
        const double heading = static_cast<double>(state.heading);
        mSink->spawnProjectile(effect.mId, r.model,
            poseOf({ state.x, state.y, state.z + static_cast<double>(effect.mHeight) },
                { std::sin(heading), std::cos(heading), 0.0 }));
        ++b.mStats.mEffectsShown;
        return true;
    }

    bool NeutralCombatBridge::Writer::doHideEffect(std::uint32_t token)
    {
        NeutralCombatBridge& b = mBridge;
        if (mSink == nullptr)
            return false;
        for (std::size_t i = 0; i < b.mEffects.size(); ++i)
            if (b.mEffects[i].mToken == token)
            {
                mSink->removeProjectile(b.mEffects[i].mId);
                b.mEffects.erase(b.mEffects.begin() + static_cast<std::ptrdiff_t>(i));
                ++b.mStats.mEffectsHidden;
                return true;
            }
        return false;
    }

    bool NeutralCombatBridge::Writer::castRay(
        CombatSubject from, const openeden::sdk::game::RayRequest& request, openeden::sdk::game::RayHit& out)
    {
        if (mSink == nullptr)
        {
            ++mBridge.mStats.mRaysRefused;
            out = {};
            return false;
        }
        return guarded([&] { return doCastRay(from, request, out); });
    }

    bool NeutralCombatBridge::Writer::showEffect(
        CombatSubject subject, const openeden::sdk::game::EffectRequest& request)
    {
        if (mSink == nullptr)
        {
            ++mBridge.mStats.mEffectsRefused;
            return false;
        }
        return guarded([&] { return doShowEffect(subject, request); });
    }

    bool NeutralCombatBridge::Writer::hideEffect(std::uint32_t token)
    {
        return guarded([&] { return doHideEffect(token); });
    }

    void NeutralCombatBridge::followEffects(NeutralClipSink& sink, NeutralCombatWorld* world)
    {
        for (std::size_t i = 0; i < mEffects.size();)
        {
            const Effect& effect = mEffects[i];
            openeden::sdk::game::SubjectState state;
            const bool gone = !effect.mSubject.player && mActors.keyOfHandle(effect.mSubject.actor) == nullptr;
            if (gone)
            {
                sink.removeProjectile(effect.mId);
                ++mStats.mEffectsLost;
                mEffects.erase(mEffects.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (world != nullptr && world->subjectState(effect.mSubject, state))
            {
                const double heading = static_cast<double>(state.heading);
                sink.moveProjectile(effect.mId,
                    poseOf({ state.x, state.y, state.z + static_cast<double>(effect.mHeight) },
                        { std::sin(heading), std::cos(heading), 0.0 }));
            }
            ++i;
        }
    }

    bool NeutralCombatBridge::playerAttackEdge(openeden::sdk::game::CombatRules* rules, bool pressed)
    {
        if (rules == nullptr)
        {
            ++mStats.mPlayerAttackRefused;
            return false;
        }
        try
        {
            rules->onPlayerAttackEdge(pressed);
        }
        catch (...)
        {
            ++mStats.mPlayerAttackRefused;
            ++mStats.mRulesFailures;
            return false;
        }
        ++mStats.mPlayerAttackEdges;
        return true;
    }
}
