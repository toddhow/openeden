// Task 0178 (combat stage S5-6): the bridge's side of projectiles, the clip speed, the clip event time and the player's
// attack edges, tested with a fake pack, a fake sink and a fake world (no Jolt, no Ptr). Written from the contract
// (combat_rules.hpp 0.3) and the bridge header.
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
    using MWPhysics::NeutralProjectilePose;
    using MWPhysics::NeutralProjectile::Vec;

    constexpr float kPi = 3.14159265358979f;

    // Records what it is told; each combatUpdate runs mOnUpdate once (the test's own requests).
    class Rules final : public CombatRules
    {
    public:
        Rules()
        {
            mResults.reserve(64);
            mEvents.reserve(64);
            mEdges.reserve(8);
        }
        void onClipEvent(const ClipEvent& event) override { mEvents.push_back({ event.kind, event.clipTime }); }
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void onProjectileResult(const ProjectileResult& result) override
        {
            mResultsInUpdate.push_back(mInUpdate);
            mResults.push_back(result);
        }
        void onPlayerAttackEdge(bool pressed) override { mEdges.push_back(pressed); }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            mInUpdate = true;
            if (mOnUpdate)
                mOnUpdate(writer);
            mOnUpdate = nullptr;
            mKeptWriter = &writer;
            mInUpdate = false;
        }

        std::function<void(CombatRequestWriter&)> mOnUpdate;
        std::vector<ProjectileResult> mResults;
        std::vector<bool> mResultsInUpdate;
        std::vector<std::pair<ClipEventKind, float>> mEvents;
        std::vector<bool> mEdges;
        CombatRequestWriter* mKeptWriter = nullptr;
        bool mInUpdate = false;
    };

    struct ProjectileOp
    {
        enum Kind
        {
            Spawn,
            Move,
            Remove,
        } mKind;
        std::uint32_t mId;
        std::string mModel;
        NeutralProjectilePose mPose;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        Sink()
        {
            mProjectileOps.reserve(4096);
            mSpeeds.reserve(8);
        }
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void setClipSpeed(const void* key, float multiplier) override { mSpeeds.push_back({ key, multiplier }); }
        void spawnProjectile(std::uint32_t id, std::string_view model, const NeutralProjectilePose& pose) override
        {
            mProjectileOps.push_back({ ProjectileOp::Spawn, id, std::string(model), pose });
        }
        void moveProjectile(std::uint32_t id, const NeutralProjectilePose& pose) override
        {
            mProjectileOps.push_back({ ProjectileOp::Move, id, {}, pose });
        }
        void removeProjectile(std::uint32_t id) override
        {
            mProjectileOps.push_back({ ProjectileOp::Remove, id, {}, {} });
        }
        std::size_t count(ProjectileOp::Kind kind) const
        {
            std::size_t n = 0;
            for (const ProjectileOp& op : mProjectileOps)
                n += op.mKind == kind ? 1 : 0;
            return n;
        }
        std::vector<ProjectileOp> mProjectileOps;
        std::vector<std::pair<const void*, float>> mSpeeds;
    };

    class World final : public MWPhysics::NeutralCombatWorld
    {
    public:
        World() { mCandidates.reserve(16); }
        bool subjectState(CombatSubject, SubjectState&) override { return false; }
        void collectCandidates(std::vector<MWPhysics::NeutralSweep::Candidate>& out) override
        {
            for (const auto& c : mCandidates)
                out.push_back(c);
        }
        bool rayWorld(const Vec& p0, const Vec& p1, double& fraction) override
        {
            if (mWallX > 0.0 && p0.x < mWallX && p1.x >= mWallX)
            {
                fraction = (mWallX - p0.x) / (p1.x - p0.x);
                return true;
            }
            return false;
        }
        std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view, bool) override { return mMeta; }
        std::vector<MWPhysics::NeutralSweep::Candidate> mCandidates;
        double mWallX = 0.0;
        std::shared_ptr<const HavokPack::ClipMeta> mMeta;
    };

    struct ProjectileFixture : ::testing::Test
    {
        int mKeyA = 0;
        int mKeyB = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        Rules mRules;
        Sink mSink;
        World mWorld;
        std::shared_ptr<const HavokPack::ClipMeta> mMeta;
        MWPhysics::NeutralClipMetaFn mMetaFn = [this](const void*) -> HavokPack::ClipProgress {
            return { mMeta, true, 0.f };
        };
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mHandleB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);

        void frame(float seconds = 0.1f) { mCombat.update(&mRules, seconds, mSink, mMetaFn, &mWorld); }

        // The shooter A at the origin (its capsule is a candidate like any other: the owner must be skipped).
        ProjectileRequest request(float heading = kPi / 2.f)
        {
            ProjectileRequest r;
            r.token = 33;
            r.x = 0.0;
            r.y = 0.0;
            r.z = 64.0;
            r.heading = heading; // +X
            r.pitch = 0.f;
            r.speed = 1000.f;
            r.gravity = 0.f;
            r.range = 5000.f;
            r.stickDepth = 20.f;
            r.stickSeconds = 10.f;
            r.model = "meshes/weapons/iron/ironarrowprojectile.nif";
            return r;
        }

        bool launch(const ProjectileRequest& r, CombatSubject owner)
        {
            bool accepted = false;
            mRules.mOnUpdate = [&](CombatRequestWriter& w) { accepted = w.launchProjectile(owner, r); };
            frame();
            return accepted;
        }
        bool launch(const ProjectileRequest& r) { return launch(r, CombatSubject::of(mHandleA)); }

        static MWPhysics::NeutralSweep::Candidate capsule(ActorHandle handle, double x, double y)
        {
            MWPhysics::NeutralSweep::Candidate c;
            c.mTarget = SweepTarget::Actor;
            c.mActor = handle;
            c.mX = x;
            c.mY = y;
            c.mZ = 0.0;
            c.mRadius = 22.f;
            c.mHeight = 128.f;
            return c;
        }
    };

    TEST_F(ProjectileFixture, aLaunchOutsideTheUpdateIsRefused)
    {
        frame(); // a first update keeps the writer
        mRules.mOnUpdate = [](CombatRequestWriter&) {};
        frame();
        ASSERT_NE(mRules.mKeptWriter, nullptr);
        EXPECT_FALSE(mRules.mKeptWriter->launchProjectile(CombatSubject::of(mHandleA), request()));
        EXPECT_GE(mCombat.stats().mProjectilesRefused, 1u);
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
    }

    TEST_F(ProjectileFixture, badRequestsAreRefusedAndCounted)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<ProjectileRequest> bad;
        auto add = [&](auto&& change) {
            ProjectileRequest r = request();
            change(r);
            bad.push_back(r);
        };
        add([&](ProjectileRequest& r) { r.speed = 0.f; });
        add([&](ProjectileRequest& r) { r.speed = -5.f; });
        add([&](ProjectileRequest& r) { r.speed = nan; });
        add([&](ProjectileRequest& r) { r.speed = 2.0e6f; });
        add([&](ProjectileRequest& r) { r.gravity = -1.f; });
        add([&](ProjectileRequest& r) { r.gravity = nan; });
        add([&](ProjectileRequest& r) { r.range = 0.f; });
        add([&](ProjectileRequest& r) { r.range = nan; });
        add([&](ProjectileRequest& r) { r.pitch = kPi / 2.f; });
        add([&](ProjectileRequest& r) { r.pitch = -kPi; });
        add([&](ProjectileRequest& r) { r.heading = nan; });
        add([&](ProjectileRequest& r) { r.x = std::numeric_limits<double>::infinity(); });
        add([&](ProjectileRequest& r) { r.stickDepth = -1.f; });
        add([&](ProjectileRequest& r) { r.stickSeconds = nan; });
        add([&](ProjectileRequest& r) { r.model = "meshes/../secret.nif"; });
        add([&](ProjectileRequest& r) { r.model = "c:/windows/x.nif"; });
        const std::string tooLong(NeutralCombatBridge::kMaxProjectileModelPath + 1, 'a');
        add([&](ProjectileRequest& r) { r.model = tooLong; });
        for (const ProjectileRequest& r : bad)
            EXPECT_FALSE(launch(r)) << "speed " << r.speed << " gravity " << r.gravity << " range " << r.range;
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        EXPECT_EQ(mCombat.stats().mProjectilesRefused, bad.size());
        EXPECT_TRUE(mSink.mProjectileOps.empty());
    }

    TEST_F(ProjectileFixture, aStaleOwnerAndThePlayerWithNoShooterAreHandledByTheOwnersKind)
    {
        EXPECT_FALSE(launch(request(), CombatSubject::of({ 999, 1 })));
        EXPECT_FALSE(launch(request(), CombatSubject{}));
        EXPECT_TRUE(launch(request(), CombatSubject::ofPlayer())); // the player owns shots too
    }

    TEST_F(ProjectileFixture, aLaunchSpawnsTheModelAtTheOriginFacingTheHeadingAndPitch)
    {
        ProjectileRequest r = request(0.f); // +Y
        r.pitch = kPi / 6.f; // 30 degrees up
        r.z = 70.0;
        ASSERT_TRUE(launch(r));
        ASSERT_EQ(mSink.mProjectileOps.size(), 1u);
        const ProjectileOp& op = mSink.mProjectileOps[0];
        EXPECT_EQ(op.mKind, ProjectileOp::Spawn);
        EXPECT_NE(op.mId, 0u);
        EXPECT_EQ(op.mModel, "meshes/weapons/iron/ironarrowprojectile.nif");
        EXPECT_NEAR(op.mPose.mZ, 70.0, 1e-9);
        // direction = (sin h cos p, cos h cos p, sin p)
        EXPECT_NEAR(op.mPose.mDirX, 0.f, 1e-6f);
        EXPECT_NEAR(op.mPose.mDirY, std::cos(kPi / 6.f), 1e-6f);
        EXPECT_NEAR(op.mPose.mDirZ, 0.5f, 1e-6f);
        EXPECT_EQ(mCombat.projectilesInFlight(), 1u);
        EXPECT_EQ(mCombat.stats().mProjectilesLaunched, 1u);
    }

    TEST_F(ProjectileFixture, aShotMovesEveryFrameAndExpiresAtItsRangeWithTheResultToldInTheNextUpdate)
    {
        ProjectileRequest r = request();
        r.range = 250.f; // 1000 u/s at 0.1 s a frame: 100, 100, then 50 of the third step
        ASSERT_TRUE(launch(r));
        frame(); // step 1: x = 100
        frame(); // step 2: x = 200
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mSink.count(ProjectileOp::Move), 2u);
        EXPECT_NEAR(mSink.mProjectileOps.back().mPose.mX, 200.0, 1e-3); // dt is the float 0.1f
        frame(); // step 3 ends it at x = 250: told at the START of the next update
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        frame();
        ASSERT_EQ(mRules.mResults.size(), 1u);
        const ProjectileResult& result = mRules.mResults[0];
        EXPECT_EQ(result.fate, ProjectileFate::Expired);
        EXPECT_EQ(result.token, 33u);
        EXPECT_EQ(result.owner, CombatSubject::of(mHandleA));
        EXPECT_NEAR(result.x, 250.0, 1e-3);
        EXPECT_NEAR(result.distance, 250.f, 1e-3f);
        EXPECT_NEAR(result.flightSeconds, 0.25f, 1e-5f);
        EXPECT_FALSE(mRules.mResultsInUpdate[0]); // before combatUpdate, never from inside a request
        EXPECT_EQ(mSink.count(ProjectileOp::Remove), 1u);
        EXPECT_EQ(mCombat.stats().mProjectilesExpired, 1u);
        frame();
        EXPECT_EQ(mRules.mResults.size(), 1u); // once
    }

    TEST_F(ProjectileFixture, anActorInTheWayIsHitNotTheOwner)
    {
        mWorld.mCandidates = { capsule(mHandleA, 50.0, 0.0), capsule(mHandleB, 250.0, 0.0) };
        ASSERT_TRUE(launch(request()));
        for (int i = 0; i < 3; ++i)
            frame(); // 100, 200, then 228 (the surface of B at 250 - 22)
        frame(); // told
        ASSERT_EQ(mRules.mResults.size(), 1u);
        const ProjectileResult& result = mRules.mResults[0];
        EXPECT_EQ(result.fate, ProjectileFate::HitActor);
        EXPECT_EQ(result.actor, mHandleB);
        EXPECT_NEAR(result.x, 228.0, 1e-6);
        EXPECT_NEAR(result.flightSeconds, 0.228f, 1e-5f);
        EXPECT_EQ(mCombat.stats().mProjectileHitsActor, 1u);
        EXPECT_EQ(mSink.count(ProjectileOp::Remove), 1u); // an arrow that hits an actor is not left in the air
        EXPECT_EQ(mCombat.projectilesStuck(), 0u);
    }

    TEST_F(ProjectileFixture, aDeadActorIsNotHit)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        mRules.mOnUpdate = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDeath(mHandleB)); };
        frame();
        ASSERT_TRUE(launch(request()));
        for (int i = 0; i < 6; ++i)
            frame();
        ASSERT_EQ(mRules.mResults.size(), 0u); // still flying: nothing in the way
        EXPECT_EQ(mCombat.projectilesInFlight(), 1u);
    }

    TEST_F(ProjectileFixture, thePlayerIsHitByAnActorsShot)
    {
        MWPhysics::NeutralSweep::Candidate player = capsule({}, 250.0, 0.0);
        player.mTarget = SweepTarget::Player;
        mWorld.mCandidates = { player };
        ASSERT_TRUE(launch(request()));
        for (int i = 0; i < 4; ++i)
            frame();
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_EQ(mRules.mResults[0].fate, ProjectileFate::HitPlayer);
        EXPECT_EQ(mCombat.stats().mProjectileHitsPlayer, 1u);
    }

    TEST_F(ProjectileFixture, aWallStopsTheShotAndTheArrowStaysDrawnIntoItForTheStickTime)
    {
        mWorld.mWallX = 250.0;
        ProjectileRequest r = request();
        r.stickDepth = 20.f;
        r.stickSeconds = 0.35f;
        ASSERT_TRUE(launch(r));
        for (int i = 0; i < 3; ++i)
            frame(); // the wall is crossed in step 3, at x = 250
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        EXPECT_EQ(mCombat.projectilesStuck(), 1u);
        // The last move puts the arrow 20 units into the wall along its flight: x = 270.
        const ProjectileOp& last = mSink.mProjectileOps.back();
        EXPECT_EQ(last.mKind, ProjectileOp::Move);
        EXPECT_NEAR(last.mPose.mX, 270.0, 1e-6);
        EXPECT_NEAR(last.mPose.mDirX, 1.f, 1e-6f);
        EXPECT_EQ(mSink.count(ProjectileOp::Remove), 0u);
        frame(); // told
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_EQ(mRules.mResults[0].fate, ProjectileFate::HitWorld);
        EXPECT_NEAR(mRules.mResults[0].x, 250.0, 1e-6);
        // 0.35 s of sticking from the hit: the told frame took 0.1, two more leave 0.05, the next one passes 0.
        for (int i = 0; i < 2; ++i)
            frame();
        EXPECT_EQ(mCombat.projectilesStuck(), 1u);
        frame();
        EXPECT_EQ(mCombat.projectilesStuck(), 0u);
        EXPECT_EQ(mSink.count(ProjectileOp::Remove), 1u);
        EXPECT_EQ(mCombat.stats().mProjectileHitsWorld, 1u);
    }

    TEST_F(ProjectileFixture, aShotWhoseOwnerHasGoneIsNotToldAndNothingIsLeftDrawn)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        ASSERT_TRUE(launch(request()));
        frame();
        mActors.remove(nullptr, &mKeyA); // the shooter's cell unloads
        for (int i = 0; i < 4; ++i)
            frame();
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mCombat.stats().mProjectileResultsDropped, 1u);
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        EXPECT_EQ(mSink.count(ProjectileOp::Remove), mSink.count(ProjectileOp::Spawn)); // nothing left drawn
    }

    TEST_F(ProjectileFixture, atMostSixtyFourShotsFlyAtOnceAndEachGetsItsOwnNumber)
    {
        std::vector<bool> accepted;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            for (int i = 0; i < 70; ++i)
            {
                ProjectileRequest r = request();
                r.token = 100 + static_cast<std::uint32_t>(i);
                accepted.push_back(w.launchProjectile(CombatSubject::of(mHandleA), r));
            }
        };
        frame();
        std::size_t yes = 0;
        for (bool a : accepted)
            yes += a ? 1 : 0;
        EXPECT_EQ(yes, NeutralCombatBridge::kMaxProjectiles);
        EXPECT_EQ(mCombat.projectilesInFlight(), NeutralCombatBridge::kMaxProjectiles);
        std::vector<std::uint32_t> ids;
        for (const ProjectileOp& op : mSink.mProjectileOps)
            if (op.mKind == ProjectileOp::Spawn)
                ids.push_back(op.mId);
        std::sort(ids.begin(), ids.end());
        EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    }

    TEST_F(ProjectileFixture, theSameShotsGiveTheSameTraceTwice)
    {
        auto run = [&]() {
            NeutralActorBridge actors;
            int key = 0;
            const ActorHandle owner = actors.add(nullptr, &key, 11, 1, "A", 0, 0, 0, 0);
            NeutralCombatBridge bridge(actors);
            Rules rules;
            Sink sink;
            World world;
            world.mWallX = 400.0;
            rules.mOnUpdate = [&](CombatRequestWriter& w) {
                ProjectileRequest r = request(1.0f);
                r.pitch = 0.2f;
                r.gravity = 686.f;
                (void)w.launchProjectile(CombatSubject::of(owner), r);
            };
            for (int i = 0; i < 40; ++i)
                bridge.update(&rules, 1.f / 60.f, sink, [](const void*) { return HavokPack::ClipProgress{}; }, &world);
            std::vector<double> xs;
            for (const ProjectileOp& op : sink.mProjectileOps)
                xs.push_back(op.mPose.mX + op.mPose.mY * 3.0 + op.mPose.mZ * 7.0);
            return xs;
        };
        const std::vector<double> a = run();
        const std::vector<double> b = run();
        ASSERT_FALSE(a.empty());
        EXPECT_EQ(a, b);
    }

    TEST_F(ProjectileFixture, aFrameWithShotsInTheAirDoesNotAllocate)
    {
        mWorld.mCandidates = { capsule(mHandleB, 5000.0, 0.0) };
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            for (int i = 0; i < 8; ++i)
            {
                ProjectileRequest r = request();
                r.range = 1.0e6f;
                r.token = static_cast<std::uint32_t>(i + 1);
                (void)w.launchProjectile(CombatSubject::of(mHandleA), r);
            }
        };
        frame();
        Sink quiet;
        for (int i = 0; i < 3; ++i)
            mCombat.update(&mRules, 0.016f, quiet, mMetaFn, &mWorld);
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 50; ++i)
            mCombat.update(&mRules, 0.016f, quiet, mMetaFn, &mWorld);
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(mCombat.projectilesInFlight(), 8u);
    }

    // ---- the clip speed ------------------------------------------------------------------------------------------

    std::shared_ptr<const HavokPack::ClipMeta> attackMeta()
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 1.3f;
        meta->mSpeed = 1.f;
        meta->mEvents = { { "preHitFrame", 0.233f }, { "weaponSwing", 0.333f }, { "HitFrame", 0.367f },
            { "attackStop", 1.287f } };
        meta->sortEvents();
        return meta;
    }

    TEST_F(ProjectileFixture, aClipSpeedMakesTheClipsEventsFireThatMuchSooner)
    {
        mMeta = attackMeta();
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            ClipRequest c;
            c.slot = ClipSlot::BowDraw;
            c.clip = "Bow_DrawLight";
            c.token = 5;
            EXPECT_TRUE(w.playClip(CombatSubject::of(mHandleA), c));
            EXPECT_TRUE(w.setClipSpeed(CombatSubject::of(mHandleA), 2.f));
        };
        frame();
        ASSERT_EQ(mSink.mSpeeds.size(), 1u);
        EXPECT_EQ(mSink.mSpeeds[0].first, &mKeyA);
        EXPECT_FLOAT_EQ(mSink.mSpeeds[0].second, 2.f);
        // The clock starts at 0 in the next frame and then runs 0.2 playback seconds a 0.1 s frame: 0, 0.2, 0.4.
        frame();
        frame();
        EXPECT_EQ(mRules.mEvents.size(), 0u); // 0.233 is not yet passed at 0.2
        frame();
        ASSERT_EQ(mRules.mEvents.size(), 3u); // 0.4 passes 0.233, 0.333 and 0.367 together
        EXPECT_EQ(mRules.mEvents[2].first, ClipEventKind::Hit);
    }

    TEST_F(ProjectileFixture, aBadClipSpeedOrNoClipIsRefused)
    {
        mMeta = attackMeta();
        std::vector<bool> results;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            results.push_back(w.setClipSpeed(CombatSubject::of(mHandleA), 2.f)); // no clip is playing
            ClipRequest c;
            c.slot = ClipSlot::BowDraw;
            c.clip = "Bow_DrawLight";
            c.token = 5;
            (void)w.playClip(CombatSubject::of(mHandleA), c);
            for (const float bad : { 0.f, 0.05f, 11.f, std::numeric_limits<float>::quiet_NaN() })
                results.push_back(w.setClipSpeed(CombatSubject::of(mHandleA), bad));
            results.push_back(w.setClipSpeed(CombatSubject::ofPlayer(), 2.f));
        };
        frame();
        EXPECT_EQ(std::count(results.begin(), results.end(), true), 0);
        EXPECT_TRUE(mSink.mSpeeds.empty());
    }

    // ---- the clip's event time -----------------------------------------------------------------------------------

    TEST_F(ProjectileFixture, theEventTimeOfAClipComesFromItsSidecar)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 2.f;
        meta->mSpeed = 1.f;
        meta->mEvents = { { "arrowAttach", 0.3f }, { "bowDrawStart", 0.8f }, { "bowDrawn", 1.66667f } };
        meta->sortEvents();
        float drawn = -1.f;
        float release = -1.f;
        std::vector<bool> results;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            results.push_back(w.clipEventTime("Bow_DrawLight", false, ClipEventKind::BowDrawn, drawn)); // not ready
        };
        frame();
        EXPECT_FALSE(results[0]);
        mWorld.mMeta = meta;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            results.push_back(w.clipEventTime("Bow_DrawLight", false, ClipEventKind::BowDrawn, drawn));
            results.push_back(w.clipEventTime("Bow_DrawLight", false, ClipEventKind::BowRelease, release));
            float unused = 0.f;
            results.push_back(w.clipEventTime("bad name!", false, ClipEventKind::BowDrawn, unused));
        };
        frame();
        EXPECT_TRUE(results[1]);
        EXPECT_FLOAT_EQ(drawn, 1.66667f);
        EXPECT_FALSE(results[2]); // the clip has no such event
        EXPECT_FALSE(results[3]);
    }

    TEST_F(ProjectileFixture, theReleaseAndDrawNamesOfTheBowClipsAreClassified)
    {
        EXPECT_EQ(MWPhysics::classifyClipEvent("arrowRelease"), ClipEventKind::BowRelease);
        EXPECT_EQ(MWPhysics::classifyClipEvent("ARROWRELEASE"), ClipEventKind::BowRelease);
        EXPECT_EQ(MWPhysics::classifyClipEvent("bowDrawn"), ClipEventKind::BowDrawn);
        EXPECT_EQ(MWPhysics::classifyClipEvent("arrowAttach"), ClipEventKind::Other);
    }

    // ---- the player's attack edges ---------------------------------------------------------------------------------

    TEST_F(ProjectileFixture, thePlayersPressAndReleaseReachThePack)
    {
        EXPECT_TRUE(mCombat.playerAttackEdge(&mRules, true));
        EXPECT_TRUE(mCombat.playerAttackEdge(&mRules, false));
        EXPECT_EQ(mRules.mEdges, (std::vector<bool>{ true, false }));
        EXPECT_EQ(mCombat.stats().mPlayerAttackEdges, 2u);
        EXPECT_FALSE(mCombat.playerAttackEdge(nullptr, true));
    }
}
