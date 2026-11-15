// Task 0181, milestone M-S6 (Papyrus slice 2): the new natives (MoveTo, HasKeyword, SetCurrentStageID, IsStageDone,
// AddSpell), the enable overrides, and the host's save state (saveState / restoreSavedState), on generated scripts, a
// generated quest index and a fake game. No game data. Written first and run red against stubs (the new members of
// PapyrusHost and PapyrusGameData answered nothing and refused).
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kRefA = 0x01000A00;
    constexpr FormId kRefB = 0x01000A01;
    constexpr FormId kKeywordA = 0x01000C01;
    constexpr FormId kKeywordB = 0x01000C02;
    constexpr FormId kSpellA = 0x01000D01;
    constexpr FormId kBaseA = 0x01000B00;

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

    Bytes questRecord(std::uint32_t id, std::uint16_t flags, const Bytes& stages)
    {
        Bytes b;
        b.append(edid("Q" + std::to_string(id & 0xFFF)));
        b.append(sub4("DNAM", Bytes().u16(flags).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(stages);
        b.append(sub4("ANAM", Bytes().u32(8)));
        // one alias (id 2) filled by a reference
        b.append(sub4("ALST", Bytes().u32(2)));
        b.append(sub4("ALID", Bytes().zstring("Alias2")));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(sub4("ALFR", Bytes().u32(kRefA)));
        b.append(sub4("ALED", Bytes()));
        return record4("QUST", 0u, id, b);
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
        bool hasKeyword(FormId form, FormId keyword) const override
        {
            const auto it = mKeywords.find(form);
            return it != mKeywords.end() && it->second.count(keyword) != 0;
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
        std::map<FormId, std::set<FormId>> mKeywords;
    };

    void addNativeClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        form.function("RegisterForSingleUpdate").native().param("afInterval", "Float");
        form.function("RegisterForUpdate").native().param("afInterval", "Float");
        form.function("UnregisterForUpdate").native();
        form.function("HasKeyword", "Bool").native().param("akKeyword", "Keyword");
        data.add(form);
        ScriptBuilder keyword("Keyword");
        keyword.parent("Form");
        data.add(keyword);
        ScriptBuilder spell("Spell");
        spell.parent("Form");
        data.add(spell);

        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("Enable").native().param("abFadeIn", "Bool");
        ref.function("Disable").native().param("abFadeOut", "Bool");
        ref.function("IsDisabled", "Bool").native();
        ref.function("GetPositionX", "Float").native();
        ref.function("GetPositionY", "Float").native();
        ref.function("GetPositionZ", "Float").native();
        ref.function("MoveTo")
            .native()
            .param("akTarget", "ObjectReference")
            .param("afXOffset", "Float")
            .param("afYOffset", "Float")
            .param("afZOffset", "Float")
            .param("abMatchRotation", "Bool");
        data.add(ref);

        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("AddSpell", "Bool").native().param("akSpell", "Spell").param("abVerbose", "Bool");
        data.add(actor);

        ScriptBuilder quest("Quest");
        quest.parent("Form");
        quest.function("SetStage", "Bool").native().param("auiStage", "Int");
        quest.function("GetStage", "Int").native();
        quest.function("GetStageDone", "Bool").native().param("auiStage", "Int");
        quest.function("IsStageDone", "Bool").native().param("auiStage", "Int");
        quest.function("SetCurrentStageID").native().param("auiStageID", "Int");
        quest.function("SetObjectiveDisplayed")
            .native()
            .param("aiObjective", "Int")
            .param("abDisplayed", "Bool")
            .param("abForce", "Bool");
        quest.function("IsObjectiveDisplayed", "Bool").native().param("aiObjective", "Int");
        data.add(quest);

        ScriptBuilder alias("Alias");
        alias.function("GetOwningQuest", "Quest").native();
        data.add(alias);
        ScriptBuilder refAlias("ReferenceAlias");
        refAlias.parent("Alias");
        refAlias.function("GetReference", "ObjectReference").native();
        refAlias.function("ForceRefTo").native().param("akNewRef", "ObjectReference");
        data.add(refAlias);

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
            mData.mClasses[kRefA] = "ObjectReference";
            mData.mClasses[kRefB] = "Actor";
            mData.mPositions[kRefA] = { 1.0, 2.0, 3.0 };
            mData.mPositions[kRefB] = { 100.0, 200.0, 300.0 };
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) {
                (error ? mErrors : mLogs).push_back(line);
                mAll.push_back(line);
            });
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
        std::vector<std::string> mAll;
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

    // ---- scripts ---------------------------------------------------------------------------------------------------

    // On a reference: MoveTo a target with offsets, then read the position back.
    ScriptBuilder moverScript()
    {
        ScriptBuilder s("Mover");
        s.parent("ObjectReference");
        s.var("posX", "Float");
        s.var("posY", "Float");
        s.var("posZ", "Float");
        s.autoProperty("Target", "ObjectReference");
        s.function("Go")
            .local("n", "None")
            .callMethod("MoveTo", Id("self"), Id("n"),
                { Id("::Target_var"), Flt(10.0f), Flt(0.0f), Flt(5.0f), Bool(true) })
            .local("x", "Float")
            .callMethod("GetPositionX", Id("self"), Id("x"))
            .assign(Id("posX"), Id("x"))
            .callMethod("GetPositionY", Id("self"), Id("x"))
            .assign(Id("posY"), Id("x"))
            .callMethod("GetPositionZ", Id("self"), Id("x"))
            .assign(Id("posZ"), Id("x"));
        return s;
    }

    ScriptBuilder keywordScript()
    {
        ScriptBuilder s("KwScript");
        s.parent("ObjectReference");
        s.var("hasA", "Bool");
        s.var("hasB", "Bool");
        s.autoProperty("KwA", "Keyword");
        s.autoProperty("KwB", "Keyword");
        s.function("Check")
            .local("b", "Bool")
            .callMethod("HasKeyword", Id("self"), Id("b"), { Id("::KwA_var") })
            .assign(Id("hasA"), Id("b"))
            .callMethod("HasKeyword", Id("self"), Id("b"), { Id("::KwB_var") })
            .assign(Id("hasB"), Id("b"));
        return s;
    }

    ScriptBuilder spellScript()
    {
        ScriptBuilder s("SpellScript");
        s.parent("Actor");
        s.var("first", "Bool");
        s.var("second", "Bool");
        s.autoProperty("TheSpell", "Spell");
        s.function("Add")
            .local("b", "Bool")
            .callMethod("AddSpell", Id("self"), Id("b"), { Id("::TheSpell_var"), Bool(true) })
            .assign(Id("first"), Id("b"))
            .callMethod("AddSpell", Id("self"), Id("b"), { Id("::TheSpell_var"), Bool(true) })
            .assign(Id("second"), Id("b"));
        return s;
    }

    // On the quest: SetCurrentStageID, then read the stage and whether it is done; and the real SetStage for contrast.
    ScriptBuilder stageScript()
    {
        ScriptBuilder s("StageScript");
        s.parent("Quest");
        s.var("current", "Int");
        s.var("doneAfterCurrent", "Bool");
        s.var("doneAfterSet", "Bool");
        s.var("lastStage", "Int", Int(-1));
        s.function("SetCurrent")
            .local("n", "None")
            .callMethod("SetCurrentStageID", Id("self"), Id("n"), { Int(10) })
            .local("v", "Int")
            .callMethod("GetStage", Id("self"), Id("v"))
            .assign(Id("current"), Id("v"))
            .local("b", "Bool")
            .callMethod("IsStageDone", Id("self"), Id("b"), { Int(10) })
            .assign(Id("doneAfterCurrent"), Id("b"));
        s.function("SetReal")
            .local("r", "Bool")
            .callMethod("SetStage", Id("self"), Id("r"), { Int(10) })
            .local("b", "Bool")
            .callMethod("IsStageDone", Id("self"), Id("b"), { Int(10) })
            .assign(Id("doneAfterSet"), Id("b"));
        s.function("OnStageSet")
            .param("auiStageID", "Int")
            .param("auiItemID", "Int")
            .assign(Id("lastStage"), Id("auiStageID"));
        return s;
    }

    ScriptBuilder fragmentScript()
    {
        ScriptBuilder s("QF_Slice2");
        s.parent("Quest");
        s.var("fragRan", "Int");
        s.function("Fragment_3").assign(Id("fragRan"), Int(1));
        return s;
    }

    vmad::Vmad questWithFragment(std::vector<vmad::Script> scripts)
    {
        vmad::Vmad v = vmadOf(std::move(scripts));
        v.mHasFragments = true;
        v.mFragmentScript = "QF_Slice2";
        v.mFragments.push_back({ 10, 1, "QF_Slice2", "Fragment_3" });
        return v;
    }

    // ---- the natives -------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusSlice2, MoveTo_records_the_position_and_GetPosition_reads_it_back)
    {
        Rig rig;
        rig.mData.add(moverScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        ASSERT_EQ(rig.mHost->attachReference(
                      kRefA, kBaseA, false, { script("Mover", { objProp("Target", kRefB) }) }),
            1u);
        rig.tick();
        EXPECT_FALSE(rig.mHost->positionOverride(kRefA).has_value());
        ASSERT_TRUE(rig.mHost->callFunction(kRefA, "Mover", "Go"));
        EXPECT_EQ(rig.number(kRefA, "Mover", "posX"), 110.0) << "the target's x plus the offset";
        EXPECT_EQ(rig.number(kRefA, "Mover", "posY"), 200.0);
        EXPECT_EQ(rig.number(kRefA, "Mover", "posZ"), 305.0);
        const auto moved = rig.mHost->positionOverride(kRefA);
        ASSERT_TRUE(moved.has_value());
        EXPECT_EQ((*moved)[0], 110.0);
        EXPECT_EQ((*moved)[2], 305.0);
        EXPECT_GE(rig.mHost->counters().mNatives.at("objectreference.moveto"), 1u);
    }

    TEST(SkyrimPapyrusSlice2, MoveTo_a_target_with_no_known_position_changes_nothing)
    {
        Rig rig;
        rig.mData.add(moverScript());
        rig.mData.mPositions.erase(kRefB);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("Mover", { objProp("Target", kRefB) }) });
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kRefA, "Mover", "Go"));
        EXPECT_FALSE(rig.mHost->positionOverride(kRefA).has_value());
        EXPECT_EQ(rig.number(kRefA, "Mover", "posX"), 1.0) << "the reference stays where the data put it";
    }

    TEST(SkyrimPapyrusSlice2, HasKeyword_asks_the_game_data)
    {
        Rig rig;
        rig.mData.add(keywordScript());
        rig.mData.mKeywords[kRefA] = { kKeywordA };
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false,
            { script("KwScript", { objProp("KwA", kKeywordA), objProp("KwB", kKeywordB) }) });
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kRefA, "KwScript", "Check"));
        EXPECT_EQ(rig.number(kRefA, "KwScript", "hasA"), 1.0);
        EXPECT_EQ(rig.number(kRefA, "KwScript", "hasB"), 0.0);
    }

    TEST(SkyrimPapyrusSlice2, SetCurrentStageID_sets_the_stage_without_the_fragment_or_the_done_mark)
    {
        Rig rig;
        rig.mData.add(stageScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = questWithFragment({ script("QF_Slice2"), script("StageScript") });
        rig.mHost->startGame();
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kQuestA, "StageScript", "SetCurrent"));
        EXPECT_EQ(rig.mState.stage(kQuestA), 10.f);
        EXPECT_EQ(rig.number(kQuestA, "StageScript", "current"), 10.0) << "GetStage sees it";
        EXPECT_EQ(rig.number(kQuestA, "StageScript", "doneAfterCurrent"), 0.0) << "it does not mark the stage done";
        EXPECT_FALSE(rig.mState.stageDone(kQuestA, 10));
        EXPECT_EQ(rig.mHost->counters().mFragmentsRun, 0u) << "no fragment";
        rig.tick();
        EXPECT_EQ(rig.number(kQuestA, "StageScript", "lastStage"), -1.0) << "no OnStageSet either";
    }

    TEST(SkyrimPapyrusSlice2, IsStageDone_is_true_after_a_real_SetStage)
    {
        Rig rig;
        rig.mData.add(stageScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mVmads[kQuestA] = questWithFragment({ script("QF_Slice2"), script("StageScript") });
        rig.mHost->startGame();
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kQuestA, "StageScript", "SetReal"));
        EXPECT_EQ(rig.number(kQuestA, "StageScript", "doneAfterSet"), 1.0);
        EXPECT_GE(rig.mHost->counters().mNatives.at("quest.isstagedone"), 1u);
    }

    TEST(SkyrimPapyrusSlice2, AddSpell_is_recorded_and_true_only_the_first_time)
    {
        Rig rig;
        rig.mData.add(spellScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefB, kBaseA, true, { script("SpellScript", { objProp("TheSpell", kSpellA) }) });
        rig.tick();
        EXPECT_FALSE(rig.mHost->hasSpell(kRefB, kSpellA));
        ASSERT_TRUE(rig.mHost->callFunction(kRefB, "SpellScript", "Add"));
        EXPECT_EQ(rig.number(kRefB, "SpellScript", "first"), 1.0);
        EXPECT_EQ(rig.number(kRefB, "SpellScript", "second"), 0.0);
        EXPECT_TRUE(rig.mHost->hasSpell(kRefB, kSpellA));
        EXPECT_FALSE(rig.mHost->hasSpell(kRefA, kSpellA));
    }

    TEST(SkyrimPapyrusSlice2, the_enable_override_is_listed_for_the_pack_to_push_and_replay)
    {
        Rig rig;
        ScriptBuilder s("Hider");
        s.parent("ObjectReference");
        s.function("Hide").local("n", "None").callMethod("Disable", Id("self"), Id("n"), { Bool(false) });
        s.function("Show").local("n", "None").callMethod("Enable", Id("self"), Id("n"), { Bool(false) });
        rig.mData.add(s);
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        rig.mHost->attachReference(kRefA, kBaseA, false, { script("Hider") });
        EXPECT_FALSE(rig.mHost->enableOverride(kRefA).has_value());
        ASSERT_TRUE(rig.mHost->callFunction(kRefA, "Hider", "Hide"));
        ASSERT_TRUE(rig.mHost->enableOverride(kRefA).has_value());
        EXPECT_FALSE(*rig.mHost->enableOverride(kRefA));
        ASSERT_TRUE(rig.mHost->callFunction(kRefA, "Hider", "Show"));
        EXPECT_TRUE(*rig.mHost->enableOverride(kRefA));
        const auto all = rig.mHost->enableOverrides();
        ASSERT_EQ(all.size(), 1u);
        EXPECT_EQ(all[0].first, kRefA);
    }

    // ---- the save state -----------------------------------------------------------------------------------------------

    // A quest with a repeating timer, a wait, a stage set, an alias forced to a reference, an objective and a counter, and
    // a reference script that has been disabled, moved and given a spell: every kind of state the host keeps.
    ScriptBuilder worldScript()
    {
        ScriptBuilder s("WorldScript");
        s.parent("Quest");
        s.var("updates", "Int");
        s.var("waited", "Int");
        s.var("ticksSeen", "Int");
        s.var("aliasRef", "Int");
        s.autoProperty("TheAlias", "ReferenceAlias");
        s.autoProperty("Other", "ObjectReference");
        s.function("OnInit")
            .local("n", "None")
            .callMethod("RegisterForUpdate", Id("self"), Id("n"), { Flt(2.0f) })
            .local("r", "Bool")
            .callMethod("SetStage", Id("self"), Id("r"), { Int(10) })
            .callMethod("ForceRefTo", Id("::TheAlias_var"), Id("n"), { Id("::Other_var") })
            .callMethod("SetObjectiveDisplayed", Id("self"), Id("n"), { Int(1), Bool(true), Bool(false) })
            .callStatic("Utility", "Wait", Id("n"), { Flt(8.0f) })
            .assign(Id("waited"), Int(1));
        s.function("OnUpdate").iadd(Id("updates"), Id("updates"), Int(1));
        s.function("ReadAlias")
            .local("o", "ObjectReference")
            .callMethod("GetReference", Id("::TheAlias_var"), Id("o"))
            .local("id", "Int")
            .callMethod("GetFormID", Id("o"), Id("id"))
            .assign(Id("aliasRef"), Id("id"));
        return s;
    }

    ScriptBuilder refWorldScript()
    {
        ScriptBuilder s("RefWorld");
        s.parent("Actor");
        s.var("loads", "Int");
        s.autoProperty("Target", "ObjectReference");
        s.autoProperty("TheSpell", "Spell");
        s.function("OnLoad").iadd(Id("loads"), Id("loads"), Int(1));
        s.function("Mutate")
            .local("n", "None")
            .callMethod("Disable", Id("self"), Id("n"), { Bool(false) })
            .callMethod("MoveTo", Id("self"), Id("n"), { Id("::Target_var"), Flt(1.0f), Flt(2.0f), Flt(3.0f), Bool(true) })
            .local("b", "Bool")
            .callMethod("AddSpell", Id("self"), Id("b"), { Id("::TheSpell_var"), Bool(true) });
        return s;
    }

    void setUpWorld(Rig& rig)
    {
        rig.mData.add(worldScript());
        rig.mData.add(refWorldScript());
        rig.mData.add(fragmentScript());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled, stage(10, 0)) });
        vmad::Vmad v = questWithFragment({ script("QF_Slice2"),
            script("WorldScript", { objProp("TheAlias", kQuestA, 2), objProp("Other", kRefB) }) });
        rig.mVmads[kQuestA] = v;
    }

    void attachRef(Rig& rig)
    {
        rig.mHost->attachReference(kRefB, kBaseA, true,
            { script("RefWorld", { objProp("Target", kRefA), objProp("TheSpell", kSpellA) }) });
    }

    // Runs the start, attaches the reference, ticks and mutates; the state the later tests save.
    void runToSavePoint(Rig& rig)
    {
        setUpWorld(rig);
        rig.mHost->startGame();
        attachRef(rig);
        rig.mHost->onCellLoaded(7, { kRefB });
        rig.tick(30, 0.1f); // script time 3.0: one update has fired, the wait is parked
        ASSERT_TRUE(rig.mHost->callFunction(kRefB, "RefWorld", "Mutate"));
        rig.tick(5, 0.1f);
    }

    std::string countersText(const PapyrusHost& host)
    {
        const PapyrusHostCounters& c = host.counters();
        std::string text;
        for (const auto& [k, v] : c.mEvents)
            text += k + "=" + std::to_string(v) + ";";
        for (const auto& [k, v] : c.mNatives)
            text += k + "=" + std::to_string(v) + ";";
        text += std::to_string(c.mInstancesAttached) + "/" + std::to_string(c.mFragmentsRun) + "/"
            + std::to_string(c.mStageSets) + "/" + std::to_string(c.mUpdatesFired) + "/" + std::to_string(c.mTicks)
            + "/" + std::to_string(c.mStacksRun) + "/" + std::to_string(c.mPropertiesSet);
        return text;
    }

    TEST(SkyrimPapyrusSlice2, saveState_is_empty_before_the_game_starts)
    {
        Rig rig;
        setUpWorld(rig);
        EXPECT_FALSE(rig.mHost->started());
        EXPECT_TRUE(rig.mHost->saveState().empty());
        rig.mHost->startGame();
        EXPECT_TRUE(rig.mHost->started());
        EXPECT_FALSE(rig.mHost->saveState().empty());
    }

    TEST(SkyrimPapyrusSlice2, a_restored_host_continues_exactly_as_the_uninterrupted_one)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_FALSE(bytes.empty());
        const std::size_t logAt = a.mAll.size();

        Rig b;
        setUpWorld(b);
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        ASSERT_TRUE(outcome.mOk) << outcome.mError;
        EXPECT_TRUE(b.mHost->started()) << "a restored host does not start the game again";
        EXPECT_EQ(b.mHost->scriptTime(), a.mHost->scriptTime());
        EXPECT_EQ(countersText(*b.mHost), countersText(*a.mHost));
        const std::size_t restoredLogAt = b.mAll.size();

        for (int i = 0; i < 120; ++i)
        {
            a.mHost->tick(0.1f);
            b.mHost->tick(0.1f);
        }
        EXPECT_EQ(b.number(kQuestA, "WorldScript", "updates"), a.number(kQuestA, "WorldScript", "updates"));
        EXPECT_GE(*a.number(kQuestA, "WorldScript", "updates"), 5.0) << "the repeating timer kept firing";
        EXPECT_EQ(b.number(kQuestA, "WorldScript", "waited"), 1.0) << "the parked wait woke up after the restore";
        EXPECT_EQ(a.number(kQuestA, "WorldScript", "waited"), 1.0);
        EXPECT_EQ(countersText(*b.mHost), countersText(*a.mHost));
        const std::vector<std::string> expected(a.mAll.begin() + static_cast<std::ptrdiff_t>(logAt), a.mAll.end());
        const std::vector<std::string> got(b.mAll.begin() + static_cast<std::ptrdiff_t>(restoredLogAt), b.mAll.end());
        EXPECT_EQ(got, expected) << "the same log lines after the save point";
        EXPECT_EQ(b.mHost->saveState(), a.mHost->saveState()) << "and the same state, byte for byte";
    }

    TEST(SkyrimPapyrusSlice2, the_host_tables_come_back)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        // the enable override, the moved position and the recorded spell
        ASSERT_TRUE(b.mHost->enableOverride(kRefB).has_value());
        EXPECT_FALSE(*b.mHost->enableOverride(kRefB));
        EXPECT_FALSE(b.mHost->referenceEnabled(kRefB));
        const auto moved = b.mHost->positionOverride(kRefB);
        ASSERT_TRUE(moved.has_value());
        EXPECT_EQ((*moved)[0], 2.0) << "kRefA is at (1, 2, 3); the offsets are (1, 2, 3)";
        EXPECT_TRUE(b.mHost->hasSpell(kRefB, kSpellA));
        // the quest state that scripts changed lives in the quest state the pack gave the host
        EXPECT_EQ(b.mState.stage(kQuestA), 10.f);
        EXPECT_TRUE(b.mState.stageDone(kQuestA, 10));
        // the forced alias and the objective flag
        EXPECT_TRUE(b.mHost->callFunction(kQuestA, "WorldScript", "ReadAlias"));
        EXPECT_EQ(b.number(kQuestA, "WorldScript", "aliasRef"), static_cast<double>(kRefB)) << "ForceRefTo came back";
        // variables
        EXPECT_EQ(b.number(kRefB, "RefWorld", "loads"), a.number(kRefB, "RefWorld", "loads"));
        EXPECT_EQ(b.mHost->formVariable(kRefB, "RefWorld", "Target"), std::optional<FormId>(kRefA));
    }

    TEST(SkyrimPapyrusSlice2, a_reference_attached_before_the_save_is_not_attached_again_after_it)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        const std::uint64_t before = b.mHost->counters().mInstancesAttached;
        EXPECT_EQ(b.mHost->attachReference(kRefB, kBaseA, true, { script("RefWorld") }), 0u)
            << "its cell loads again in the new process; the scripts are attached already";
        EXPECT_EQ(b.mHost->counters().mInstancesAttached, before);
        b.mHost->startGame();
        b.tick();
        EXPECT_EQ(b.mHost->counters().mInstancesAttached, before) << "startGame does nothing after a restore";
    }

    TEST(SkyrimPapyrusSlice2, saving_again_after_a_restore_gives_the_same_bytes)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        EXPECT_EQ(b.mHost->saveState(), bytes) << "canonical: restore then save is the same bytes";
    }

    TEST(SkyrimPapyrusSlice2, a_restore_replaces_what_a_running_host_held)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        const std::string countersAtSave = countersText(*a.mHost);
        a.tick(60, 0.1f); // the same host goes on, then loads its own earlier save (an in-process load)
        EXPECT_NE(countersText(*a.mHost), countersAtSave);
        ASSERT_TRUE(a.mHost->restoreSavedState(bytes).mOk);
        EXPECT_EQ(countersText(*a.mHost), countersAtSave);
        EXPECT_EQ(a.mHost->saveState(), bytes);
    }

    TEST(SkyrimPapyrusSlice2, the_quest_change_sink_hears_the_restored_stages)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        std::vector<std::pair<FormId, std::uint32_t>> heard;
        b.mHost->setQuestChanged([&](FormId q, std::uint32_t s) { heard.emplace_back(q, s); });
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        bool found = false;
        for (const auto& [quest, stageNumber] : heard)
            found = found || (quest == kQuestA && stageNumber == 10);
        EXPECT_TRUE(found) << "the package brain's own copy of the quest state follows a restore";
    }

    TEST(SkyrimPapyrusSlice2, every_truncation_of_the_state_is_refused_and_changes_nothing)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        b.mHost->startGame();
        b.tick();
        const std::string before = countersText(*b.mHost);
        const std::vector<std::uint8_t> savedBefore = b.mHost->saveState();
        std::size_t step = bytes.size() / 400 + 1;
        for (std::size_t length = 0; length < bytes.size(); length += step)
        {
            const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
            const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(cut);
            ASSERT_FALSE(outcome.mOk) << "length " << length;
            ASSERT_FALSE(outcome.mError.empty());
        }
        EXPECT_EQ(countersText(*b.mHost), before);
        EXPECT_EQ(b.mHost->saveState(), savedBefore) << "a refused state changed nothing";
    }

    TEST(SkyrimPapyrusSlice2, damaged_states_never_crash_and_a_state_that_is_accepted_runs)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        std::uint64_t seed = 0x9E3779B97F4A7C15ull;
        auto next = [&seed]() {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        int accepted = 0;
        for (int i = 0; i < 200; ++i)
        {
            std::vector<std::uint8_t> damaged = bytes;
            const int flips = 1 + static_cast<int>(next() % 3);
            for (int f = 0; f < flips; ++f)
                damaged[next() % damaged.size()] ^= static_cast<std::uint8_t>(1u << (next() % 8));
            Rig b;
            setUpWorld(b);
            const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(damaged);
            if (outcome.mOk)
            {
                ++accepted;
                EXPECT_NO_THROW(b.tick(10, 0.1f));
            }
        }
        EXPECT_LT(accepted, 200) << "most damage is noticed";
    }

    TEST(SkyrimPapyrusSlice2, a_state_naming_a_form_the_game_no_longer_has_is_refused_by_the_attach_key_check)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        b.mData.mClasses.erase(kRefB); // the placed reference of the saved instance is gone from the data
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        EXPECT_FALSE(outcome.mOk);
        EXPECT_FALSE(outcome.mError.empty());
        EXPECT_FALSE(b.mHost->started()) << "nothing changed";
    }

    TEST(SkyrimPapyrusSlice2, a_state_whose_script_is_gone_is_refused)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        setUpWorld(b);
        b.mData.mScripts.erase("refworld");
        EXPECT_FALSE(b.mHost->restoreSavedState(bytes).mOk);
    }

    TEST(SkyrimPapyrusSlice2, a_restore_needs_the_quest_data)
    {
        Rig a;
        runToSavePoint(a);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b; // no quests() call: the host has no quest index
        EXPECT_FALSE(b.mHost->restoreSavedState(bytes).mOk);
    }

    // ---- the demo script for the in-game check (tools/neutral_session_check/papyrusdemo.sh) ----------------------------
    // Not a test of anything: with OPENEDEN_PAPYRUS_DEMO_DIR set it writes scripts/oedemotick.pex there (a generated
    // script, no game asset). It counts updates every 2 script seconds, prints "<Label><n>" with Debug.Trace and, when n
    // reaches the property Limit, disables its reference and stops its timer.
    TEST(SkyrimPapyrusDemo, writes_the_demo_script_when_asked)
    {
        const char* const dir = std::getenv("OPENEDEN_PAPYRUS_DEMO_DIR");
        if (dir == nullptr || *dir == '\0')
            GTEST_SKIP() << "OPENEDEN_PAPYRUS_DEMO_DIR is not set";
        ScriptBuilder s("OEDemoTick");
        s.parent("ObjectReference");
        s.var("n", "Int");
        s.autoProperty("Limit", "Int");
        s.autoProperty("Label", "String");
        s.function("OnInit")
            .local("nn", "None")
            .callMethod("RegisterForUpdate", Id("self"), Id("nn"), { Flt(2.0f) });
        s.function("OnUpdate")
            .local("nn", "None")
            .local("s", "String")
            .local("m", "String")
            .local("hit", "Bool")
            .iadd(Id("n"), Id("n"), Int(1))
            .op(PexAsm::Op::Cast, { Id("s"), Id("n") })
            .op(PexAsm::Op::StrCat, { Id("m"), Id("::Label_var"), Id("s") })
            .callStatic("Debug", "Trace", Id("nn"), { Id("m"), Int(0) })
            .op(PexAsm::Op::CompareEq, { Id("hit"), Id("n"), Id("::Limit_var") })
            .jmpf(Id("hit"), 3)
            .callMethod("Disable", Id("self"), Id("nn"), { Bool(false) })
            .callMethod("UnregisterForUpdate", Id("self"), Id("nn"));
        const std::vector<std::uint8_t> bytes = s.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        const std::filesystem::path folder = std::filesystem::path(dir) / "scripts";
        std::filesystem::create_directories(folder);
        std::ofstream out(folder / "oedemotick.pex", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good());
    }
}
