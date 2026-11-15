// Task 0171 (combat stage S5-3b): a blow needs a clear line (walls, doors), and the player's swing timing comes from
// the sidecar. The bridge half is tested with a fake pack, sink and world; the geometry half (a wall, a door, an actor
// in the way) is tested against a real Jolt system in testneutralsight.cpp. Written from the headers before the code
// (the first run was against stubs, see the task's stage log).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;
    using MWPhysics::NeutralSweep::Candidate;

    struct Ray
    {
        double x0, y0, z0, x1, y1, z1;
    };

    // A world whose walls are planes y = const (blocking every segment that crosses them) and which logs every ray.
    class SightWorld final : public MWPhysics::NeutralCombatWorld
    {
    public:
        SightWorld()
        {
            mCandidates.reserve(64);
            mRays.reserve(4096);
        }
        bool subjectState(CombatSubject, SubjectState& out) override
        {
            out = {};
            return true;
        }
        void collectCandidates(std::vector<Candidate>& out) override
        {
            for (const Candidate& c : mCandidates)
                out.push_back(c);
        }
        bool lineBlocked(double x0, double y0, double z0, double x1, double y1, double z1) override
        {
            if (mRays.size() < mRays.capacity())
                mRays.push_back({ x0, y0, z0, x1, y1, z1 });
            for (const double wall : mWallsY)
                if ((y0 < wall) != (y1 < wall))
                    return true;
            return false;
        }
        std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view clip, bool) override
        {
            mAsked = std::string(clip);
            return mMeta;
        }
        std::vector<Candidate> mCandidates;
        std::vector<double> mWallsY;
        std::vector<Ray> mRays;
        std::shared_ptr<const HavokPack::ClipMeta> mMeta;
        std::string mAsked;
    };

    class Pack final : public CombatRules
    {
    public:
        Pack() { mHits.reserve(64); }
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit* hits, std::size_t count) override
        {
            mHits.assign(hits, hits + count);
            ++mResults;
        }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mDo)
                mDo(writer);
            mDo = nullptr;
            mKept = &writer;
        }
        std::function<void(CombatRequestWriter&)> mDo;
        std::vector<SweepHit> mHits;
        int mResults = 0;
        CombatRequestWriter* mKept = nullptr;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
    };

    Candidate body(ActorHandle handle, double x, double y, double z = 0.0)
    {
        Candidate c;
        c.mTarget = SweepTarget::Actor;
        c.mActor = handle;
        c.mX = x;
        c.mY = y;
        c.mZ = z;
        c.mRadius = 22.f;
        c.mHeight = 128.f;
        return c;
    }

    SweepRequest swing(double z = 0.0)
    {
        SweepRequest r;
        r.token = 1;
        r.z = z;
        r.reach = 140.f;
        r.halfAngle = 0.61f;
        r.heightBelow = 16.f;
        r.heightAbove = 130.f;
        return r;
    }

    struct CombatSight : ::testing::Test
    {
        int mKeyA = 0;
        int mKeyB = 0;
        int mKeyC = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        Pack mPack;
        Sink mSink;
        SightWorld mWorld;
        MWPhysics::NeutralClipMetaFn mMetaFn = [](const void*) { return HavokPack::ClipProgress(); };
        ActorHandle mA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);
        ActorHandle mC = mActors.add(nullptr, &mKeyC, 13, 1, "C", 0, 0, 0, 0);

        void frame(std::function<void(CombatRequestWriter&)> doIt = nullptr)
        {
            mPack.mDo = std::move(doIt);
            mCombat.update(&mPack, 0.1f, mSink, mMetaFn, &mWorld);
        }
        void sweepFromA(const SweepRequest& r = swing())
        {
            frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.requestSweep(CombatSubject::of(mA), r)); });
            frame(); // the result is told in the next frame
        }
    };

    TEST_F(CombatSight, withNoWallTheTargetIsHit)
    {
        mWorld.mCandidates = { body(mA, 0, 0), body(mB, 0, 60) };
        sweepFromA();
        ASSERT_EQ(mPack.mHits.size(), 1u);
        EXPECT_TRUE(mPack.mHits[0].actor == mB);
        EXPECT_EQ(mCombat.stats().mSweepHitsBlocked, 0u);
    }

    TEST_F(CombatSight, aWallBetweenTheTwoCapsulesBlocksTheHit)
    {
        mWorld.mCandidates = { body(mA, 0, 0), body(mB, 0, 60) };
        mWorld.mWallsY = { 30.0 };
        sweepFromA();
        EXPECT_EQ(mPack.mResults, 1); // the result is still told, with no hit
        EXPECT_TRUE(mPack.mHits.empty());
        EXPECT_EQ(mCombat.stats().mSweepHitsBlocked, 1u);
        EXPECT_EQ(mCombat.stats().mSweepHits, 0u);
    }

    TEST_F(CombatSight, onlyTheHitBehindTheWallIsDroppedTheNearOneStays)
    {
        // B at 40 (touched at y 18) is in front of the wall (y = 55); C at 90 (touched at y 68) is behind it. The arc
        // catches both.
        mWorld.mCandidates = { body(mA, 0, 0), body(mB, 0, 40), body(mC, 0, 90) };
        mWorld.mWallsY = { 55.0 };
        sweepFromA();
        ASSERT_EQ(mPack.mHits.size(), 1u);
        EXPECT_TRUE(mPack.mHits[0].actor == mB);
        EXPECT_EQ(mCombat.stats().mSweepHitsBlocked, 1u);
    }

    TEST_F(CombatSight, anActorBetweenTheTwoDoesNotBlockTheOneBehindIt)
    {
        // The world only answers for geometry: actors are never walls, so both stand in the arc as before.
        mWorld.mCandidates = { body(mA, 0, 0), body(mB, 0, 40), body(mC, 0, 90) };
        sweepFromA();
        ASSERT_EQ(mPack.mHits.size(), 2u);
        EXPECT_TRUE(mPack.mHits[0].actor == mB);
        EXPECT_TRUE(mPack.mHits[1].actor == mC);
        EXPECT_EQ(mCombat.stats().mSweepHitsBlocked, 0u);
    }

    TEST_F(CombatSight, theRayRunsFromTheWeaponHeightToTheTargetsCapsule)
    {
        mWorld.mCandidates = { body(mA, 0, 0, 10), body(mB, 0, 60, 10) };
        sweepFromA(swing(10.0));
        ASSERT_EQ(mWorld.mRays.size(), 1u);
        const Ray& ray = mWorld.mRays[0];
        EXPECT_DOUBLE_EQ(ray.x0, 0.0);
        EXPECT_DOUBLE_EQ(ray.y0, 0.0);
        EXPECT_NEAR(ray.z0, 10.0 + NeutralCombatBridge::kWeaponHeight, 1e-9);
        // The far end is on the target's capsule, at the weapon's height (inside the capsule's height).
        EXPECT_NEAR(ray.y1, 60.0 - 22.0, 1e-6);
        EXPECT_NEAR(ray.z1, 10.0 + NeutralCombatBridge::kWeaponHeight, 1e-9);
    }

    TEST_F(CombatSight, theAimStaysInsideTheCapsuleWhenTheTargetStandsHigher)
    {
        // The target's feet are 100 above the attacker's: the weapon height (80) is below its feet.
        mWorld.mCandidates = { body(mA, 0, 0, 0), body(mB, 0, 60, 100) };
        sweepFromA(swing(0.0));
        ASSERT_EQ(mWorld.mRays.size(), 1u);
        EXPECT_NEAR(mWorld.mRays[0].z1, 100.0 + NeutralCombatBridge::kAimMargin, 1e-6);
    }

    TEST_F(CombatSight, thePlayerAsATargetNeedsALineToo)
    {
        Candidate player;
        player.mTarget = SweepTarget::Player;
        player.mY = 60;
        player.mRadius = 22.f;
        player.mHeight = 128.f;
        mWorld.mCandidates = { body(mB, 0, 0), player };
        mWorld.mWallsY = { 30.0 };
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.requestSweep(CombatSubject::of(mB), swing())); });
        frame();
        EXPECT_TRUE(mPack.mHits.empty());
        EXPECT_EQ(mCombat.stats().mSweepHitsBlocked, 1u);
    }

    TEST_F(CombatSight, aFrameOfBlockedAndClearSweepsAllocatesNothing)
    {
        mWorld.mCandidates = { body(mA, 0, 0), body(mB, 0, 40), body(mC, 0, 90) };
        mWorld.mWallsY = { 55.0 };
        frame([&](CombatRequestWriter& w) { (void)w.requestSweep(CombatSubject::of(mA), swing()); });
        frame();
        std::size_t allocations = 0;
        {
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
                    r.reach = 140.f;
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
            Sink sink;
            SightWorld world;
            world.mCandidates = mWorld.mCandidates;
            world.mWallsY = mWorld.mWallsY;
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 200; ++i)
                mCombat.update(&quiet, 0.016f, sink, mMetaFn, &world);
            allocations = counter.count();
            EXPECT_EQ(quiet.mHits, 199u); // one clear hit a frame; the one behind the wall is dropped
        }
        EXPECT_EQ(allocations, 0u);
    }

    // ---- the player's swing timing --------------------------------------------------------------------------------

    std::shared_ptr<const HavokPack::ClipMeta> metaOf(
        float duration, float speed, std::vector<std::pair<std::string, float>> events)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = duration;
        meta->mSpeed = speed;
        for (auto& e : events)
            meta->mEvents.push_back({ e.first, e.second });
        meta->sortEvents();
        return meta;
    }

    TEST(CombatClipTiming, theHitFrameAndTheLengthComeFromTheMeta)
    {
        const auto meta
            = metaOf(1.287f, 1.f, { { "preHitFrame", 0.2f }, { "HitFrame", 0.367f }, { "attackStop", 1.2f } });
        ClipTiming timing;
        ASSERT_TRUE(MWPhysics::clipTimingOf(meta.get(), timing));
        EXPECT_TRUE(timing.hasHit);
        EXPECT_FLOAT_EQ(timing.hitTime, 0.367f);
        EXPECT_FLOAT_EQ(timing.length, 1.287f);
    }

    TEST(CombatClipTiming, theLengthIsPlaybackSecondsWhenTheGeneratorHasASpeed)
    {
        const auto meta = metaOf(2.f, 2.f, { { "hitframe", 0.3f } });
        ClipTiming timing;
        ASSERT_TRUE(MWPhysics::clipTimingOf(meta.get(), timing));
        EXPECT_FLOAT_EQ(timing.length, 1.f);
        EXPECT_FLOAT_EQ(timing.hitTime, 0.3f); // stored in playback seconds already
    }

    TEST(CombatClipTiming, aClipWithNoHitFrameHasTheLengthOnly)
    {
        const auto meta = metaOf(1.5f, 1.f, { { "attackStop", 1.4f } });
        ClipTiming timing;
        ASSERT_TRUE(MWPhysics::clipTimingOf(meta.get(), timing));
        EXPECT_FALSE(timing.hasHit);
        EXPECT_FLOAT_EQ(timing.length, 1.5f);
    }

    TEST(CombatClipTiming, noMetaOrANoLengthMetaIsRefused)
    {
        ClipTiming timing;
        timing.length = 9.f;
        EXPECT_FALSE(MWPhysics::clipTimingOf(nullptr, timing));
        EXPECT_FLOAT_EQ(timing.length, 0.f);
        const auto empty = metaOf(0.f, 1.f, { { "HitFrame", 0.1f } });
        EXPECT_FALSE(MWPhysics::clipTimingOf(empty.get(), timing));
    }

    TEST_F(CombatSight, theWriterAnswersTheTimingFromTheWorldsMetaOnlyWhenItIsReady)
    {
        frame([&](CombatRequestWriter& w) {
            ClipTiming timing;
            EXPECT_FALSE(w.clipTiming("1HM_AttackRight", false, timing)); // not read yet: no answer
        });
        EXPECT_EQ(mWorld.mAsked, "1HM_AttackRight"); // but the read was asked for
        mWorld.mMeta = metaOf(1.287f, 1.f, { { "HitFrame", 0.367f } });
        frame([&](CombatRequestWriter& w) {
            ClipTiming timing;
            ASSERT_TRUE(w.clipTiming("1HM_AttackRight", false, timing));
            EXPECT_FLOAT_EQ(timing.hitTime, 0.367f);
            EXPECT_FLOAT_EQ(timing.length, 1.287f);
        });
    }

    TEST_F(CombatSight, theTimingIsRefusedOutsideAnUpdateAndForABadName)
    {
        mWorld.mMeta = metaOf(1.287f, 1.f, { { "HitFrame", 0.367f } });
        frame();
        ASSERT_NE(mPack.mKept, nullptr);
        ClipTiming timing;
        EXPECT_FALSE(mPack.mKept->clipTiming("1HM_AttackRight", false, timing)); // the writer is stale
        frame([&](CombatRequestWriter& w) {
            EXPECT_FALSE(w.clipTiming("", false, timing));
            EXPECT_FALSE(w.clipTiming("a/b", false, timing));
        });
    }
}
