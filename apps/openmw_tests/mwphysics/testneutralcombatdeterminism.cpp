// Task 0183: with a meta waiter set (the host sets one when it runs with a fixed frame time, as every repeatable test
// run does) the bridge waits for the meta of a clip it has just started, so the frame on which EACH event is delivered
// (the ones at clip time 0 too, which a late meta used to deliver in the frame it arrived) and the order of the events
// of several clips in one frame no longer depend on how long a worker took to read the sidecar. Written from the header
// before the waiter existed (the setter was a stub that did nothing).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    class TwoClips final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent& event) override
        {
            mLog.push_back(std::to_string(mFrame) + " " + std::to_string(event.token) + " " + std::string(event.name));
        }
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mFrame != 1)
                return;
            for (int i = 0; i < 2; ++i)
            {
                ClipRequest request;
                request.slot = ClipSlot::Attack;
                request.clip = i == 0 ? "1HM_AttackRight" : "Bow_Release";
                request.token = static_cast<std::uint32_t>(1 + i);
                (void)writer.playClip(mSubject[i], request);
            }
        }
        CombatSubject mSubject[2];
        int mFrame = 0;
        std::vector<std::string> mLog;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
    };

    constexpr float kDt = 1.f / 60.f;

    std::shared_ptr<const HavokPack::ClipMeta> makeMeta(float second)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 1.3f;
        meta->mSpeed = 1.f;
        meta->mEvents = { { "SoundPlay", 0.f }, { "arrowRelease", second }, { "attackStop", 1.287f } };
        meta->sortEvents();
        return meta;
    }

    struct Result
    {
        std::vector<std::string> mLog;
        int mWaits = 0;
    };

    /// Both clips are asked for in frame 1; the first meta is visible from frame `readyA`, the second from `readyB`.
    Result run(int readyA, int readyB, bool withWaiter)
    {
        const auto metaA = makeMeta(0.0333f);
        const auto metaB = makeMeta(0.0667f);
        int keyA = 0;
        int keyB = 0;
        NeutralActorBridge actors;
        const ActorHandle a = actors.add(nullptr, &keyA, 11, 1, "A", 0, 0, 0, 0);
        const ActorHandle b = actors.add(nullptr, &keyB, 12, 1, "B", 0, 0, 0, 0);
        NeutralCombatBridge bridge(actors);
        TwoClips rules;
        rules.mSubject[0] = CombatSubject::of(a);
        rules.mSubject[1] = CombatSubject::of(b);
        Sink sink;
        Result result;
        int frame = 0;
        const MWPhysics::NeutralClipMetaFn metaFn = [&](const void* key) {
            const bool isA = key == &keyA;
            if (frame < (isA ? readyA : readyB))
                return HavokPack::ClipProgress();
            return HavokPack::ClipProgress{ isA ? metaA : metaB, true, 0.f };
        };
        if (withWaiter)
            bridge.setMetaWaiter([&](const void* key) {
                ++result.mWaits;
                const bool isA = key == &keyA;
                return HavokPack::ClipProgress{ isA ? metaA : metaB, true, 0.f };
            });
        for (frame = 1; frame <= 120; ++frame)
        {
            rules.mFrame = frame;
            bridge.update(&rules, kDt, sink, metaFn);
        }
        result.mLog = rules.mLog;
        return result;
    }

    TEST(CombatMetaWaiter, withAWaiterEveryEventComesOnTheSameFrameWhenTheMetaArrives)
    {
        const Result reference = run(2, 2, true);
        ASSERT_GE(reference.mLog.size(), 6u);
        for (const std::pair<int, int>& ready : { std::pair{ 2, 9 }, { 9, 2 }, { 12, 12 }, { 30, 3 } })
        {
            const Result other = run(ready.first, ready.second, true);
            EXPECT_EQ(other.mLog, reference.mLog) << "meta visible in frames " << ready.first << " and " << ready.second;
        }
    }

    TEST(CombatMetaWaiter, theWaiterIsAskedOncePerClipAndOnlyWhenTheMetaIsNotThereYet)
    {
        EXPECT_EQ(run(40, 40, true).mWaits, 2);
        EXPECT_EQ(run(2, 2, true).mWaits, 0); // visible at the first poll: nothing to wait for
    }

    TEST(CombatMetaWaiter, withoutAWaiterTheEventsStillFollowTheArrival)
    {
        // The behaviour of every build before this task, and of a game that runs with a real frame time.
        EXPECT_NE(run(2, 2, false).mLog, run(9, 2, false).mLog);
    }
}
