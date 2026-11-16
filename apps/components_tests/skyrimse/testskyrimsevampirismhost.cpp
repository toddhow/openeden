// Task 0260 (vampirism): what the Papyrus host gained for the game's own PlayerVampireQuestScript: a call with arguments (the
// player and a stage), an event sent to one script (OnUpdateGameTime), a GLOB value the pack sets (GameDaysPassed), a
// variable the pack sets, and the two control natives the quest's game-time update asks. Generated scripts, no game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>
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
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestV = 0x01000800;
    constexpr FormId kVampRef = 0x01000A00;
    constexpr FormId kVampBase = 0x01000B00;
    constexpr FormId kDaysGlobal = 0x01000C00;
    constexpr FormId kPlayerRef = 0x14;

    LoadOrder vampOrder()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes vampQuestRecord()
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("QVamp")));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("INDX", Bytes().u16(10).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return record4("QUST", 0u, kQuestV, b);
    }

    class VampData final : public PapyrusGameData
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
        bool globalValue(FormId global, float& value) const override
        {
            if (global != kDaysGlobal)
                return false;
            value = 1.5f; // the GLOB's stored value
            return true;
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
    };

    void addVampClasses(VampData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder gv("GlobalVariable");
        gv.parent("Form");
        gv.function("GetValue", "Float").native();
        data.add(gv);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("IsMovementControlsEnabled", "Bool").global().native();
        game.function("IsFightingControlsEnabled", "Bool").global().native();
        data.add(game);
    }

    // The shape of PlayerVampireQuestScript's functions: Progression(Actor, Int), a game-time event, a read of the days global.
    ScriptBuilder vampScript()
    {
        ScriptBuilder s("TestVamp");
        s.parent("ObjectReference");
        s.var("lastStage", "Int", Int(-1));
        s.var("actorId", "Int", Int(-1));
        s.var("ticks", "Int");
        s.var("seenDays", "Float", Flt(-1.f));
        s.var("movement", "Bool");
        s.var("fighting", "Bool");
        s.var("floatArg", "Float", Flt(-1.f));
        s.var("VampireStatus", "Int");
        s.var("LastFeedTime", "Float");
        s.autoProperty("DaysGlobal", "GlobalVariable");
        s.function("Progression")
            .param("akPlayer", "Actor")
            .param("aiStage", "Int")
            .local("n", "Int")
            .callMethod("GetFormID", Id("akPlayer"), Id("n"), {})
            .assign(Id("actorId"), Id("n"))
            .assign(Id("lastStage"), Id("aiStage"));
        s.function("WithFloat").param("afValue", "Float").assign(Id("floatArg"), Id("afValue"));
        s.function("ReadDays")
            .local("f", "Float")
            .callMethod("GetValue", Id("::DaysGlobal_var"), Id("f"), {})
            .assign(Id("seenDays"), Id("f"));
        s.function("Controls")
            .local("a", "Bool")
            .local("b", "Bool")
            .callStatic("Game", "IsMovementControlsEnabled", Id("a"))
            .assign(Id("movement"), Id("a"))
            .callStatic("Game", "IsFightingControlsEnabled", Id("b"))
            .assign(Id("fighting"), Id("b"));
        s.function("OnUpdateGameTime").iadd(Id("ticks"), Id("ticks"), Int(1));
        return s;
    }

    vmad::Property globalProp()
    {
        vmad::Property p;
        p.mName = "DaysGlobal";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = kDaysGlobal;
        p.mValue.mAlias = -1;
        return p;
    }

    struct VampHostRig
    {
        VampHostRig()
        {
            addVampClasses(mData);
            mData.add(vampScript());
            mData.mClasses[kVampRef] = "ObjectReference";
            mData.mClasses[kPlayerRef] = "Actor";
            mData.mClasses[kDaysGlobal] = "GlobalVariable";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("QUST"), 0, vampQuestRecord()));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", vampOrder(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", vampOrder()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            vmad::Script s;
            s.mName = "TestVamp";
            s.mProperties = { globalProp() };
            EXPECT_EQ(mHost->attachReference(kVampRef, kVampBase, false, { s }), 1u);
            tick();
        }
        ~VampHostRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        double number(const char* name) { return mHost->numberVariable(kVampRef, "TestVamp", name).value_or(-99); }

        VampData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimVampirismHost, a_call_with_the_player_and_a_stage_reaches_the_function)
    {
        VampHostRig rig;
        const PapyrusHost::CallArgument args[] = { PapyrusHost::CallArgument::form(kPlayerRef),
            PapyrusHost::CallArgument::integer(3) };
        ASSERT_TRUE(rig.mHost->callFunctionWith(kVampRef, "TestVamp", "Progression", args));
        EXPECT_EQ(rig.number("actorId"), 20.0);
        EXPECT_EQ(rig.number("lastStage"), 3.0);
    }

    TEST(SkyrimVampirismHost, a_float_argument_and_a_wrong_name_or_form_are_handled)
    {
        VampHostRig rig;
        const PapyrusHost::CallArgument f[] = { PapyrusHost::CallArgument::number(2.5f) };
        ASSERT_TRUE(rig.mHost->callFunctionWith(kVampRef, "TestVamp", "WithFloat", f));
        EXPECT_FLOAT_EQ(static_cast<float>(rig.number("floatArg")), 2.5f);
        EXPECT_FALSE(rig.mHost->callFunctionWith(kVampRef, "NoSuchScript", "WithFloat", f));
        EXPECT_FALSE(rig.mHost->callFunctionWith(0x777777, "TestVamp", "WithFloat", f));
        EXPECT_FALSE(rig.mHost->callFunctionWith(kVampRef, "TestVamp", "NoSuchFunction", f));
    }

    TEST(SkyrimVampirismHost, an_event_sent_to_one_script_runs_at_the_next_tick_each_time)
    {
        VampHostRig rig;
        EXPECT_EQ(rig.number("ticks"), 0.0);
        ASSERT_TRUE(rig.mHost->sendEventTo(kVampRef, "TestVamp", "OnUpdateGameTime"));
        rig.tick();
        EXPECT_EQ(rig.number("ticks"), 1.0);
        ASSERT_TRUE(rig.mHost->sendEventTo(kVampRef, "TestVamp", "OnUpdateGameTime"));
        ASSERT_TRUE(rig.mHost->sendEventTo(kVampRef, "TestVamp", "OnUpdateGameTime"));
        rig.tick();
        EXPECT_EQ(rig.number("ticks"), 3.0);
        EXPECT_FALSE(rig.mHost->sendEventTo(0x777777, "TestVamp", "OnUpdateGameTime"));
        EXPECT_FALSE(rig.mHost->sendEventTo(kVampRef, "Other", "OnUpdateGameTime"));
    }

    TEST(SkyrimVampirismHost, a_global_the_pack_sets_is_what_GetValue_answers_and_the_stored_value_is_the_default)
    {
        VampHostRig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kVampRef, "TestVamp", "ReadDays"));
        EXPECT_FLOAT_EQ(static_cast<float>(rig.number("seenDays")), 1.5f);
        rig.mHost->setGlobalValue(kDaysGlobal, 42.25f);
        ASSERT_TRUE(rig.mHost->callFunction(kVampRef, "TestVamp", "ReadDays"));
        EXPECT_FLOAT_EQ(static_cast<float>(rig.number("seenDays")), 42.25f);
        float value = 0.f;
        EXPECT_TRUE(rig.mHost->globalSetByScript(kDaysGlobal, value));
        EXPECT_FLOAT_EQ(value, 42.25f);
        rig.mHost->setGlobalValue(0, 9.f); // form 0 is no global
        EXPECT_FALSE(rig.mHost->globalSetByScript(0, value));
    }

    TEST(SkyrimVampirismHost, a_numeric_variable_can_be_set_by_the_pack_and_a_wrong_name_or_kind_is_refused)
    {
        VampHostRig rig;
        ASSERT_TRUE(rig.mHost->setNumberVariable(kVampRef, "TestVamp", "VampireStatus", 3.0));
        EXPECT_EQ(rig.number("VampireStatus"), 3.0);
        ASSERT_TRUE(rig.mHost->setNumberVariable(kVampRef, "TestVamp", "LastFeedTime", 7.5));
        EXPECT_FLOAT_EQ(static_cast<float>(rig.number("LastFeedTime")), 7.5f);
        ASSERT_TRUE(rig.mHost->setNumberVariable(kVampRef, "TestVamp", "movement", 1.0));
        EXPECT_EQ(rig.number("movement"), 1.0);
        EXPECT_FALSE(rig.mHost->setNumberVariable(kVampRef, "TestVamp", "NoSuchVariable", 1.0));
        EXPECT_FALSE(rig.mHost->setNumberVariable(kVampRef, "Other", "VampireStatus", 1.0));
        EXPECT_FALSE(rig.mHost->setNumberVariable(0x777777, "TestVamp", "VampireStatus", 1.0));
    }

    TEST(SkyrimVampirismHost, the_player_controls_are_always_enabled_for_the_quests_game_time_update)
    {
        VampHostRig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kVampRef, "TestVamp", "Controls"));
        EXPECT_EQ(rig.number("movement"), 1.0);
        EXPECT_EQ(rig.number("fighting"), 1.0);
    }
}
