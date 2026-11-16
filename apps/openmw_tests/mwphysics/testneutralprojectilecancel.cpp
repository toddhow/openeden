// Task 0262: CombatRequestWriter::cancelProjectile on the neutral combat bridge (a stuck arrow or rune glyph is removed
// at once; one in flight is removed; an unknown token and a call outside combatUpdate are refused).
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

    class CancelRules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mOnUpdate)
                mOnUpdate(writer);
            mOnUpdate = nullptr;
            mKeptWriter = &writer;
        }
        std::function<void(CombatRequestWriter&)> mOnUpdate;
        CombatRequestWriter* mKeptWriter = nullptr;
    };

    class CancelSink final : public MWPhysics::NeutralClipSink
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

    class CancelWorld final : public MWPhysics::NeutralCombatWorld
    {
    public:
        bool subjectState(CombatSubject, SubjectState&) override { return false; }
        void collectCandidates(std::vector<MWPhysics::NeutralSweep::Candidate>&) override {}
        bool rayWorld(const Vec& p0, const Vec& p1, double& fraction) override
        {
            if (mWallX > 0.0 && p0.x < mWallX && p1.x >= mWallX)
            {
                fraction = (mWallX - p0.x) / (p1.x - p0.x);
                return true;
            }
            return false;
        }
        std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view, bool) override { return {}; }
        double mWallX = 0.0;
    };

    struct CancelFixture : ::testing::Test
    {
        int mKeyA = 0;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat{ mActors };
        CancelRules mRules;
        CancelSink mSink;
        CancelWorld mWorld;
        MWPhysics::NeutralClipMetaFn mMetaFn
            = [](const void*) -> HavokPack::ClipProgress { return { nullptr, true, 0.f }; };
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);

        void frame(float seconds = 0.1f) { mCombat.update(&mRules, seconds, mSink, mMetaFn, &mWorld); }

        static ProjectileRequest request(std::uint32_t token)
        {
            ProjectileRequest r;
            r.token = token;
            r.z = 64.0;
            r.heading = kHalfPi; // +X
            r.speed = 1000.f;
            r.range = 5000.f;
            r.stickDepth = 20.f;
            r.stickSeconds = 600.f;
            r.model = "meshes/magic/runefire.nif";
            return r;
        }
        bool launch(std::uint32_t token)
        {
            bool accepted = false;
            mRules.mOnUpdate = [&](CombatRequestWriter& w) {
                accepted = w.launchProjectile(CombatSubject::of(mHandleA), request(token));
            };
            frame();
            return accepted;
        }
        bool cancel(std::uint32_t token)
        {
            bool removed = false;
            mRules.mOnUpdate = [&](CombatRequestWriter& w) { removed = w.cancelProjectile(token); };
            frame();
            return removed;
        }
    };

    TEST_F(CancelFixture, aStuckProjectileIsRemovedAtOnce)
    {
        mWorld.mWallX = 250.0;
        ASSERT_TRUE(launch(77));
        for (int i = 0; i < 5; ++i)
            frame();
        ASSERT_EQ(mCombat.projectilesStuck(), 1u);
        ASSERT_TRUE(mSink.mRemoved.empty());
        EXPECT_TRUE(cancel(77));
        EXPECT_EQ(mCombat.projectilesStuck(), 0u);
        ASSERT_EQ(mSink.mRemoved.size(), 1u);
        EXPECT_EQ(mSink.mRemoved[0], mSink.mSpawned[0]);
        // Its stick time must not remove it a second time.
        for (int i = 0; i < 3; ++i)
            frame(1000.f);
        EXPECT_EQ(mSink.mRemoved.size(), 1u);
    }

    TEST_F(CancelFixture, aProjectileStillInFlightIsRemovedAndNeverTold)
    {
        ASSERT_TRUE(launch(78));
        ASSERT_EQ(mCombat.projectilesInFlight(), 1u);
        EXPECT_TRUE(cancel(78));
        EXPECT_EQ(mCombat.projectilesInFlight(), 0u);
        ASSERT_EQ(mSink.mRemoved.size(), 1u);
        EXPECT_EQ(mSink.mRemoved[0], mSink.mSpawned[0]);
    }

    TEST_F(CancelFixture, anUnknownOrAlreadyCancelledTokenIsRefused)
    {
        mWorld.mWallX = 250.0;
        ASSERT_TRUE(launch(79));
        for (int i = 0; i < 5; ++i)
            frame();
        EXPECT_FALSE(cancel(1234));
        EXPECT_TRUE(mSink.mRemoved.empty());
        EXPECT_TRUE(cancel(79));
        EXPECT_FALSE(cancel(79));
        EXPECT_EQ(mSink.mRemoved.size(), 1u);
    }

    TEST_F(CancelFixture, aCancelOutsideTheUpdateIsRefused)
    {
        mWorld.mWallX = 250.0;
        ASSERT_TRUE(launch(80));
        for (int i = 0; i < 5; ++i)
            frame();
        ASSERT_NE(mRules.mKeptWriter, nullptr);
        EXPECT_FALSE(mRules.mKeptWriter->cancelProjectile(80));
        EXPECT_EQ(mCombat.projectilesStuck(), 1u);
        EXPECT_TRUE(mSink.mRemoved.empty());
    }
}
