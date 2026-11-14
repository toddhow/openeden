// Task 0170 (combat stage S5-2): the engine's side of the combat hooks, tested with a fake pack, a fake sink and a fake
// clip meta provider, so no Skyrim data, no Jolt world and no Ptr is needed. The tests were written from the header
// (neutralcombatbridge.hpp) and the contract (combat_rules.hpp).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    struct SeenEvent
    {
        ClipEventKind mKind;
        float mTime;
        std::uint32_t mToken;
        ClipSlot mSlot;
        bool mWasInUpdate;
    };

    // Records what it is told; each combatUpdate plays the clips of mWanted (once each) and keeps the writer.
    class TestCombat final : public CombatRules
    {
    public:
        TestCombat()
        {
            mEvents.reserve(64);
            mFinished.reserve(16);
        }
        void onClipEvent(const ClipEvent& event) override
        {
            if (mThrowOnEvent)
                throw std::runtime_error("pack failure");
            mEvents.push_back({ event.kind, event.clipTime, event.token, event.slot, mInUpdate });
            if (event.kind == ClipEventKind::Hit)
                mHitName = std::string(event.name);
        }
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void onClipFinished(CombatSubject, std::uint32_t token, ClipEnd how) override
        {
            mFinished.push_back({ token, how });
        }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            mInUpdate = true;
            mResults.clear();
            for (const Wanted& wanted : mWanted)
            {
                ClipRequest request;
                request.slot = wanted.mSlot;
                request.clip = wanted.mClip;
                request.blendSeconds = wanted.mBlend;
                request.token = wanted.mToken;
                mResults.push_back(writer.playClip(wanted.mSubject, request));
            }
            mWanted.clear();
            if (mStop)
                mStopResult = writer.stopClip(*mStop);
            mStop.reset();
            mKeptWriter = &writer;
            mInUpdate = false;
        }

        struct Wanted
        {
            CombatSubject mSubject;
            ClipSlot mSlot = ClipSlot::Attack;
            std::string mClip = "1HM_AttackRight";
            float mBlend = 0.1f;
            std::uint32_t mToken = 1;
        };
        std::vector<Wanted> mWanted;
        std::optional<CombatSubject> mStop;
        bool mStopResult = false;
        std::vector<bool> mResults;
        std::vector<SeenEvent> mEvents;
        std::vector<std::pair<std::uint32_t, ClipEnd>> mFinished;
        std::string mHitName;
        CombatRequestWriter* mKeptWriter = nullptr;
        bool mInUpdate = false;
        bool mThrowOnEvent = false;
    };

    struct SinkOp
    {
        const void* mKey;
        std::string mClip; // empty: stop
        bool mPerSex;
    };

    class TestSink final : public MWPhysics::NeutralClipSink
    {
    public:
        TestSink() { mOps.reserve(32); }
        void playClip(const void* key, std::string_view clip, bool perSex) override
        {
            mOps.push_back({ key, std::string(clip), perSex });
        }
        void stopClip(const void* key) override { mOps.push_back({ key, std::string(), false }); }
        std::vector<SinkOp> mOps;
    };

    // The attack clip of 1HM_AttackRight, from animationdata/defaultmale.txt (events in playback seconds).
    std::shared_ptr<const HavokPack::ClipMeta> attackMeta(float speed = 1.f, float duration = 1.3f)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = duration;
        meta->mSpeed = speed;
        meta->mEvents = { { "preHitFrame", 0.233f }, { "weaponSwing", 0.333f }, { "HitFrame", 0.367f },
            { "attackStop", 1.287f } };
        meta->sortEvents();
        return meta;
    }

    struct Fixture : ::testing::Test
    {
        int mKeyA = 0;
        int mKeyB = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        TestCombat mRules;
        TestSink mSink;
        std::shared_ptr<const HavokPack::ClipMeta> mMeta = attackMeta();
        bool mMetaReady = true;
        bool mStarted = true; ///< the renderer has begun to sample the clip
        float mRendererTime = 0.f; ///< how far into the clip it was when the bridge first looked
        MWPhysics::NeutralClipMetaFn mMetaFn = [this](const void*) -> HavokPack::ClipProgress {
            return { mMetaReady ? mMeta : nullptr, mStarted, mRendererTime };
        };
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mHandleB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);

        void frame(float seconds = 0.1f) { mCombat.update(&mRules, seconds, mSink, mMetaFn); }
        void startSwing(std::uint32_t token = 1)
        {
            mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "1HM_AttackRight", 0.1f, token });
            frame(); // the request is made in this frame's combatUpdate
        }
    };

    TEST_F(Fixture, aSwingDeliversTheSidecarEventsInClipOrderAtTheFramesThatPassThem)
    {
        startSwing(7);
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_TRUE(mRules.mResults[0]);
        ASSERT_EQ(mSink.mOps.size(), 1u);
        EXPECT_EQ(mSink.mOps[0].mKey, &mKeyA);
        EXPECT_EQ(mSink.mOps[0].mClip, "1HM_AttackRight");
        EXPECT_TRUE(mRules.mEvents.empty()); // the clock has not started: it begins with the next frame
        // Frame 1 starts the clock at 0, then 0.1 s steps: 0.1, 0.2, 0.3, 0.4 ...
        std::vector<std::pair<int, ClipEventKind>> byFrame; // frame index at which each event arrives
        for (int i = 1; i <= 16; ++i)
        {
            const std::size_t before = mRules.mEvents.size();
            frame(0.1f);
            for (std::size_t k = before; k < mRules.mEvents.size(); ++k)
                byFrame.push_back({ i, mRules.mEvents[k].mKind });
        }
        ASSERT_EQ(mRules.mEvents.size(), 4u);
        EXPECT_EQ(mRules.mEvents[0].mKind, ClipEventKind::PreHit);
        EXPECT_EQ(mRules.mEvents[1].mKind, ClipEventKind::WeaponSwing);
        EXPECT_EQ(mRules.mEvents[2].mKind, ClipEventKind::Hit);
        EXPECT_EQ(mRules.mEvents[3].mKind, ClipEventKind::AttackStop);
        EXPECT_FLOAT_EQ(mRules.mEvents[0].mTime, 0.233f);
        EXPECT_FLOAT_EQ(mRules.mEvents[2].mTime, 0.367f);
        EXPECT_FLOAT_EQ(mRules.mEvents[3].mTime, 1.287f);
        for (const SeenEvent& e : mRules.mEvents)
        {
            EXPECT_EQ(e.mToken, 7u);
            EXPECT_EQ(e.mSlot, ClipSlot::Attack);
            EXPECT_FALSE(e.mWasInUpdate); // events come BEFORE combatUpdate, never from inside a request
        }
        EXPECT_EQ(mRules.mHitName, "HitFrame");
        // The clock: frame 1 = 0.0 (first frame with a clock), frame n = (n - 1) * 0.1. 0.233 passes in frame 4 (0.3),
        // 0.333 in frame 5 (0.4), 0.367 in frame 5 too (several events in one frame), 1.287 in frame 14 (1.3).
        ASSERT_EQ(byFrame.size(), 4u);
        EXPECT_EQ(byFrame[0].first, 4);
        EXPECT_EQ(byFrame[1].first, 5);
        EXPECT_EQ(byFrame[2].first, 5);
        EXPECT_EQ(byFrame[3].first, 14);
        // The clip ends at its duration (1.3): frame 14 (clock 1.3 >= playback duration).
        ASSERT_EQ(mRules.mFinished.size(), 1u);
        EXPECT_EQ(mRules.mFinished[0].first, 7u);
        EXPECT_EQ(mRules.mFinished[0].second, ClipEnd::Completed);
        EXPECT_EQ(mCombat.playing(), 0u);
        ASSERT_EQ(mSink.mOps.size(), 2u);
        EXPECT_TRUE(mSink.mOps[1].mClip.empty()); // stopped
        EXPECT_EQ(mCombat.stats().mClipsCompleted, 1u);
        EXPECT_EQ(mCombat.stats().mEventsDelivered, 4u);
    }

    TEST_F(Fixture, aLongFrameDeliversEverythingItPassesInOrderAndEndsTheClip)
    {
        startSwing();
        frame(0.f); // clock starts
        frame(5.f);
        ASSERT_EQ(mRules.mEvents.size(), 4u);
        for (std::size_t i = 1; i < mRules.mEvents.size(); ++i)
            EXPECT_LE(mRules.mEvents[i - 1].mTime, mRules.mEvents[i].mTime);
        ASSERT_EQ(mRules.mFinished.size(), 1u);
        EXPECT_EQ(mRules.mFinished[0].second, ClipEnd::Completed);
    }

    TEST_F(Fixture, thePlaybackSpeedScalesTheClipLengthNotTheEventTimes)
    {
        // At speed 2 the animation (2.6 s) plays in 1.3 playback seconds; the stored times are playback times.
        mMeta = attackMeta(2.f, 2.6f);
        startSwing();
        frame(0.f);
        frame(1.0f);
        EXPECT_EQ(mRules.mEvents.size(), 3u); // 0.233, 0.333, 0.367; attackStop at 1.287 is not yet passed
        EXPECT_TRUE(mRules.mFinished.empty());
        frame(0.4f); // clock 1.4 >= 2.6 / 2
        EXPECT_EQ(mRules.mEvents.size(), 4u);
        EXPECT_EQ(mRules.mFinished.size(), 1u);
    }

    TEST_F(Fixture, noClockRunsUntilTheMetaArrivesAndThenTimeStartsAtZero)
    {
        mMetaReady = false;
        startSwing();
        for (int i = 0; i < 10; ++i)
            frame(0.2f);
        EXPECT_TRUE(mRules.mEvents.empty());
        EXPECT_EQ(mCombat.playing(), 1u);
        mMetaReady = true;
        frame(0.2f); // starts the clock: time 0, no event yet
        EXPECT_TRUE(mRules.mEvents.empty());
        frame(0.3f); // time 0.3: preHitFrame (0.233) passed
        ASSERT_EQ(mRules.mEvents.size(), 1u);
        EXPECT_EQ(mRules.mEvents[0].mKind, ClipEventKind::PreHit);
    }

    TEST_F(Fixture, theClockStartsFromTheTimeTheRendererHasReachedSoEventsAndThePoseAgree)
    {
        // The renderer began sampling the clip 0.35 s before the bridge first looked (a worker was slow): the events the
        // drawn pose has already passed (0.233, 0.333) fire at once, in order; the rest follow by the frame time.
        mRendererTime = 0.35f;
        startSwing(9);
        frame(0.f);
        ASSERT_EQ(mRules.mEvents.size(), 2u);
        EXPECT_EQ(mRules.mEvents[0].mKind, ClipEventKind::PreHit);
        EXPECT_EQ(mRules.mEvents[1].mKind, ClipEventKind::WeaponSwing);
        frame(0.05f); // clock 0.40: HitFrame (0.367)
        ASSERT_EQ(mRules.mEvents.size(), 3u);
        EXPECT_EQ(mRules.mEvents[2].mKind, ClipEventKind::Hit);
    }

    TEST_F(Fixture, noClockRunsWhileTheRendererHasNotStartedTheClip)
    {
        mStarted = false; // the clip is still being decoded
        startSwing();
        for (int i = 0; i < 10; ++i)
            frame(0.2f);
        EXPECT_TRUE(mRules.mEvents.empty());
        mStarted = true;
        mRendererTime = 0.f;
        frame(0.2f);
        frame(0.3f);
        ASSERT_EQ(mRules.mEvents.size(), 1u);
        EXPECT_EQ(mRules.mEvents[0].mKind, ClipEventKind::PreHit);
    }

    TEST_F(Fixture, aMetaThatNeverComesEndsTheClipAsFailed)
    {
        mMetaReady = false;
        startSwing(3);
        for (int i = 0; i < 70; ++i)
            frame(0.1f);
        ASSERT_EQ(mRules.mFinished.size(), 1u);
        EXPECT_EQ(mRules.mFinished[0].first, 3u);
        EXPECT_EQ(mRules.mFinished[0].second, ClipEnd::Failed);
        EXPECT_EQ(mCombat.playing(), 0u);
        EXPECT_EQ(mCombat.stats().mClipsFailed, 1u);
        EXPECT_TRUE(mSink.mOps.back().mClip.empty()); // the renderer's clip is stopped too
    }

    TEST_F(Fixture, aNewRequestInterruptsTheRunningClipAndTheEndIsToldNextFrame)
    {
        startSwing(1);
        frame(0.3f);
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::PowerAttack, "1HM_AttackPower", 0.1f, 2 });
        frame(0.1f);
        EXPECT_TRUE(mRules.mFinished.empty()); // told at the start of the next frame
        frame(0.1f);
        ASSERT_EQ(mRules.mFinished.size(), 1u);
        EXPECT_EQ(mRules.mFinished[0].first, 1u);
        EXPECT_EQ(mRules.mFinished[0].second, ClipEnd::Interrupted);
        EXPECT_EQ(mCombat.playing(), 1u);
        EXPECT_EQ(mSink.mOps.back().mClip, "1HM_AttackPower"); // the new clip was started on the renderer
    }

    TEST_F(Fixture, stopClipEndsItAsInterruptedAndIsATrueNoOpWithoutOne)
    {
        startSwing(5);
        mRules.mStop = CombatSubject::of(mHandleA);
        frame();
        EXPECT_TRUE(mRules.mStopResult);
        EXPECT_EQ(mCombat.playing(), 0u);
        EXPECT_TRUE(mSink.mOps.back().mClip.empty());
        mRules.mStop = CombatSubject::of(mHandleB); // nothing plays on B
        frame();
        EXPECT_TRUE(mRules.mStopResult);
        ASSERT_EQ(mRules.mFinished.size(), 1u);
        EXPECT_EQ(mRules.mFinished[0].second, ClipEnd::Interrupted);
    }

    TEST_F(Fixture, requestsThatBreakTheContractAreRefusedAndCounted)
    {
        mRules.mWanted.push_back({ CombatSubject::ofPlayer(), ClipSlot::Attack, "1HM_AttackRight", 0.1f, 1 });
        mRules.mWanted.push_back({ CombatSubject::of(ActorHandle{ 99, 99 }), ClipSlot::Attack, "x", 0.1f, 2 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::None, "1HM_AttackRight", 0.1f, 3 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "", 0.1f, 4 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "bad name", 0.1f, 5 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "../x", 0.1f, 6 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, std::string(65, 'a'), 0.1f, 7 });
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "ok", -1.f, 8 });
        mRules.mWanted.push_back(
            { CombatSubject::of(mHandleA), ClipSlot::Attack, "ok", std::numeric_limits<float>::quiet_NaN(), 9 });
        frame();
        ASSERT_EQ(mRules.mResults.size(), 9u);
        for (bool result : mRules.mResults)
            EXPECT_FALSE(result);
        EXPECT_EQ(mCombat.stats().mRefused, 9u);
        EXPECT_EQ(mCombat.playing(), 0u);
        EXPECT_TRUE(mSink.mOps.empty());
    }

    TEST_F(Fixture, theWriterRefusesOutsideCombatUpdate)
    {
        frame(); // keeps the writer
        ASSERT_NE(mRules.mKeptWriter, nullptr);
        ClipRequest request;
        request.slot = ClipSlot::Attack;
        request.clip = "1HM_AttackRight";
        EXPECT_FALSE(mRules.mKeptWriter->playClip(CombatSubject::of(mHandleA), request));
        EXPECT_FALSE(mRules.mKeptWriter->stopClip(CombatSubject::of(mHandleA)));
        EXPECT_EQ(mCombat.stats().mRefused, 2u);
        EXPECT_TRUE(mSink.mOps.empty());
    }

    TEST_F(Fixture, withoutRulesNothingRunsAndRunningClipsAreStopped)
    {
        startSwing();
        ASSERT_EQ(mCombat.playing(), 1u);
        mCombat.update(nullptr, 0.1f, mSink, mMetaFn);
        EXPECT_EQ(mCombat.playing(), 0u);
        EXPECT_TRUE(mSink.mOps.back().mClip.empty());
        EXPECT_TRUE(mRules.mFinished.empty()); // nobody to tell
    }

    TEST_F(Fixture, anActorThatWentAwayEndsItsClipSilently)
    {
        startSwing();
        mActors.remove(nullptr, &mKeyA);
        frame(0.5f);
        EXPECT_EQ(mCombat.playing(), 0u);
        EXPECT_TRUE(mRules.mEvents.empty());
        EXPECT_TRUE(mRules.mFinished.empty());
    }

    TEST_F(Fixture, aPackThatThrowsInAnEventDoesNotStopTheBridge)
    {
        startSwing();
        mRules.mThrowOnEvent = true;
        frame(0.f);
        frame(2.f);
        EXPECT_EQ(mCombat.stats().mRulesFailures, 4u); // one per event
        EXPECT_EQ(mRules.mFinished.size(), 1u);
    }

    TEST_F(Fixture, classifyNamesTheSidecarEventsIgnoringCase)
    {
        using MWPhysics::classifyClipEvent;
        EXPECT_EQ(classifyClipEvent("preHitFrame"), ClipEventKind::PreHit);
        EXPECT_EQ(classifyClipEvent("WEAPONSWING"), ClipEventKind::WeaponSwing);
        EXPECT_EQ(classifyClipEvent("hitframe"), ClipEventKind::Hit);
        EXPECT_EQ(classifyClipEvent("attackStop"), ClipEventKind::AttackStop);
        EXPECT_EQ(classifyClipEvent("AttackWinStart"), ClipEventKind::AttackWindowStart);
        EXPECT_EQ(classifyClipEvent("AttackWinEnd"), ClipEventKind::AttackWindowEnd);
        EXPECT_EQ(classifyClipEvent("InitiateWinBegin"), ClipEventKind::BlockWindowStart);
        EXPECT_EQ(classifyClipEvent("InitiateWinEnd"), ClipEventKind::BlockWindowEnd);
        EXPECT_EQ(classifyClipEvent("BowReleaseFast"), ClipEventKind::BowRelease);
        EXPECT_EQ(classifyClipEvent("staggerStop"), ClipEventKind::StaggerStop);
        EXPECT_EQ(classifyClipEvent("recoilStop"), ClipEventKind::RecoilStop);
        EXPECT_EQ(classifyClipEvent("FootRight"), ClipEventKind::Other);
        EXPECT_EQ(classifyClipEvent(""), ClipEventKind::Other);
    }

    TEST_F(Fixture, deliveringEventsAndAdvancingTheClocksAllocatesNothing)
    {
        // Warm up: one full swing, so that every container has grown to its working size.
        startSwing();
        for (int i = 0; i < 20; ++i)
            frame(0.1f);
        mRules.mEvents.clear();
        mRules.mFinished.clear();
        mRules.mWanted.push_back({ CombatSubject::of(mHandleA), ClipSlot::Attack, "1HM_AttackRight", 0.1f, 2 });
        frame(); // the request itself (its sink copies the name: not part of the per-frame path)
        TestSink quiet; // a sink that does not copy names
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 20; ++i)
            mCombat.update(&mRules, 0.1f, quiet, mMetaFn);
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(mRules.mEvents.size(), 4u);
    }
}
