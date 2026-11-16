// Task 0192 (combat on many actors): the engine's combat bridge and the REAL Skyrim pack rules (SkyrimCombat) together,
// with a fake world, a fake sink and fake data, and many fighters at once. What it checks: a steady fight of N fighters
// swings, sweeps, hits and reports damage (so the numbers below are about real work), and how many heap allocations a
// frame of it makes (the guide: none in per-frame code). Counting uses the shared operator new replacement of
// openmw-tests (support/allocationcounter.hpp).
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    constexpr float kDt = 1.f / 60.f;
    constexpr WorldId kBase = 100;

    class Data final : public game_skyrimse::CombatData
    {
    public:
        Data()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = game_skyrimse::WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = game_skyrimse::kFirstSkillActorValue;
            mFighter.mHealthMax = 1.0e6f; // nobody dies: a steady fight
            mFighter.mStaminaMax = 1.0e6f;
            mFighter.mSkills[0] = 50;
            mPlayer.mHealthMax = 1.0e6f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId, game_skyrimse::FighterProfile& out) override
        {
            out = mFighter;
            return true;
        }
        game_skyrimse::FighterProfile playerFighter() override { return mPlayer; }
        const game_skyrimse::WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironsword" ? &mSword : nullptr;
        }
        game_skyrimse::CombatSettings settings() override { return {}; }

        game_skyrimse::WeaponRecord mSword;
        game_skyrimse::FighterProfile mFighter;
        game_skyrimse::FighterProfile mPlayer;
    };

    // The world: the player at the origin and the actors on a ring round it, listed in the order they were added.
    class World final : public MWPhysics::NeutralCombatWorld
    {
    public:
        struct Body
        {
            ActorHandle mHandle;
            double mX = 0.0;
            double mY = 0.0;
        };
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out.healthMax = 1000.f;
                out.health = 1000.f;
                return true;
            }
            for (const Body& b : mBodies)
                if (b.mHandle == subject.actor)
                {
                    out.x = b.mX;
                    out.y = b.mY;
                    out.heading = static_cast<float>(std::atan2(-b.mX, -b.mY)); // faces the player (the pack turns it)
                    return true;
                }
            return false;
        }
        void collectCandidates(std::vector<MWPhysics::NeutralSweep::Candidate>& out) override
        {
            for (const Body& b : mBodies)
            {
                MWPhysics::NeutralSweep::Candidate c;
                c.mTarget = SweepTarget::Actor;
                c.mActor = b.mHandle;
                c.mX = b.mX;
                c.mY = b.mY;
                c.mRadius = 22.f;
                c.mHeight = 128.f;
                out.push_back(c);
            }
            MWPhysics::NeutralSweep::Candidate p;
            p.mTarget = SweepTarget::Player;
            p.mRadius = 22.f;
            p.mHeight = 128.f;
            out.push_back(p);
        }
        std::vector<Body> mBodies;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override { ++mPlays; }
        void stopClip(const void*) override {}
        int mPlays = 0;
    };

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

    // N fighters on a ring of radius 90 round the player, all hostile to it (the pack's crowd test rule), walked
    // through the real bridge frame by frame.
    struct Fight
    {
        explicit Fight(int fighters)
            : mKeys(static_cast<std::size_t>(fighters))
            , mCombat(mActors)
        {
            mRules.setData(&mData);
            mRules.setTestWeapon("IronSword");
            mRules.setTestCrowd(fighters, 0, 0.f);
            for (int i = 0; i < fighters; ++i)
            {
                const ActorHandle handle = mActors.add(nullptr, &mKeys[static_cast<std::size_t>(i)],
                    static_cast<WorldId>(1000 + i), kBase, "Guard", 0, 0, 0, 0);
                ActorInfo info;
                info.handle = handle;
                info.base = kBase;
                info.editorId = "Guard";
                mRules.onActorAdded(info);
                const double angle = static_cast<double>(i) / static_cast<double>(fighters) * 6.2831853;
                mWorld.mBodies.push_back({ handle, std::cos(angle) * 90.0, std::sin(angle) * 90.0 });
            }
        }
        void frame() { mCombat.update(&mRules, kDt, mSink, mMetaFn, &mWorld); }

        std::vector<int> mKeys;
        NeutralActorBridge mActors;
        NeutralCombatBridge mCombat;
        Data mData;
        game_skyrimse::SkyrimCombat mRules;
        World mWorld;
        Sink mSink;
        std::shared_ptr<const HavokPack::ClipMeta> mMeta = attackMeta();
        MWPhysics::NeutralClipMetaFn mMetaFn
            = [this](const void*) -> HavokPack::ClipProgress { return { mMeta, true, 0.f }; };
    };

    struct Measure
    {
        std::size_t mFrames = 0;
        std::size_t mAllocations = 0;
        std::size_t mFramesWithAllocations = 0;
        std::size_t mWorstFrame = 0;
    };

    Measure measure(Fight& fight, int frames)
    {
        Measure m;
        for (int i = 0; i < frames; ++i)
        {
            TestSupport::AllocationCounter counter;
            fight.frame();
            const std::size_t n = counter.count();
            ++m.mFrames;
            m.mAllocations += n;
            if (n > 0)
                ++m.mFramesWithAllocations;
            m.mWorstFrame = std::max(m.mWorstFrame, n);
        }
        return m;
    }

    TEST(NeutralCombatMany, aSteadyFightOfManyFightersDoesRealWork)
    {
        Fight fight(16);
        for (int i = 0; i < 1200; ++i)
            fight.frame();
        const auto& stats = fight.mCombat.stats();
        std::cout << "[ STATS ] sweeps " << stats.mSweeps << " hits " << stats.mSweepHits << " damage "
                  << stats.mDamageReports << " refused " << stats.mDamageRefused << " clips " << stats.mClipsStarted
                  << " completed " << stats.mClipsCompleted << " events " << stats.mEventsDelivered << "\n";
        std::cout << "[ STATS ] the bridge reserves " << fight.mCombat.reservedBytes() << " bytes in all (fixed, whatever "
                  << "the number of fighters)\n";
        EXPECT_GT(stats.mSweeps, 100u); // 16 fighters x 20 s / 2.5 s each = about 128
        EXPECT_GT(stats.mSweepHits, 100u);
        EXPECT_GT(stats.mDamageReports, 100u);
        EXPECT_EQ(stats.mSweepsRefused, 0u);
        EXPECT_EQ(stats.mTableFull, 0u);
        EXPECT_EQ(stats.mRulesFailures, 0u);
        EXPECT_EQ(stats.mWriterFailures, 0u);
    }

    TEST(NeutralCombatMany, theAllocationsOfASteadyFightAreCountedPerFrame)
    {
        for (const int fighters : { 1, 8, 16, 32 })
        {
            Fight fight(fighters);
            for (int i = 0; i < 1200; ++i) // warm up: every table at its working size, 20 s of fighting
                fight.frame();
            const Measure m = measure(fight, 1200);
            std::cout << "[ MEASURE ] " << fighters << " fighters: " << m.mAllocations << " allocations in "
                      << m.mFrames << " frames, " << m.mFramesWithAllocations << " frames with any, worst frame " << m.mWorstFrame
                      << "\n";
            // The bound is set from the measurement (see the task log).
            EXPECT_EQ(m.mAllocations, 0u) << fighters << " fighters";
        }
    }
}
