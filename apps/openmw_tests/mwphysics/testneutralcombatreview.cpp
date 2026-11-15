// Task 0171, review fixes of the engine's side of the combat hooks (apps/openmw/mwphysics/neutralcombatbridge.hpp): the
// dead list is bounded and pruned, nothing a call into the engine throws reaches the pack, a session with no combat
// reserves nothing, a stale actor is never stopped, and the player's health stays in range. Written first and shown
// red against the bridge before the fixes (the header carried only the accessors).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;
    using MWPhysics::NeutralSweep::Candidate;

    class Rules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mDo)
                mDo(writer);
            mDo = nullptr;
        }
        std::function<void(CombatRequestWriter&)> mDo;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override
        {
            if (mThrow)
                throw std::runtime_error("sink");
            ++mPlays;
        }
        void stopClip(const void* key) override
        {
            if (mThrow)
                throw std::runtime_error("sink");
            mStopped.push_back(key);
        }
        void setHeading(const void*, float) override
        {
            if (mThrow)
                throw std::runtime_error("sink");
        }
        bool mThrow = false;
        int mPlays = 0;
        std::vector<const void*> mStopped;
    };

    class World final : public MWPhysics::NeutralCombatWorld
    {
    public:
        bool subjectState(CombatSubject, SubjectState& out) override
        {
            if (mThrow)
                throw std::runtime_error("world");
            out = {};
            out.healthMax = 100.f;
            return true;
        }
        void collectCandidates(std::vector<Candidate>&) override
        {
            if (mThrow)
                throw std::runtime_error("world");
        }
        void applyPlayerDamage(const DamageReport& report) override
        {
            if (mThrow)
                throw std::runtime_error("world");
            mPlayerHealth = report.healthAfter;
            ++mPlayerHits;
        }
        bool lineBlocked(double, double, double, double, double, double) override
        {
            if (mThrow)
                throw std::runtime_error("world");
            return false;
        }
        std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view, bool) override
        {
            if (mThrow)
                throw std::runtime_error("world");
            return {};
        }
        void actorDied(const void*, ActorHandle) override
        {
            if (mThrow)
                throw std::runtime_error("world");
        }
        bool mThrow = false;
        float mPlayerHealth = -1.f;
        int mPlayerHits = 0;
    };

    SweepRequest swing()
    {
        SweepRequest r;
        r.token = 1;
        r.reach = 100.f;
        r.halfAngle = 0.6f;
        r.heightBelow = 16.f;
        r.heightAbove = 130.f;
        return r;
    }

    struct BridgeReview : ::testing::Test
    {
        NeutralActorBridge mActors;
        NeutralCombatBridge mBridge{ mActors };
        Rules mRules;
        Sink mSink;
        World mWorld;
        MWPhysics::NeutralClipMetaFn mMeta = [](const void*) { return HavokPack::ClipProgress(); };

        void frame(std::function<void(CombatRequestWriter&)> doIt = nullptr)
        {
            mRules.mDo = std::move(doIt);
            mBridge.update(&mRules, 0.1f, mSink, mMeta, &mWorld);
        }
    };

    // ---- finding 8: a session with no combat holds nothing --------------------------------------------------------

    TEST_F(BridgeReview, aBridgeThatNeverRunsRulesReservesNothing)
    {
        EXPECT_EQ(mBridge.reservedBytes(), 0u);
        mBridge.update(nullptr, 0.1f, mSink, mMeta, &mWorld);
        mBridge.update(nullptr, 0.1f, mSink, mMeta, &mWorld);
        EXPECT_EQ(mBridge.reservedBytes(), 0u);
        frame();
        EXPECT_GT(mBridge.reservedBytes(), 100000u); // the candidate table alone is about 200 KB
    }

    // ---- finding 3: the dead list ----------------------------------------------------------------------------------

    TEST_F(BridgeReview, theDeadOfActorsThatAreGoneArePruned)
    {
        std::vector<int> keys(10);
        std::vector<ActorHandle> handles;
        for (std::size_t i = 0; i < keys.size(); ++i)
            handles.push_back(mActors.add(nullptr, &keys[i], static_cast<WorldId>(100 + i), 1, "A", 0, 0, 0, 0));
        frame([&](CombatRequestWriter& w) {
            for (const ActorHandle& h : handles)
                EXPECT_TRUE(w.reportDeath(h));
        });
        EXPECT_EQ(mBridge.deadCount(), 10u);
        for (std::size_t i = 0; i < 5; ++i)
            mActors.remove(nullptr, &keys[i]);
        for (std::size_t i = 0; i < NeutralCombatBridge::kDeadPruneUpdates + 1; ++i)
            frame();
        EXPECT_EQ(mBridge.deadCount(), 5u); // before the fix: 10 for ever
        for (std::size_t i = 0; i < 5; ++i)
            EXPECT_FALSE(mBridge.isDead(handles[i]));
        for (std::size_t i = 5; i < 10; ++i)
            EXPECT_TRUE(mBridge.isDead(handles[i]));
    }

    TEST_F(BridgeReview, aFullDeadListIsPrunedAndOtherwiseSaysSo)
    {
        constexpr std::size_t kMax = NeutralCombatBridge::kMaxDead;
        std::vector<int> keys(kMax + 2);
        std::vector<ActorHandle> handles;
        for (std::size_t i = 0; i < keys.size(); ++i)
            handles.push_back(mActors.add(nullptr, &keys[i], static_cast<WorldId>(1000 + i), 1, "A", 0, 0, 0, 0));
        bool lastAccepted = true;
        frame([&](CombatRequestWriter& w) {
            for (std::size_t i = 0; i < kMax; ++i)
                EXPECT_TRUE(w.reportDeath(handles[i]));
            lastAccepted = w.reportDeath(handles[kMax]); // every one of the kMax dead is still here: no room
        });
        EXPECT_FALSE(lastAccepted); // before the fix: true, and the actor was not remembered
        EXPECT_FALSE(mBridge.isDead(handles[kMax]));
        EXPECT_EQ(mBridge.stats().mDeadTableFull, 1u);
        // The cell unloads: the dead go, a new corpse finds room.
        for (std::size_t i = 0; i < kMax; ++i)
            mActors.remove(nullptr, &keys[i]);
        bool accepted = false;
        frame([&](CombatRequestWriter& w) { accepted = w.reportDeath(handles[kMax + 1]); });
        EXPECT_TRUE(accepted);
        EXPECT_TRUE(mBridge.isDead(handles[kMax + 1]));
        EXPECT_LE(mBridge.deadCount(), 2u);
    }

    TEST_F(BridgeReview, theDeadListNeverAllocatesInAFrame)
    {
        std::vector<int> keys(64);
        std::vector<ActorHandle> handles;
        for (std::size_t i = 0; i < keys.size(); ++i)
            handles.push_back(mActors.add(nullptr, &keys[i], static_cast<WorldId>(500 + i), 1, "A", 0, 0, 0, 0));
        frame();
        const std::size_t reserved = mBridge.reservedBytes();
        frame([&](CombatRequestWriter& w) {
            for (const ActorHandle& h : handles)
                (void)w.reportDeath(h);
        });
        EXPECT_EQ(mBridge.reservedBytes(), reserved);
    }

    // ---- finding 9: a clip of an actor that is gone is not stopped with a stale key --------------------------------

    TEST_F(BridgeReview, withoutRulesOnlyTheClipsOfLiveActorsAreStopped)
    {
        int keyA = 0;
        int keyB = 0;
        const ActorHandle a = mActors.add(nullptr, &keyA, 11, 1, "A", 0, 0, 0, 0);
        const ActorHandle b = mActors.add(nullptr, &keyB, 12, 1, "B", 0, 0, 0, 0);
        frame([&](CombatRequestWriter& w) {
            ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = "1HM_AttackRight";
            request.token = 1;
            EXPECT_TRUE(w.playClip(CombatSubject::of(a), request));
            request.token = 2;
            EXPECT_TRUE(w.playClip(CombatSubject::of(b), request));
        });
        mActors.remove(nullptr, &keyA);
        mBridge.update(nullptr, 0.1f, mSink, mMeta, &mWorld);
        ASSERT_EQ(mSink.mStopped.size(), 1u); // before the fix: two, one for a key that is gone
        EXPECT_EQ(mSink.mStopped[0], &keyB);
        (void)b;
    }

    // ---- finding 6: nothing thrown by the engine's side reaches the pack
    // ---------------------------------------------

    TEST_F(BridgeReview, aSinkThatThrowsMakesTheRequestFailAndNothingEscapes)
    {
        int key = 0;
        const ActorHandle a = mActors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
        mSink.mThrow = true;
        bool played = true;
        bool faced = true;
        EXPECT_NO_THROW(frame([&](CombatRequestWriter& w) {
            ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = "1HM_AttackRight";
            request.token = 1;
            played = w.playClip(CombatSubject::of(a), request);
            faced = w.faceHeading(CombatSubject::of(a), 1.f);
        }));
        EXPECT_FALSE(played);
        EXPECT_FALSE(faced);
        EXPECT_GE(mBridge.stats().mWriterFailures, 2u);
        mSink.mThrow = false;
        EXPECT_NO_THROW(frame()); // the bridge is usable afterwards
    }

    TEST_F(BridgeReview, aWorldThatThrowsMakesEveryRequestFailAndNothingEscapes)
    {
        int key = 0;
        const ActorHandle a = mActors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
        mWorld.mThrow = true;
        std::vector<bool> answers;
        EXPECT_NO_THROW(frame([&](CombatRequestWriter& w) {
            SubjectState state;
            ClipTiming timing;
            DamageReport damage;
            damage.victimIsPlayer = true;
            damage.attacker = a;
            damage.amount = 1.f;
            damage.healthAfter = 50.f;
            damage.healthMax = 100.f;
            answers.push_back(w.subjectState(CombatSubject::ofPlayer(), state));
            answers.push_back(w.requestSweep(CombatSubject::of(a), swing()));
            answers.push_back(w.reportDamage(damage));
            answers.push_back(w.reportDeath(a));
            answers.push_back(w.clipTiming("1HM_AttackRight", false, timing));
        }));
        ASSERT_EQ(answers.size(), 5u);
        for (const bool answer : answers)
            EXPECT_FALSE(answer);
        EXPECT_GE(mBridge.stats().mWriterFailures, 5u);
    }

    TEST_F(BridgeReview, aMetaProviderThatThrowsLeavesTheClipWaitingAndNothingEscapes)
    {
        int key = 0;
        const ActorHandle a = mActors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
        frame([&](CombatRequestWriter& w) {
            ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = "1HM_AttackRight";
            request.token = 1;
            EXPECT_TRUE(w.playClip(CombatSubject::of(a), request));
        });
        mMeta = [](const void*) -> HavokPack::ClipProgress { throw std::runtime_error("renderer"); };
        EXPECT_NO_THROW(frame());
        EXPECT_GE(mBridge.stats().mWriterFailures, 1u);
        EXPECT_EQ(mBridge.playing(), 1u);
    }

    // ---- finding 10: the player's health stays in [0, max] ----------------------------------------------------------

    TEST_F(BridgeReview, thePlayersHealthIsClampedToItsRange)
    {
        int key = 0;
        const ActorHandle a = mActors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
        DamageReport damage;
        damage.victimIsPlayer = true;
        damage.attacker = a;
        damage.amount = 5.f;
        damage.healthMax = 100.f;
        damage.healthAfter = -20.f;
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDamage(damage)); });
        EXPECT_FLOAT_EQ(mWorld.mPlayerHealth, 0.f); // before the fix: -20
        damage.healthAfter = 250.f;
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDamage(damage)); });
        EXPECT_FLOAT_EQ(mWorld.mPlayerHealth, 100.f);
        damage.healthAfter = 40.f;
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDamage(damage)); });
        EXPECT_FLOAT_EQ(mWorld.mPlayerHealth, 40.f);
    }
}
