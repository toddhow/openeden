// Task 0171 (combat stage S5-3b follow-up): the combat clip clock does not depend on worker timing. The meta of a clip
// is read on a worker and the renderer starts sampling the clip when its decode is done, so the frame on which the bridge
// first SEES the meta (and the renderer's playback time then) varies from run to run. The clock of a clip counts from the
// frame the clip was requested, so the frame on which an event is delivered must not change with it. Written from the
// bridge header before the fix (the first run was against the old clock, which started at the renderer's time).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    class OneClip final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent& event) override
        {
            if (event.kind == ClipEventKind::Hit)
                mHitFrame = mFrame;
            if (event.kind == ClipEventKind::AttackStop)
                mStopFrame = mFrame;
            ++mEvents;
        }
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void onClipFinished(CombatSubject, std::uint32_t, ClipEnd how) override
        {
            if (how == ClipEnd::Completed)
                mFinishedFrame = mFrame;
        }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mFrame == 1)
            {
                ClipRequest request;
                request.slot = ClipSlot::Attack;
                request.clip = "1HM_AttackRight";
                request.token = 7;
                mAccepted = writer.playClip(mSubject, request);
            }
        }
        CombatSubject mSubject;
        int mFrame = 0;
        int mHitFrame = -1;
        int mStopFrame = -1;
        int mFinishedFrame = -1;
        int mEvents = 0;
        bool mAccepted = false;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
    };

    struct Frames
    {
        int mHit = -1;
        int mStop = -1;
        int mFinished = -1;
    };

    constexpr float kDt = 1.f / 60.f;

    // Runs 120 frames; the clip is asked for in frame 1. The meta becomes visible to the bridge in frame `readyAt`, with
    // the renderer reporting `rendererTime` playback seconds and `started`.
    Frames run(int readyAt, float rendererTime, bool started)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 1.3f;
        meta->mSpeed = 1.f;
        meta->mEvents = { { "preHitFrame", 0.233f }, { "HitFrame", 0.367f }, { "attackStop", 1.287f } };
        meta->sortEvents();

        int key = 0;
        NeutralActorBridge actors;
        const ActorHandle actor = actors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
        NeutralCombatBridge bridge(actors);
        OneClip rules;
        rules.mSubject = CombatSubject::of(actor);
        Sink sink;
        int frame = 0;
        const MWPhysics::NeutralClipMetaFn metaFn = [&](const void*) {
            if (frame < readyAt)
                return HavokPack::ClipProgress();
            return HavokPack::ClipProgress{ meta, started, rendererTime };
        };
        for (frame = 1; frame <= 120; ++frame)
        {
            rules.mFrame = frame;
            bridge.update(&rules, kDt, sink, metaFn);
        }
        EXPECT_TRUE(rules.mAccepted);
        return { rules.mHitFrame, rules.mStopFrame, rules.mFinishedFrame };
    }

    TEST(CombatClipClock, theFrameOfAnEventDoesNotDependOnWhenTheMetaBecomesVisible)
    {
        const Frames reference = run(2, 0.f, true); // visible at once (the cached clip)
        ASSERT_GT(reference.mHit, 0);
        for (const int readyAt : { 3, 4, 5, 8 })
        {
            const Frames late = run(readyAt, 0.f, true);
            EXPECT_EQ(late.mHit, reference.mHit) << "meta visible in frame " << readyAt;
            EXPECT_EQ(late.mStop, reference.mStop) << "meta visible in frame " << readyAt;
            EXPECT_EQ(late.mFinished, reference.mFinished) << "meta visible in frame " << readyAt;
        }
    }

    TEST(CombatClipClock, theRenderersOwnPlaybackTimeDoesNotMoveTheClock)
    {
        const Frames reference = run(2, 0.f, true);
        for (const float rendererTime : { kDt, 3 * kDt, 0.1f })
        {
            const Frames other = run(3, rendererTime, true);
            EXPECT_EQ(other.mHit, reference.mHit) << "renderer time " << rendererTime;
            EXPECT_EQ(other.mFinished, reference.mFinished) << "renderer time " << rendererTime;
        }
    }

    TEST(CombatClipClock, theClockCountsFromTheRequestSoTheHitFrameIsWhereTheSidecarSaysItIs)
    {
        // Asked in frame 1, the first update with a clock is frame 2 (time 0); HitFrame 0.367 s is 22 frames later:
        // 0.367 / (1/60) = 22.02, so the event fires in frame 2 + 23 = 25 (the first frame whose clock passes it).
        const Frames frames = run(2, 0.f, true);
        EXPECT_EQ(frames.mHit, 25);
        EXPECT_GT(frames.mFinished, frames.mHit);
    }

    TEST(CombatClipClock, aMetaThatArrivesAfterAnEventStillDeliversItInOrderAtOnce)
    {
        // Visible only in frame 40 (0.65 s after the request): every event whose time has passed is delivered in
        // that frame, in clip-time order, and the later ones keep their frames.
        const Frames late = run(40, 0.f, true);
        EXPECT_EQ(late.mHit, 40);
        const Frames reference = run(2, 0.f, true);
        EXPECT_EQ(late.mStop, reference.mStop);
    }
}
