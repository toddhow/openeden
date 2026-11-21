// Task 0312: the ship from Windhelm to Raven Rock (Solstheim). Travel to a placed reference of an UNRELATED world (the
// Dragonborn ship's `Game.FastTravel(pDLC2RRSolstheimLandingMarker)`), and the binding that names the destination's
// world. Generated indexes, no game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/map.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/travel.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::travel;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kTamriel = kSelf | 0x100;
    constexpr FormId kSolstheim = kSelf | 0x108;
    constexpr FormId kTamrielCell = kSelf | 0x101;
    constexpr FormId kSolstheimCell = kSelf | 0x104;
    constexpr FormId kInterior = kSelf | 0x102;
    constexpr FormId kLanding = kSelf | 0x600; // the Raven Rock landing marker, in Solstheim
    constexpr FormId kHarbour = kSelf | 0x601; // a marker in Tamriel
    constexpr FormId kInside = kSelf | 0x602;

    PlacedEntry placedAt(FormId id, FormId cell, float x, float y, float z)
    {
        PlacedEntry e;
        e.mId = id;
        e.mCell = cell;
        e.mX = x;
        e.mY = y;
        e.mZ = z;
        return e;
    }

    struct ShipEnv
    {
        map::MarkerIndex mMarkers;
        PlacedRefIndex mPlaced;
        std::unique_ptr<map::MapService> mMap;

        ShipEnv()
        {
            map::MarkerRecords records;
            map::Marker m;
            m.mRef = kSelf | 0x200;
            m.mWorld = kTamriel;
            m.mName.mPresent = true;
            m.mName.mText = "M";
            records.mMarkers.push_back(m);
            mMarkers.add(std::move(records));
            mMarkers.finish();
            PlacedRefRecords placed;
            placed.mRefs.push_back(placedAt(kLanding, kSolstheimCell, 28932, 33531, 310));
            placed.mRefs.push_back(placedAt(kHarbour, kTamrielCell, 141333, 33717, -13990));
            placed.mRefs.push_back(placedAt(kInside, kInterior, 1, 1, 1));
            mPlaced.add(std::move(placed));
            mPlaced.finish();
            map::Hooks hooks;
            hooks.mAccess = [this](map::Access& out) {
                out.mMarkers = &mMarkers;
                out.mPlaced = &mPlaced;
                out.mCellWorld = [](FormId cell, FormId& world, bool& interior) {
                    interior = false;
                    if (cell == kTamrielCell)
                        world = kTamriel;
                    else if (cell == kSolstheimCell)
                        world = kSolstheim;
                    else if (cell == kInterior)
                    {
                        world = kNullFormId;
                        interior = true;
                    }
                    else
                        return false;
                    return true;
                };
                out.mParentOf = [](FormId) { return kNullFormId; };
                out.mInteriorWorld = [](std::string_view) { return kNullFormId; };
                return true;
            };
            mMap = std::make_unique<map::MapService>(std::move(hooks));
        }
    };

    struct ShipTest : testing::Test
    {
        ShipEnv mEnv;
        std::vector<std::string> mLog;
        std::unique_ptr<TravelService> mService;
        void SetUp() override
        {
            Hooks hooks;
            hooks.mReady = [] { return true; };
            hooks.mInCombat = [] { return false; };
            hooks.mEnemiesNear = [] { return false; };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<TravelService>(*mEnv.mMap, std::move(hooks));
        }
        bool logged(const std::string& needle) const
        {
            for (const std::string& line : mLog)
                if (line.find(needle) != std::string::npos)
                    return true;
            return false;
        }
    };

    TEST_F(ShipTest, withoutTheCrossWorldSettingAnUnrelatedWorldHasNoPlan)
    {
        Plan plan;
        EXPECT_FALSE(mService->planToRef(kTamriel, kLanding, 0, 0, plan));
        EXPECT_EQ(mService->refusalToRef(kTamriel, false, kLanding, 0, 0), Refusal::NoDestination);
    }

    TEST_F(ShipTest, withItTheShipTakesAFixedNumberOfHoursToThePlaceInTheOtherWorld)
    {
        mService->setCrossWorldHours(6);
        Plan plan;
        ASSERT_TRUE(mService->planToRef(kTamriel, kLanding, 141333, 33717, plan));
        EXPECT_FLOAT_EQ(plan.mWhere.mX, 28932.f);
        EXPECT_FLOAT_EQ(plan.mWhere.mZ, 310.f);
        EXPECT_EQ(plan.mHours, 6);
        EXPECT_EQ(plan.mWorld, kSolstheim);
        EXPECT_EQ(mService->refusalToRef(kTamriel, false, kLanding, 141333, 33717), Refusal::None);
    }

    TEST_F(ShipTest, aSameWorldPlanKeepsTheDistanceHoursAndNamesItsWorld)
    {
        mService->setCrossWorldHours(6);
        Plan plan;
        ASSERT_TRUE(mService->planToRef(kTamriel, kHarbour, 0, 0, plan));
        EXPECT_EQ(plan.mWorld, kTamriel);
        EXPECT_NE(plan.mHours, 6);
    }

    TEST_F(ShipTest, anInteriorDestinationAndAnInteriorStartStayRefused)
    {
        mService->setCrossWorldHours(6);
        Plan plan;
        EXPECT_FALSE(mService->planToRef(kTamriel, kInside, 0, 0, plan));
        EXPECT_EQ(mService->refusalToRef(kTamriel, true, kLanding, 0, 0), Refusal::Interior);
    }

    TEST_F(ShipTest, theCrossWorldCommitIsLoggedWithItsHours)
    {
        mService->setCrossWorldHours(6);
        ASSERT_TRUE(mService->commitToRef(kTamriel, false, kLanding, 141333, 33717));
        EXPECT_TRUE(logged("TRAVEL to 01000600"));
        EXPECT_TRUE(logged("6 hours"));
    }

    TEST_F(ShipTest, theWorldOfAReferenceIsAnswered)
    {
        EXPECT_EQ(mService->worldOfRef(kLanding), kSolstheim);
        EXPECT_EQ(mService->worldOfRef(kHarbour), kTamriel);
        EXPECT_EQ(mService->worldOfRef(kInside), kNullFormId);
        EXPECT_EQ(mService->worldOfRef(kSelf | 0x999), kNullFormId);
    }

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    TEST_F(ShipTest, theWorldOfARefIsReadThroughTheBindings)
    {
        FakeBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        EXPECT_GE(bindings.mVersion.minor, 2);
        ASSERT_TRUE(bindings.mFunctions.count("world_of_ref"));
        EXPECT_EQ(bindings.mFunctions["world_of_ref"].access, openeden::sdk::BindingAccess::Read);
        BindingValue ref;
        ref.type = BindingType::String;
        ref.text = "01000600";
        bindings.mArgs = { ref };
        const auto result = bindings.mFunctions["world_of_ref"].handler->call(bindings.mArgs);
        EXPECT_EQ(static_cast<FormId>(result.value.integer), kSolstheim);
    }

    // ---- the natives of Gjalund's ship script ---------------------------------------------------------------------

    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kPlayerRef = 0x14;
    constexpr FormId kActorRef = 0x01000A01;
    constexpr FormId kSolstheimWorldForm = 0x01000800;

    class ShipData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            if (form == kPlayerRef || form == kActorRef)
                return "Actor";
            return {};
        }
        FormId worldOf(FormId reference) const override { return reference == kPlayerRef ? kSolstheimWorldForm : 0; }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
    };

    struct ShipNativesTest : testing::Test
    {
        void SetUp() override
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            ref.function("GetWorldSpace", "Form").native();
            ref.function("IsInDialogueWithPlayer", "Bool").native();
            ref.function("WaitForAnimationEvent", "Bool").native().param("asEventName", "String");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("StopCombatAlarm").native();
            actor.function("EnableAI").native().param("abEnable", "Bool");
            mData.add(actor);
            ScriptBuilder game("Game");
            game.function("RequestAutoSave").global().native();
            mData.add(game);
            ScriptBuilder probe("ShipProbe");
            probe.parent("Actor");
            probe.autoProperty("Target", "ObjectReference");
            probe.var("worldId", "Int");
            probe.var("after", "Int");
            probe.var("talking", "Bool");
            probe.function("World")
                .local("w", "Form")
                .local("i", "Int")
                .callMethod("GetWorldSpace", Id("::Target_var"), Id("w"))
                .callMethod("GetFormID", Id("w"), Id("i"))
                .assign(Id("worldId"), Id("i"));
            probe.function("Wait")
                .local("b", "Bool")
                .callMethod("WaitForAnimationEvent", Id("::Target_var"), Id("b"), { Str("BoatRideFadeOut") })
                .assign(Id("after"), Int(1));
            probe.function("Misc")
                .local("b", "Bool")
                .local("n", "None")
                .callMethod("IsInDialogueWithPlayer", Id("::Target_var"), Id("b"))
                .assign(Id("talking"), Id("b"))
                .callMethod("StopCombatAlarm", Id("::Target_var"), Id("n"))
                .callMethod("EnableAI", Id("::Target_var"), Id("n"), { PexAsm::Bool(false) })
                .callStatic("Game", "RequestAutoSave", Id("n"));
            mData.add(probe);
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->startGame();
            vmad::Script script;
            script.mName = "ShipProbe";
            vmad::Property p;
            p.mName = "Target";
            p.mType = 1;
            p.mValue.mKind = vmad::ValueKind::Object;
            p.mValue.mForm = kPlayerRef;
            p.mValue.mAlias = -1;
            script.mProperties = { p };
            ASSERT_EQ(mHost->attachReference(kActorRef, 0x01000B00, true, { std::move(script) }), 1u);
            tick(3, 0.1f);
        }
        void tick(int n, float dt)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kActorRef, "ShipProbe", name); }
        bool tolerant(const std::string& key) const
        {
            const auto names = mHost->tolerantNatives();
            return std::find(names.begin(), names.end(), key) != names.end();
        }
        ShipData mData;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST_F(ShipNativesTest, getWorldSpaceAnswersTheWorldTheDataSays)
    {
        ASSERT_TRUE(mHost->callFunction(kActorRef, "ShipProbe", "World"));
        tick(3, 0.1f);
        EXPECT_EQ(number("worldId"), static_cast<double>(kSolstheimWorldForm));
        EXPECT_FALSE(tolerant("objectreference.getworldspace"));
    }

    TEST_F(ShipNativesTest, waitingForAnAnimationEventParksTheScriptForALimitedTime)
    {
        ASSERT_TRUE(mHost->callFunction(kActorRef, "ShipProbe", "Wait"));
        tick(3, 0.1f);
        EXPECT_EQ(number("after"), 0.0); // still waiting for the event
        tick(200, 0.1f); // twenty seconds
        EXPECT_EQ(number("after"), 1.0);
        EXPECT_TRUE(mErrors.empty());
    }

    TEST_F(ShipNativesTest, theDialogueCombatAiAndSaveCallsAreNoOpsThatAreNotTolerant)
    {
        ASSERT_TRUE(mHost->callFunction(kActorRef, "ShipProbe", "Misc"));
        tick(3, 0.1f);
        EXPECT_EQ(number("talking"), 0.0);
        EXPECT_TRUE(mErrors.empty());
        for (const char* key :
            { "actor.isindialoguewithplayer", "actor.stopcombatalarm", "actor.enableai", "game.requestautosave" })
            EXPECT_FALSE(tolerant(key)) << key;
    }

    // ---- the player's location in a quest's stage entry (DLC2MQ01's stage 10 has an entry for "on Solstheim")
    // ---------

    TEST(ShipLocation, thePlayerIsInALocationOrInsideItWhenThePackTellsWhereHeStands)
    {
        const FormId solstheim = kSelf | 0x10;
        const FormId ravenRock = kSelf | 0x11;
        const FormId whiterun = kSelf | 0x12;
        LocationIndex locations;
        LocationRecords records;
        LocationRecord a;
        a.mCommon.mId = solstheim;
        LocationRecord b;
        b.mCommon.mId = ravenRock;
        b.mParent = solstheim;
        LocationRecord c;
        c.mCommon.mId = whiterun;
        records.mLocations = { a, b, c };
        locations.add(std::move(records));

        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        world.setLocations(&locations);
        survey::Ctda condition;
        condition.mFunction = 359; // GetInCurrentLoc
        condition.mRunOn = 2; // a reference: the player
        condition.mReference = 0x14;
        condition.mParam1 = solstheim;
        condition.mCompare = 0;
        condition.mComparison = 1.f;
        conditions::Evaluator evaluator;
        const auto holds = [&] { return evaluator.evaluate(std::span<const survey::Ctda>(&condition, 1), world); };

        EXPECT_FALSE(world.answers(condition)); // not told yet
        world.setPlayerLocation(ravenRock);
        EXPECT_TRUE(world.answers(condition));
        EXPECT_TRUE(holds()); // Raven Rock is inside Solstheim
        world.setPlayerLocation(whiterun);
        EXPECT_FALSE(holds());
        world.setPlayerLocation(kNullFormId);
        EXPECT_FALSE(world.answers(condition));
    }

    TEST(ShipLocation, theLastSpeakerCanBeMadeToStandWherethePlayerDoes)
    {
        const FormId solstheim = kSelf | 0x10;
        const FormId ravenRock = kSelf | 0x11;
        const FormId windhelm = kSelf | 0x12;
        LocationIndex locations;
        LocationRecords records;
        LocationRecord a;
        a.mCommon.mId = solstheim;
        LocationRecord b;
        b.mCommon.mId = ravenRock;
        b.mParent = solstheim;
        LocationRecord c;
        c.mCommon.mId = windhelm;
        records.mLocations = { a, b, c };
        locations.add(std::move(records));

        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        world.setLocations(&locations);
        DialogueSpeaker speaker;
        speaker.mBase = kSelf | 0x20; // Gjalund
        speaker.mLocation = windhelm;
        speaker.mLocationKnown = true;
        world.setSpeaker(speaker);
        survey::Ctda condition;
        condition.mFunction = 359; // GetInCurrentLoc, run on the subject
        condition.mParam1 = solstheim;
        condition.mCompare = 0;
        condition.mComparison = 1.f;
        conditions::Evaluator evaluator;
        const auto holds = [&] { return evaluator.evaluate(std::span<const survey::Ctda>(&condition, 1), world); };
        EXPECT_FALSE(holds());
        world.setSpeakerLocation(ravenRock, true);
        EXPECT_TRUE(holds());
        EXPECT_EQ(world.speakerLocation(), ravenRock);
        world.setSpeakerLocation(windhelm, true);
        EXPECT_FALSE(holds());
        EXPECT_TRUE(world.speakerLocationKnown());
    }
}
