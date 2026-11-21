// Task 0342 (Laid to Rest): a quest script's RegisterForUpdate also reaches the scripts of the quest's ALIASES when the
// switch is on (MS14: MS14BurnedHouseScript and MS14AlvasCoffinScript handle OnUpdate, nothing registers them but the
// quest's own RegisterForUpdate(5)); off, an alias keeps its own registration (task 0325). Generated scripts, no game
// data.
#include <game_skyrimse/dialogue.hpp>
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
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kRef = 0x01000A01;

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

    class NoHooks final : public PapyrusWorldHooks
    {
    };

    // the quest's fragment script: registers and unregisters
    ScriptBuilder regScript()
    {
        ScriptBuilder s("QReg");
        s.parent("Quest");
        s.function("Go").local("n", "None").callMethod("RegisterForUpdate", Id("self"), Id("n"), { Flt(1.0f) });
        s.function("Stop").local("n", "None").callMethod("UnregisterForUpdate", Id("self"), Id("n"));
        return s;
    }

    // the quest's other script and the alias script: both count their updates
    ScriptBuilder tickScript(const char* name, const char* parent)
    {
        ScriptBuilder s(name);
        s.parent(parent);
        s.var("ticks", "Int");
        s.function("OnUpdate").iadd(Id("ticks"), Id("ticks"), Int(1));
        return s;
    }

    vmad::Script scriptNamed(const char* name)
    {
        vmad::Script script;
        script.mName = name;
        return script;
    }

    Bytes aliasRecord()
    {
        Bytes b;
        b.append(sub4("ALST", Bytes().u32(2)));
        b.append(sub4("ALID", Bytes().zstring("Alias2")));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(sub4("ALFR", Bytes().u32(kRef)));
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    struct Rig
    {
        explicit Rig(bool reachAliases)
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            form.function("RegisterForUpdate").native().param("afInterval", "Float");
            form.function("UnregisterForUpdate").native();
            mData.add(form);
            ScriptBuilder quest("Quest");
            quest.parent("Form");
            mData.add(quest);
            ScriptBuilder alias("ReferenceAlias");
            mData.add(alias);
            mData.add(regScript());
            mData.add(tickScript("QTick", "Quest"));
            mData.add(tickScript("AliasTick", "ReferenceAlias"));
            mData.mClasses[kQuest] = "Quest";
            mData.mClasses[kRef] = "ObjectReference";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            mHost->setQuestUpdatesReachAliases(reachAliases);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes record;
            record.append(sub4("EDID", Bytes().zstring("Q1")));
            record.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            record.append(sub4("NEXT", Bytes()));
            record.append(sub4("ANAM", Bytes().u32(8)));
            record.append(aliasRecord());
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, record)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mVmad.mScripts = { scriptNamed("QReg"), scriptNamed("QTick") };
            vmad::AliasScripts as;
            as.mQuest = kQuest;
            as.mAlias = 2;
            as.mScripts.push_back(scriptNamed("AliasTick"));
            mVmad.mAliases.push_back(as);
            mHost->setQuests(
                &mIndex, &mState, [this](FormId id) -> const vmad::Vmad* { return id == kQuest ? &mVmad : nullptr; });
            mHost->startGame();
            tick();
        }
        ~Rig()
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
        double questTicks() { return mHost->numberVariable(kQuest, "QTick", "ticks").value_or(-1.0); }
        double aliasTicks() { return mHost->aliasScriptNumber(kQuest, 2, "AliasTick", "ticks").value_or(-1.0); }
        FakeData mData;
        NoHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        vmad::Vmad mVmad;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimQuestUpdateAliases, theQuestsRegistrationReachesItsOtherScriptAndItsAliasScriptsWhenOn)
    {
        Rig rig(true);
        ASSERT_TRUE(rig.mHost->callFunction(kQuest, "QReg", "Go"));
        rig.tick(30, 0.1f); // 3 s: the interval is 1 s
        EXPECT_GE(rig.questTicks(), 2.0);
        EXPECT_GE(rig.aliasTicks(), 2.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimQuestUpdateAliases, anAliasScriptHearsNothingWhileTheSwitchIsOff)
    {
        Rig rig(false);
        ASSERT_TRUE(rig.mHost->callFunction(kQuest, "QReg", "Go"));
        rig.tick(30, 0.1f);
        EXPECT_GE(rig.questTicks(), 2.0); // task 0325: the quest's other script still hears it
        EXPECT_EQ(rig.aliasTicks(), 0.0);
    }

    TEST(SkyrimQuestUpdateAliases, unregisteringEndsTheAliasScriptsToo)
    {
        Rig rig(true);
        ASSERT_TRUE(rig.mHost->callFunction(kQuest, "QReg", "Go"));
        rig.tick(30, 0.1f);
        ASSERT_TRUE(rig.mHost->callFunction(kQuest, "QReg", "Stop"));
        const double before = rig.aliasTicks();
        EXPECT_GE(before, 2.0);
        rig.tick(40, 0.1f);
        EXPECT_EQ(rig.aliasTicks(), before);
    }
}
