// Task 0204 (Skyrim crime; docs/tasks/0204-skyrim-crime.md): the FACT crime data, the bounty numbers, the ledger and
// its save chunk, the witness rule, and the crime system over a fake world and a fake combat writer. Written from
// the task spec before the code and run red against stubs first (see the stage log of the task). Generated plugins,
// no game data.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
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

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kHold = kSelf | 0x100; // a crime faction
    constexpr FormId kHold2 = kSelf | 0x101;
    constexpr FormId kSword = kSelf | 0x300;
    constexpr FormId kGem = kSelf | 0x301;

    // ---- the FACT crime data ---------------------------------------------------------------------------------------

    LoadOrder defaultOrder()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    ActorRecords parseFaction(const Bytes& subs, bool* ok = nullptr)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("FACT"), 0, record4("FACT", 0, kSelf | 0x400, subs)));
        ActorRecords records;
        Content::Error error;
        const bool parsed
            = parseActorRecordsBytes(bytes.str(), "test.esp", defaultOrder(), ActorLimits(), records, error);
        if (ok != nullptr)
            *ok = parsed;
        return records;
    }

    Bytes crva20()
    {
        // arrest 1, attack on sight 1, murder 1000, assault 40, trespass 5, pickpocket 25, garbage u16 at 10,
        // steal 0.5, escape 100, werewolf 900
        return Bytes().u8(1).u8(1).u16(1000).u16(40).u16(5).u16(25).u16(0x3f00).f32(0.5f).u16(100).u16(900);
    }

    TEST(SkyrimCrimeData, aCrimeFactionKeepsItsValuesAndMarkers)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("CrimeFactionTest")));
        f.append(sub4("CRVA", crva20()));
        f.append(sub4("JAIL", Bytes().u32(kSelf | 0x501)));
        f.append(sub4("WAIT", Bytes().u32(kSelf | 0x502)));
        f.append(sub4("STOL", Bytes().u32(kSelf | 0x503)));
        f.append(sub4("PLCN", Bytes().u32(kSelf | 0x504)));
        f.append(sub4("JOUT", Bytes().u32(kSelf | 0x505)));
        f.append(sub4("CRGR", Bytes().u32(kSelf | 0x506)));
        bool ok = false;
        const ActorRecords records = parseFaction(f, &ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(records.mFactionRecords.size(), 1u);
        const CrimeData& c = records.mFactionRecords[0].mCrime;
        EXPECT_TRUE(c.mPresent);
        EXPECT_TRUE(c.mArrest);
        EXPECT_TRUE(c.mAttackOnSight);
        EXPECT_EQ(c.mMurder, 1000u);
        EXPECT_EQ(c.mAssault, 40u);
        EXPECT_EQ(c.mTrespass, 5u);
        EXPECT_EQ(c.mPickpocket, 25u);
        EXPECT_FLOAT_EQ(c.mStealMultiplier, 0.5f);
        EXPECT_EQ(c.mEscape, 100u);
        EXPECT_EQ(c.mWerewolf, 900u);
        EXPECT_EQ(c.mJail, kSelf | 0x501);
        EXPECT_EQ(c.mWait, kSelf | 0x502);
        EXPECT_EQ(c.mStolenChest, kSelf | 0x503);
        EXPECT_EQ(c.mPlayerChest, kSelf | 0x504);
        EXPECT_EQ(c.mJailOutfit, kSelf | 0x505);
        EXPECT_EQ(c.mGroup, kSelf | 0x506);
    }

    TEST(SkyrimCrimeData, theShorterFormsReadZeroForWhatTheyLack)
    {
        Bytes f16;
        f16.append(sub4("EDID", Bytes().zstring("Short16")));
        f16.append(sub4("CRVA", Bytes().u8(1).u8(0).u16(1000).u16(40).u16(5).u16(25).u16(0).f32(0.5f)));
        const ActorRecords r16 = parseFaction(f16);
        ASSERT_EQ(r16.mFactionRecords.size(), 1u);
        EXPECT_TRUE(r16.mFactionRecords[0].mCrime.mPresent);
        EXPECT_FALSE(r16.mFactionRecords[0].mCrime.mAttackOnSight);
        EXPECT_FLOAT_EQ(r16.mFactionRecords[0].mCrime.mStealMultiplier, 0.5f);
        EXPECT_EQ(r16.mFactionRecords[0].mCrime.mEscape, 0u);
        Bytes f12;
        f12.append(sub4("EDID", Bytes().zstring("Short12")));
        f12.append(sub4("CRVA", Bytes().u8(0).u8(0).u16(7).u16(8).u16(9).u16(10).u16(0)));
        const ActorRecords r12 = parseFaction(f12);
        ASSERT_EQ(r12.mFactionRecords.size(), 1u);
        EXPECT_EQ(r12.mFactionRecords[0].mCrime.mMurder, 7u);
        EXPECT_EQ(r12.mFactionRecords[0].mCrime.mPickpocket, 10u);
        EXPECT_FLOAT_EQ(r12.mFactionRecords[0].mCrime.mStealMultiplier, 0.f);
    }

    TEST(SkyrimCrimeData, aWrongSizedCrimeSubrecordIsOddAndDecodesToNothing)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Odd")));
        f.append(sub4("CRVA", Bytes().u32(1).u32(2).u32(3).u32(4).u32(5).u32(6)));
        f.append(sub4("JAIL", Bytes().u16(1)));
        const ActorRecords r = parseFaction(f);
        ASSERT_EQ(r.mFactionRecords.size(), 1u);
        const CrimeData& c = r.mFactionRecords[0].mCrime;
        EXPECT_FALSE(c.mPresent);
        EXPECT_EQ(c.mJail, kNullFormId);
        EXPECT_EQ(r.mOddBy.at("FACT.CRVA"), 1u);
        EXPECT_EQ(r.mOddBy.at("FACT.JAIL"), 1u);
    }

    TEST(SkyrimCrimeData, aFactionWithoutCrimeDataHasNone)
    {
        const ActorRecords r = parseFaction(sub4("EDID", Bytes().zstring("Plain")));
        ASSERT_EQ(r.mFactionRecords.size(), 1u);
        EXPECT_FALSE(r.mFactionRecords[0].mCrime.mPresent);
        EXPECT_EQ(r.mFactionRecords[0].mCrime.mJail, kNullFormId);
    }

    // ---- bounty numbers -------------------------------------------------------------------------------------------

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

    TEST(SkyrimCrimeBounty, theFlatCrimesAreTheFactionsNumbers)
    {
        const CrimeData c = hold();
        EXPECT_EQ(bountyFor(Kind::Murder, c, 0, 0), 1000);
        EXPECT_EQ(bountyFor(Kind::Assault, c, 0, 0), 40);
        EXPECT_EQ(bountyFor(Kind::Trespass, c, 0, 0), 5);
    }

    TEST(SkyrimCrimeBounty, aTheftCostsValueTimesCountTimesTheMultiplierRoundedUp)
    {
        const CrimeData c = hold();
        EXPECT_EQ(bountyFor(Kind::Theft, c, 100, 1), 50);
        EXPECT_EQ(bountyFor(Kind::Theft, c, 7, 1), 4); // 3.5 up
        EXPECT_EQ(bountyFor(Kind::Theft, c, 10, 3), 15);
    }

    TEST(SkyrimCrimeBounty, aTheftOfSomethingWorthAnythingCostsAtLeastOne)
    {
        const CrimeData c = hold();
        EXPECT_EQ(bountyFor(Kind::Theft, c, 1, 1), 1);
        EXPECT_EQ(bountyFor(Kind::Theft, c, 0, 5), 0);
        EXPECT_EQ(bountyFor(Kind::Theft, c, 5, 0), 0);
    }

    TEST(SkyrimCrimeBounty, badInputCostsNothing)
    {
        CrimeData c = hold();
        EXPECT_EQ(bountyFor(Kind::Theft, c, -5, 1), 0);
        EXPECT_EQ(bountyFor(Kind::Theft, c, 5, -1), 0);
        c.mStealMultiplier = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(bountyFor(Kind::Theft, c, 100, 1), 0);
        c.mStealMultiplier = -1.f;
        EXPECT_EQ(bountyFor(Kind::Theft, c, 100, 1), 0);
        c.mStealMultiplier = std::numeric_limits<float>::infinity();
        EXPECT_EQ(bountyFor(Kind::Theft, c, 100, 1), 0);
    }

    TEST(SkyrimCrimeBounty, aHugeTheftIsClamped)
    {
        CrimeData c = hold();
        c.mStealMultiplier = 1.f;
        EXPECT_EQ(bountyFor(Kind::Theft, c, 2000000000, 2000000000), kMaxBounty);
    }

    TEST(SkyrimCrimeBounty, aFactionWithoutCrimeDataChargesNothing)
    {
        CrimeData none;
        EXPECT_EQ(bountyFor(Kind::Murder, none, 0, 0), 0);
        EXPECT_EQ(bountyFor(Kind::Theft, none, 100, 1), 0);
    }

    TEST(SkyrimCrimeBounty, kindsHaveNames)
    {
        EXPECT_EQ(kindName(Kind::Theft), "theft");
        EXPECT_EQ(kindName(Kind::Assault), "assault");
        EXPECT_EQ(kindName(Kind::Murder), "murder");
        EXPECT_EQ(kindName(Kind::Trespass), "trespass");
    }

    // ---- the ledger -------------------------------------------------------------------------------------------------

    TEST(SkyrimCrimeLedger, bountiesAddAndClearPerFaction)
    {
        Ledger l;
        EXPECT_TRUE(l.addBounty(kHold, 40));
        EXPECT_TRUE(l.addBounty(kHold, 60));
        EXPECT_TRUE(l.addBounty(kHold2, 5));
        EXPECT_EQ(l.bounty(kHold), 100);
        EXPECT_EQ(l.bounty(kHold2), 5);
        EXPECT_EQ(l.totalBounty(), 105);
        l.clearBounty(kHold);
        EXPECT_EQ(l.bounty(kHold), 0);
        EXPECT_EQ(l.totalBounty(), 5);
        l.clearAllBounties();
        EXPECT_EQ(l.totalBounty(), 0);
    }

    TEST(SkyrimCrimeLedger, refusesNothingToAddAndClampsAHugeBounty)
    {
        Ledger l;
        EXPECT_FALSE(l.addBounty(kNullFormId, 10));
        EXPECT_FALSE(l.addBounty(kHold, 0));
        EXPECT_FALSE(l.addBounty(kHold, -3));
        EXPECT_EQ(l.totalBounty(), 0);
        EXPECT_TRUE(l.addBounty(kHold, kMaxBounty));
        EXPECT_TRUE(l.addBounty(kHold, 5000));
        EXPECT_EQ(l.bounty(kHold), kMaxBounty);
    }

    TEST(SkyrimCrimeLedger, aChangeMovesTheRevision)
    {
        Ledger l;
        const std::uint32_t r0 = l.revision();
        EXPECT_TRUE(l.addBounty(kHold, 1));
        EXPECT_GT(l.revision(), r0);
        const std::uint32_t r1 = l.revision();
        EXPECT_FALSE(l.addBounty(kHold, 0));
        EXPECT_EQ(l.revision(), r1);
    }

    TEST(SkyrimCrimeLedger, stolenItemsAreCountedAndForgotten)
    {
        Ledger l;
        EXPECT_TRUE(l.markStolen(kSword, 2));
        EXPECT_TRUE(l.markStolen(kSword, 1));
        EXPECT_TRUE(l.markStolen(kGem, 4));
        EXPECT_EQ(l.stolen(kSword), 3);
        EXPECT_EQ(l.stolenTotal(), 7);
        EXPECT_EQ(l.forgetStolen(kSword, 2), 2);
        EXPECT_EQ(l.stolen(kSword), 1);
        EXPECT_EQ(l.forgetStolen(kSword, 5), 1);
        EXPECT_EQ(l.stolen(kSword), 0);
        EXPECT_EQ(l.stolenItems().count(kSword), 0u);
        l.clearStolen();
        EXPECT_EQ(l.stolenTotal(), 0);
        EXPECT_FALSE(l.markStolen(kNullFormId, 1));
        EXPECT_FALSE(l.markStolen(kSword, 0));
        EXPECT_FALSE(l.markStolen(kSword, -1));
    }

    class MemWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemReader(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }
        std::vector<unsigned char> mBytes;
        std::size_t mPos = 0;
    };

    Ledger sampleLedger()
    {
        Ledger l;
        l.addBounty(kHold2, 5);
        l.addBounty(kHold, 100);
        l.markStolen(kGem, 4);
        l.markStolen(kSword, 1);
        return l;
    }

    TEST(SkyrimCrimeLedger, theChunkRoundTripsAndIsCanonical)
    {
        const Ledger a = sampleLedger();
        MemWriter w;
        a.write(w);
        MemReader r(w.mBytes);
        Ledger b;
        std::string error;
        ASSERT_TRUE(b.read(r, error)) << error;
        EXPECT_EQ(r.remaining(), 0u);
        EXPECT_EQ(b.bounty(kHold), 100);
        EXPECT_EQ(b.bounty(kHold2), 5);
        EXPECT_EQ(b.stolen(kGem), 4);
        EXPECT_EQ(b.stolen(kSword), 1);
        MemWriter w2;
        b.write(w2);
        EXPECT_EQ(w.mBytes, w2.mBytes);
    }

    TEST(SkyrimCrimeLedger, anEmptyLedgerWritesEightBytes)
    {
        Ledger a;
        MemWriter w;
        a.write(w);
        EXPECT_EQ(w.mBytes.size(), 8u);
    }

    TEST(SkyrimCrimeLedger, everyTruncationIsRefusedAndChangesNothing)
    {
        const Ledger a = sampleLedger();
        MemWriter w;
        a.write(w);
        for (std::size_t n = 0; n < w.mBytes.size(); ++n)
        {
            MemReader r(std::vector<unsigned char>(w.mBytes.begin(), w.mBytes.begin() + static_cast<long>(n)));
            Ledger b;
            b.addBounty(kHold2, 77);
            std::string error;
            EXPECT_FALSE(b.read(r, error)) << n;
            EXPECT_EQ(b.bounty(kHold2), 77) << n;
            EXPECT_EQ(b.bounty(kHold), 0) << n;
        }
    }

    TEST(SkyrimCrimeLedger, aHostileCountIsRefused)
    {
        MemWriter w;
        w.writeValue(static_cast<std::uint32_t>(0xFFFFFFFFu));
        w.writeValue(static_cast<std::uint32_t>(0));
        MemReader r(w.mBytes);
        Ledger b;
        std::string error;
        EXPECT_FALSE(b.read(r, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimCrimeLedger, aNegativeNullOrUnsortedEntryIsRefused)
    {
        const auto make = [](FormId f1, std::int32_t g1, FormId f2, std::int32_t g2) {
            MemWriter w;
            w.writeValue(static_cast<std::uint32_t>(2));
            w.writeValue(static_cast<std::uint32_t>(f1));
            w.writeValue(g1);
            w.writeValue(static_cast<std::uint32_t>(f2));
            w.writeValue(g2);
            w.writeValue(static_cast<std::uint32_t>(0));
            return w.mBytes;
        };
        for (const auto& bytes : { make(kHold, -1, kHold2, 5), make(kHold, 1, kHold, 5), make(kHold2, 1, kHold, 5),
                 make(0, 1, kHold, 5), make(kHold, 0, kHold2, 5), make(kHold, kMaxBounty + 1, kHold2, 5) })
        {
            MemReader r(bytes);
            Ledger b;
            std::string error;
            EXPECT_FALSE(b.read(r, error));
            EXPECT_EQ(b.totalBounty(), 0);
        }
    }

    TEST(SkyrimCrimeLedger, theChunkHandlerChecksTheVersionAndReports)
    {
        Ledger a = sampleLedger();
        LedgerChunk chunk(a);
        EXPECT_EQ(chunk.chunkName(), "crime");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemWriter w;
        chunk.write(w);
        Ledger b;
        LedgerChunk other(b);
        MemReader r(w.mBytes);
        EXPECT_TRUE(other.read(r, 1));
        EXPECT_EQ(b.bounty(kHold), 100);
        MemReader r2(w.mBytes);
        Ledger c;
        LedgerChunk third(c);
        EXPECT_FALSE(third.read(r2, 2));
        EXPECT_FALSE(third.refusal().empty());
    }

    // ---- witnesses ----------------------------------------------------------------------------------------------

    CrimeActor npc(std::uint32_t reference, FormId faction)
    {
        CrimeActor a;
        a.mHandle = { reference, 1 };
        a.mBase = reference;
        a.mCrimeFaction = faction;
        a.mName = "npc" + std::to_string(reference);
        return a;
    }

    TEST(SkyrimCrimeWitness, anActorWithinRangeAndInSightSeesTheCrime)
    {
        const Settings s;
        EXPECT_TRUE(witnessSees(s, npc(1, kHold), 500, 0, 0, 0, 0, 0, true));
    }

    TEST(SkyrimCrimeWitness, deadEnemyAndFightingActorsReportNothing)
    {
        const Settings s;
        CrimeActor dead = npc(1, kHold);
        dead.mDead = true;
        EXPECT_FALSE(witnessSees(s, dead, 100, 0, 0, 0, 0, 0, true));
        CrimeActor enemy = npc(2, kHold);
        enemy.mEnemyOfPlayer = true;
        EXPECT_FALSE(witnessSees(s, enemy, 100, 0, 0, 0, 0, 0, true));
        CrimeActor fighting = npc(3, kHold);
        fighting.mEngagedAgainstPlayer = true;
        EXPECT_FALSE(witnessSees(s, fighting, 100, 0, 0, 0, 0, 0, true));
    }

    TEST(SkyrimCrimeWitness, distanceHeightAndWallsLimitIt)
    {
        const Settings s;
        EXPECT_FALSE(witnessSees(s, npc(1, kHold), s.mWitnessDistance + 1.f, 0, 0, 0, 0, 0, true));
        EXPECT_TRUE(witnessSees(s, npc(1, kHold), s.mWitnessDistance - 1.f, 0, 0, 0, 0, 0, true));
        EXPECT_FALSE(witnessSees(s, npc(1, kHold), 100, 0, s.mWitnessHeight + 1.f, 0, 0, 0, true));
        EXPECT_FALSE(witnessSees(s, npc(1, kHold), 100, 0, 0, 0, 0, 0, false));
    }

    TEST(SkyrimCrimeWitness, nonFinitePositionsSeeNothing)
    {
        const Settings s;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(witnessSees(s, npc(1, kHold), nan, 0, 0, 0, 0, 0, true));
        EXPECT_FALSE(witnessSees(s, npc(1, kHold), 0, 0, 0, 0, nan, 0, true));
    }

    TEST(SkyrimCrimeWitness, onlyACalmLivingActorIsACrimeVictim)
    {
        EXPECT_TRUE(isCrimeVictim(npc(1, kHold)));
        CrimeActor a = npc(2, kHold);
        a.mDead = true;
        EXPECT_FALSE(isCrimeVictim(a));
        a = npc(3, kHold);
        a.mEnemyOfPlayer = true;
        EXPECT_FALSE(isCrimeVictim(a));
        a = npc(4, kHold);
        a.mEngagedAgainstPlayer = true;
        EXPECT_FALSE(isCrimeVictim(a));
    }

    TEST(SkyrimCrimeState, namesAreStable)
    {
        EXPECT_EQ(stateName(State::None), "none");
        EXPECT_EQ(stateName(State::Approach), "approach");
        EXPECT_EQ(stateName(State::Offer), "offer");
        EXPECT_EQ(stateName(State::Jailed), "jailed");
        EXPECT_EQ(stateName(State::Resisted), "resisted");
    }

    // ---- the system over a fake world ---------------------------------------------------------------------------

    class FakeWorld final : public CrimeWorld
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
        std::string factionName(FormId faction) override { return "Hold" + std::to_string(faction & 0xFF); }
        std::int32_t itemValue(FormId item) override
        {
            const auto it = mValues.find(item);
            return it == mValues.end() ? 0 : it->second;
        }
        std::int32_t playerGold() override { return mGold; }
        bool takeGold(std::int32_t amount) override
        {
            if (amount > mGold)
                return false;
            mGold -= amount;
            return true;
        }
        std::int32_t takeItem(FormId item, std::int32_t count) override
        {
            std::int32_t& held = mHeld[item];
            const std::int32_t taken = std::min(held, count);
            held -= taken;
            mTaken.push_back({ item, taken });
            return taken;
        }
        std::int32_t itemCount(FormId item) override { return mHeld[item]; }
        void guardGoTo(ActorHandle g, double x, double y, double z, ActorSpeed speed) override
        {
            mGoTo.push_back({ g.reference, x, y, z, speed });
        }
        void guardHold(ActorHandle g) override { mHolds.push_back(g.reference); }
        void guardRelease(ActorHandle g) override { mReleased.push_back(g.reference); }
        void guardAttack(ActorHandle g) override { mAttack.push_back(g.reference); }
        void log(const std::string& line) override { mLog.push_back(line); }

        struct GoTo
        {
            std::uint32_t mActor;
            double x, y, z;
            ActorSpeed mSpeed;
        };
        std::map<std::uint32_t, CrimeActor> mActors;
        std::map<FormId, CrimeData> mData;
        std::map<FormId, std::int32_t> mValues;
        std::map<FormId, std::int32_t> mHeld;
        std::vector<std::pair<FormId, std::int32_t>> mTaken;
        std::int32_t mGold = 0;
        std::vector<GoTo> mGoTo;
        std::vector<std::uint32_t> mHolds, mReleased, mAttack;
        std::vector<std::string> mLog;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayer;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest& request, RayHit& out) override
        {
            ++mRays;
            out = {};
            const double endX = request.x
                + std::sin(static_cast<double>(request.heading)) * std::cos(static_cast<double>(request.pitch))
                    * static_cast<double>(request.range);
            if (mWall && ((request.x < mWallX) != (endX < mWallX)))
                out.fate = RayFate::World;
            return true;
        }
        void put(std::uint32_t reference, double x, double y = 0.0, double z = 0.0)
        {
            SubjectState s;
            s.x = x;
            s.y = y;
            s.z = z;
            mStates[reference] = s;
        }
        std::map<std::uint32_t, SubjectState> mStates;
        SubjectState mPlayer;
        bool mWall = false;
        double mWallX = 50.0;
        std::size_t mRays = 0;
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

    struct CrimeSystemTest : ::testing::Test
    {
        Ledger mLedger;
        FakeWorld mWorld;
        FakeWriter mWriter;
        std::unique_ptr<CrimeSystem> mSystem;

        void SetUp() override
        {
            mWorld.mData[kHold] = hold();
            mWorld.mData[kHold2] = hold();
            mWorld.mData[kHold2].mMurder = 500;
            mWorld.mValues[kSword] = 100;
            mWorld.mValues[kGem] = 7;
            mSystem = std::make_unique<CrimeSystem>(mLedger, mWorld);
        }
        CrimeActor& add(std::uint32_t reference, FormId faction, double x, bool guard = false)
        {
            CrimeActor a = npc(reference, faction);
            a.mGuard = guard;
            mWorld.mActors[reference] = a;
            mWriter.put(reference, x);
            return mWorld.mActors[reference];
        }
        void step(int times = 1, float seconds = 0.5f)
        {
            for (int i = 0; i < times; ++i)
                mSystem->update(seconds, mWriter);
        }
    };

    TEST_F(CrimeSystemTest, aTheftInSightOfAWitnessCostsTheWitnessesFactionAndMarksTheItem)
    {
        add(1, kHold, 400);
        mSystem->onTheft(kSword, 1);
        EXPECT_EQ(mLedger.stolen(kSword), 1);
        EXPECT_EQ(mLedger.bounty(kHold), 0) << "the bounty waits for the next update";
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 50);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(CrimeSystemTest, aTheftNobodySeesMarksTheItemAndCostsNothing)
    {
        add(1, kHold, 400000);
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.stolen(kSword), 1);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, aWallBetweenThePlayerAndTheWitnessHidesTheCrime)
    {
        add(1, kHold, 400);
        mWriter.mWall = true;
        mWriter.mWallX = 200;
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
        EXPECT_GT(mWriter.mRays, 0u);
    }

    TEST_F(CrimeSystemTest, aWitnessWithoutACrimeFactionCollectsNothing)
    {
        add(1, kNullFormId, 400);
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, aFactionThatIsNotACrimeFactionCollectsNothing)
    {
        add(1, kSelf | 0x999, 400);
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, twoWitnessesOfOneFactionChargeItOnceAndTwoFactionsChargeBoth)
    {
        add(1, kHold, 300);
        add(2, kHold, 500);
        add(3, kHold2, 600);
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 50);
        EXPECT_EQ(mLedger.bounty(kHold2), 50);
    }

    TEST_F(CrimeSystemTest, aHostileOrDeadActorReportsNothing)
    {
        add(1, kHold, 300).mEnemyOfPlayer = true;
        add(2, kHold, 300).mDead = true;
        add(3, kHold, 300).mEngagedAgainstPlayer = true;
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, raysPerCrimeAreCapped)
    {
        for (std::uint32_t i = 1; i <= 40; ++i)
            add(i, kHold, 100 + static_cast<double>(i));
        mSystem->onTheft(kSword, 1);
        step();
        EXPECT_LE(mWriter.mRays, mSystem->settings().mMaxRays);
        EXPECT_EQ(mLedger.bounty(kHold), 50);
    }

    TEST_F(CrimeSystemTest, hittingACalmNpcIsAssaultOnceHoweverOftenItIsHit)
    {
        add(1, kHold, 300);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        mSystem->onDamage(blow(1, 5.f, 90.f));
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 40);
        mSystem->onDamage(blow(1, 5.f, 85.f));
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 40);
    }

    TEST_F(CrimeSystemTest, hittingAnEnemyOrAFighterIsNoCrime)
    {
        add(1, kHold, 300).mEnemyOfPlayer = true;
        add(2, kHold, 300).mEngagedAgainstPlayer = true;
        add(9, kHold, 400);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        mSystem->onDamage(blow(2, 5.f, 95.f));
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, aBlowThatIsNotThePlayersOrIsAtThePlayerIsIgnored)
    {
        add(1, kHold, 300);
        DamageReport npcHit = blow(1, 5.f, 95.f);
        npcHit.attackerIsPlayer = false;
        npcHit.attacker = { 7, 1 };
        mSystem->onDamage(npcHit);
        DamageReport atPlayer;
        atPlayer.victimIsPlayer = true;
        atPlayer.attackerIsPlayer = false;
        mSystem->onDamage(atPlayer);
        DamageReport none = blow(1, 0.f, 100.f); // a heal or a miss
        mSystem->onDamage(none);
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, aLethalBlowOnACalmNpcIsMurderOnly)
    {
        add(1, kHold, 300);
        add(2, kHold, 400); // a witness
        mSystem->onDamage(blow(1, 100.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 1000);
    }

    TEST_F(CrimeSystemTest, killingAfterAnAssaultAddsMurder)
    {
        add(1, kHold, 300);
        add(2, kHold, 400);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 40);
        mSystem->onDamage(blow(1, 95.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 1040);
    }

    TEST_F(CrimeSystemTest, theDeathOfAnEnemyOrOfSomeoneTheNpcsKilledIsNoCrime)
    {
        add(1, kHold, 300).mEnemyOfPlayer = true;
        add(2, kHold, 400);
        mSystem->onDamage(blow(1, 100.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        mSystem->onDeath({ 77, 1 }); // never hit by the player
        step();
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(CrimeSystemTest, killingSomeoneWhoHurtThePlayerIsSelfDefenceNotMurder)
    {
        add(1, kHold, 300);
        add(2, kHold, 400);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        DamageReport back;
        back.victimIsPlayer = true;
        back.attackerIsPlayer = false;
        back.attacker = { 1, 1 };
        back.amount = 7.f;
        mSystem->onDamage(back);
        mSystem->onDamage(blow(1, 95.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 40) << "the assault, no murder";
    }

    TEST_F(CrimeSystemTest, aVictimWhoOnlyFledAndWasEngagedIsStillMurdered)
    {
        add(1, kHold, 300);
        add(2, kHold, 400);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        mWorld.mActors[1].mEngagedAgainstPlayer = true; // the AI took it over (it flees); it never hurt the player
        mSystem->onDamage(blow(1, 95.f, 0.f));
        mWorld.mActors[1].mDead = true;
        mSystem->onDeath({ 1, 1 });
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 1040);
    }

    TEST_F(CrimeSystemTest, theVictimAloneWitnessesAnAssault)
    {
        add(1, kHold, 300);
        mSystem->onDamage(blow(1, 5.f, 95.f));
        step();
        EXPECT_EQ(mLedger.bounty(kHold), 40);
    }

    TEST_F(CrimeSystemTest, noGuardMeansNoOffer)
    {
        mLedger.addBounty(kHold, 100);
        add(1, kHold, 300);
        step(4);
        EXPECT_EQ(mSystem->state(), State::None);
        EXPECT_TRUE(mWorld.mGoTo.empty());
    }

    TEST_F(CrimeSystemTest, aGuardOfTheFactionWalksToThePlayerThenOffers)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        step();
        EXPECT_EQ(mSystem->state(), State::Approach);
        ASSERT_FALSE(mWorld.mGoTo.empty());
        EXPECT_EQ(mWorld.mGoTo.back().mActor, 10u);
        EXPECT_EQ(mWorld.mGoTo.back().mSpeed, ActorSpeed::Run);
        mWriter.put(10, 100);
        step();
        EXPECT_EQ(mSystem->state(), State::Offer);
        EXPECT_EQ(mWorld.mHolds.back(), 10u);
        EXPECT_EQ(mSystem->guardName(), "npc10");
        EXPECT_EQ(mSystem->offerBounty(), 100);
        EXPECT_EQ(mSystem->offerFaction(), kHold);
        EXPECT_FALSE(mSystem->message().empty());
    }

    TEST_F(CrimeSystemTest, aNearGuardWalksInsteadOfRunning)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 400, true);
        step();
        ASSERT_FALSE(mWorld.mGoTo.empty());
        EXPECT_EQ(mWorld.mGoTo.back().mSpeed, ActorSpeed::Walk);
    }

    TEST_F(CrimeSystemTest, aGuardOfAnotherFactionOrBeyondTheRadiusIsNotCalled)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold2, 1000, true);
        add(11, kHold, 100000, true);
        add(12, kHold, 1000, false);
        step(3);
        EXPECT_EQ(mSystem->state(), State::None);
        EXPECT_TRUE(mWorld.mGoTo.empty());
    }

    TEST_F(CrimeSystemTest, theNearestGuardIsChosen)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        add(11, kHold, 1500, true);
        step();
        ASSERT_FALSE(mWorld.mGoTo.empty());
        EXPECT_EQ(mWorld.mGoTo.back().mActor, 11u);
    }

    TEST_F(CrimeSystemTest, aGuardThatWalksIsToldWhereThePlayerIsWhenThePlayerMoves)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        step();
        const std::size_t first = mWorld.mGoTo.size();
        step(); // the player did not move: no new goal
        EXPECT_EQ(mWorld.mGoTo.size(), first);
        mWriter.mPlayer.x = 500.0;
        step(2);
        EXPECT_GT(mWorld.mGoTo.size(), first);
        EXPECT_NEAR(mWorld.mGoTo.back().x, 500.0, 1e-6);
    }

    TEST_F(CrimeSystemTest, aGuardThatNeverArrivesGivesUp)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        step();
        ASSERT_EQ(mSystem->state(), State::Approach);
        step(42, 1.f); // the approach timeout is 40 s, the cooldown that follows 20 s
        EXPECT_NE(mSystem->state(), State::Approach);
        EXPECT_EQ(mWorld.mReleased.back(), 10u);
    }

    TEST_F(CrimeSystemTest, aGuardThatIsGoneIsReplacedOrTheApproachEnds)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        step();
        ASSERT_EQ(mSystem->state(), State::Approach);
        mWorld.mActors.erase(10);
        add(11, kHold, 2000, true);
        step(2);
        EXPECT_EQ(mSystem->state(), State::Approach);
        EXPECT_EQ(mWorld.mGoTo.back().mActor, 11u);
        mWorld.mActors.clear();
        step(2);
        EXPECT_EQ(mSystem->state(), State::None);
    }

    TEST_F(CrimeSystemTest, aDeadGuardDoesNotOffer)
    {
        mLedger.addBounty(kHold, 100);
        add(10, kHold, 3000, true);
        step();
        mWorld.mActors[10].mDead = true;
        mWriter.put(10, 100);
        step(2);
        EXPECT_NE(mSystem->state(), State::Offer);
    }

    struct OfferTest : CrimeSystemTest
    {
        void SetUp() override
        {
            CrimeSystemTest::SetUp();
            mLedger.addBounty(kHold, 100);
            mLedger.markStolen(kSword, 1);
            mWorld.mHeld[kSword] = 1;
            add(10, kHold, 3000, true);
            step();
            mWriter.put(10, 100);
            step();
            ASSERT_EQ(mSystem->state(), State::Offer);
        }
    };

    TEST_F(OfferTest, payTakesTheGoldClearsTheBountyAndConfiscatesTheStolenGoods)
    {
        mWorld.mGold = 150;
        EXPECT_TRUE(mSystem->canPay());
        EXPECT_EQ(mSystem->choose("pay"), ChoiceStatus::Ok);
        EXPECT_EQ(mWorld.mGold, 50);
        EXPECT_EQ(mLedger.bounty(kHold), 0);
        EXPECT_EQ(mLedger.stolenTotal(), 0);
        EXPECT_EQ(mWorld.mHeld[kSword], 0);
        EXPECT_EQ(mSystem->state(), State::None);
        EXPECT_EQ(mWorld.mReleased.back(), 10u);
    }

    TEST_F(OfferTest, payWithoutEnoughGoldIsRefusedAndTheOfferStands)
    {
        mWorld.mGold = 99;
        EXPECT_FALSE(mSystem->canPay());
        EXPECT_EQ(mSystem->choose(Choice::Pay), ChoiceStatus::NotEnoughGold);
        EXPECT_EQ(mWorld.mGold, 99);
        EXPECT_EQ(mLedger.bounty(kHold), 100);
        EXPECT_EQ(mSystem->state(), State::Offer);
    }

    TEST_F(OfferTest, resistTurnsTheGuardHostileAndKeepsTheBounty)
    {
        EXPECT_EQ(mSystem->choose("resist"), ChoiceStatus::Ok);
        EXPECT_EQ(mSystem->state(), State::Resisted);
        ASSERT_FALSE(mWorld.mAttack.empty());
        EXPECT_EQ(mWorld.mAttack.back(), 10u);
        EXPECT_EQ(mLedger.bounty(kHold), 100);
        step(10);
        EXPECT_NE(mSystem->state(), State::Offer) << "a resisted bounty does not offer again at once";
    }

    TEST_F(OfferTest, otherGuardsOfTheFactionJoinWhenThePlayerResists)
    {
        add(11, kHold, 2000, true);
        add(12, kHold2, 2000, true);
        EXPECT_EQ(mSystem->choose("resist"), ChoiceStatus::Ok);
        const auto& a = mWorld.mAttack;
        EXPECT_NE(std::find(a.begin(), a.end(), 11u), a.end());
        EXPECT_EQ(std::find(a.begin(), a.end(), 12u), a.end());
    }

    TEST_F(OfferTest, jailClearsTheBountyConfiscatesAndHoldsThePlayerUntilReleased)
    {
        EXPECT_EQ(mSystem->choose("jail"), ChoiceStatus::Ok);
        EXPECT_EQ(mSystem->state(), State::Jailed);
        EXPECT_EQ(mSystem->jailFaction(), kHold);
        EXPECT_EQ(mLedger.bounty(kHold), 0);
        EXPECT_EQ(mLedger.stolenTotal(), 0);
        EXPECT_EQ(mWorld.mHeld[kSword], 0);
        step(30, 1.f);
        EXPECT_EQ(mSystem->state(), State::Jailed) << "only released() ends it";
        mSystem->released();
        EXPECT_EQ(mSystem->state(), State::None);
        EXPECT_EQ(mSystem->jailFaction(), kNullFormId);
    }

    TEST_F(OfferTest, anUnknownChoiceChangesNothing)
    {
        EXPECT_EQ(mSystem->choose("bribe"), ChoiceStatus::Unknown);
        EXPECT_EQ(mSystem->state(), State::Offer);
    }

    TEST_F(OfferTest, anOfferThatIsNotAnsweredLapses)
    {
        step(125, 1.f); // lapses after 120 s; the cooldown after that is 20 s
        EXPECT_EQ(mSystem->state(), State::None);
        EXPECT_EQ(mLedger.bounty(kHold), 100);
    }

    TEST_F(OfferTest, afterAPaymentTheSameFactionWaitsForTheCooldown)
    {
        mWorld.mGold = 500;
        ASSERT_EQ(mSystem->choose("pay"), ChoiceStatus::Ok);
        mLedger.addBounty(kHold, 40);
        step(4, 0.5f);
        EXPECT_EQ(mSystem->state(), State::None);
        step(60, 0.5f);
        EXPECT_NE(mSystem->state(), State::None);
    }

    TEST_F(CrimeSystemTest, aChoiceWithNoOfferIsRefused)
    {
        EXPECT_EQ(mSystem->choose("pay"), ChoiceStatus::NoOffer);
        EXPECT_EQ(mSystem->choose("jail"), ChoiceStatus::NoOffer);
        EXPECT_EQ(mSystem->choose("resist"), ChoiceStatus::NoOffer);
    }

    TEST_F(CrimeSystemTest, aHugeBountyMakesTheGuardsAttackInsteadOfOffering)
    {
        mLedger.addBounty(kHold, mSystem->settings().mAttackOnSightBounty);
        add(10, kHold, 1000, true);
        step(2);
        EXPECT_EQ(mSystem->state(), State::Resisted);
        ASSERT_FALSE(mWorld.mAttack.empty());
        EXPECT_EQ(mWorld.mAttack.back(), 10u);
    }

    TEST_F(CrimeSystemTest, twoRunsOfTheSameCrimesGiveTheSameTraceAndLedger)
    {
        const auto run = [](FakeWorld& world, Ledger& ledger) {
            FakeWriter writer;
            world.mData[kHold] = hold();
            world.mValues[kSword] = 100;
            CrimeSystem system(ledger, world);
            for (std::uint32_t i = 1; i <= 6; ++i)
            {
                CrimeActor a = npc(i, kHold);
                a.mGuard = i > 4;
                world.mActors[i] = a;
                writer.put(i, 300.0 * static_cast<double>(i));
            }
            system.onTheft(kSword, 2);
            system.onDamage(blow(2, 5.f, 95.f));
            for (int i = 0; i < 40; ++i)
            {
                system.update(0.5f, writer);
                writer.put(5, std::max(100.0, 1500.0 - 100.0 * i));
            }
        };
        FakeWorld w1, w2;
        Ledger l1, l2;
        run(w1, l1);
        run(w2, l2);
        EXPECT_EQ(w1.mLog, w2.mLog);
        EXPECT_FALSE(w1.mLog.empty());
        EXPECT_EQ(l1.bounties(), l2.bounties());
        EXPECT_EQ(w1.mGoTo.size(), w2.mGoTo.size());
    }

    TEST_F(CrimeSystemTest, aLogLineNamesTheCrimeAndTheFaction)
    {
        add(1, kHold, 400);
        mSystem->onTheft(kSword, 1);
        step();
        bool found = false;
        for (const std::string& line : mWorld.mLog)
            if (line.rfind("CRIME ", 0) == 0 && line.find("theft") != std::string::npos)
                found = true;
        EXPECT_TRUE(found);
    }
}
