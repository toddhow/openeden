// Task 0176, milestone M-S6 (Papyrus wiring): the Skyrim SE pack's Papyrus host, run on generated scripts, a generated
// quest index and a fake game. No game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Str;
    namespace Op = PexAsm::Op;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800; // start game enabled, stage 10 has a fragment
    constexpr FormId kQuestB = 0x01000801; // not start enabled
    constexpr FormId kQuestC = 0x01000802; // start enabled, with a start up stage 10
    constexpr FormId kRefA = 0x01000A00;
    constexpr FormId kRefB = 0x01000A01;
    constexpr FormId kBaseA = 0x01000B00;
    constexpr FormId kPlayer = 0x14;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes stage(std::uint16_t index, std::uint8_t flags)
    {
        Bytes b;
        b.append(sub4("INDX", Bytes().u16(index).u8(flags).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return b;
    }

    Bytes questRecord(std::uint32_t id, std::uint16_t flags, const Bytes& stages, const Bytes& aliases = Bytes())
    {
        Bytes b;
        b.append(edid("Q" + std::to_string(id & 0xFFF)));
        b.append(sub4("DNAM", Bytes().u16(flags).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(stages);
        b.append(sub4("ANAM", Bytes().u32(8)));
        b.append(aliases);
        return record4("QUST", 0u, id, b);
    }

    Bytes aliasRef(std::uint32_t id, std::uint32_t reference)
    {
        Bytes b;
        b.append(sub4("ALST", Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring("Alias" + std::to_string(id))));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(sub4("ALFR", Bytes().u32(reference)));
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    // ---- the fake game ------------------------------------------------------------------------------------------

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            ++mReads;
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
            const auto it = mPositions.find(reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        FormId baseOf(FormId reference) const override
        {
            const auto it = mBases.find(reference);
            return it != mBases.end() ? it->second : 0;
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
        std::map<FormId, std::array<double, 3>> mPositions;
        std::map<FormId, FormId> mBases;
        int mReads = 0;
    };

    // The native classes of Skyrim as the tests need them (the real ones come from the game's .pex files).
    void addNativeClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        form.function("GetName", "String").native();
        form.function("RegisterForSingleUpdate").native().param("afInterval", "Float");
        form.function("RegisterForUpdate").native().param("afInterval", "Float");
        form.function("UnregisterForUpdate").native();
        data.add(form);

        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("Enable").native().param("abFadeIn", "Bool");
        ref.function("Disable").native().param("abFadeOut", "Bool");
        ref.function("IsDisabled", "Bool").native();
        ref.function("GetPositionX", "Float").native();
        ref.function("GetPositionY", "Float").native();
        ref.function("GetPositionZ", "Float").native();
        ref.function("GetBaseObject", "Form").native();
        ref.function("Activate", "Bool")
            .native()
            .param("akActivator", "ObjectReference")
            .param("abDefaultProcessingOnly", "Bool");
        data.add(ref);

        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("IsGhost", "Bool").native();
        data.add(actor);

        ScriptBuilder quest("Quest");
        quest.parent("Form");
        quest.function("SetStage", "Bool").native().param("auiStage", "Int");
        quest.function("GetStage", "Int").native();
        quest.function("GetStageDone", "Bool").native().param("auiStage", "Int");
        quest.function("IsRunning", "Bool").native();
        quest.function("Start", "Bool").native();
        quest.function("Stop").native();
        data.add(quest);

        ScriptBuilder alias("Alias");
        alias.function("GetOwningQuest", "Quest").native();
        data.add(alias);
        ScriptBuilder refAlias("ReferenceAlias");
        refAlias.parent("Alias");
        refAlias.function("GetReference", "ObjectReference").native();
        refAlias.function("ForceRefTo").native().param("akNewRef", "ObjectReference");
        data.add(refAlias);

        ScriptBuilder game("Game");
        game.function("GetPlayer", "Actor").global().native();
        data.add(game);

        ScriptBuilder utility("Utility");
        utility.function("Wait").global().native().param("afWaitTimeSeconds", "Float");
        data.add(utility);
    }

    struct Rig
    {
        Rig()
        {
            addNativeClasses(mData);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kQuestB] = "Quest";
            mData.mClasses[kQuestC] = "Quest";
            mData.mClasses[kRefA] = "ObjectReference";
            mData.mClasses[kRefB] = "Actor";
            mData.mClasses[kPlayer] = "Actor";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
        }

        ~Rig()
        {
            if (::testing::Test::HasFailure())
            {
                for (const std::string& line : mLogs)
                    std::cerr << "  log: " << line << std::endl;
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
            }
        }

        void quests(const std::vector<Bytes>& records)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : records)
                all.append(r);
            bytes.append(group4(Content::fourCC("QUST"), 0, all));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [this](FormId quest) -> const vmad::Vmad* {
                const auto it = mVmads.find(quest);
                return it != mVmads.end() ? &it->second : nullptr;
            });
        }

        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }

        std::optional<double> number(FormId form, const std::string& script, const std::string& name)
        {
            return mHost->numberVariable(form, script, name);
        }

        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::map<FormId, vmad::Vmad> mVmads;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    vmad::Property intProp(const std::string& name, std::int32_t value)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 3;
        p.mValue.mKind = vmad::ValueKind::Int;
        p.mValue.mInteger = value;
        return p;
    }
    vmad::Property objProp(const std::string& name, FormId form, std::int16_t alias = -1)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = alias;
        return p;
    }
    vmad::Script script(const std::string& name, std::vector<vmad::Property> props = {})
    {
        vmad::Script s;
        s.mName = name;
        s.mProperties = std::move(props);
        return s;
    }
    vmad::Vmad vmadOf(std::vector<vmad::Script> scripts)
    {
        vmad::Vmad v;
        v.mScripts = std::move(scripts);
        return v;
    }

    // A quest script with one of each kind of property and an OnInit that records that it ran.
    ScriptBuilder questScript()
    {
        ScriptBuilder s("QS");
        s.parent("Quest");
        s.var("ran", "Int");
        s.var("lastStage", "Int", Int(-1));
        s.autoProperty("Count", "Int");
        s.autoProperty("Label", "String");
        s.autoProperty("Ratio", "Float");
        s.autoProperty("Flag", "Bool");
        s.autoProperty("Friend", "Quest");
        s.function("OnInit").assign(Id("ran"), Int(1));
        s.function("OnStageSet")
            .param("auiStageID", "Int")
            .param("auiItemID", "Int")
            .assign(Id("lastStage"), Id("auiStageID"));
        return s;
    }

    // ---- attaching and properties ---------------------------------------------------------------------------------

    TEST(SkyrimPapyrusHost, startGame_attaches_the_scripts_of_start_enabled_quests_only_and_runs_OnInit)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)),
            questRecord(kQuestB, 0, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("QS") });
        rig.mVmads[kQuestB] = vmadOf({ script("QS") });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestA, "QS", "ran"), 1.0);
        EXPECT_FALSE(rig.number(kQuestB, "QS", "ran").has_value())
            << "a quest that does not start enabled is not attached";
        EXPECT_EQ(rig.mHost->counters().mInstancesAttached, 1u);
        EXPECT_EQ(rig.mHost->counters().mEvents.at("oninit"), 1u);
    }

    TEST(SkyrimPapyrusHost, startGame_runs_only_once)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("QS") });
        rig.mHost->startGame();
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.mHost->counters().mInstancesAttached, 1u);
    }

    TEST(SkyrimPapyrusHost, properties_of_every_kind_are_filled_before_OnInit_runs)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)),
            questRecord(kQuestB, 0, stage(10, 0)) });
        vmad::Property label;
        label.mName = "Label";
        label.mType = 2;
        label.mValue.mKind = vmad::ValueKind::String;
        label.mValue.mText = "hello";
        vmad::Property ratio;
        ratio.mName = "Ratio";
        ratio.mType = 4;
        ratio.mValue.mKind = vmad::ValueKind::Float;
        ratio.mValue.mNumber = 2.5f;
        vmad::Property flag;
        flag.mName = "Flag";
        flag.mType = 5;
        flag.mValue.mKind = vmad::ValueKind::Bool;
        flag.mValue.mInteger = 1;
        rig.mVmads[kQuestA]
            = vmadOf({ script("QS", { intProp("Count", 7), label, ratio, flag, objProp("Friend", kQuestB) }) });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestA, "QS", "Count"), 7.0);
        EXPECT_EQ(rig.number(kQuestA, "QS", "Ratio"), 2.5);
        EXPECT_EQ(rig.number(kQuestA, "QS", "Flag"), 1.0);
        EXPECT_EQ(rig.mHost->stringVariable(kQuestA, "QS", "Label"), std::optional<std::string>("hello"));
        EXPECT_EQ(rig.mHost->formVariable(kQuestA, "QS", "Friend"), std::optional<FormId>(kQuestB));
        EXPECT_EQ(rig.mHost->counters().mPropertiesSet, 5u);
        EXPECT_EQ(rig.mHost->counters().mPropertiesRejected, 0u);
    }

    TEST(SkyrimPapyrusHost, a_property_the_script_does_not_have_is_skipped_and_counted)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("QS", { intProp("NoSuchProperty", 1), intProp("Count", 3) }) });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestA, "QS", "Count"), 3.0);
        EXPECT_EQ(rig.mHost->counters().mPropertiesSet, 1u);
        EXPECT_EQ(rig.mHost->counters().mPropertiesSkipped + rig.mHost->counters().mPropertiesRejected, 1u);
    }

    TEST(SkyrimPapyrusHost, a_script_with_no_pex_is_counted_and_the_others_still_attach)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("NoSuchScript"), script("QS") });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.mHost->counters().mScriptsMissing, 1u);
        EXPECT_EQ(rig.number(kQuestA, "QS", "ran"), 1.0);
    }

    TEST(SkyrimPapyrusHost, the_parent_and_the_classes_a_script_names_are_loaded_from_the_string_table)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("QS") });
        rig.mHost->startGame();
        EXPECT_GE(rig.mHost->counters().mScriptsLoaded, 3u) << "QS, Quest and Form at least";
        const int reads = rig.mData.mReads;
        rig.mHost->attachQuest(kQuestB); // nothing to attach: no VMAD
        EXPECT_EQ(rig.mData.mReads, reads) << "a script already loaded is never read again";
    }

    // ---- quest natives, fragments and OnStageSet ------------------------------------------------------------------

    // A script that sets stage 10 on its own quest from OnInit, and one that records what happened.
    ScriptBuilder driverScript()
    {
        ScriptBuilder s("Driver");
        s.parent("Quest");
        s.var("result", "Int");
        s.function("OnInit")
            .local("r", "Bool")
            .callMethod("SetStage", Id("self"), Id("r"), { Int(10) })
            .assign(Id("result"), Id("r"));
        return s;
    }

    ScriptBuilder fragmentScript()
    {
        ScriptBuilder s("QF_Test");
        s.parent("Quest");
        s.var("fragRan", "Int");
        s.function("Fragment_3").assign(Id("fragRan"), Int(1));
        return s;
    }

    vmad::Vmad questWithFragment(std::vector<vmad::Script> scripts)
    {
        vmad::Vmad v = vmadOf(std::move(scripts));
        v.mHasFragments = true;
        v.mFragmentScript = "QF_Test";
        v.mFragments.push_back({ 10, 1, "QF_Test", "Fragment_3" });
        return v;
    }

    TEST(SkyrimPapyrusHost, SetStage_changes_the_quest_state_runs_the_fragment_and_sends_OnStageSet)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.mData.add(driverScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = questWithFragment({ script("QF_Test"), script("QS"), script("Driver") });
        std::vector<std::pair<FormId, std::uint32_t>> changes;
        rig.mHost->setQuestChanged([&](FormId q, std::uint32_t s) { changes.emplace_back(q, s); });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.mState.stage(kQuestA), 10.f);
        EXPECT_TRUE(rig.mState.stageDone(kQuestA, 10));
        EXPECT_EQ(rig.number(kQuestA, "Driver", "result"), 1.0);
        EXPECT_EQ(rig.number(kQuestA, "QF_Test", "fragRan"), 1.0) << "the fragment of stage 10";
        EXPECT_EQ(rig.number(kQuestA, "QS", "lastStage"), 10.0) << "OnStageSet reaches the other scripts of the quest";
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].first, kQuestA);
        EXPECT_EQ(changes[0].second, 10u);
        EXPECT_EQ(rig.mHost->counters().mStageSets, 1u);
        EXPECT_EQ(rig.mHost->counters().mFragmentsRun, 1u);
    }

    TEST(SkyrimPapyrusHost, setting_a_stage_that_is_done_does_not_run_its_fragment_again)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.mData.add(driverScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = questWithFragment({ script("QF_Test"), script("Driver") });
        rig.mHost->startGame();
        rig.tick();
        ASSERT_EQ(rig.mHost->counters().mFragmentsRun, 1u);
        EXPECT_TRUE(rig.mHost->callFunction(kQuestA, "Driver", "OnInit")); // SetStage(10) once more
        rig.tick();
        EXPECT_EQ(rig.mHost->counters().mFragmentsRun, 1u);
        EXPECT_EQ(rig.mHost->counters().mStageSets, 1u);
    }

    TEST(SkyrimPapyrusHost, the_stage_fragment_of_a_start_up_stage_runs_when_the_game_starts)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestC, QuestFlags::StartGameEnabled, stage(10, StageFlags::StartUp)) });
        rig.mVmads[kQuestC] = questWithFragment({ script("QF_Test"), script("QS") });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestC, "QF_Test", "fragRan"), 1.0);
        EXPECT_EQ(rig.mHost->counters().mFragmentsRun, 1u);
        EXPECT_EQ(rig.mHost->counters().mStageSets, 0u) << "the start up stage was already set by the initial state";
    }

    TEST(SkyrimPapyrusHost, quest_natives_read_the_state)
    {
        Rig rig;
        ScriptBuilder s("Reader");
        s.parent("Quest");
        s.var("stageSeen", "Int");
        s.var("running", "Bool");
        s.var("done10", "Bool");
        s.var("done99", "Bool");
        s.function("OnInit")
            .local("v", "Int")
            .callMethod("GetStage", Id("self"), Id("v"))
            .assign(Id("stageSeen"), Id("v"))
            .local("b", "Bool")
            .callMethod("IsRunning", Id("self"), Id("b"))
            .assign(Id("running"), Id("b"))
            .callMethod("GetStageDone", Id("self"), Id("b"), { Int(10) })
            .assign(Id("done10"), Id("b"))
            .callMethod("GetStageDone", Id("self"), Id("b"), { Int(99) })
            .assign(Id("done99"), Id("b"));
        rig.mData.add(s);
        rig.quests({ questRecord(kQuestC, QuestFlags::StartGameEnabled, stage(10, StageFlags::StartUp)) });
        rig.mVmads[kQuestC] = vmadOf({ script("Reader") });
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestC, "Reader", "stageSeen"), 10.0);
        EXPECT_EQ(rig.number(kQuestC, "Reader", "running"), 1.0);
        EXPECT_EQ(rig.number(kQuestC, "Reader", "done10"), 1.0);
        EXPECT_EQ(rig.number(kQuestC, "Reader", "done99"), 0.0);
        EXPECT_GE(rig.mHost->counters().mNatives.at("quest.getstage"), 1u);
    }

    // ---- time: updates and waits ----------------------------------------------------------------------------------

    TEST(SkyrimPapyrusHost, RegisterForSingleUpdate_sends_OnUpdate_once_when_the_time_has_come)
    {
        Rig rig;
        ScriptBuilder s("Timer");
        s.parent("Quest");
        s.var("updates", "Int");
        s.function("OnInit")
            .local("n", "None")
            .callMethod("RegisterForSingleUpdate", Id("self"), Id("n"), { Flt(2.0f) });
        s.function("OnUpdate").iadd(Id("updates"), Id("updates"), Int(1));
        rig.mData.add(s);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("Timer") });
        rig.mHost->startGame();
        rig.mHost->tick(1.0f); // OnInit runs at script time 1.0 and asks for an update 2 seconds later: due at 3.0
        EXPECT_EQ(rig.number(kQuestA, "Timer", "updates"), 0.0);
        rig.mHost->tick(1.0f);
        EXPECT_EQ(rig.number(kQuestA, "Timer", "updates"), 0.0) << "time 2.0";
        rig.mHost->tick(0.5f);
        EXPECT_EQ(rig.number(kQuestA, "Timer", "updates"), 0.0) << "time 2.5";
        rig.mHost->tick(0.6f);
        EXPECT_EQ(rig.number(kQuestA, "Timer", "updates"), 1.0) << "time 3.1";
        rig.mHost->tick(5.0f);
        EXPECT_EQ(rig.number(kQuestA, "Timer", "updates"), 1.0) << "single means single";
        EXPECT_EQ(rig.mHost->counters().mUpdatesFired, 1u);
    }

    TEST(SkyrimPapyrusHost, Utility_Wait_parks_the_script_until_the_script_time_has_passed)
    {
        Rig rig;
        ScriptBuilder s("Waiter");
        s.parent("Quest");
        s.var("before", "Int");
        s.var("after", "Int");
        s.function("OnInit")
            .local("n", "None")
            .assign(Id("before"), Int(1))
            .callStatic("Utility", "Wait", Id("n"), { Flt(3.0f) })
            .assign(Id("after"), Int(1));
        rig.mData.add(s);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("Waiter") });
        rig.mHost->startGame();
        rig.mHost->tick(1.0f);
        EXPECT_EQ(rig.number(kQuestA, "Waiter", "before"), 1.0);
        EXPECT_EQ(rig.number(kQuestA, "Waiter", "after"), 0.0);
        rig.mHost->tick(1.0f);
        EXPECT_EQ(rig.number(kQuestA, "Waiter", "after"), 0.0);
        rig.mHost->tick(2.1f); // time 4.1: the wait began at 1.0
        EXPECT_EQ(rig.number(kQuestA, "Waiter", "after"), 1.0);
    }

    // ---- references -----------------------------------------------------------------------------------------------

    ScriptBuilder refScript()
    {
        ScriptBuilder s("RefScript");
        s.parent("ObjectReference");
        s.var("order", "Int");
        s.var("activator", "Int");
        s.var("posX", "Float");
        s.var("player", "Int");
        s.var("disabled", "Bool");
        s.var("combat", "Bool");
        s.autoProperty("Owner", "Quest");
        s.function("OnCellLoad")
            .binary(Op::IMul, Id("order"), Id("order"), Int(10))
            .iadd(Id("order"), Id("order"), Int(1));
        s.function("OnLoad").binary(Op::IMul, Id("order"), Id("order"), Int(10)).iadd(Id("order"), Id("order"), Int(2));
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("id", "Int")
            .callMethod("GetFormID", Id("akActionRef"), Id("id"))
            .assign(Id("activator"), Id("id"));
        s.function("Probe")
            .local("x", "Float")
            .callMethod("GetPositionX", Id("self"), Id("x"))
            .assign(Id("posX"), Id("x"))
            .local("p", "Actor")
            .callStatic("Game", "GetPlayer", Id("p"))
            .local("pid", "Int")
            .callMethod("GetFormID", Id("p"), Id("pid"))
            .assign(Id("player"), Id("pid"))
            .local("c", "Bool")
            .callMethod("IsGhost", Id("p"), Id("c"))
            .assign(Id("combat"), Id("c"));
        s.function("DisableMe").local("n", "None").callMethod("Disable", Id("self"), Id("n"), { Bool(false) });
        return s;
    }

    TEST(SkyrimPapyrusHost, a_reference_script_gets_OnInit_then_OnCellLoad_then_OnLoad_when_its_cell_loads)
    {
        Rig rig;
        rig.mData.add(refScript());
        rig.mData.mBases[kRefA] = kBaseA;
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        EXPECT_EQ(
            rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript", { objProp("Owner", kQuestA) }) }),
            1u);
        rig.mHost->onCellLoaded(0x01000C00, { kRefA, kRefB });
        rig.tick();
        EXPECT_EQ(rig.number(kRefA, "RefScript", "order"), 12.0);
        EXPECT_EQ(rig.mHost->formVariable(kRefA, "RefScript", "Owner"), std::optional<FormId>(kQuestA));
        EXPECT_EQ(rig.mHost->counters().mEvents.at("oncellload"), 1u);
        EXPECT_EQ(rig.mHost->counters().mEvents.at("onload"), 1u);
    }

    TEST(SkyrimPapyrusHost, OnActivate_carries_the_activating_reference)
    {
        Rig rig;
        rig.mData.add(refScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript") });
        rig.mHost->onActivate(kRefA, kPlayer);
        rig.tick();
        EXPECT_EQ(rig.number(kRefA, "RefScript", "activator"), static_cast<double>(kPlayer));
        EXPECT_EQ(rig.mHost->counters().mEvents.at("onactivate"), 1u);
    }

    TEST(SkyrimPapyrusHost, an_event_for_a_reference_with_no_script_does_nothing)
    {
        Rig rig;
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->onActivate(kRefB, kPlayer);
        rig.mHost->onCellLoaded(0x01000C00, { kRefB });
        rig.tick();
        EXPECT_TRUE(rig.mHost->counters().mEvents.empty());
    }

    TEST(SkyrimPapyrusHost, object_natives_answer_from_the_game_data)
    {
        Rig rig;
        rig.mData.add(refScript());
        rig.mData.mPositions[kRefA] = { 100.0, 200.0, 300.0 };
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript") });
        rig.tick();
        const auto result = rig.mHost->callFunction(kRefA, "RefScript", "Probe");
        EXPECT_TRUE(result) << "Probe ran";
        EXPECT_EQ(rig.number(kRefA, "RefScript", "posX"), 100.0);
        EXPECT_EQ(rig.number(kRefA, "RefScript", "player"), static_cast<double>(kPlayer)) << "Game.GetPlayer";
    }

    TEST(SkyrimPapyrusHost, a_native_nobody_answers_is_tolerant_logged_once_and_listed)
    {
        Rig rig;
        rig.mData.add(refScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript") });
        rig.tick();
        EXPECT_TRUE(rig.mHost->callFunction(kRefA, "RefScript", "Probe"));
        EXPECT_TRUE(rig.mHost->callFunction(kRefA, "RefScript", "Probe"));
        EXPECT_EQ(rig.number(kRefA, "RefScript", "combat"), 0.0) << "the zero of a Bool";
        const std::vector<std::string> tolerant = rig.mHost->tolerantNatives();
        ASSERT_EQ(tolerant.size(), 1u);
        EXPECT_EQ(tolerant[0], "actor.isghost");
        int logged = 0;
        for (const std::string& line : rig.mLogs)
            logged += line.find("IsGhost") != std::string::npos ? 1 : 0;
        EXPECT_EQ(logged, 1) << "the runtime logs a tolerant native once";
        EXPECT_GE(rig.mHost->runtimeStats().unregisteredNativeCalls, 2u);
    }

    TEST(SkyrimPapyrusHost, Disable_changes_the_enabled_state_and_tells_the_sink)
    {
        Rig rig;
        rig.mData.add(refScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        std::vector<std::pair<FormId, bool>> seen;
        rig.mHost->setEnableChanged([&](FormId r, bool e) { seen.emplace_back(r, e); });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript") });
        rig.tick();
        EXPECT_TRUE(rig.mHost->referenceEnabled(kRefA));
        EXPECT_TRUE(rig.mHost->callFunction(kRefA, "RefScript", "DisableMe"));
        EXPECT_FALSE(rig.mHost->referenceEnabled(kRefA));
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0].first, kRefA);
        EXPECT_FALSE(seen[0].second);
    }

    // ---- aliases --------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusHost, an_alias_property_reads_the_filled_reference_and_an_alias_script_is_attached)
    {
        Rig rig;
        ScriptBuilder holder("AliasHolder");
        holder.parent("Quest");
        holder.var("refId", "Int");
        holder.autoProperty("TheAlias", "ReferenceAlias");
        holder.function("OnInit")
            .local("r", "ObjectReference")
            .callMethod("GetReference", Id("::TheAlias_var"), Id("r"))
            .local("id", "Int")
            .callMethod("GetFormID", Id("r"), Id("id"))
            .assign(Id("refId"), Id("id"));
        rig.mData.add(holder);
        ScriptBuilder aliasScript("AliasScript");
        aliasScript.parent("ReferenceAlias");
        aliasScript.var("ran", "Int");
        aliasScript.function("OnInit").assign(Id("ran"), Int(1));
        rig.mData.add(aliasScript);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0), aliasRef(2, kRefA)) });
        vmad::Vmad v = vmadOf({ script("AliasHolder", { objProp("TheAlias", kQuestA, 2) }) });
        vmad::AliasScripts as;
        as.mQuest = kQuestA;
        as.mAlias = 2;
        as.mScripts.push_back(script("AliasScript"));
        v.mAliases.push_back(as);
        rig.mVmads[kQuestA] = v;
        rig.mHost->startGame();
        rig.tick();
        EXPECT_EQ(rig.number(kQuestA, "AliasHolder", "refId"), static_cast<double>(kRefA));
        EXPECT_EQ(rig.mHost->aliasScriptNumber(kQuestA, 2, "AliasScript", "ran"), std::optional<double>(1.0));
    }

    // ---- determinism, counters and the save hook ------------------------------------------------------------------

    TEST(SkyrimPapyrusHost, two_hosts_given_the_same_input_leave_the_same_counters)
    {
        auto run = []() {
            Rig rig;
            rig.mData.add(questScript());
            rig.mData.add(driverScript());
            rig.mData.add(fragmentScript());
            rig.mData.add(refScript());
            rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)),
                questRecord(kQuestC, QuestFlags::StartGameEnabled, stage(10, StageFlags::StartUp)) });
            rig.mVmads[kQuestA] = questWithFragment({ script("QF_Test"), script("QS"), script("Driver") });
            rig.mVmads[kQuestC] = questWithFragment({ script("QF_Test"), script("QS") });
            rig.mHost->startGame();
            rig.mHost->attachReference(kRefA, kBaseA, false, { script("RefScript") });
            rig.mHost->onCellLoaded(1, { kRefA });
            rig.tick(10);
            const PapyrusHostCounters& c = rig.mHost->counters();
            std::string text;
            for (const auto& [k, v] : c.mEvents)
                text += k + "=" + std::to_string(v) + ";";
            for (const auto& [k, v] : c.mNatives)
                text += k + "=" + std::to_string(v) + ";";
            text += std::to_string(c.mInstancesAttached) + "/" + std::to_string(c.mFragmentsRun) + "/"
                + std::to_string(rig.mHost->runtimeStats().instructions);
            return text;
        };
        EXPECT_EQ(run(), run());
        EXPECT_NE(run().find("oninit="), std::string::npos) << "the run did something";
    }

    TEST(SkyrimPapyrusHost, the_save_hook_round_trips_the_runtime_state_between_ticks)
    {
        Rig rig;
        rig.mData.add(questScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = vmadOf({ script("QS", { intProp("Count", 4) }) });
        rig.mHost->startGame();
        rig.tick();
        const openeden::script::SaveBlob blob = rig.mHost->serialiseState();
        EXPECT_NE(blob.formatVersion, 0u);
        EXPECT_FALSE(blob.bytes.empty());
        EXPECT_TRUE(rig.mHost->restoreState(blob));
        EXPECT_EQ(rig.number(kQuestA, "QS", "Count"), 4.0);
    }

    // Task 0310 (A Daedra's Best Friend): the meeting trigger of Barbas is a box with no script of its own; the game's
    // defaultSetStageAliasScript sits on the QUEST ALIAS that holds it, so the player entering the volume must reach
    // the alias script (written first, red against the host that only told scripts on the reference itself)
    TEST(SkyrimPapyrusHost, a_trigger_volume_wakes_the_alias_script_of_the_reference_a_quest_alias_holds)
    {
        Rig rig;
        ScriptBuilder aliasScript("TriggerAlias");
        aliasScript.parent("ReferenceAlias");
        aliasScript.var("entered", "Int");
        aliasScript.function("OnTriggerEnter")
            .param("akActionRef", "ObjectReference")
            .iadd(Id("entered"), Id("entered"), Int(1));
        rig.mData.add(aliasScript);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0), aliasRef(2, kRefA)) });
        vmad::Vmad v = vmadOf({});
        vmad::AliasScripts as;
        as.mQuest = kQuestA;
        as.mAlias = 2;
        as.mScripts.push_back(script("TriggerAlias"));
        v.mAliases.push_back(as);
        rig.mVmads[kQuestA] = v;
        rig.mHost->startGame();
        rig.tick();
        TriggerVolume volume;
        volume.mType = kPrimitiveSphere;
        volume.mBounds[0] = 100.0;
        ASSERT_TRUE(rig.mHost->addTrigger(kRefA, volume));
        PapyrusPlayerView view;
        view.mKnown = true;
        view.mX = 10.0;
        view.mPlace.mKnown = true;
        rig.mHost->setPlayerView(view);
        EXPECT_EQ(rig.mHost->updateTriggers(), 1u);
        rig.tick();
        EXPECT_EQ(rig.mHost->aliasScriptNumber(kQuestA, 2, "TriggerAlias", "entered"), std::optional<double>(1.0));
        // and leaving sends no OnTriggerEnter again
        view.mX = 5000.0;
        rig.mHost->setPlayerView(view);
        (void)rig.mHost->updateTriggers();
        rig.tick();
        EXPECT_EQ(rig.mHost->aliasScriptNumber(kQuestA, 2, "TriggerAlias", "entered"), std::optional<double>(1.0));
    }
}
