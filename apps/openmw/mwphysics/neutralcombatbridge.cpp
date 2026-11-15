#include "neutralcombatbridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

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
            { "BowReleaseFast", ClipEventKind::BowRelease },
            { "staggerStop", ClipEventKind::StaggerStop },
            { "recoilStop", ClipEventKind::RecoilStop },
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
                }
                catch (...)
                {
                    ++mStats.mWriterFailures; // the clip keeps waiting; the timeout below still ends it
                }
                if (progress.mMeta)
                    p.mMeta = progress.mMeta;
                if (!p.mMeta)
                {
                    p.mWaited += seconds;
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
                p.mCurrent += seconds;
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
        const auto updateStart = traceOn() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
        try
        {
            rules->combatUpdate(seconds, mWriter);
        }
        catch (...)
        {
            ++mStats.mRulesFailures;
        }
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
}
