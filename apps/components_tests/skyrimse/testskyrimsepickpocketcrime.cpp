// Task 0208: a caught pickpocket try is a crime (Kind::Pickpocket): the CRVA pickpocket bounty in the crime faction of
// whoever saw it. Written from the spec before the code and run red against a stub.
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::crime;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::RayFate;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    constexpr FormId kHold = 0x010000a0;
    constexpr FormId kSword = 0x00012eb7;

    CrimeData hold()
    {
        CrimeData d;
        d.mPresent = true;
        d.mMurder = 1000;
        d.mAssault = 40;
        d.mTrespass = 5;
        d.mPickpocket = 25;
        d.mStealMultiplier = 0.5f;
        return d;
    }

    class World final : public CrimeWorld
    {
    public:
        void actors(std::vector<CrimeActor>& out) override
        {
            for (const auto& [ref, a] : mActors)
                out.push_back(a);
        }
        bool actor(ActorHandle handle, CrimeActor& out) override
        {
            const auto it = mActors.find(handle.reference);
            if (it == mActors.end())
                return false;
            out = it->second;
            return true;
        }
        const CrimeData* crimeData(FormId faction) override
        {
            const auto it = mData.find(faction);
            return it == mData.end() ? nullptr : &it->second;
        }
        std::string factionName(FormId) override { return "Hold"; }
        std::int32_t itemValue(FormId) override { return 100; }
        std::int32_t playerGold() override { return 0; }
        bool takeGold(std::int32_t) override { return false; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, openeden::sdk::game::ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string& line) override { mLog.push_back(line); }

        std::map<std::uint32_t, CrimeActor> mActors;
        std::map<FormId, CrimeData> mData;
        std::vector<std::string> mLog;
    };

    class Writer final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = 120.0; // the victim stands next to the player
            return true;
        }
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = {};
            return true;
        }
    };

    CrimeActor victim(FormId faction)
    {
        CrimeActor a;
        a.mHandle = { 7, 1 };
        a.mBase = 7;
        a.mCrimeFaction = faction;
        a.mName = "Hulda";
        return a;
    }

    TEST(SkyrimPickpocketCrimeKind, hasANameAndTheCrvaPickpocketBounty)
    {
        EXPECT_EQ(kindName(Kind::Pickpocket), "pickpocket");
        EXPECT_EQ(bountyFor(Kind::Pickpocket, hold(), 0, 0), 25);
        EXPECT_EQ(bountyFor(Kind::Pickpocket, hold(), 500, 3), 25); // the item and count do not matter
        CrimeData none = hold();
        none.mPickpocket = 0;
        EXPECT_EQ(bountyFor(Kind::Pickpocket, none, 100, 1), 0);
    }

    struct PickpocketCrimeTest : ::testing::Test
    {
        Ledger mLedger;
        World mWorld;
        Writer mWriter;
        std::unique_ptr<CrimeSystem> mSystem;
        void SetUp() override
        {
            mWorld.mData[kHold] = hold();
            mSystem = std::make_unique<CrimeSystem>(mLedger, mWorld);
        }
    };

    TEST_F(PickpocketCrimeTest, aCaughtTryCostsTheVictimsFaction)
    {
        mWorld.mActors[7] = victim(kHold);
        mSystem->onPickpocketCaught(7);
        EXPECT_EQ(mLedger.bounty(kHold), 0) << "the bounty waits for the next update";
        EXPECT_EQ(mSystem->pendingCount(), 1u);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.bounty(kHold), 25);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(PickpocketCrimeTest, aCaughtTryMarksNoItemStolenBecauseNothingWasTaken)
    {
        mWorld.mActors[7] = victim(kHold);
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.stolenTotal(), 0);
    }

    TEST_F(PickpocketCrimeTest, aVictimWithoutACrimeFactionCollectsNothing)
    {
        mWorld.mActors[7] = victim(kNullFormId);
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(PickpocketCrimeTest, aTryInSightOfOnlyAnEnemyIsNotACrime)
    {
        CrimeActor a = victim(kHold);
        a.mEnemyOfPlayer = true;
        mWorld.mActors[7] = a;
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(PickpocketCrimeTest, theLogNamesTheCrime)
    {
        mWorld.mActors[7] = victim(kHold);
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        bool found = false;
        for (const std::string& line : mWorld.mLog)
            found = found || line.find("pickpocket witnessed by Hulda") != std::string::npos;
        EXPECT_TRUE(found);
    }

    TEST_F(PickpocketCrimeTest, theQueueIsBounded)
    {
        mWorld.mActors[7] = victim(kHold);
        for (int i = 0; i < 10000; ++i)
            mSystem->onPickpocketCaught(7);
        EXPECT_LT(mSystem->pendingCount(), 10000u);
    }
}
