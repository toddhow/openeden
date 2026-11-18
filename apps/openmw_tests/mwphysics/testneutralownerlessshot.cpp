// Task 0299: a trap's shot (ProjectileRequest::ownerless) on the neutral combat bridge. Nobody fired it: the owner is
// CombatSubject{} (invalid), nothing is excluded from its hits (the player and every actor can be hit), and the result
// carries the same invalid owner and the token. Without the flag an invalid owner is still refused. A fake pack, sink
// and world, as in testneutralprojectilebridge.cpp. Written before the bridge change and run red against it.
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;
    using MWPhysics::NeutralProjectilePose;
    using MWPhysics::NeutralProjectile::Vec;

    constexpr float kHalfPi = 1.5707963f;

    class Rules final : public CombatRules
    {
    public:
        Rules() { mResults.reserve(16); }
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void onProjectileResult(const ProjectileResult& result) override { mResults.push_back(result); }
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mOnUpdate)
                mOnUpdate(writer);
            mOnUpdate = nullptr;
        }
        std::function<void(CombatRequestWriter&)> mOnUpdate;
        std::vector<ProjectileResult> mResults;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void setClipSpeed(const void*, float) override {}
        void spawnProjectile(std::uint32_t id, std::string_view, const NeutralProjectilePose&) override
        {
            mSpawned.push_back(id);
        }
        void moveProjectile(std::uint32_t, const NeutralProjectilePose&) override {}
        void removeProjectile(std::uint32_t id) override { mRemoved.push_back(id); }
        std::vector<std::uint32_t> mSpawned;
        std::vector<std::uint32_t> mRemoved;
    };

    class World final : public MWPhysics::NeutralCombatWorld
    {
    public:
        bool subjectState(CombatSubject, SubjectState&) override { return false; }
        void collectCandidates(std::vector<MWPhysics::NeutralSweep::Candidate>& out) override
        {
            for (const auto& c : mCandidates)
                out.push_back(c);
        }
        bool rayWorld(const Vec&, const Vec&, double&) override { return false; }
        std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view, bool) override { return {}; }
        std::vector<MWPhysics::NeutralSweep::Candidate> mCandidates;
    };

    struct OwnerlessShot : ::testing::Test
    {
        int mKeyA = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        Rules mRules;
        Sink mSink;
        World mWorld;
        MWPhysics::NeutralClipMetaFn mMetaFn = [](const void*) -> HavokPack::ClipProgress {
            return { nullptr, true, 0.f };
        };
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);

        void frame(float seconds = 0.1f) { mCombat.update(&mRules, seconds, mSink, mMetaFn, &mWorld); }

        static ProjectileRequest request(bool ownerless)
        {
            ProjectileRequest r;
            r.token = 55;
            r.z = 64.0;
            r.heading = kHalfPi; // +X
            r.speed = 1000.f;
            r.range = 5000.f;
            r.stickDepth = 20.f;
            r.stickSeconds = 10.f;
            r.model = "meshes/traps/dartprojectile.nif";
            r.ownerless = ownerless;
            return r;
        }
        bool launch(const ProjectileRequest& r, CombatSubject owner)
        {
            bool accepted = false;
            mRules.mOnUpdate = [&](CombatRequestWriter& w) { accepted = w.launchProjectile(owner, r); };
            frame();
            return accepted;
        }
        static MWPhysics::NeutralSweep::Candidate capsule(SweepTarget target, ActorHandle handle, double x)
        {
            MWPhysics::NeutralSweep::Candidate c;
            c.mTarget = target;
            c.mActor = handle;
            c.mX = x;
            c.mRadius = 22.f;
            c.mHeight = 128.f;
            return c;
        }
    };

    TEST_F(OwnerlessShot, anOwnerlessShotWithNoOwnerIsAccepted)
    {
        EXPECT_TRUE(launch(request(true), CombatSubject{}));
        EXPECT_EQ(mCombat.projectilesInFlight(), 1u);
        EXPECT_EQ(mSink.mSpawned.size(), 1u);
    }

    TEST_F(OwnerlessShot, withoutTheFlagAnInvalidOwnerIsStillRefused)
    {
        EXPECT_FALSE(launch(request(false), CombatSubject{}));
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
    }

    TEST_F(OwnerlessShot, theFlagWithARealOwnerIsRefusedAsConfused)
    {
        EXPECT_FALSE(launch(request(true), CombatSubject::of(mHandleA)));
        EXPECT_FALSE(launch(request(true), CombatSubject::ofPlayer()));
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
    }

    TEST_F(OwnerlessShot, anOwnerlessShotHitsThePlayerAndTellsTheTokenWithNoOwner)
    {
        mWorld.mCandidates = { capsule(SweepTarget::Player, {}, 250.0) };
        ASSERT_TRUE(launch(request(true), CombatSubject{}));
        for (int i = 0; i < 5; ++i)
            frame();
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_EQ(mRules.mResults[0].fate, ProjectileFate::HitPlayer);
        EXPECT_EQ(mRules.mResults[0].token, 55u);
        EXPECT_FALSE(mRules.mResults[0].owner.valid());
    }

    TEST_F(OwnerlessShot, anOwnerlessShotHitsAnActorToo)
    {
        mWorld.mCandidates = { capsule(SweepTarget::Actor, mHandleA, 250.0) };
        ASSERT_TRUE(launch(request(true), CombatSubject{}));
        for (int i = 0; i < 5; ++i)
            frame();
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_EQ(mRules.mResults[0].fate, ProjectileFate::HitActor);
        EXPECT_EQ(mRules.mResults[0].actor, mHandleA);
    }

    TEST_F(OwnerlessShot, anOwnerlessShotStopsAtNothingAndExpiresAtItsRange)
    {
        ProjectileRequest r = request(true);
        r.range = 300.f;
        ASSERT_TRUE(launch(r, CombatSubject{}));
        for (int i = 0; i < 6; ++i)
            frame();
        ASSERT_EQ(mRules.mResults.size(), 1u);
        EXPECT_EQ(mRules.mResults[0].fate, ProjectileFate::Expired);
    }
}
