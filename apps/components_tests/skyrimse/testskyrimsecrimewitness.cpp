// Task 0235: the witness callback of the crime rules (CrimeSystem::setWitnessHook): a crime that counts calls it once
// for each witness (nearest first) with the kind of crime, so the pack can make the witness speak. Over a fake world
// and a fake combat writer; no game data. Written before the hook and run red against a stub (see the stage log of the
// task).
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::crime;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorSpeed;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::DamageReport;
    using openeden::sdk::game::RayFate;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    constexpr FormId kHold = 0x01000100;
    constexpr FormId kSword = 0x01000300;

    CrimeData hold()
    {
        CrimeData c;
        c.mPresent = true;
        c.mMurder = 1000;
        c.mAssault = 40;
        c.mTrespass = 5;
        c.mPickpocket = 25;
        c.mStealMultiplier = 0.5f;
        return c;
    }

    class WitnessWorld final : public CrimeWorld
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
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}
        std::map<std::uint32_t, CrimeActor> mActors;
        std::map<FormId, CrimeData> mData;
    };

    class WitnessWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
                return true;
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest& request, RayHit& out) override
        {
            out = {};
            const double endX = request.x
                + std::sin(static_cast<double>(request.heading)) * std::cos(static_cast<double>(request.pitch))
                    * static_cast<double>(request.range);
            if (mWall && ((request.x < 50.0) != (endX < 50.0)))
                out.fate = RayFate::World;
            return true;
        }
        std::map<std::uint32_t, SubjectState> mStates;
        bool mWall = false;
    };

    struct Seen
    {
        Kind mKind;
        std::uint32_t mWitness;
    };

    struct WitnessHookTest : ::testing::Test
    {
        Ledger mLedger;
        WitnessWorld mWorld;
        WitnessWriter mWriter;
        std::unique_ptr<CrimeSystem> mSystem;
        std::vector<Seen> mSeen;

        void SetUp() override
        {
            mWorld.mData[kHold] = hold();
            mSystem = std::make_unique<CrimeSystem>(mLedger, mWorld);
            mSystem->setWitnessHook([this](Kind kind, ActorHandle witness) {
                mSeen.push_back({ kind, witness.reference });
            });
        }
        void add(std::uint32_t reference, double x, FormId faction = kHold)
        {
            CrimeActor a;
            a.mHandle = { reference, 1 };
            a.mBase = reference;
            a.mCrimeFaction = faction;
            a.mName = "npc" + std::to_string(reference);
            mWorld.mActors[reference] = a;
            SubjectState s;
            s.x = x;
            mWriter.mStates[reference] = s;
        }
        void step(int times = 1)
        {
            for (int i = 0; i < times; ++i)
                mSystem->update(0.5f, mWriter);
        }
    };

    DamageReport blow(std::uint32_t victim, float amount, float healthAfter)
    {
        DamageReport r;
        r.attackerIsPlayer = true;
        r.victim = { victim, 1 };
        r.amount = amount;
        r.healthAfter = healthAfter;
        r.healthMax = 100.f;
        return r;
    }

    TEST_F(WitnessHookTest, aWitnessedTheftTellsTheHookTheWitnessAndTheKind)
    {
        add(1, 400);
        mSystem->onTheft(kSword, 1);
        EXPECT_TRUE(mSeen.empty()) << "the witnesses are judged by the next update";
        step();
        ASSERT_EQ(mSeen.size(), 1u);
        EXPECT_EQ(mSeen[0].mKind, Kind::Theft);
        EXPECT_EQ(mSeen[0].mWitness, 1u);
    }

    TEST_F(WitnessHookTest, everyWitnessIsToldNearestFirst)
    {
        add(7, 900);
        add(3, 300);
        add(5, 600);
        mSystem->onTheft(kSword, 1);
        step();
        ASSERT_EQ(mSeen.size(), 3u);
        EXPECT_EQ(mSeen[0].mWitness, 3u);
        EXPECT_EQ(mSeen[1].mWitness, 5u);
        EXPECT_EQ(mSeen[2].mWitness, 7u);
    }

    TEST_F(WitnessHookTest, aCrimeNobodySeesTellsNobody)
    {
        step();
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_TRUE(mSeen.empty());
    }

    TEST_F(WitnessHookTest, aWallBetweenTheWitnessAndThePlayerHidesTheCrime)
    {
        add(1, 400);
        mWriter.mWall = true;
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_TRUE(mSeen.empty());
    }

    TEST_F(WitnessHookTest, aWitnessWithoutACrimeFactionIsNotCounted)
    {
        add(1, 400, 0);
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_TRUE(mSeen.empty()) << "a crime nobody collects for is no witnessed crime";
        EXPECT_EQ(mLedger.bounty(kHold), 0);
    }

    TEST_F(WitnessHookTest, assaultAndMurderAreTold)
    {
        add(1, 300);
        add(2, 500); // a bystander
        mSystem->onDamage(blow(1, 5.f, 95.f));
        step();
        // the victim and the bystander both saw the assault; the victim is nearer
        ASSERT_EQ(mSeen.size(), 2u);
        EXPECT_EQ(mSeen[0].mKind, Kind::Assault);
        EXPECT_EQ(mSeen[0].mWitness, 1u);
        EXPECT_EQ(mSeen[1].mWitness, 2u);
        mSeen.clear();
        mSystem->onDamage(blow(1, 100.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        step();
        ASSERT_EQ(mSeen.size(), 1u);
        EXPECT_EQ(mSeen[0].mKind, Kind::Murder);
        EXPECT_EQ(mSeen[0].mWitness, 2u);
    }

    TEST_F(WitnessHookTest, aTrespassIsToldOnceEvenThoughItIsJudgedAgain)
    {
        add(1, 400);
        mSystem->onTrespass(0x01000900, Place::Cell);
        step(40);
        std::size_t trespass = 0;
        for (const Seen& s : mSeen)
            if (s.mKind == Kind::Trespass)
                ++trespass;
        EXPECT_EQ(trespass, 1u);
    }

    TEST_F(WitnessHookTest, withNoHookTheBountyIsTheSame)
    {
        add(1, 400);
        mSystem->setWitnessHook({});
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 50);
        EXPECT_TRUE(mSeen.empty());
    }

    TEST_F(WitnessHookTest, aHookThatThrowsDoesNotChangeTheBounty)
    {
        add(1, 400);
        mSystem->setWitnessHook([](Kind, ActorHandle) { throw std::runtime_error("no"); });
        mSystem->onTheft(kSword, 1);
        EXPECT_NO_THROW(step());
        EXPECT_EQ(mLedger.bounty(kHold), 50);
    }
}
