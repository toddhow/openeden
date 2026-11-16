// Task 0213 (docs/tasks/0213-skyrim-clock-trespass-benches.md, section 2): trespass for the player. The pure rules
// (which cells are a trespass, whose things are the player's), the crime system over a fake world and combat writer (a
// trespass is judged later by the witness rule, charged once per visit, a bed once per repeat window), the crime
// service's two new bindings, the CELL owner decode and the world view's cell access. Written from the spec before the
// code and run red against stubs first. Generated plugins, no game data.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/crime_service.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::crime;
    namespace fs = std::filesystem;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorSpeed;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::RayFate;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kHold = kSelf | 0x100;
    constexpr FormId kOwnerNpc = kSelf | 0x500;
    constexpr FormId kOwnerFaction = kSelf | 0x501;
    constexpr FormId kBedA = kSelf | 0x600;
    constexpr FormId kBedB = kSelf | 0x601;
    constexpr FormId kCellA = kSelf | 0x700;

    // ---- the pure rules ------------------------------------------------------------------------------------------

    CellAccess access(bool interior, bool pub, FormId owner)
    {
        CellAccess a;
        a.mKnown = true;
        a.mInterior = interior;
        a.mPublic = pub;
        a.mOwner = owner;
        return a;
    }

    TEST(SkyrimTrespassRules, thePlayersOwnThingsAreTheNpcAndTheFactionOfThePlayer)
    {
        EXPECT_TRUE(isPlayersOwner(kPlayerNpcId));
        EXPECT_TRUE(isPlayersOwner(0x00000DB1)); // PlayerFaction
        EXPECT_FALSE(isPlayersOwner(kOwnerNpc));
        EXPECT_FALSE(isPlayersOwner(kOwnerFaction));
        EXPECT_FALSE(isPlayersOwner(kNullFormId));
    }

    TEST(SkyrimTrespassRules, aPrivateOwnedInteriorIsATrespassForAPersonOrAFactionOwner)
    {
        EXPECT_TRUE(cellEntryIsTrespass(access(true, false, kOwnerNpc)));
        EXPECT_TRUE(cellEntryIsTrespass(access(true, false, kOwnerFaction)));
    }

    TEST(SkyrimTrespassRules, aPublicCellIsNeverATrespassWhoeverOwnsIt)
    {
        EXPECT_FALSE(cellEntryIsTrespass(access(true, true, kOwnerNpc)));
        EXPECT_FALSE(cellEntryIsTrespass(access(true, true, kOwnerFaction)));
    }

    TEST(SkyrimTrespassRules, anUnownedCellAnExteriorAndAnUnknownCellAreNeverATrespass)
    {
        EXPECT_FALSE(cellEntryIsTrespass(access(true, false, kNullFormId)));
        EXPECT_FALSE(cellEntryIsTrespass(access(false, false, kOwnerNpc)));
        CellAccess unknown = access(true, false, kOwnerNpc);
        unknown.mKnown = false;
        EXPECT_FALSE(cellEntryIsTrespass(unknown));
        EXPECT_FALSE(cellEntryIsTrespass(CellAccess{}));
    }

    TEST(SkyrimTrespassRules, aCellThePlayerOwnsOrAnUnresolvedOwnerIsNoTrespass)
    {
        EXPECT_FALSE(cellEntryIsTrespass(access(true, false, kPlayerNpcId)));
        EXPECT_FALSE(cellEntryIsTrespass(access(true, false, 0x00000DB1)));
        EXPECT_FALSE(cellEntryIsTrespass(access(true, false, kUnresolvedFormId)));
    }

    TEST(SkyrimTrespassRules, theSettingsDefaultsAreTheDocumentedOnes)
    {
        const Settings s;
        EXPECT_FLOAT_EQ(s.mTrespassDelay, 1.5f);
        EXPECT_FLOAT_EQ(s.mTrespassRetry, 0.5f);
        EXPECT_FLOAT_EQ(s.mTrespassWatchCell, 30.f);
        EXPECT_FLOAT_EQ(s.mTrespassWatchBed, 2.f);
        EXPECT_FLOAT_EQ(s.mTrespassRepeat, 120.f);
    }

    // ---- the system over a fake world ---------------------------------------------------------------------------

    CrimeData hold(std::uint16_t trespass = 5)
    {
        CrimeData c;
        c.mPresent = true;
        c.mMurder = 1000;
        c.mAssault = 40;
        c.mTrespass = trespass;
        c.mStealMultiplier = 0.5f;
        return c;
    }

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
        std::string factionName(FormId) override { return "Hold"; }
        std::int32_t itemValue(FormId) override { return 0; }
        std::int32_t playerGold() override { return 0; }
        bool takeGold(std::int32_t) override { return false; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string& line) override { mLog.push_back(line); }

        bool logged(const std::string& needle) const
        {
            return std::any_of(
                mLog.begin(), mLog.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
        }
        std::map<std::uint32_t, CrimeActor> mActors;
        std::map<FormId, CrimeData> mData;
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
            out = {};
            const double endX = request.x
                + std::sin(static_cast<double>(request.heading)) * std::cos(static_cast<double>(request.pitch))
                    * static_cast<double>(request.range);
            if (mWall && ((request.x < mWallX) != (endX < mWallX)))
                out.fate = RayFate::World;
            return true;
        }
        void put(std::uint32_t reference, double x)
        {
            SubjectState s;
            s.x = x;
            mStates[reference] = s;
        }
        std::map<std::uint32_t, SubjectState> mStates;
        SubjectState mPlayer;
        bool mWall = false;
        double mWallX = 50.0;
    };

    struct SkyrimTrespassSystem : ::testing::Test
    {
        Ledger mLedger;
        FakeWorld mWorld;
        FakeWriter mWriter;
        std::unique_ptr<CrimeSystem> mSystem;

        void SetUp() override
        {
            mWorld.mData[kHold] = hold();
            mSystem = std::make_unique<CrimeSystem>(mLedger, mWorld);
        }
        CrimeActor& witness(std::uint32_t reference, double x = 300.0)
        {
            CrimeActor a;
            a.mHandle = { reference, 1 };
            a.mBase = reference;
            a.mCrimeFaction = kHold;
            a.mName = "npc" + std::to_string(reference);
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

    TEST_F(SkyrimTrespassSystem, aWitnessedCellTrespassIsJudgedAfterTheDelayAndChargedTheFactionsNumber)
    {
        witness(1);
        mSystem->onTrespass(kCellA, Place::Cell);
        step(2); // 1.0 s: before the delay of 1.5 s
        EXPECT_EQ(mLedger.bounty(kHold), 0) << "a new cell's actors need a moment to exist";
        step(2); // 2.0 s
        EXPECT_EQ(mLedger.bounty(kHold), 5);
        EXPECT_TRUE(mWorld.logged("trespass witnessed"));
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassSystem, aCellVisitIsChargedOnceHoweverLongThePlayerStays)
    {
        witness(1);
        mSystem->onTrespass(kCellA, Place::Cell);
        step(80);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
    }

    TEST_F(SkyrimTrespassSystem, aWitnessThatArrivesLaterStillChargesTheVisit)
    {
        mSystem->onTrespass(kCellA, Place::Cell);
        step(20); // 10 s with nobody there
        EXPECT_EQ(mLedger.bounty(kHold), 0);
        EXPECT_EQ(mSystem->pendingCount(), 1u) << "the visit stays pending while it is watched";
        witness(1);
        step(2);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassSystem, aVisitNobodyEverSeesCostsNothingAndIsDroppedAfterTheWatchTime)
    {
        mSystem->onTrespass(kCellA, Place::Cell);
        step(70); // 35 s
        EXPECT_EQ(mLedger.totalBounty(), 0);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
        EXPECT_TRUE(mWorld.logged("trespass not witnessed"));
        witness(1);
        step(4);
        EXPECT_EQ(mLedger.totalBounty(), 0) << "a dropped visit is not revived by someone arriving afterwards";
    }

    TEST_F(SkyrimTrespassSystem, leavingTheCellDropsThePendingVisit)
    {
        mSystem->onTrespass(kCellA, Place::Cell);
        step(2);
        mSystem->onLeftPlace();
        EXPECT_EQ(mSystem->pendingCount(), 0u);
        witness(1);
        step(6);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(SkyrimTrespassSystem, leavingACellDoesNotDropABedAttempt)
    {
        witness(1);
        mSystem->onTrespass(kBedA, Place::Bed);
        mSystem->onLeftPlace();
        step(5);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
    }

    TEST_F(SkyrimTrespassSystem, aWallBetweenThePlayerAndTheWitnessHidesTheTrespass)
    {
        witness(1);
        mWriter.mWall = true;
        mWriter.mWallX = 200.0;
        mSystem->onTrespass(kCellA, Place::Cell);
        step(80);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(SkyrimTrespassSystem, aHostileOrDeadOrFarWitnessSeesNothing)
    {
        witness(1).mEnemyOfPlayer = true;
        witness(2).mDead = true;
        witness(3, 400000.0);
        witness(4).mCrimeFaction = kNullFormId;
        mSystem->onTrespass(kCellA, Place::Cell);
        step(80);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(SkyrimTrespassSystem, aFactionWithNoTrespassValueChargesNothingButTheWitnessIsConsumed)
    {
        mWorld.mData[kHold] = hold(0);
        witness(1);
        mSystem->onTrespass(kCellA, Place::Cell);
        step(10);
        EXPECT_EQ(mLedger.totalBounty(), 0);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassSystem, aBedAttemptIsWatchedForTwoSecondsOnly)
    {
        mSystem->onTrespass(kBedA, Place::Bed);
        step(10); // 5 s with nobody there
        EXPECT_EQ(mSystem->pendingCount(), 0u);
        witness(1);
        step(6);
        EXPECT_EQ(mLedger.totalBounty(), 0);
    }

    TEST_F(SkyrimTrespassSystem, theSameBedTwiceWithinTheRepeatWindowIsChargedOnce)
    {
        witness(1);
        mSystem->onTrespass(kBedA, Place::Bed);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
        mSystem->onTrespass(kBedA, Place::Bed);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassSystem, theSameBedAfterTheRepeatWindowIsChargedAgain)
    {
        witness(1);
        mSystem->onTrespass(kBedA, Place::Bed);
        step(6);
        step(250); // 125 s
        mSystem->onTrespass(kBedA, Place::Bed);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 10);
    }

    TEST_F(SkyrimTrespassSystem, anotherBedWithinTheWindowIsChargedAtOnce)
    {
        witness(1);
        mSystem->onTrespass(kBedA, Place::Bed);
        mSystem->onTrespass(kBedB, Place::Bed);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 10) << "two beds are two offences";
    }

    TEST_F(SkyrimTrespassSystem, aNullPlaceIsIgnored)
    {
        witness(1);
        mSystem->onTrespass(kNullFormId, Place::Bed);
        mSystem->onTrespass(kNullFormId, Place::Cell);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassSystem, aSecondVisitAfterLeavingIsAnotherOffence)
    {
        witness(1);
        mSystem->onTrespass(kCellA, Place::Cell);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 5);
        mSystem->onLeftPlace();
        mSystem->onTrespass(kCellA, Place::Cell);
        step(6);
        EXPECT_EQ(mLedger.bounty(kHold), 10);
    }

    TEST_F(SkyrimTrespassSystem, thePendingTableIsBounded)
    {
        for (std::uint32_t i = 0; i < 400; ++i)
            mSystem->onTrespass(kSelf | (0x1000 + i), Place::Bed);
        EXPECT_LE(mSystem->pendingCount(), 64u);
    }

    // ---- the service ---------------------------------------------------------------------------------------------

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            EXPECT_EQ(args.size(), it->second.params.size()) << name;
            return it->second.handler->call(args);
        }
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

    class QuietWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = 100.0;
            return true;
        }
    };

    struct SkyrimTrespassService : ::testing::Test
    {
        CrimeData mData = hold();
        std::vector<CrimeActor> mActors;
        std::map<std::string, CellAccess> mCells;
        std::map<FormId, FormId> mBedOwners;
        std::vector<std::string> mLog;
        std::unique_ptr<CrimeService> mService;
        StubBindings mBindings;
        QuietWriter mWriter;

        void SetUp() override
        {
            CrimeActor w;
            w.mHandle = { 11, 1 };
            w.mBase = 11;
            w.mCrimeFaction = kHold;
            w.mGuard = true;
            w.mName = "guard";
            mActors.push_back(w);
            mCells["fralia"] = access(true, false, kOwnerNpc);
            mCells["mare"] = access(true, true, kOwnerFaction);
            mCells["outside"] = access(false, false, kNullFormId);
            mCells["jail"] = access(true, false, kOwnerFaction);
            mBedOwners[kBedA] = kOwnerNpc;
            mBedOwners[kBedB] = kPlayerNpcId;
            ServiceHooks hooks;
            hooks.mActors
                = [this](std::vector<CrimeActor>& out) { out.insert(out.end(), mActors.begin(), mActors.end()); };
            hooks.mActor = [this](ActorHandle h, CrimeActor& out) {
                for (const CrimeActor& a : mActors)
                    if (a.mHandle == h)
                    {
                        out = a;
                        return true;
                    }
                return false;
            };
            hooks.mCrimeData = [this](FormId f) { return f == kHold ? &mData : nullptr; };
            hooks.mFactionName = [](FormId) { return std::string("Whiterun"); };
            hooks.mItemValue = [](FormId) { return 100; };
            hooks.mGold = [] { return 200; };
            hooks.mTakeGold = [](std::int32_t) { return true; };
            hooks.mTakeItem = [](FormId, std::int32_t) { return 0; };
            hooks.mItemCount = [](FormId) { return 0; };
            hooks.mJail = [](FormId, JailDestination& out) {
                out.mValid = true;
                out.mCell = "jail";
                return true;
            };
            hooks.mCellAccess = [this](const std::string& name, CellAccess& out) {
                const auto it = mCells.find(name);
                if (it == mCells.end())
                    return false;
                out = it->second;
                return true;
            };
            hooks.mPlacementOwner = [this](FormId placement) {
                const auto it = mBedOwners.find(placement);
                return it == mBedOwners.end() ? kNullFormId : it->second;
            };
            hooks.mLog = [this](const std::string& s) { mLog.push_back(s); };
            mService = std::make_unique<CrimeService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
        void run(int halfSeconds)
        {
            for (int i = 0; i < halfSeconds; ++i)
                mService->system().update(0.5f, mWriter);
        }
        std::int32_t bounty() { return mService->ledger().bounty(kHold); }
    };

    TEST_F(SkyrimTrespassService, theTwoNewFunctionsAreDeclaredAsMutateCalls)
    {
        ASSERT_NE(mBindings.mFunctions.find("entered_cell"), mBindings.mFunctions.end());
        ASSERT_NE(mBindings.mFunctions.find("trespass_bed"), mBindings.mFunctions.end());
        EXPECT_EQ(mBindings.mFunctions["entered_cell"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["trespass_bed"].access, openeden::sdk::BindingAccess::Mutate);
    }

    TEST_F(SkyrimTrespassService, theFirstReportedCellOnlyRecordsItThePlayerStartedOrLoadedThere)
    {
        mService->enteredCell("fralia");
        run(10);
        EXPECT_EQ(bounty(), 0);
        EXPECT_EQ(mService->system().pendingCount(), 0u);
    }

    TEST_F(SkyrimTrespassService, enteringAPrivateOwnedHouseWithAWitnessCostsTheTrespassBounty)
    {
        mService->enteredCell("mare");
        mService->enteredCell("fralia");
        run(10);
        EXPECT_EQ(bounty(), 5);
    }

    TEST_F(SkyrimTrespassService, enteringAPublicCellAnUnownedCellOrAnUnknownCellCostsNothing)
    {
        mService->enteredCell("outside");
        mService->enteredCell("mare");
        mService->enteredCell("outside");
        mService->enteredCell("nowhere");
        run(10);
        EXPECT_EQ(bounty(), 0);
    }

    TEST_F(SkyrimTrespassService, theBindingEnteredCellDoesTheSame)
    {
        (void)mBindings.call("entered_cell", { str("mare") });
        (void)mBindings.call("entered_cell", { str("fralia") });
        run(10);
        EXPECT_EQ(bounty(), 5);
    }

    TEST_F(SkyrimTrespassService, theCellNameIsMatchedWithoutCase)
    {
        mService->enteredCell("mare");
        mService->enteredCell("Fralia");
        run(10);
        EXPECT_EQ(bounty(), 5) << "editor ids are case-insensitive";
    }

    TEST_F(SkyrimTrespassService, leavingAfterAVisitDropsItAndAnotherCellDoesNotInheritIt)
    {
        mService->enteredCell("mare");
        mService->enteredCell("fralia");
        mService->enteredCell("mare"); // out again before anybody saw
        run(10);
        EXPECT_EQ(bounty(), 0);
    }

    TEST_F(SkyrimTrespassService, theCellTheJailSendsThePlayerToIsNoTrespass)
    {
        mService->enteredCell("mare");
        mService->theft(0x01000300, 1); // a theft, so that a guard offers
        run(8);
        ASSERT_EQ(mService->system().state(), State::Offer);
        ASSERT_EQ(mService->choose("jail"), ChoiceStatus::Ok);
        ASSERT_EQ(mService->system().state(), State::Jailed);
        const std::int32_t before = bounty();
        mService->enteredCell("jail");
        run(10);
        EXPECT_EQ(mService->system().pendingCount(), 0u);
        EXPECT_EQ(bounty(), before);
    }

    TEST_F(SkyrimTrespassService, aBedOfSomebodyElseIsATrespassAndThePlayersOwnIsNot)
    {
        mService->trespassBed(kBedB);
        run(8);
        EXPECT_EQ(bounty(), 0);
        mService->trespassBed(kBedA);
        run(8);
        EXPECT_EQ(bounty(), 5);
    }

    TEST_F(SkyrimTrespassService, anUnknownBedOrAnUnownedOneIsNoTrespass)
    {
        mService->trespassBed(kSelf | 0x999);
        mService->trespassBed(kNullFormId);
        run(8);
        EXPECT_EQ(bounty(), 0);
    }

    TEST_F(SkyrimTrespassService, theBindingTrespassBedTakesTheIdAsHexText)
    {
        (void)mBindings.call("trespass_bed", { str("01000600") });
        run(8);
        EXPECT_EQ(bounty(), 5);
    }

    TEST_F(SkyrimTrespassService, aBadIdTextIsIgnoredAndLogged)
    {
        (void)mBindings.call("trespass_bed", { str("not an id") });
        run(8);
        EXPECT_EQ(bounty(), 0);
        EXPECT_FALSE(mLog.empty());
    }

    TEST(SkyrimTrespassNoHooks, withNoHooksTheNewCallsDoNothing)
    {
        CrimeService service{ ServiceHooks{} };
        service.enteredCell("anywhere");
        service.enteredCell("somewhere");
        service.trespassBed(kBedA);
        EXPECT_EQ(service.system().pendingCount(), 0u);
    }

    // ---- the CELL owner and the view's cell access ---------------------------------------------------------------

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

    Bytes cellGroup(std::uint32_t id, const std::string& edid, std::uint16_t flags, const Bytes& extra)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(edid)));
        subs.append(sub4("FULL", Bytes().zstring(edid)));
        subs.append(sub4("DATA", Bytes().u16(flags)));
        subs.append(extra);
        Bytes sub = record4("CELL", 0, id, subs);
        sub.append(group4(id, kGrupCellChildren, group4(id, kGrupTemporary, Bytes())));
        return group4(Content::fourCC("CELL"), kGrupTop,
            group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub)));
    }

    Bytes pluginOf(const Bytes& body)
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "a", "b", 1, {}, 24, 44);
        bytes.append(body);
        return bytes;
    }

    bool parseCell(const Bytes& body, ParsedPlugin& out, Content::Error& error)
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("test.esm", false, err));
        return parseRecordsBytes(pluginOf(body).str(), "test.esm", order, ParseLimits(), out, error);
    }

    TEST(SkyrimCellOwner, anXownOnACellIsDecodedAndAResolvedFormId)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseCell(
            cellGroup(0x00000D2A, "OwnedRoom", CellFlags::Interior, sub4("XOWN", Bytes().u32(0x00000555))), plugin,
            error))
            << error.describe();
        ASSERT_EQ(plugin.mCells.size(), 1u);
        EXPECT_EQ(plugin.mCells[0].mOwner, 0x00000555u);
    }

    TEST(SkyrimCellOwner, aCellWithoutXownHasNoOwner)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseCell(cellGroup(0x00000D2A, "Room", CellFlags::Interior, Bytes()), plugin, error));
        ASSERT_EQ(plugin.mCells.size(), 1u);
        EXPECT_EQ(plugin.mCells[0].mOwner, kNullFormId);
    }

    TEST(SkyrimCellOwner, anXownOfTheWrongSizeIsIgnoredAndTheRestOfTheCellStillParses)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseCell(cellGroup(0x00000D2A, "Room", CellFlags::Interior | CellFlags::PublicArea,
                                  sub4("XOWN", Bytes().u16(7))),
            plugin, error));
        ASSERT_EQ(plugin.mCells.size(), 1u);
        EXPECT_EQ(plugin.mCells[0].mOwner, kNullFormId);
        EXPECT_NE(plugin.mCells[0].mFlags & CellFlags::PublicArea, 0);
    }

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path() / ("oe_trespass_" + std::to_string(counter++));
            fs::remove_all(mPath);
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
        fs::path write(const std::string& name, const Bytes& bytes) const
        {
            const fs::path p = mPath / name;
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    TEST(SkyrimCellAccess, theViewAnswersPublicOwnerAndInteriorByNameAndById)
    {
        TempDir dir;
        Bytes body;
        body.append(
            cellGroup(0x00000D20, "PrivateHome", CellFlags::Interior, sub4("XOWN", Bytes().u32(0x00000555))));
        body.append(cellGroup(0x00000D31, "PublicInn", CellFlags::Interior | CellFlags::PublicArea,
            sub4("XOWN", Bytes().u32(0x00000556))));
        body.append(cellGroup(0x00000D42, "EmptyRoom", CellFlags::Interior, Bytes()));
        const fs::path path = dir.write("test.esm", pluginOf(body));
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
        ASSERT_TRUE(view.startLoad({ path }, nullptr));
        for (int i = 0; i < 4000 && !view.isReady(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ASSERT_TRUE(view.isReady());

        CellAccess a;
        ASSERT_TRUE(view.cellAccessByName("privatehome", a)) << "editor ids are matched without case";
        EXPECT_TRUE(a.mKnown);
        EXPECT_TRUE(a.mInterior);
        EXPECT_FALSE(a.mPublic);
        EXPECT_NE(a.mOwner, kNullFormId);
        EXPECT_TRUE(cellEntryIsTrespass(a));

        ASSERT_TRUE(view.cellAccessByName("PublicInn", a));
        EXPECT_TRUE(a.mPublic);
        EXPECT_NE(a.mOwner, kNullFormId);
        EXPECT_FALSE(cellEntryIsTrespass(a));

        ASSERT_TRUE(view.cellAccessByName("EmptyRoom", a));
        EXPECT_EQ(a.mOwner, kNullFormId);
        EXPECT_FALSE(cellEntryIsTrespass(a));

        EXPECT_FALSE(view.cellAccessByName("NoSuchCell", a));
        EXPECT_FALSE(view.cellAccessOf(0x00123456, a));
    }
}
