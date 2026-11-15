// Task 0171 (combat stage S5-3): the bridge's sweep, damage and death handling (apps/openmw/mwphysics/
// neutralcombatbridge.hpp), tested with a fake pack, a fake sink and a fake world, so no Jolt world and no Ptr is
// needed. Written from the header and the contract (combat_rules.hpp, 0.2).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;
    using MWPhysics::NeutralSweep::Candidate;

    // A world with fixed bodies. Records what the bridge tells it.
    class TestWorld final : public MWPhysics::NeutralCombatWorld
    {
    public:
        TestWorld() { mCandidates.reserve(64); }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out.x = 0;
                out.y = 80;
                out.heading = 3.f;
                out.health = 90.f;
                out.healthMax = 100.f;
                return true;
            }
            out.x = 1;
            out.y = 2;
            out.z = 3;
            out.heading = 0.5f;
            return true;
        }
        void collectCandidates(std::vector<Candidate>& out) override
        {
            for (const Candidate& c : mCandidates)
                out.push_back(c);
        }
        void applyPlayerDamage(const DamageReport& report) override
        {
            mPlayerHealth = report.healthAfter;
            ++mPlayerHits;
        }
        void actorDied(const void* key, ActorHandle actor) override { mDied.push_back({ key, actor }); }
        std::vector<Candidate> mCandidates;
        float mPlayerHealth = -1.f;
        int mPlayerHits = 0;
        std::vector<std::pair<const void*, ActorHandle>> mDied;
    };

    struct Delivered
    {
        CombatSubject mSubject;
        std::uint32_t mToken;
        std::vector<SweepHit> mHits;
        bool mInUpdate;
    };

    class TestCombat final : public CombatRules
    {
    public:
        TestCombat() { mDelivered.reserve(64); }
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject subject, std::uint32_t token, const SweepHit* hits, std::size_t count) override
        {
            Delivered d{ subject, token, {}, mInUpdate };
            for (std::size_t i = 0; i < count; ++i)
                d.mHits.push_back(hits[i]);
            mDelivered.push_back(std::move(d));
        }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            mInUpdate = true;
            if (mDo)
                mDo(writer);
            mDo = nullptr;
            mKept = &writer;
            mInUpdate = false;
        }
        std::function<void(CombatRequestWriter&)> mDo;
        std::vector<Delivered> mDelivered;
        CombatRequestWriter* mKept = nullptr;
        bool mInUpdate = false;
    };

    class TestSink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override { ++mPlays; }
        void stopClip(const void*) override { ++mStops; }
        int mPlays = 0;
        int mStops = 0;
        void setHeading(const void* key, float heading) override { mHeadings.push_back({ key, heading }); }
        std::vector<std::pair<const void*, float>> mHeadings;
    };

    Candidate actorBody(ActorHandle handle, double x, double y)
    {
        Candidate c;
        c.mTarget = SweepTarget::Actor;
        c.mActor = handle;
        c.mX = x;
        c.mY = y;
        c.mRadius = 22.f;
        c.mHeight = 128.f;
        return c;
    }

    SweepRequest swing(std::uint32_t token = 1)
    {
        SweepRequest r;
        r.token = token;
        r.reach = 100.f;
        r.halfAngle = 0.61f;
        r.heightBelow = 16.f;
        r.heightAbove = 130.f;
        return r;
    }

    struct CombatHit : ::testing::Test
    {
        int mKeyA = 0;
        int mKeyB = 0;
        int mKeyC = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        TestCombat mRules;
        TestSink mSink;
        TestWorld mWorld;
        MWPhysics::NeutralClipMetaFn mMetaFn = [](const void*) { return HavokPack::ClipProgress(); };
        ActorHandle mA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);
        ActorHandle mC = mActors.add(nullptr, &mKeyC, 13, 1, "C", 0, 0, 0, 0);

        void frame(std::function<void(CombatRequestWriter&)> doIt = nullptr)
        {
            mRules.mDo = std::move(doIt);
            mCombat.update(&mRules, 0.1f, mSink, mMetaFn, &mWorld);
        }
    };

    TEST_F(CombatHit, aSweepIsCastWhenAskedAndTheResultIsToldInTheNextFrameBeforeCombatUpdate)
    {
        mWorld.mCandidates = { actorBody(mA, 0, 0), actorBody(mB, 0, 60), actorBody(mC, 0, 40) };
        bool accepted = false;
        frame([&](CombatRequestWriter& w) { accepted = w.requestSweep(CombatSubject::of(mA), swing(7)); });
        EXPECT_TRUE(accepted);
        EXPECT_TRUE(mRules.mDelivered.empty()); // never from inside the request, nor in the same frame
        EXPECT_EQ(mCombat.stats().mSweeps, 1u);
        frame();
        ASSERT_EQ(mRules.mDelivered.size(), 1u);
        const Delivered& d = mRules.mDelivered[0];
        EXPECT_TRUE(d.mSubject == CombatSubject::of(mA));
        EXPECT_EQ(d.mToken, 7u);
        EXPECT_FALSE(d.mInUpdate); // told before combatUpdate runs
        // The attacker's own body is left out; C (centre 40 ahead) comes before B (60).
        ASSERT_EQ(d.mHits.size(), 2u);
        EXPECT_TRUE(d.mHits[0].actor == mC);
        EXPECT_TRUE(d.mHits[1].actor == mB);
        EXPECT_NEAR(d.mHits[0].distance, 18.f, 1e-3f);
        frame();
        EXPECT_EQ(mRules.mDelivered.size(), 1u); // once only
        EXPECT_EQ(mCombat.stats().mSweepHits, 2u);
    }

    TEST_F(CombatHit, thePlayerCanSweepAndBeSweptButNeverHitsItself)
    {
        Candidate player;
        player.mTarget = SweepTarget::Player;
        player.mY = 80;
        player.mRadius = 22.f;
        player.mHeight = 128.f;
        mWorld.mCandidates = { player, actorBody(mB, 0, 50) };
        frame([&](CombatRequestWriter& w) {
            EXPECT_TRUE(w.requestSweep(CombatSubject::of(mA), swing(1)));
            EXPECT_TRUE(w.requestSweep(CombatSubject::ofPlayer(), swing(2)));
        });
        frame();
        ASSERT_EQ(mRules.mDelivered.size(), 2u);
        // The actor's swing sees B (centre 50) then the player (80); the player's swing sees only B.
        EXPECT_EQ(mRules.mDelivered[0].mHits.size(), 2u);
        EXPECT_EQ(mRules.mDelivered[0].mHits[1].target, SweepTarget::Player);
        ASSERT_EQ(mRules.mDelivered[1].mHits.size(), 1u);
        EXPECT_EQ(mRules.mDelivered[1].mHits[0].target, SweepTarget::Actor);
    }

    TEST_F(CombatHit, badSweepRequestsAreRefusedAndCounted)
    {
        std::vector<bool> results;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        frame([&](CombatRequestWriter& w) {
            SweepRequest r = swing();
            results.push_back(w.requestSweep(CombatSubject{}, r)); // no subject
            results.push_back(w.requestSweep(CombatSubject::of(ActorHandle{ 99, 1 }), r)); // stale
            r.reach = 0.f;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r = swing();
            r.halfAngle = 0.f;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r.halfAngle = 3.2f;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r = swing();
            r.heightAbove = 0.f;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r = swing();
            r.heightBelow = -1.f;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r = swing();
            r.heading = nan;
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
            r = swing();
            r.x = std::numeric_limits<double>::infinity();
            results.push_back(w.requestSweep(CombatSubject::of(mA), r));
        });
        ASSERT_EQ(results.size(), 9u);
        for (const bool accepted : results)
            EXPECT_FALSE(accepted);
        EXPECT_EQ(mCombat.stats().mSweepsRefused, 9u);
        EXPECT_EQ(mCombat.stats().mSweeps, 0u);
        // Outside combatUpdate the writer refuses everything, and without a world there is nothing to sweep.
        ASSERT_NE(mRules.mKept, nullptr);
        EXPECT_FALSE(mRules.mKept->requestSweep(CombatSubject::of(mA), swing()));
        EXPECT_FALSE(mRules.mKept->reportDamage(DamageReport{}));
        mRules.mDo = [&](CombatRequestWriter& w) { EXPECT_FALSE(w.requestSweep(CombatSubject::of(mA), swing())); };
        mCombat.update(&mRules, 0.1f, mSink, mMetaFn, nullptr);
    }

    TEST_F(CombatHit, aResultForAnActorThatWentAwayIsDroppedNotTold)
    {
        mWorld.mCandidates = { actorBody(mB, 0, 50) };
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.requestSweep(CombatSubject::of(mA), swing(3))); });
        mActors.remove(nullptr, &mKeyA);
        frame();
        EXPECT_TRUE(mRules.mDelivered.empty());
        EXPECT_EQ(mCombat.stats().mSweepResultsDropped, 1u);
    }

    TEST_F(CombatHit, damageToThePlayerWritesTheHealthBackAndAnActorVictimIsOnlyCounted)
    {
        frame([&](CombatRequestWriter& w) {
            DamageReport onPlayer;
            onPlayer.attacker = mA;
            onPlayer.victimIsPlayer = true;
            onPlayer.amount = 7.f;
            onPlayer.healthAfter = 83.f;
            onPlayer.healthMax = 100.f;
            EXPECT_TRUE(w.reportDamage(onPlayer));
            DamageReport onActor;
            onActor.attackerIsPlayer = true;
            onActor.victim = mB;
            onActor.amount = 5.f;
            onActor.healthAfter = 45.f;
            onActor.healthMax = 50.f;
            EXPECT_TRUE(w.reportDamage(onActor));
        });
        EXPECT_EQ(mWorld.mPlayerHits, 1);
        EXPECT_FLOAT_EQ(mWorld.mPlayerHealth, 83.f); // only the report on the player reached the engine's health
        EXPECT_EQ(mCombat.stats().mDamageReports, 2u);
    }

    TEST_F(CombatHit, damageThatIsNotFiniteNegativeOrForAStaleActorIsRefused)
    {
        frame([&](CombatRequestWriter& w) {
            DamageReport r;
            r.attacker = mA;
            r.victim = mB;
            r.amount = 1.f;
            r.healthAfter = 1.f;
            r.healthMax = 2.f;
            DamageReport bad = r;
            bad.amount = -1.f;
            EXPECT_FALSE(w.reportDamage(bad));
            bad = r;
            bad.healthAfter = std::numeric_limits<float>::quiet_NaN();
            EXPECT_FALSE(w.reportDamage(bad));
            bad = r;
            bad.victim = ActorHandle{ 77, 1 };
            EXPECT_FALSE(w.reportDamage(bad));
            bad = r;
            bad.attacker = ActorHandle{};
            EXPECT_FALSE(w.reportDamage(bad));
            EXPECT_TRUE(w.reportDamage(r));
        });
        EXPECT_EQ(mCombat.stats().mDamageRefused, 4u);
        EXPECT_EQ(mCombat.stats().mDamageReports, 1u);
        EXPECT_EQ(mWorld.mPlayerHits, 0);
    }

    TEST_F(CombatHit, aDeathIsToldToTheWorldOnceAndTheDeadAreNotSweptAgain)
    {
        mWorld.mCandidates = { actorBody(mB, 0, 50) };
        bool first = false;
        bool second = false;
        bool stale = true;
        frame([&](CombatRequestWriter& w) {
            first = w.reportDeath(mB);
            second = w.reportDeath(mB);
            stale = w.reportDeath(ActorHandle{ 55, 1 });
        });
        EXPECT_TRUE(first);
        EXPECT_TRUE(second); // a second report is ignored and still succeeds
        EXPECT_FALSE(stale);
        ASSERT_EQ(mWorld.mDied.size(), 1u);
        EXPECT_EQ(mWorld.mDied[0].first, &mKeyB);
        EXPECT_TRUE(mWorld.mDied[0].second == mB);
        EXPECT_TRUE(mCombat.isDead(mB));
        EXPECT_FALSE(mCombat.isDead(mA));
        EXPECT_EQ(mCombat.stats().mDeaths, 1u);
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.requestSweep(CombatSubject::of(mA), swing(4))); });
        frame();
        ASSERT_EQ(mRules.mDelivered.size(), 1u);
        EXPECT_TRUE(mRules.mDelivered[0].mHits.empty()); // the corpse is not a target
    }

    TEST_F(CombatHit, aDeathClipHoldsItsLastPoseWhereAnyOtherClipIsStoppedWhenItEnds)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 0.3f;
        meta->sortEvents();
        MWPhysics::NeutralClipMetaFn metaFn = [&](const void*) { return HavokPack::ClipProgress{ meta, true, 0.f }; };
        const auto play = [&](ClipSlot slot, std::uint32_t token) {
            mRules.mDo = [=, this](CombatRequestWriter& w) {
                ClipRequest request;
                request.slot = slot;
                request.clip = "1HM_StaggerBackMedium";
                request.token = token;
                EXPECT_TRUE(w.playClip(CombatSubject::of(mA), request));
            };
            mCombat.update(&mRules, 0.1f, mSink, metaFn, &mWorld);
            for (int i = 0; i < 6; ++i)
            {
                mRules.mDo = nullptr;
                mCombat.update(&mRules, 0.1f, mSink, metaFn, &mWorld);
            }
        };
        play(ClipSlot::Recoil, 1);
        EXPECT_EQ(mCombat.stats().mClipsCompleted, 1u);
        EXPECT_EQ(mSink.mStops, 1); // an ordinary clip is stopped at its end
        play(ClipSlot::Death, 2);
        EXPECT_EQ(mCombat.stats().mClipsCompleted, 2u);
        EXPECT_EQ(mSink.mStops, 1); // the death clip is not: the body stays down
        EXPECT_EQ(mSink.mPlays, 2);
    }

    TEST_F(CombatHit, subjectStateAndFacingGoThroughTheWorldAndTheSink)
    {
        frame([&](CombatRequestWriter& w) {
            SubjectState state;
            EXPECT_TRUE(w.subjectState(CombatSubject::ofPlayer(), state));
            EXPECT_FLOAT_EQ(state.healthMax, 100.f);
            EXPECT_FLOAT_EQ(state.health, 90.f);
            EXPECT_TRUE(w.subjectState(CombatSubject::of(mA), state));
            EXPECT_DOUBLE_EQ(state.z, 3.0);
            EXPECT_FALSE(w.subjectState(CombatSubject::of(ActorHandle{ 70, 1 }), state)); // stale
            EXPECT_TRUE(w.faceHeading(CombatSubject::of(mA), 1.25f));
            EXPECT_FALSE(w.faceHeading(CombatSubject::ofPlayer(), 1.25f));
            EXPECT_FALSE(w.faceHeading(CombatSubject::of(mA), std::numeric_limits<float>::infinity()));
        });
        ASSERT_EQ(mSink.mHeadings.size(), 1u);
        EXPECT_EQ(mSink.mHeadings[0].first, &mKeyA);
        EXPECT_FLOAT_EQ(mSink.mHeadings[0].second, 1.25f);
    }

    TEST_F(CombatHit, aFrameOfSweepsAndDeliveryAllocatesNothing)
    {
        mWorld.mCandidates = { actorBody(mA, 0, 0), actorBody(mB, 0, 60), actorBody(mC, 0, 40) };
        mRules.mDelivered.reserve(300);
        // Warm up once so that every table is sized.
        frame([&](CombatRequestWriter& w) { (void)w.requestSweep(CombatSubject::of(mA), swing(1)); });
        frame();
        mRules.mDelivered.clear();
        std::size_t allocations = 0;
        {
            // The pack side is a fake that stores copies (allocating); count only the bridge: no delivery vector here.
            class Quiet final : public CombatRules
            {
            public:
                void onClipEvent(const ClipEvent&) override {}
                void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t count) override
                {
                    mHits += count;
                }
                void combatUpdate(float, CombatRequestWriter& writer) override
                {
                    SweepRequest r;
                    r.token = ++mToken;
                    r.reach = 100.f;
                    r.halfAngle = 0.61f;
                    r.heightBelow = 16.f;
                    r.heightAbove = 130.f;
                    (void)writer.requestSweep(CombatSubject::of(mActor), r);
                }
                ActorHandle mActor;
                std::uint32_t mToken = 0;
                std::size_t mHits = 0;
            } quiet;
            quiet.mActor = mA;
            TestSink sink;
            TestWorld world;
            world.mCandidates = mWorld.mCandidates;
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 200; ++i)
                mCombat.update(&quiet, 0.016f, sink, mMetaFn, &world);
            allocations = counter.count();
            EXPECT_EQ(quiet.mHits, 199u * 2u); // two hits a frame, the first frame's result arrives in the second
        }
        EXPECT_EQ(allocations, 0u);
    }
}
