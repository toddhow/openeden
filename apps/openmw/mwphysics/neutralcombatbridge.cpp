#include "neutralcombatbridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
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
            return a.size() == b.size()
                && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
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

    NeutralCombatBridge::NeutralCombatBridge(const NeutralActorBridge& actors)
        : mActors(actors)
        , mWriter(*this)
    {
        mPlaying.reserve(kMaxPlaying);
        mLate.reserve(2 * kMaxPlaying);
    }

    void NeutralCombatBridge::clear()
    {
        mPlaying.clear();
        mLate.clear();
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

    bool NeutralCombatBridge::Writer::playClip(CombatSubject subject, const ClipRequest& request)
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

    bool NeutralCombatBridge::Writer::stopClip(CombatSubject subject)
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

    void NeutralCombatBridge::update(
        CombatRules* rules, float seconds, NeutralClipSink& sink, const NeutralClipMetaFn& metaFn)
    {
        if (rules == nullptr)
        {
            for (const Playing& p : mPlaying)
                sink.stopClip(p.mKey);
            clear();
            return;
        }
        ++mStats.mUpdates;
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;
        for (const Finished& finished : mLate)
            tellFinished(*rules, finished);
        mLate.clear();

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
                const HavokPack::ClipProgress progress = metaFn ? metaFn(p.mKey) : HavokPack::ClipProgress{};
                if (progress.mMeta && progress.mStarted)
                    p.mMeta = progress.mMeta;
                if (!p.mMeta)
                {
                    p.mWaited += seconds;
                    if (p.mWaited > kMetaTimeoutSeconds)
                    {
                        sink.stopClip(p.mKey);
                        ++mStats.mClipsFailed;
                        tellFinished(*rules, { p.mSubject, p.mToken, ClipEnd::Failed });
                        mPlaying.erase(mPlaying.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }
                    ++i;
                    continue;
                }
                // The first frame with a clock: the time the renderer has reached in the clip (0 when it started this
                // very frame; events at or before it fire now, in order).
                p.mPrevious = -1.f;
                p.mCurrent = (std::isfinite(progress.mPlaybackTime) && progress.mPlaybackTime > 0.f) ? progress.mPlaybackTime : 0.f;
            }
            else
            {
                p.mPrevious = p.mCurrent;
                p.mCurrent += seconds;
            }
            const CombatSubject subject = p.mSubject;
            const ClipSlot slot = p.mSlot;
            const std::uint32_t token = p.mToken;
            const bool finished
                = HavokPack::advanceClipEvents(*p.mMeta, p.mPrevious, p.mCurrent, false,
                    [&](const HavokPack::ClipMetaEvent& event, float clipTime) {
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
                sink.stopClip(p.mKey);
                ++mStats.mClipsCompleted;
                tellFinished(*rules, { subject, token, ClipEnd::Completed });
                mPlaying.erase(mPlaying.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }

        mWriter.mSink = &sink;
        try
        {
            rules->combatUpdate(seconds, mWriter);
        }
        catch (...)
        {
            ++mStats.mRulesFailures;
        }
        mWriter.mSink = nullptr;
    }
}
