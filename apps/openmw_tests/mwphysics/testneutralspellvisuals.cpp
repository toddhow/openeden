// Task 0191 (visible spells and beams): the bridge side of the 0.5 additions to the combat contract: castRay (the line
// of a held aimed spell, a touch spell or a beam), visual-only projectiles (the stream of a beam) and shown effects
// (the body of a ward). Tested with a fake pack, a fake sink and a fake world (no Jolt, no Ptr). Written from
// combat_rules.hpp 0.5 and the bridge header.
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

    class Rules final : public CombatRules
    {
    public:
        Rules() { mResults.reserve(64); }
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void onProjectileResult(const ProjectileResult& result) override { mResults.push_back(result); }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mOnUpdate)
                mOnUpdate(writer);
            mOnUpdate = nullptr;
            mKeptWriter = &writer;
        }
        std::function<void(CombatRequestWriter&)> mOnUpdate;
        std::vector<ProjectileResult> mResults;
        CombatRequestWriter* mKeptWriter = nullptr;
    };

    struct Op
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
        Sink() { mOps.reserve(4096); }
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void spawnProjectile(std::uint32_t id, std::string_view model, const NeutralProjectilePose& pose) override
        {
            mOps.push_back({ Op::Spawn, id, std::string(model), pose });
        }
        void moveProjectile(std::uint32_t id, const NeutralProjectilePose& pose) override
        {
            mOps.push_back({ Op::Move, id, {}, pose });
        }
        void removeProjectile(std::uint32_t id) override { mOps.push_back({ Op::Remove, id, {}, {} }); }
        std::size_t count(Op::Kind kind) const
        {
            std::size_t n = 0;
            for (const Op& op : mOps)
                n += op.mKind == kind ? 1 : 0;
            return n;
        }
        std::vector<Op> mOps;
    };

    // A sink that keeps nothing, so a test can count what the bridge itself allocates.
    class NullSink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void spawnProjectile(std::uint32_t, std::string_view, const NeutralProjectilePose&) override {}
        void moveProjectile(std::uint32_t, const NeutralProjectilePose&) override {}
        void removeProjectile(std::uint32_t) override {}
    };

    class World final : public MWPhysics::NeutralCombatWorld
    {
    public:
        World() { mCandidates.reserve(16); }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = subject.player ? mPlayer : mState;
            return true;
        }
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
        std::vector<MWPhysics::NeutralSweep::Candidate> mCandidates;
        double mWallX = 0.0;
        SubjectState mState;
        SubjectState mPlayer;
    };

    struct SpellVisualsFixture : ::testing::Test
    {
        int mKeyA = 0;
        int mKeyB = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        Rules mRules;
        Sink mSink;
        World mWorld;
        MWPhysics::NeutralClipMetaFn mMetaFn = [](const void*) -> HavokPack::ClipProgress { return {}; };
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mHandleB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);

        void frame(float seconds = 0.1f) { mCombat.update(&mRules, seconds, mSink, mMetaFn, &mWorld); }

        static RayRequest ray(float heading = kPi / 2.f, float range = 1000.f)
        {
            RayRequest r;
            r.x = 0.0;
            r.y = 0.0;
            r.z = 64.0;
            r.heading = heading; // +X
            r.pitch = 0.f;
            r.range = range;
            return r;
        }

        // Casts in one update and returns what the writer answered.
        bool cast(const RayRequest& r, RayHit& hit, CombatSubject from)
        {
            bool accepted = false;
            mRules.mOnUpdate = [&](CombatRequestWriter& w) { accepted = w.castRay(from, r, hit); };
            frame();
            return accepted;
        }
        bool cast(const RayRequest& r, RayHit& hit) { return cast(r, hit, CombatSubject::of(mHandleA)); }

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

    // ---- castRay -------------------------------------------------------------------------------------------------

    TEST_F(SpellVisualsFixture, aRayOutsideTheUpdateIsRefused)
    {
        frame();
        mRules.mOnUpdate = [](CombatRequestWriter&) {};
        frame();
        ASSERT_NE(mRules.mKeptWriter, nullptr);
        RayHit hit;
        EXPECT_FALSE(mRules.mKeptWriter->castRay(CombatSubject::of(mHandleA), ray(), hit));
        EXPECT_GE(mCombat.stats().mRaysRefused, 1u);
    }

    TEST_F(SpellVisualsFixture, aRayThatMeetsNothingEndsAtItsFarEnd)
    {
        RayHit hit;
        hit.fate = RayFate::World;
        ASSERT_TRUE(cast(ray(kPi / 2.f, 600.f), hit));
        EXPECT_EQ(hit.fate, RayFate::Nothing);
        EXPECT_NEAR(hit.x, 600.0, 1e-3);
        EXPECT_NEAR(hit.y, 0.0, 1e-3);
        EXPECT_NEAR(hit.z, 64.0, 1e-3);
        EXPECT_NEAR(hit.distance, 600.f, 1e-3f);
        EXPECT_EQ(mCombat.stats().mRaysCast, 1u);
    }

    TEST_F(SpellVisualsFixture, aRayHitsTheNearestActorAndNeverItsOwnBody)
    {
        mWorld.mCandidates = { capsule(mHandleA, 0.0, 0.0), capsule(mHandleB, 250.0, 0.0) };
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_EQ(hit.fate, RayFate::Actor);
        EXPECT_EQ(hit.actor, mHandleB);
        EXPECT_NEAR(hit.x, 228.0, 1e-6); // the surface of B's capsule at 250 - 22
        EXPECT_NEAR(hit.distance, 228.f, 1e-4f);
    }

    TEST_F(SpellVisualsFixture, aWallNearerThanTheActorWins)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        mWorld.mWallX = 100.0;
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_EQ(hit.fate, RayFate::World);
        EXPECT_NEAR(hit.x, 100.0, 1e-6);
        EXPECT_NEAR(hit.distance, 100.f, 1e-4f);
    }

    TEST_F(SpellVisualsFixture, aWallBeyondTheActorDoesNotMatter)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        mWorld.mWallX = 400.0;
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_EQ(hit.fate, RayFate::Actor);
        EXPECT_EQ(hit.actor, mHandleB);
    }

    TEST_F(SpellVisualsFixture, aWallBeyondTheRangeIsNotHit)
    {
        mWorld.mWallX = 700.0;
        RayHit hit;
        ASSERT_TRUE(cast(ray(kPi / 2.f, 600.f), hit));
        EXPECT_EQ(hit.fate, RayFate::Nothing);
    }

    TEST_F(SpellVisualsFixture, aDeadActorIsNotHit)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        mRules.mOnUpdate = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDeath(mHandleB)); };
        frame();
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_EQ(hit.fate, RayFate::Nothing);
    }

    TEST_F(SpellVisualsFixture, anActorsRayCanHitThePlayerAndThePlayersOwnRayNeverDoes)
    {
        MWPhysics::NeutralSweep::Candidate player = capsule({}, 250.0, 0.0);
        player.mTarget = SweepTarget::Player;
        mWorld.mCandidates = { player };
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_EQ(hit.fate, RayFate::Player);
        EXPECT_NEAR(hit.x, 228.0, 1e-6);

        mWorld.mCandidates = { capsule({}, 0.0, 0.0) };
        mWorld.mCandidates[0].mTarget = SweepTarget::Player;
        RayHit own;
        ASSERT_TRUE(cast(ray(), own, CombatSubject::ofPlayer()));
        EXPECT_EQ(own.fate, RayFate::Nothing);
    }

    TEST_F(SpellVisualsFixture, aPitchedRayRisesAlongItsLine)
    {
        RayRequest r = ray(0.f, 200.f); // +Y
        r.pitch = kPi / 6.f; // 30 degrees up
        RayHit hit;
        ASSERT_TRUE(cast(r, hit));
        EXPECT_EQ(hit.fate, RayFate::Nothing);
        EXPECT_NEAR(hit.x, 0.0, 1e-6);
        EXPECT_NEAR(hit.y, 200.0 * std::cos(kPi / 6.f), 1e-3);
        EXPECT_NEAR(hit.z, 64.0 + 100.0, 1e-3);
    }

    TEST_F(SpellVisualsFixture, aRayDoesNotSpawnOrMoveAnything)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        RayHit hit;
        ASSERT_TRUE(cast(ray(), hit));
        EXPECT_TRUE(mSink.mOps.empty());
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
    }

    TEST_F(SpellVisualsFixture, badRaysAreRefusedAndCounted)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<RayRequest> bad;
        auto add = [&](auto&& change) {
            RayRequest r = ray();
            change(r);
            bad.push_back(r);
        };
        add([&](RayRequest& r) { r.range = 0.f; });
        add([&](RayRequest& r) { r.range = -3.f; });
        add([&](RayRequest& r) { r.range = nan; });
        add([&](RayRequest& r) { r.range = 2.0e6f; });
        add([&](RayRequest& r) { r.heading = nan; });
        add([&](RayRequest& r) { r.pitch = kPi / 2.f; });
        add([&](RayRequest& r) { r.pitch = nan; });
        add([&](RayRequest& r) { r.x = std::numeric_limits<double>::infinity(); });
        add([&](RayRequest& r) { r.z = std::numeric_limits<double>::quiet_NaN(); });
        for (const RayRequest& r : bad)
        {
            RayHit hit;
            EXPECT_FALSE(cast(r, hit)) << "range " << r.range << " pitch " << r.pitch;
        }
        EXPECT_EQ(mCombat.stats().mRaysRefused, bad.size());
        EXPECT_EQ(mCombat.stats().mRaysCast, 0u);
        RayHit hit;
        EXPECT_FALSE(cast(ray(), hit, CombatSubject::of({ 999, 1 }))); // a stale handle
        EXPECT_FALSE(cast(ray(), hit, CombatSubject{})); // nobody
    }

    // ---- visual-only projectiles ---------------------------------------------------------------------------------

    ProjectileRequest shot(float range = 250.f)
    {
        ProjectileRequest r;
        r.token = 71;
        r.x = 0.0;
        r.y = 0.0;
        r.z = 64.0;
        r.heading = kPi / 2.f;
        r.speed = 1000.f;
        r.range = range;
        r.stickDepth = 20.f;
        r.stickSeconds = 10.f;
        r.model = "meshes/magic/firesprayfx01.nif";
        r.visualOnly = true;
        return r;
    }

    TEST_F(SpellVisualsFixture, aVisualShotIsDrawnAndGoesWithoutAWordToThePack)
    {
        bool accepted = false;
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { accepted = w.launchProjectile(CombatSubject::of(mHandleA), shot()); };
        frame();
        ASSERT_TRUE(accepted);
        EXPECT_EQ(mSink.count(Op::Spawn), 1u);
        for (int i = 0; i < 4; ++i)
            frame();
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        EXPECT_EQ(mSink.count(Op::Remove), 1u);
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mCombat.stats().mVisualShotsLaunched, 1u);
        EXPECT_EQ(mCombat.stats().mVisualShotsEnded, 1u);
    }

    TEST_F(SpellVisualsFixture, aVisualShotThatMeetsAnActorOrAWallEndsSilentlyAndSticksInNothing)
    {
        mWorld.mCandidates = { capsule(mHandleB, 150.0, 0.0) };
        bool accepted = false;
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { accepted = w.launchProjectile(CombatSubject::of(mHandleA), shot(500.f)); };
        frame();
        ASSERT_TRUE(accepted);
        for (int i = 0; i < 4; ++i)
            frame();
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);

        mWorld.mCandidates.clear();
        mWorld.mWallX = 150.0;
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { accepted = w.launchProjectile(CombatSubject::of(mHandleA), shot(500.f)); };
        frame();
        for (int i = 0; i < 4; ++i)
            frame();
        EXPECT_TRUE(mRules.mResults.empty());
        EXPECT_EQ(mCombat.projectilesStuck(), 0u); // a visual shot does not stay in the wall
        EXPECT_EQ(mCombat.stats().mVisualShotsEnded, 2u);
    }

    // ---- shown effects -------------------------------------------------------------------------------------------

    EffectRequest effect(std::uint32_t token = 5)
    {
        EffectRequest r;
        r.token = token;
        r.heightOffset = 40.f;
        r.model = "meshes/magic/wardbodyfx.nif";
        return r;
    }

    TEST_F(SpellVisualsFixture, anEffectIsSpawnedAtTheSubjectWithTheOffsetFacingItsHeading)
    {
        mWorld.mState.x = 100.0;
        mWorld.mState.y = 200.0;
        mWorld.mState.z = 10.0;
        mWorld.mState.heading = kPi / 2.f; // +X
        bool shown = false;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) { shown = w.showEffect(CombatSubject::of(mHandleA), effect()); };
        frame();
        ASSERT_TRUE(shown);
        ASSERT_EQ(mSink.count(Op::Spawn), 1u);
        const Op& op = mSink.mOps[0];
        EXPECT_EQ(op.mModel, "meshes/magic/wardbodyfx.nif");
        EXPECT_NEAR(op.mPose.mX, 100.0, 1e-6);
        EXPECT_NEAR(op.mPose.mY, 200.0, 1e-6);
        EXPECT_NEAR(op.mPose.mZ, 50.0, 1e-6);
        EXPECT_NEAR(op.mPose.mDirX, 1.f, 1e-6f);
        EXPECT_NEAR(op.mPose.mDirY, 0.f, 1e-6f);
        EXPECT_NEAR(op.mPose.mDirZ, 0.f, 1e-6f);
        EXPECT_EQ(mCombat.stats().mEffectsShown, 1u);
    }

    TEST_F(SpellVisualsFixture, anEffectFollowsTheSubjectEveryFrameUntilItIsHidden)
    {
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleA), effect())); };
        frame();
        mWorld.mState.x = 33.0;
        mWorld.mState.y = -7.0;
        frame();
        ASSERT_GE(mSink.count(Op::Move), 1u);
        const Op& last = mSink.mOps.back();
        EXPECT_EQ(last.mKind, Op::Move);
        EXPECT_NEAR(last.mPose.mX, 33.0, 1e-6);
        EXPECT_NEAR(last.mPose.mY, -7.0, 1e-6);
        bool hidden = false;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) { hidden = w.hideEffect(5); };
        frame();
        EXPECT_TRUE(hidden);
        EXPECT_EQ(mSink.count(Op::Remove), 1u);
        const std::size_t moves = mSink.count(Op::Move);
        frame();
        EXPECT_EQ(mSink.count(Op::Move), moves); // nothing moves once it is hidden
        EXPECT_EQ(mCombat.stats().mEffectsHidden, 1u);
    }

    TEST_F(SpellVisualsFixture, hidingAnEffectThatIsNotShownReturnsFalse)
    {
        bool hidden = true;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) { hidden = w.hideEffect(99); };
        frame();
        EXPECT_FALSE(hidden);
    }

    TEST_F(SpellVisualsFixture, showingATokenAgainReplacesTheEffect)
    {
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleA), effect()));
            EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleB), effect()));
        };
        frame();
        EXPECT_EQ(mSink.count(Op::Spawn), 2u);
        EXPECT_EQ(mSink.count(Op::Remove), 1u);
        mSink.mOps.clear();
        frame();
        EXPECT_EQ(mSink.count(Op::Move), 1u); // one effect is left
    }

    TEST_F(SpellVisualsFixture, anEffectGoesWhenItsSubjectGoes)
    {
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleB), effect())); };
        frame();
        mActors.remove(nullptr, &mKeyB);
        frame();
        EXPECT_EQ(mSink.count(Op::Remove), 1u);
        EXPECT_EQ(mCombat.stats().mEffectsLost, 1u);
        mSink.mOps.clear();
        frame();
        EXPECT_TRUE(mSink.mOps.empty());
    }

    TEST_F(SpellVisualsFixture, thePlayersEffectFollowsThePlayer)
    {
        mWorld.mPlayer.x = 5.0;
        mWorld.mPlayer.y = 6.0;
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.showEffect(CombatSubject::ofPlayer(), effect())); };
        frame();
        ASSERT_EQ(mSink.count(Op::Spawn), 1u);
        EXPECT_NEAR(mSink.mOps[0].mPose.mX, 5.0, 1e-6);
        EXPECT_NEAR(mSink.mOps[0].mPose.mY, 6.0, 1e-6);
    }

    TEST_F(SpellVisualsFixture, everythingShownGoesWhenThePackIsGone)
    {
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleA), effect())); };
        frame();
        mCombat.update(nullptr, 0.1f, mSink, mMetaFn, &mWorld);
        EXPECT_EQ(mSink.count(Op::Remove), 1u);
    }

    TEST_F(SpellVisualsFixture, atMostSixteenEffectsAreShown)
    {
        std::size_t accepted = 0;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            for (std::uint32_t t = 1; t <= 20; ++t)
                accepted += w.showEffect(CombatSubject::of(mHandleA), effect(t)) ? 1u : 0u;
        };
        frame();
        EXPECT_EQ(accepted, kMaxShownEffects);
        EXPECT_EQ(mCombat.stats().mEffectsRefused, 20u - kMaxShownEffects);
    }

    TEST_F(SpellVisualsFixture, badEffectRequestsAreRefused)
    {
        const std::string tooLong(NeutralCombatBridge::kMaxProjectileModelPath + 1, 'a');
        std::vector<EffectRequest> bad;
        auto add = [&](auto&& change) {
            EffectRequest r = effect();
            change(r);
            bad.push_back(r);
        };
        add([&](EffectRequest& r) { r.model = {}; });
        add([&](EffectRequest& r) { r.model = "meshes/../x.nif"; });
        add([&](EffectRequest& r) { r.model = "c:/x.nif"; });
        add([&](EffectRequest& r) { r.model = tooLong; });
        add([&](EffectRequest& r) { r.heightOffset = std::numeric_limits<float>::quiet_NaN(); });
        std::size_t refused = 0;
        mRules.mOnUpdate = [&](CombatRequestWriter& w) {
            for (const EffectRequest& r : bad)
                refused += w.showEffect(CombatSubject::of(mHandleA), r) ? 0u : 1u;
            refused += w.showEffect(CombatSubject::of({ 999, 1 }), effect()) ? 0u : 1u; // a stale handle
        };
        frame();
        EXPECT_EQ(refused, bad.size() + 1);
        EXPECT_TRUE(mSink.mOps.empty());
        EXPECT_EQ(mCombat.stats().mEffectsRefused, bad.size() + 1);
    }

    TEST_F(SpellVisualsFixture, anEffectOutsideTheUpdateIsRefused)
    {
        frame();
        mRules.mOnUpdate = [](CombatRequestWriter&) {};
        frame();
        ASSERT_NE(mRules.mKeptWriter, nullptr);
        EXPECT_FALSE(mRules.mKeptWriter->showEffect(CombatSubject::of(mHandleA), effect()));
        EXPECT_FALSE(mRules.mKeptWriter->hideEffect(5));
    }

    // ---- no allocation -------------------------------------------------------------------------------------------

    TEST_F(SpellVisualsFixture, framesWithRaysEffectsAndVisualShotsAllocateNothing)
    {
        mWorld.mCandidates = { capsule(mHandleB, 250.0, 0.0) };
        mRules.mOnUpdate
            = [&](CombatRequestWriter& w) { EXPECT_TRUE(w.showEffect(CombatSubject::of(mHandleA), effect())); };
        frame();
        NullSink quiet;
        for (int i = 0; i < 3; ++i)
            mCombat.update(&mRules, 0.016f, quiet, mMetaFn, &mWorld);
        std::size_t casts = 0;
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 30; ++i)
        {
            mRules.mOnUpdate = [&](CombatRequestWriter& w) {
                RayHit hit;
                casts += w.castRay(CombatSubject::of(mHandleA), ray(), hit) ? 1u : 0u;
                ProjectileRequest p = shot(120.f);
                (void)w.launchProjectile(CombatSubject::of(mHandleA), p);
            };
            mCombat.update(&mRules, 0.016f, quiet, mMetaFn, &mWorld);
        }
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(casts, 30u);
    }
}
