// Task 0223 (milestone M-S7): carriages and the world map indoors. Travel to a placed reference (the carriage drivers'
// Game.FastTravel), the request the Papyrus native leaves for the global travel script, the `skyrimse.travel` and
// `skyrimse.map` bindings that carry it, and the natives the carriage scripts call (GlobalVariable, the fades, the
// player controls, the sit). Generated plugins, hand-made indexes and generated scripts; no game data.
#include <game_skyrimse/map.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/travel.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::travel;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kWorld = kSelf | 0x100; // the world the player stands in
    constexpr FormId kChild = kSelf | 0x107; // borrows its land from kWorld
    constexpr FormId kOtherWorld = kSelf | 0x108;
    constexpr FormId kOutdoors = kSelf | 0x101; // a cell of kWorld
    constexpr FormId kChildCell = kSelf | 0x103; // a cell of kChild
    constexpr FormId kOtherCell = kSelf | 0x104; // a cell of kOtherWorld
    constexpr FormId kIndoors = kSelf | 0x102; // an interior cell
    constexpr FormId kDestA = kSelf | 0x600; // a carriage destination marker in kWorld
    constexpr FormId kDestB = kSelf | 0x601; // one in the child world
    constexpr FormId kDestInside = kSelf | 0x602; // one in an interior
    constexpr FormId kDestOther = kSelf | 0x603; // one in an unrelated world
    constexpr FormId kDestGone = kSelf | 0x604; // a deleted one

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

    struct CarriageEnv
    {
        map::MarkerIndex mMarkers;
        PlacedRefIndex mPlaced;
        std::unique_ptr<map::MapService> mMap;
        bool mAccessReady = true;

        CarriageEnv()
        {
            map::MarkerRecords records;
            map::Marker m;
            m.mRef = kSelf | 0x200;
            m.mWorld = kWorld;
            m.mName.mPresent = true;
            m.mName.mText = "M";
            records.mMarkers.push_back(m);
            mMarkers.add(std::move(records));
            mMarkers.finish();
            PlacedRefRecords placed;
            placed.mRefs.push_back(placedAt(kDestA, kOutdoors, 40000, 0, 55));
            placed.mRefs.push_back(placedAt(kDestB, kChildCell, 0, 90000, 66));
            placed.mRefs.push_back(placedAt(kDestInside, kIndoors, 1, 1, 1));
            placed.mRefs.push_back(placedAt(kDestOther, kOtherCell, 5, 5, 5));
            placed.mRefs.push_back(placedAt(kDestGone, kOutdoors, 9, 9, 9));
            placed.mRefs.back().mDeleted = true;
            mPlaced.add(std::move(placed));
            mPlaced.finish();
            map::Hooks hooks;
            hooks.mAccess = [this](map::Access& out) {
                if (!mAccessReady)
                    return false;
                out.mMarkers = &mMarkers;
                out.mPlaced = &mPlaced;
                out.mCellWorld = [](FormId cell, FormId& world, bool& interior) {
                    interior = false;
                    if (cell == kOutdoors)
                        world = kWorld;
                    else if (cell == kChildCell)
                        world = kChild;
                    else if (cell == kOtherCell)
                        world = kOtherWorld;
                    else if (cell == kIndoors)
                    {
                        world = kNullFormId;
                        interior = true;
                    }
                    else
                        return false;
                    return true;
                };
                out.mParentOf = [](FormId world) { return world == kChild ? kWorld : kNullFormId; };
                out.mInteriorWorld = [](std::string_view editorId) {
                    return editorId == "TheMare" ? kWorld : (editorId == "TheCellar" ? kChild : kNullFormId);
                };
                return true;
            };
            mMap = std::make_unique<map::MapService>(std::move(hooks));
        }
    };

    struct CarriageTravelTest : testing::Test
    {
        CarriageEnv mEnv;
        bool mReady = true;
        bool mCombat = false;
        bool mEnemies = false;
        std::size_t mFollowers = 0;
        std::vector<std::string> mLog;
        std::unique_ptr<TravelService> mService;

        void SetUp() override
        {
            Hooks hooks;
            hooks.mReady = [this] { return mReady; };
            hooks.mInCombat = [this] { return mCombat; };
            hooks.mEnemiesNear = [this] { return mEnemies; };
            hooks.mFollowerCount = [this] { return mFollowers; };
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

    // ---- the plan to a reference ------------------------------------------------------------------------------------

    TEST_F(CarriageTravelTest, thePlanIsThePositionOfTheReferenceFacingTheWayTheTravelGoes)
    {
        Plan plan;
        ASSERT_TRUE(mService->planToRef(kWorld, kDestA, 0, 0, plan));
        EXPECT_FLOAT_EQ(plan.mWhere.mX, 40000.f);
        EXPECT_FLOAT_EQ(plan.mWhere.mY, 0.f);
        EXPECT_FLOAT_EQ(plan.mWhere.mZ, 55.f);
        EXPECT_FALSE(plan.mWhere.mFromLink);
        EXPECT_NEAR(plan.mWhere.mYaw, 3.14159265358979323846 / 2, 1e-9); // due east: the way the player travelled
        EXPECT_NEAR(plan.mDistance, 40000.0, 1e-6);
        EXPECT_EQ(plan.mHours, 5);
    }

    TEST_F(CarriageTravelTest, aReferenceInTheChildWorldIsReachableFromTheParentAndTheOtherWayRound)
    {
        Plan plan;
        EXPECT_TRUE(mService->planToRef(kChild, kDestA, 0, 0, plan));
        EXPECT_TRUE(mService->planToRef(kWorld, kDestB, 0, 0, plan));
    }

    TEST_F(CarriageTravelTest, aReferenceThatIsNotUsableHasNoPlan)
    {
        Plan plan;
        EXPECT_FALSE(mService->planToRef(kWorld, kDestInside, 0, 0, plan)); // an interior
        EXPECT_FALSE(mService->planToRef(kWorld, kDestOther, 0, 0, plan)); // an unrelated world
        EXPECT_FALSE(mService->planToRef(kWorld, kDestGone, 0, 0, plan)); // deleted
        EXPECT_FALSE(mService->planToRef(kWorld, kSelf | 0x999, 0, 0, plan)); // no such reference
        EXPECT_FALSE(mService->planToRef(kWorld, kNullFormId, 0, 0, plan));
    }

    TEST_F(CarriageTravelTest, withTheMapNotReadTherePlanIsNone)
    {
        CarriageEnv fresh;
        fresh.mAccessReady = false;
        TravelService service(*fresh.mMap, Hooks());
        Plan plan;
        EXPECT_FALSE(service.planToRef(kWorld, kDestA, 0, 0, plan));
    }

    TEST_F(CarriageTravelTest, everyRefusalToARefComesFromItsOwnCondition)
    {
        mReady = false;
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kDestA, 0, 0), Refusal::NotReady);
        mReady = true;
        EXPECT_EQ(mService->refusalToRef(kWorld, true, kDestA, 0, 0), Refusal::Interior);
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kSelf | 0x999, 0, 0), Refusal::UnknownMarker);
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kDestInside, 0, 0), Refusal::NoDestination);
        mCombat = true;
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kDestA, 0, 0), Refusal::InCombat);
        mCombat = false;
        mEnemies = true;
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kDestA, 0, 0), Refusal::EnemiesNear);
        mEnemies = false;
        EXPECT_EQ(mService->refusalToRef(kWorld, false, kDestA, 0, 0), Refusal::None);
    }

    TEST_F(CarriageTravelTest, aRefusedCommitToARefChangesNothingAndIsCounted)
    {
        mCombat = true;
        EXPECT_FALSE(mService->commitToRef(kWorld, false, kDestA, 0, 0));
        EXPECT_EQ(mService->performed(), 0u);
        EXPECT_EQ(mService->refused(), 1u);
        EXPECT_TRUE(logged("TRAVEL refused: combat"));
    }

    TEST_F(CarriageTravelTest, aCommitToARefIsCountedLoggedAndDiscoversTheArrival)
    {
        mFollowers = 1;
        ASSERT_TRUE(mService->commitToRef(kWorld, false, kDestA, 0, 0));
        EXPECT_EQ(mService->performed(), 1u);
        EXPECT_TRUE(logged("TRAVEL to 01000600"));
        EXPECT_TRUE(logged("5 hours"));
        EXPECT_TRUE(logged("1 followers"));
    }

    // ---- the request the native leaves -----------------------------------------------------------------------------

    TEST_F(CarriageTravelTest, aRequestIsRememberedWithANumberThatMovesOnEveryRequest)
    {
        EXPECT_EQ(mService->requestSequence(), 0u);
        EXPECT_EQ(mService->requestedRef(), kNullFormId);
        EXPECT_TRUE(mService->queueRequest(kDestA));
        EXPECT_EQ(mService->requestSequence(), 1u);
        EXPECT_EQ(mService->requestedRef(), kDestA);
        EXPECT_TRUE(mService->queueRequest(kDestA)); // the same place again is a new request
        EXPECT_EQ(mService->requestSequence(), 2u);
        EXPECT_TRUE(mService->queueRequest(kDestB));
        EXPECT_EQ(mService->requestedRef(), kDestB);
        EXPECT_FALSE(mService->queueRequest(kNullFormId));
        EXPECT_EQ(mService->requestSequence(), 3u);
    }

    // ---- the bindings -----------------------------------------------------------------------------------------------

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
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
            mArgs = std::move(args);
            return it->second.handler->call(mArgs);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    BindingValue i32(std::int32_t v)
    {
        BindingValue b;
        b.type = BindingType::Int32;
        b.integer = v;
        return b;
    }
    BindingValue dbl(double v)
    {
        BindingValue b;
        b.type = BindingType::Double;
        b.number = v;
        return b;
    }
    BindingValue flag(bool v)
    {
        BindingValue b;
        b.type = BindingType::Bool;
        b.boolean = v;
        return b;
    }
    BindingValue str(const std::string& v)
    {
        BindingValue b;
        b.type = BindingType::String;
        b.text = v;
        return b;
    }

    struct CarriageBindingsTest : CarriageTravelTest
    {
        FakeBindings mBindings;
        void SetUp() override
        {
            CarriageTravelTest::SetUp();
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
        std::int32_t world() const { return static_cast<std::int32_t>(kWorld); }
    };

    TEST_F(CarriageBindingsTest, theNamespaceKeepsItsMajorVersionAndGainsTheReferenceFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.travel");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_GE(mBindings.mVersion.minor, 1);
        for (const char* name : { "request_seq", "request_ref", "refusal_ref", "plan_ref", "commit_ref" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["commit_ref"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["request_seq"].access, openeden::sdk::BindingAccess::Read);
        EXPECT_EQ(mBindings.mFunctions["plan_ref"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(CarriageBindingsTest, theRequestIsReadAsANumberAndAHexId)
    {
        EXPECT_EQ(mBindings.call("request_seq").value.integer, 0);
        EXPECT_EQ(std::string(mBindings.call("request_ref").value.text), "");
        (void)mService->queueRequest(kDestB);
        EXPECT_EQ(mBindings.call("request_seq").value.integer, 1);
        EXPECT_EQ(std::string(mBindings.call("request_ref").value.text), "01000601");
    }

    TEST_F(CarriageBindingsTest, thePlanAndTheRefusalOfAReferenceAreReadLikeAMarkers)
    {
        const auto plan = mBindings.call("plan_ref", { i32(world()), str("01000600"), dbl(0), dbl(0) });
        ASSERT_EQ(plan.value.count, 6u);
        EXPECT_DOUBLE_EQ(plan.value.elements[0].number, 40000.0);
        EXPECT_DOUBLE_EQ(plan.value.elements[2].number, 55.0);
        EXPECT_DOUBLE_EQ(plan.value.elements[4].number, 5.0);
        EXPECT_EQ(mBindings.call("plan_ref", { i32(world()), str("01000999"), dbl(0), dbl(0) }).value.count, 0u);
        EXPECT_EQ(
            std::string(mBindings.call("refusal_ref", { i32(world()), flag(false), str("01000600"), dbl(0), dbl(0) })
                            .value.text),
            "");
        EXPECT_EQ(
            std::string(mBindings.call("refusal_ref", { i32(world()), flag(true), str("01000600"), dbl(0), dbl(0) })
                            .value.text),
            "interior");
        EXPECT_EQ(
            std::string(
                mBindings.call("refusal_ref", { i32(world()), flag(false), str("zz"), dbl(0), dbl(0) }).value.text),
            "unknown");
    }

    TEST_F(CarriageBindingsTest, theCommitToARefRunsOnlyWhenTravelIsAllowed)
    {
        (void)mBindings.call("commit_ref", { i32(world()), flag(true), str("01000600"), dbl(0), dbl(0) });
        EXPECT_EQ(mService->performed(), 0u);
        EXPECT_EQ(mService->refused(), 1u);
        (void)mBindings.call("commit_ref", { i32(world()), flag(false), str("01000600"), dbl(0), dbl(0) });
        EXPECT_EQ(mService->performed(), 1u);
    }

    // ---- the map indoors --------------------------------------------------------------------------------------------

    TEST(CarriageMapIndoors, theMapNamespaceAnswersTheWorldOfAnInteriorByItsEditorId)
    {
        CarriageEnv env;
        FakeBindings bindings;
        ASSERT_TRUE(env.mMap->declare(bindings).ok);
        EXPECT_TRUE(bindings.mFunctions.count("interior_world"));
        EXPECT_EQ(bindings.call("interior_world", { str("TheMare") }).value.integer, static_cast<std::int32_t>(kWorld));
        EXPECT_EQ(
            bindings.call("interior_world", { str("TheCellar") }).value.integer, static_cast<std::int32_t>(kChild));
        EXPECT_EQ(bindings.call("interior_world", { str("Nowhere") }).value.integer, 0);
        EXPECT_EQ(bindings.call("interior_world", { str("") }).value.integer, 0);
        EXPECT_EQ(bindings.mFunctions["interior_world"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST(CarriageMapIndoors, withoutTheHookTheAnswerIsZero)
    {
        CarriageEnv env;
        map::Hooks hooks;
        hooks.mAccess = [&env](map::Access& out) {
            out.mMarkers = &env.mMarkers;
            out.mPlaced = &env.mPlaced;
            return true;
        };
        map::MapService service(std::move(hooks));
        FakeBindings bindings;
        ASSERT_TRUE(service.declare(bindings).ok);
        EXPECT_EQ(bindings.call("interior_world", { str("TheMare") }).value.integer, 0);
    }

    // ---- the natives of the carriage scripts -----------------------------------------------------------------------

    constexpr FormId kDriver = 0x01000A01;
    constexpr FormId kPlayer = 0x14;
    constexpr FormId kGold = 0x0000000F;
    constexpr FormId kCostGlobal = 0x01000C01;
    constexpr FormId kSmallCostGlobal = 0x01000C02;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kFade = 0x01000D01;

    class CarriageData final : public PapyrusGameData
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
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            (void)reference;
            x = y = z = 0.0;
            return true;
        }
        bool globalValue(FormId global, float& value) const override
        {
            const auto it = mGlobals.find(global);
            if (it == mGlobals.end())
                return false;
            value = it->second;
            return true;
        }
        FormId questOfInfo(FormId info) const override
        {
            const auto it = mQuestOfInfo.find(info);
            return it != mQuestOfInfo.end() ? it->second : 0;
        }
        std::map<FormId, FormId> mQuestOfInfo;
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, float> mGlobals;
    };

    class GoldHooks final : public PapyrusWorldHooks
    {
    public:
        bool addItem(FormId owner, FormId item, std::int32_t count) override
        {
            mItems[{ owner, item }] += count;
            return true;
        }
        std::int32_t removeItem(FormId owner, FormId item, std::int32_t count) override
        {
            std::int32_t& have = mItems[{ owner, item }];
            const std::int32_t take = std::min(have, count);
            have -= take;
            mRemoved.push_back(take);
            return take;
        }
        std::int32_t itemCount(FormId owner, FormId item) override { return mItems[{ owner, item }]; }
        bool isItem(FormId form) override { return form == kGold; }
        std::map<std::pair<FormId, FormId>, std::int32_t> mItems;
        std::vector<std::int32_t> mRemoved;
    };

    void addClasses(CarriageData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("RemoveItem")
            .native()
            .param("akItemToRemove", "Form")
            .param("aiCount", "Int")
            .param("abSilent", "Bool")
            .param("akOtherContainer", "ObjectReference");
        ref.function("IsFurnitureInUse", "Bool").native().param("abIgnoreReserved", "Bool");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("PlayIdle", "Bool").native().param("akIdle", "Form");
        actor.function("Say")
            .native()
            .param("akTopicToSay", "Form")
            .param("akActorToSpeakAs", "Actor")
            .param("abSpeakInPlayersHead", "Bool");
        actor.function("IsInFaction", "Bool").native().param("akFaction", "Form");
        actor.function("IsInCombat", "Bool").native();
        actor.function("SetVehicle").native().param("akVehicle", "ObjectReference");
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("FastTravel").global().native().param("akDestination", "ObjectReference");
        for (const char* name : { "DisablePlayerControls", "EnablePlayerControls" })
        {
            auto& f = game.function(name).global().native();
            for (const char* p : { "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8" })
                f.param(p, "Bool");
            f.param("a9", "Int");
        }
        game.function("FadeOutGame")
            .global()
            .native()
            .param("a1", "Bool")
            .param("a2", "Bool")
            .param("a3", "Float")
            .param("a4", "Float");
        data.add(game);
        ScriptBuilder global("GlobalVariable");
        global.parent("Form");
        global.function("GetValue", "Float").native();
        global.function("SetValue").native().param("afNewValue", "Float");
        data.add(global);
        ScriptBuilder imod("ImageSpaceModifier");
        imod.parent("Form");
        imod.function("Apply").native().param("afStrength", "Float");
        imod.function("PopTo").native().param("akOther", "ImageSpaceModifier").param("afStrength", "Float");
        imod.function("Remove").native();
        data.add(imod);
    }

    // What CarriageSystemScript does, boiled down: pay the fee from the globals, fade, travel.
    ScriptBuilder probeScript()
    {
        ScriptBuilder s("CarriageProbe");
        s.parent("Actor");
        s.autoProperty("Dest", "ObjectReference");
        s.autoProperty("Gold", "Form");
        s.autoProperty("Cost", "GlobalVariable");
        s.autoProperty("Driver", "Actor");
        s.autoProperty("Fade", "ImageSpaceModifier");
        s.var("feeSeen", "Float");
        s.var("sitting", "Bool");
        s.var("inFaction", "Bool");
        s.var("inCombat", "Bool");
        s.function("ReadCost")
            .local("v", "Float")
            .callMethod("GetValue", Id("::Cost_var"), Id("v"))
            .assign(Id("feeSeen"), Id("v"));
        s.function("SetCost").local("n", "None").callMethod("SetValue", Id("::Cost_var"), Id("n"), { Flt(77.0f) });
        s.function("Go").local("n", "None").callStatic("Game", "FastTravel", Id("n"), { Id("::Dest_var") });
        s.function("GoNowhere").local("n", "None").callStatic("Game", "FastTravel", Id("n"), { Nil() });
        s.function("Sit")
            .local("b", "Bool")
            .callMethod("IsFurnitureInUse", Id("::Dest_var"), Id("b"), { Bool(false) })
            .assign(Id("sitting"), Id("b"));
        s.function("Faction")
            .local("b", "Bool")
            .callMethod("IsInFaction", Id("::Driver_var"), Id("b"), { Nil() })
            .assign(Id("inFaction"), Id("b"));
        s.function("Combat")
            .local("b", "Bool")
            .callMethod("IsInCombat", Id("::Driver_var"), Id("b"))
            .assign(Id("inCombat"), Id("b"));
        s.function("Fades")
            .local("n", "None")
            .callStatic("Game", "DisablePlayerControls", Id("n"),
                { Bool(true), Bool(true), Bool(false), Bool(false), Bool(false), Bool(true), Bool(true), Bool(false),
                    Int(0) })
            .callMethod("Apply", Id("::Fade_var"), Id("n"), { Flt(1.0f) })
            .callMethod("PopTo", Id("::Fade_var"), Id("n"), { Id("::Fade_var"), Flt(1.0f) })
            .callMethod("Remove", Id("::Fade_var"), Id("n"))
            .callStatic("Game", "FadeOutGame", Id("n"), { Bool(true), Bool(true), Flt(0.5f), Flt(3.0f) })
            .callMethod("SetVehicle", Id("::Driver_var"), Id("n"), { Id("::Dest_var") })
            .callMethod("PlayIdle", Id("::Driver_var"), Id("n"), { Nil() })
            .callMethod("Say", Id("::Driver_var"), Id("n"), { Nil(), Nil(), Bool(false) })
            .callStatic("Game", "EnablePlayerControls", Id("n"),
                { Bool(true), Bool(true), Bool(true), Bool(true), Bool(true), Bool(true), Bool(true), Bool(true),
                    Int(0) });
        // the fee, then the travel: the cost is read from the global as the carriage script does (value as int)
        s.function("PayAndGo")
            .local("v", "Float")
            .local("i", "Int")
            .local("n", "None")
            .callMethod("GetValue", Id("::Cost_var"), Id("v"))
            .op(PexAsm::Op::Cast, { Id("i"), Id("v") })
            .callMethod("RemoveItem", Id("::Driver_var"), Id("n"), { Id("::Gold_var"), Id("i"), Bool(false), Nil() })
            .callStatic("Game", "FastTravel", Id("n"), { Id("::Dest_var") });
        return s;
    }

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    struct CarriageNativesTest : testing::Test
    {
        void SetUp() override
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.mClasses[kDriver] = "Actor";
            mData.mClasses[kPlayer] = "Actor";
            mData.mClasses[kDestA] = "ObjectReference";
            mData.mClasses[kGold] = "Form";
            mData.mClasses[kCostGlobal] = "GlobalVariable";
            mData.mClasses[kSmallCostGlobal] = "GlobalVariable";
            mData.mClasses[kFade] = "ImageSpaceModifier";
            mData.mGlobals[kCostGlobal] = 20.0f;
            mData.mGlobals[kSmallCostGlobal] = 50.0f;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            mHost->setFastTravelSink([this](FormId destination) {
                mTravelled.push_back(destination);
                return true;
            });
            mHost->startGame();
            vmad::Script script;
            script.mName = "CarriageProbe";
            script.mProperties = { objProp("Dest", kDestA), objProp("Gold", kGold), objProp("Cost", kCostGlobal),
                objProp("Driver", kDriver), objProp("Fade", kFade) };
            ASSERT_EQ(mHost->attachReference(kDriver, kBase, true, { std::move(script) }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kDriver, "CarriageProbe", function);
            tick();
            return ok;
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kDriver, "CarriageProbe", name); }
        bool tolerant(const std::string& key) const
        {
            const auto names = mHost->tolerantNatives();
            return std::find(names.begin(), names.end(), key) != names.end();
        }

        CarriageData mData;
        GoldHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<FormId> mTravelled;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST_F(CarriageNativesTest, gameFastTravelHandsTheDestinationToTheSink)
    {
        ASSERT_TRUE(call("Go"));
        ASSERT_EQ(mTravelled.size(), 1u);
        EXPECT_EQ(mTravelled[0], kDestA);
        EXPECT_FALSE(tolerant("game.fasttravel"));
        EXPECT_TRUE(mErrors.empty());
    }

    TEST_F(CarriageNativesTest, aNoneDestinationTravelsNowhere)
    {
        ASSERT_TRUE(call("GoNowhere"));
        EXPECT_TRUE(mTravelled.empty());
        EXPECT_FALSE(tolerant("game.fasttravel"));
    }

    TEST_F(CarriageNativesTest, withoutASinkFastTravelIsHarmlessAndCounted)
    {
        mHost->setFastTravelSink({});
        ASSERT_TRUE(call("Go"));
        EXPECT_TRUE(mTravelled.empty());
        EXPECT_TRUE(mErrors.empty());
        EXPECT_EQ(mHost->counters().mNatives.count("game.fasttravel"), 1u);
    }

    TEST_F(CarriageNativesTest, aGlobalVariableReadsItsRecordsValueAndKeepsWhatAScriptSets)
    {
        ASSERT_TRUE(call("ReadCost"));
        EXPECT_EQ(number("feeSeen"), 20.0);
        ASSERT_TRUE(call("SetCost"));
        ASSERT_TRUE(call("ReadCost"));
        EXPECT_EQ(number("feeSeen"), 77.0);
        EXPECT_FALSE(tolerant("globalvariable.getvalue"));
    }

    TEST_F(CarriageNativesTest, aFormTheDataKnowsAsAGlobalIsAGlobalVariableEvenWithNoClassEntry)
    {
        mData.mClasses.erase(kCostGlobal); // the pack gives no class for it: the host asks the data for a global value
        ASSERT_TRUE(call("ReadCost"));
        EXPECT_EQ(number("feeSeen"), 20.0);
    }

    TEST_F(CarriageNativesTest, aGlobalThePackDoesNotKnowReadsZero)
    {
        mData.mGlobals.erase(kCostGlobal);
        ASSERT_TRUE(call("ReadCost"));
        EXPECT_EQ(number("feeSeen"), 0.0);
    }

    TEST_F(CarriageNativesTest, theScriptPaysTheFeeItselfThroughTheInventoryNativesAndThenTravels)
    {
        mHooks.mItems[std::make_pair(kDriver, kGold)] = 100; // the probe pays from the object it names
        ASSERT_TRUE(call("PayAndGo"));
        EXPECT_EQ(mHooks.itemCount(kDriver, kGold), 80);
        ASSERT_EQ(mHooks.mRemoved.size(), 1u);
        EXPECT_EQ(mHooks.mRemoved[0], 20);
        ASSERT_EQ(mTravelled.size(), 1u);
    }

    TEST_F(CarriageNativesTest, theFadesTheControlsTheVehicleAndTheTalkAreNoOpsThatAreNotTolerant)
    {
        ASSERT_TRUE(call("Fades"));
        EXPECT_TRUE(mErrors.empty());
        for (const char* key : { "game.disableplayercontrols", "game.enableplayercontrols", "game.fadeoutgame",
                 "imagespacemodifier.apply", "imagespacemodifier.popto", "imagespacemodifier.remove",
                 "actor.setvehicle", "actor.playidle", "actor.say" })
        {
            EXPECT_FALSE(tolerant(key)) << key;
            EXPECT_EQ(mHost->counters().mNatives.count(key), 1u) << key;
        }
    }

    TEST_F(CarriageNativesTest, theCartSeatCountsAsTakenAndNobodyIsInAFactionOrInCombat)
    {
        ASSERT_TRUE(call("Sit"));
        EXPECT_EQ(number("sitting"), 1.0);
        ASSERT_TRUE(call("Faction"));
        EXPECT_EQ(number("inFaction"), 0.0);
        ASSERT_TRUE(call("Combat"));
        EXPECT_EQ(number("inCombat"), 0.0);
        EXPECT_FALSE(tolerant("objectreference.isfurnitureinuse"));
        EXPECT_FALSE(tolerant("actor.isinfaction"));
    }

    // ---- GetOwningQuest() as a quest's own script (the carriage topics' fragments)
    // -----------------------------------

    constexpr FormId kOwnerQuest = 0x01000E01;
    constexpr FormId kOwnerInfo = 0x01000E02;
    constexpr FormId kPlainQuest = 0x01000E03;
    constexpr FormId kPlainInfo = 0x01000E04;

    // A TopicInfo script that does what `(GetOwningQuest() as carriagesystemscript).Travel(...)` does.
    void addOwningQuestScripts(CarriageData& data)
    {
        ScriptBuilder topicInfo("TopicInfo");
        topicInfo.parent("Form");
        topicInfo.function("GetOwningQuest", "Quest").native();
        data.add(topicInfo);
        ScriptBuilder quest("Quest");
        quest.parent("Form");
        data.add(quest);
        ScriptBuilder qs("OwnerQuestScript");
        qs.parent("Quest");
        qs.var("pings", "Int");
        qs.function("Ping").param("n", "Int").iadd(Id("pings"), Id("pings"), Id("n"));
        data.add(qs);
        ScriptBuilder fragment("OwnerFragment");
        fragment.parent("TopicInfo");
        fragment.function("Run")
            .local("q", "Quest")
            .local("s", "OwnerQuestScript")
            .local("n", "None")
            .callMethod("GetOwningQuest", Id("self"), Id("q"))
            .op(PexAsm::Op::Cast, { Id("s"), Id("q") })
            .callMethod("Ping", Id("s"), Id("n"), { Int(2) });
        data.add(fragment);
    }

    struct CarriageOwnerTest : testing::Test
    {
        void SetUp() override
        {
            addOwningQuestScripts(mData);
            mData.mClasses[kOwnerQuest] = "Quest";
            mData.mClasses[kOwnerInfo] = "TopicInfo";
            mData.mClasses[kPlainQuest] = "Quest";
            mData.mClasses[kPlainInfo] = "TopicInfo";
            mData.mQuestOfInfo[kOwnerInfo] = kOwnerQuest;
            mData.mQuestOfInfo[kPlainInfo] = kPlainQuest;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->startGame();
            vmad::Script quest;
            quest.mName = "OwnerQuestScript";
            ASSERT_EQ(mHost->attachReference(kOwnerQuest, kBase, false, { std::move(quest) }), 1u);
            for (const FormId info : { kOwnerInfo, kPlainInfo })
            {
                vmad::Script fragment;
                fragment.mName = "OwnerFragment";
                ASSERT_EQ(mHost->attachReference(info, kBase, false, { std::move(fragment) }), 1u);
            }
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        CarriageData mData;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST_F(CarriageOwnerTest, theOwningQuestOfAScriptedQuestIsItsScriptSoACastToTheScriptKeepsIt)
    {
        ASSERT_TRUE(mHost->callFunction(kOwnerInfo, "OwnerFragment", "Run"));
        tick();
        EXPECT_EQ(mHost->numberVariable(kOwnerQuest, "OwnerQuestScript", "pings"), 2.0);
        EXPECT_TRUE(mErrors.empty());
    }

    TEST_F(CarriageOwnerTest, aQuestWithNoScriptStillAnswersAsAFormAndTheCastGivesNone)
    {
        ASSERT_TRUE(mHost->callFunction(kPlainInfo, "OwnerFragment", "Run"));
        tick();
        EXPECT_EQ(mHost->numberVariable(kOwnerQuest, "OwnerQuestScript", "pings"), 0.0);
    }
}
