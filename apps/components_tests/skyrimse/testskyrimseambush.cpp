// Task 0302 (draugr ambushes): what lets a scripted sleeper out. Five groups, none needs game data:
// (1) the wake rule (ambush.hpp), (2) the Papyrus host's OnGetUp and OnCombatStateChanged, the natives that
// masterAmbushScript's OnGetUp handler calls (GetParentCell, Cell.IsAttached) and the hook that tells the pack of a
// script's SetAV, (3) Variable01 read from the host's actor values by the condition overlay, (4) the combat AI taking an
// Aggression a script set, and the fight's state told to the pack. The test script is a small stand-in for
// masterAmbushScript (no states: the host's own state machine is not under test here). Written before the code, run red
// against stubs.
#include <game_skyrimse/ambush.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/follower_state.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    // ---- (1) the wake rule -----------------------------------------------------------------------------------------

    ambush::Sleeper sleeperAt(WorldId reference, double x, double y, double z)
    {
        ambush::Sleeper s;
        s.mReference = reference;
        s.mX = x;
        s.mY = y;
        s.mZ = z;
        return s;
    }

    ambush::Player playerAt(double x, double y, double z)
    {
        ambush::Player p;
        p.mKnown = true;
        p.mX = x;
        p.mY = y;
        p.mZ = z;
        return p;
    }

    TEST(SkyrimAmbushRule, thePlayerWithinTheDistanceAndHeightIsInRange)
    {
        const ambush::Rules rules;
        const ambush::Sleeper bed = sleeperAt(10, 1000.0, 2000.0, 0.0);
        EXPECT_TRUE(ambush::inRange(bed, playerAt(1000.0, 2000.0, 0.0), rules));
        EXPECT_TRUE(ambush::inRange(bed, playerAt(1000.0 + 500.0, 2000.0, 100.0), rules));
        // the distance is horizontal: 3-4-5 triangle scaled to exactly the wake distance is still in range
        EXPECT_TRUE(ambush::inRange(
            bed, playerAt(1000.0 + 0.6 * ambush::kWakeDistance, 2000.0 + 0.8 * ambush::kWakeDistance, 0.0), rules));
    }

    TEST(SkyrimAmbushRule, FartherThanTheDistanceOrOnAnotherFloorIsOutOfRange)
    {
        const ambush::Rules rules;
        const ambush::Sleeper bed = sleeperAt(10, 0.0, 0.0, 0.0);
        EXPECT_FALSE(ambush::inRange(bed, playerAt(ambush::kWakeDistance + 1.0, 0.0, 0.0), rules));
        EXPECT_FALSE(ambush::inRange(bed, playerAt(0.0, 0.0, ambush::kWakeHeight + 1.0), rules));
        EXPECT_FALSE(ambush::inRange(bed, playerAt(0.0, 0.0, -ambush::kWakeHeight - 1.0), rules));
    }

    TEST(SkyrimAmbushRule, anUnknownOrBrokenPlaceIsNeverInRange)
    {
        const ambush::Rules rules;
        const ambush::Sleeper bed = sleeperAt(10, 0.0, 0.0, 0.0);
        ambush::Player unknown = playerAt(0.0, 0.0, 0.0);
        unknown.mKnown = false;
        EXPECT_FALSE(ambush::inRange(bed, unknown, rules));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(ambush::inRange(bed, playerAt(nan, 0.0, 0.0), rules));
        EXPECT_FALSE(ambush::inRange(sleeperAt(10, 0.0, nan, 0.0), playerAt(0.0, 0.0, 0.0), rules));
        EXPECT_FALSE(ambush::inRange(
            bed, playerAt(std::numeric_limits<double>::infinity(), 0.0, 0.0), rules));
    }

    TEST(SkyrimAmbushRule, theRulesCanBeChangedByTheCaller)
    {
        ambush::Rules tight;
        tight.mDistance = 100.0;
        tight.mHeight = 10.0;
        const ambush::Sleeper bed = sleeperAt(10, 0.0, 0.0, 0.0);
        EXPECT_TRUE(ambush::inRange(bed, playerAt(99.0, 0.0, 9.0), tight));
        EXPECT_FALSE(ambush::inRange(bed, playerAt(101.0, 0.0, 0.0), tight));
        EXPECT_FALSE(ambush::inRange(bed, playerAt(0.0, 0.0, 11.0), tight));
    }

    TEST(SkyrimAmbushRule, aggressionIsRoundedAndCutToZeroThroughThree)
    {
        EXPECT_EQ(ambush::aggressionFromValue(0.0), 0);
        EXPECT_EQ(ambush::aggressionFromValue(1.0), 1);
        EXPECT_EQ(ambush::aggressionFromValue(2.0), 2);
        EXPECT_EQ(ambush::aggressionFromValue(2.4), 2);
        EXPECT_EQ(ambush::aggressionFromValue(2.6), 3);
        EXPECT_EQ(ambush::aggressionFromValue(3.0), 3);
        EXPECT_EQ(ambush::aggressionFromValue(7.0), 3);
        EXPECT_EQ(ambush::aggressionFromValue(-1.0), 0);
        EXPECT_EQ(ambush::aggressionFromValue(std::numeric_limits<double>::quiet_NaN()), 0);
        EXPECT_EQ(ambush::aggressionFromValue(std::numeric_limits<double>::infinity()), 3);
    }

    TEST(SkyrimAmbushWatch, aSleeperInRangeWakesOnceAndOthersStayAsleep)
    {
        ambush::Watch watch;
        const std::vector<ambush::Sleeper> sleepers = { sleeperAt(10, 0.0, 0.0, 0.0), sleeperAt(11, 5000.0, 0.0, 0.0) };
        std::vector<WorldId> wake;
        EXPECT_EQ(watch.update(sleepers, playerAt(100.0, 0.0, 0.0), wake), 1u);
        ASSERT_EQ(wake.size(), 1u);
        EXPECT_EQ(wake[0], 10u);
        EXPECT_TRUE(watch.woken(10));
        EXPECT_FALSE(watch.woken(11));
        wake.clear();
        EXPECT_EQ(watch.update(sleepers, playerAt(100.0, 0.0, 0.0), wake), 0u); // once
        EXPECT_TRUE(wake.empty());
    }

    TEST(SkyrimAmbushWatch, theWokenComeInAscendingPlacementOrderWhateverTheInputOrder)
    {
        ambush::Watch watch;
        const std::vector<ambush::Sleeper> sleepers
            = { sleeperAt(30, 0.0, 0.0, 0.0), sleeperAt(10, 10.0, 0.0, 0.0), sleeperAt(20, 20.0, 0.0, 0.0) };
        std::vector<WorldId> wake;
        EXPECT_EQ(watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake), 3u);
        EXPECT_EQ(wake, (std::vector<WorldId>{ 10, 20, 30 }));
    }

    TEST(SkyrimAmbushWatch, aRowOfSleepersIsReleasedAFewAtATime)
    {
        ambush::Watch watch;
        std::vector<ambush::Sleeper> sleepers;
        for (WorldId i = 1; i <= 12; ++i)
            sleepers.push_back(sleeperAt(i, 0.0, 0.0, 0.0));
        std::vector<WorldId> wake;
        EXPECT_EQ(watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake), ambush::kMaxWakesPerUpdate);
        EXPECT_EQ(wake.size(), ambush::kMaxWakesPerUpdate);
        EXPECT_EQ(wake.front(), 1u);
        EXPECT_EQ(wake.back(), static_cast<WorldId>(ambush::kMaxWakesPerUpdate));
        wake.clear();
        EXPECT_EQ(watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake), 12u - ambush::kMaxWakesPerUpdate);
        EXPECT_EQ(wake.front(), static_cast<WorldId>(ambush::kMaxWakesPerUpdate + 1));
        EXPECT_EQ(wake.back(), 12u);
        EXPECT_EQ(watch.wokenCount(), 12u);
    }

    TEST(SkyrimAmbushWatch, aSleeperOutOfRangeIsNotRememberedAndWakesWhenThePlayerComes)
    {
        ambush::Watch watch;
        const std::vector<ambush::Sleeper> sleepers = { sleeperAt(10, 0.0, 0.0, 0.0) };
        std::vector<WorldId> wake;
        EXPECT_EQ(watch.update(sleepers, playerAt(3000.0, 0.0, 0.0), wake), 0u);
        EXPECT_FALSE(watch.woken(10));
        EXPECT_EQ(watch.update(sleepers, playerAt(300.0, 0.0, 0.0), wake), 1u);
        EXPECT_TRUE(watch.woken(10));
    }

    TEST(SkyrimAmbushWatch, forgetAndClearLetASleeperWakeAgain)
    {
        ambush::Watch watch;
        const std::vector<ambush::Sleeper> sleepers = { sleeperAt(10, 0.0, 0.0, 0.0), sleeperAt(11, 0.0, 0.0, 0.0) };
        std::vector<WorldId> wake;
        ASSERT_EQ(watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake), 2u);
        watch.forget(10);
        EXPECT_FALSE(watch.woken(10));
        EXPECT_TRUE(watch.woken(11));
        wake.clear();
        EXPECT_EQ(watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake), 1u);
        EXPECT_EQ(wake, (std::vector<WorldId>{ 10 }));
        watch.clear();
        EXPECT_EQ(watch.wokenCount(), 0u);
    }

    TEST(SkyrimAmbushWatch, anUnknownPlayerWakesNobody)
    {
        ambush::Watch watch;
        const std::vector<ambush::Sleeper> sleepers = { sleeperAt(10, 0.0, 0.0, 0.0) };
        std::vector<WorldId> wake;
        ambush::Player nobody;
        EXPECT_EQ(watch.update(sleepers, nobody, wake), 0u);
        EXPECT_TRUE(wake.empty());
    }

    TEST(SkyrimAmbushWatch, theMemoryIsBounded)
    {
        ambush::Watch watch;
        std::vector<WorldId> wake;
        // wake more placements than the memory holds: it stops growing and the call still answers
        for (WorldId start = 1; start <= ambush::kMaxRemembered + 64; start += ambush::kMaxWakesPerUpdate)
        {
            std::vector<ambush::Sleeper> sleepers;
            for (WorldId i = 0; i < ambush::kMaxWakesPerUpdate; ++i)
                sleepers.push_back(sleeperAt(start + i, 0.0, 0.0, 0.0));
            wake.clear();
            (void)watch.update(sleepers, playerAt(0.0, 0.0, 0.0), wake);
        }
        EXPECT_LE(watch.wokenCount(), ambush::kMaxRemembered);
    }

    // ---- (2) the host: OnGetUp, OnCombatStateChanged, the natives of the wake handler ------------------------------

    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kPlain = 0x01000A02;
    constexpr FormId kFurniture = 0x01000A03;
    constexpr FormId kCell = 0x01000A10;
    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kBase = 0x01000B00;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class FakeData final : public PapyrusGameData
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
        FormId cellOf(FormId reference) const override
        {
            const auto it = mCells.find(reference);
            return it != mCells.end() ? it->second : 0;
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, FormId> mCells;
    };

    class RecordingHooks final : public PapyrusWorldHooks
    {
    public:
        void actorValueSet(FormId actor, std::string_view lowerName, double value) override
        {
            mSet.push_back({ actor, std::string(lowerName), value });
        }
        struct Set
        {
            FormId mActor;
            std::string mName;
            double mValue;
        };
        std::vector<Set> mSet;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder cell("Cell");
        cell.parent("Form");
        cell.function("IsAttached", "Bool").native();
        data.add(cell);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("GetParentCell", "Cell").native();
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("SetAV").native().param("asValueName", "String").param("afValue", "Float");
        actor.function("SetActorValue").native().param("asValueName", "String").param("afValue", "Float");
        actor.function("EvaluatePackage").native();
        data.add(actor);
    }

    // What masterAmbushScript's waiting state does, without the states: OnGetUp checks that the actor's cell is attached
    // and lets it out (Variable01 1, Aggression 2); OnCombatStateChanged counts.
    ScriptBuilder wakerScript()
    {
        ScriptBuilder s("Waker");
        s.parent("Actor");
        s.var("gotUp", "Int");
        s.var("furn", "ObjectReference");
        s.var("attached", "Bool");
        s.var("fights", "Int");
        s.var("lastState", "Int");
        s.var("target", "Actor");
        s.function("OnGetUp")
            .param("akFurniture", "ObjectReference")
            .local("c", "Cell")
            .local("b", "Bool")
            .local("n", "None")
            .iadd(Id("gotUp"), Id("gotUp"), Int(1))
            .assign(Id("furn"), Id("akFurniture"))
            .callMethod("GetParentCell", Id("self"), Id("c"))
            .callMethod("IsAttached", Id("c"), Id("b"))
            .assign(Id("attached"), Id("b"))
            .callMethod("SetAV", Id("self"), Id("n"), { Str("Variable01"), Flt(1.0f) })
            .callMethod("SetAV", Id("self"), Id("n"), { Str("Aggression"), Flt(2.0f) })
            .callMethod("EvaluatePackage", Id("self"), Id("n"));
        s.function("OnCombatStateChanged")
            .param("akTarget", "Actor")
            .param("aeCombatState", "Int")
            .iadd(Id("fights"), Id("fights"), Int(1))
            .assign(Id("lastState"), Id("aeCombatState"))
            .assign(Id("target"), Id("akTarget"));
        return s;
    }

    ScriptBuilder plainScript()
    {
        ScriptBuilder s("Plain");
        s.parent("Actor");
        s.var("x", "Int");
        s.function("Nothing").iadd(Id("x"), Id("x"), Int(1));
        return s;
    }

    vmad::Script scriptOn(const char* name)
    {
        vmad::Script s;
        s.mName = name;
        return s;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(wakerScript());
            mData.add(plainScript());
            mData.mClasses[kQuest] = "Quest";
            mData.mClasses[kActor] = "Actor";
            mData.mClasses[kPlain] = "Actor";
            mData.mClasses[kFurniture] = "ObjectReference";
            mData.mClasses[kCell] = "Cell";
            mData.mCells[kActor] = kCell;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            ContentTests::Bytes bytes
                = ContentTests::tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            ContentTests::Bytes quest;
            quest.append(ContentTests::sub4("EDID", ContentTests::Bytes().zstring("Q1")));
            quest.append(ContentTests::sub4("DNAM",
                ContentTests::Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(ContentTests::sub4("NEXT", ContentTests::Bytes()));
            bytes.append(ContentTests::group4(
                Content::fourCC("QUST"), 0, ContentTests::record4("QUST", 0u, kQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            EXPECT_EQ(mHost->attachReference(kActor, kBase, true, { scriptOn("Waker") }), 1u);
            EXPECT_EQ(mHost->attachReference(kPlain, kBase, true, { scriptOn("Plain") }), 1u);
            tick();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kActor, "Waker", name); }

        FakeData mData;
        RecordingHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimAmbushHost, onlyAScriptThatHandlesOnGetUpIsAskedToWake)
    {
        Rig rig;
        EXPECT_TRUE(rig.mHost->handlesGetUp(kActor));
        EXPECT_FALSE(rig.mHost->handlesGetUp(kPlain));
        EXPECT_FALSE(rig.mHost->handlesGetUp(0x01000FFF)); // no instance at all
        EXPECT_FALSE(rig.mHost->handlesGetUp(0));
    }

    TEST(SkyrimAmbushHost, onGetUpRunsTheHandlerWithTheFurnitureAndTheNativesItCalls)
    {
        Rig rig;
        rig.mHost->onGetUp(kActor, kFurniture);
        rig.tick();
        EXPECT_EQ(rig.number("gotUp"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Waker", "furn"), kFurniture);
        // GetParentCell gave the actor's cell and Cell.IsAttached answered true for it
        EXPECT_EQ(rig.number("attached"), 1.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimAmbushHost, aScriptsSetAvOfVariable01AndAggressionIsToldToTheWorld)
    {
        Rig rig;
        rig.mHost->onGetUp(kActor, kFurniture);
        rig.tick();
        bool variable = false, aggression = false;
        for (const RecordingHooks::Set& set : rig.mHooks.mSet)
        {
            if (set.mActor != kActor)
                continue;
            if (set.mName == "variable01" && set.mValue == 1.0)
                variable = true;
            if (set.mName == "aggression" && set.mValue == 2.0)
                aggression = true;
        }
        EXPECT_TRUE(variable);
        EXPECT_TRUE(aggression);
    }

    TEST(SkyrimAmbushHost, aScriptsVariable01IsWhatTheConditionOverlayReads)
    {
        Rig rig;
        FollowerState state;
        rig.mHost->setFollowerState(&state);
        EXPECT_FALSE(state.actorValue(kActor, kVariable01Index).has_value()); // nothing set it yet
        rig.mHost->onGetUp(kActor, kFurniture);
        rig.tick();
        const std::optional<float> value = state.actorValue(kActor, kVariable01Index);
        ASSERT_TRUE(value.has_value());
        EXPECT_FLOAT_EQ(*value, 1.f);
        EXPECT_FALSE(state.actorValue(kPlain, kVariable01Index).has_value()); // another actor: untouched
    }

    TEST(SkyrimAmbushHost, onCombatStateChangedCarriesTheTargetAndTheState)
    {
        Rig rig;
        rig.mHost->onCombatStateChanged(kActor, kPlain, 1);
        rig.tick();
        EXPECT_EQ(rig.number("fights"), 1.0);
        EXPECT_EQ(rig.number("lastState"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Waker", "target"), kPlain);
        rig.mHost->onCombatStateChanged(kActor, 0, 0);
        rig.tick();
        EXPECT_EQ(rig.number("fights"), 2.0);
        EXPECT_EQ(rig.number("lastState"), 0.0);
    }

    TEST(SkyrimAmbushHost, theEventsGoToTheScriptsOfThatActorOnly)
    {
        Rig rig;
        rig.mHost->onGetUp(kPlain, kFurniture);
        rig.mHost->onCombatStateChanged(kPlain, kActor, 1);
        rig.tick();
        EXPECT_EQ(rig.number("gotUp"), 0.0);
        EXPECT_EQ(rig.number("fights"), 0.0);
        EXPECT_TRUE(rig.mErrors.empty()); // a script without the handler is no error
    }

    TEST(SkyrimAmbushHost, withNoHooksASetAvStillWorksAndTellsNobody)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        rig.mHost->onGetUp(kActor, kFurniture);
        rig.tick();
        EXPECT_EQ(rig.number("gotUp"), 1.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    // ---- (3) the condition overlay ---------------------------------------------------------------------------------

    TEST(SkyrimAmbushOverlay, variable01ComesFromTheActorValueSourceAndWaitingForPlayerStillDoes)
    {
        FollowerState state;
        state.setActorValueSource([](FormId reference, std::string_view key) -> std::optional<float> {
            if (reference != 7)
                return std::nullopt;
            if (key == kVariable01Key)
                return 1.f;
            if (key == kWaitingForPlayerKey)
                return 0.75f;
            return std::nullopt;
        });
        EXPECT_FLOAT_EQ(state.actorValue(7, kVariable01Index).value_or(-1.f), 1.f);
        EXPECT_FLOAT_EQ(state.actorValue(7, kWaitingForPlayerIndex).value_or(-1.f), 0.75f);
        EXPECT_FALSE(state.actorValue(8, kVariable01Index).has_value());
        EXPECT_FALSE(state.actorValue(7, 0x45).has_value()); // another actor value: not ours
    }

    TEST(SkyrimAmbushOverlay, withNoSourceNothingIsSet)
    {
        FollowerState state;
        EXPECT_FALSE(state.actorValue(7, kVariable01Index).has_value());
    }

    // ---- (4) the combat AI --------------------------------------------------------------------------------------------

    constexpr WorldId kCombatBase = 100;

    class FakeCombatData final : public CombatData
    {
    public:
        FakeCombatData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 100.f;
            npc.mSkills[0] = 50;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4; // foolhardy: it never flees in these tests
            npc.mAi.mAggression = 0; // an ambusher's record: calm
            mProfiles[kCombatBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeCombatWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct AmbushCombat : ::testing::Test
    {
        FakeCombatData mData;
        SkyrimCombat mCombat;
        FakeCombatWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<std::string> mTrace;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        std::vector<std::pair<ActorHandle, bool>> mStates;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.setCombatStateSink([this](ActorHandle actor, bool engaged) { mStates.emplace_back(actor, engaged); });
            mCombat.onActorAdded(infoOf(10, kCombatBase, "Draugr"));
            mWriter.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 100.0, 600.0, 5.0, 0.f, 90.f, 100.f };
        }
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mCombat.combatUpdate(dt, mWriter);
                mCombat.drainAiMoves(mMoves);
            }
        }
        bool traced(const std::string& text) const
        {
            for (const std::string& line : mTrace)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
    };

    TEST_F(AmbushCombat, aCalmActorStaysCalmUntilAScriptSetsItsAggression)
    {
        run(40);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        mCombat.setAggression(mA, 2);
        run(40);
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(traced("(on sight)"));
    }

    TEST_F(AmbushCombat, aggressionOneAndTwoBothAttackANeutralPlayerOnSightButZeroDoesNot)
    {
        mCombat.setAggression(mA, 0);
        run(40);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        mCombat.setAggression(mA, 1);
        run(40);
        EXPECT_EQ(mCombat.aiEngaged(), 0u) << "aggression 1 attacks enemies only: the player is a neutral here";
    }

    TEST_F(AmbushCombat, theFightIsToldToThePackAndSoIsItsEnd)
    {
        mCombat.setAggression(mA, 2);
        run(40);
        ASSERT_EQ(mStates.size(), 1u);
        EXPECT_EQ(mStates[0].first, mA);
        EXPECT_TRUE(mStates[0].second);
        EXPECT_TRUE(mCombat.scriptStopCombat(mA));
        run(2);
        ASSERT_GE(mStates.size(), 2u);
        EXPECT_EQ(mStates.back().first, mA);
        EXPECT_FALSE(mStates.back().second);
    }

    TEST_F(AmbushCombat, aCalmedActorIsNotHostileOnSightAfterwards)
    {
        mCombat.setAggression(mA, 2);
        run(40);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        ASSERT_TRUE(mCombat.scriptStopCombat(mA));
        mCombat.setAggression(mA, 0); // masterAmbushScript.onReset
        run(5);
        const std::size_t before = mStates.size();
        run(80); // the actor rests after a script's stop, and with aggression 0 it never looks again
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(mStates.size(), before);
    }

    TEST_F(AmbushCombat, anAggressionSetBeforeTheActorIsKnownIsAppliedWhenItIsAdded)
    {
        const ActorHandle later{ 11, 1 };
        mWriter.mStates[11] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.setAggression(later, 3); // the script ran before the actor reached the combat
        mCombat.onActorAdded(infoOf(11, kCombatBase, "Draugr"));
        run(40);
        EXPECT_GE(mCombat.aiEngaged(), 1u);
        bool laterEngaged = false;
        for (const auto& s : mStates)
            if (s.first == later && s.second)
                laterEngaged = true;
        EXPECT_TRUE(laterEngaged);
    }

    TEST_F(AmbushCombat, anotherActorIsNotAffected)
    {
        const ActorHandle other{ 12, 1 };
        mWriter.mStates[12] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.onActorAdded(infoOf(12, kCombatBase, "Draugr"));
        mCombat.setAggression(mA, 2);
        run(40);
        for (const auto& s : mStates)
            EXPECT_NE(s.first, other);
    }

    TEST_F(AmbushCombat, theAggressionIsCutToThree)
    {
        mCombat.setAggression(mA, 200);
        run(40);
        EXPECT_EQ(mCombat.aiEngaged(), 1u); // a frenzied actor still attacks; nothing overflows
    }
}
