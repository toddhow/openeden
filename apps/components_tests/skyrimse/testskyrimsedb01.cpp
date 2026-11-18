// Task 0294 (Dark Brotherhood start): Utility.WaitGameTime parks the script (a latent native) instead of returning at
// once. The game's WIDeadBodyCleanupScript loops on it and used up the instruction budget (66,666 tolerant calls) when
// it returned immediately. Generated scripts, no game data. Written first and run red against the unregistered native.
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
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kRef = 0x01000A01;
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
        bool referencePosition(FormId, double&, double&, double&) const override { return false; }
        bool initiallyDead(FormId) const override { return false; }
        FormId linkedRef(FormId reference, FormId) const override
        {
            const auto it = mLinks.find(reference);
            return it != mLinks.end() ? it->second : 0;
        }
        FormId formFromFile(std::string_view, std::uint32_t) const override { return 0; }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, FormId> mLinks;
    };

    class NoHooks final : public PapyrusWorldHooks
    {
    };

    ScriptBuilder sleeper()
    {
        ScriptBuilder s("Sleeper");
        s.parent("ObjectReference");
        s.var("done", "Int");
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("n", "None")
            .callStatic("Utility", "WaitGameTime", Id("n"), { PexAsm::Flt(1.0f) })
            .iadd(Id("done"), Id("done"), Int(1));
        return s;
    }

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            ScriptBuilder utility("Utility");
            utility.function("WaitGameTime", "None").global().native().param("afInterval", "Float");
            mData.add(utility);
            mData.add(sleeper());
            mData.mClasses[kRef] = "ObjectReference";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            vmad::Script script;
            script.mName = "Sleeper";
            EXPECT_EQ(mHost->attachReference(kRef, kBase, false, { std::move(script) }), 1u);
            tick();
        }
        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        FakeData mData;
        NoHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimWaitGameTime, the_script_waits_for_the_game_time_instead_of_returning_at_once)
    {
        Rig rig;
        rig.mHost->onActivate(kRef, 0x14);
        rig.tick(30, 0.1f); // 3 s of script time: one game hour is 180 s at the default time scale of 20
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Sleeper", "done").value_or(-1.0), 0.0);
        rig.tick(1000, 0.2f); // 200 s
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Sleeper", "done").value_or(-1.0), 1.0);
    }

    TEST(SkyrimWaitGameTime, the_wait_is_not_a_tolerant_native_any_more)
    {
        Rig rig;
        rig.mHost->onActivate(kRef, 0x14);
        rig.tick(5, 0.1f);
        for (const std::string& line : rig.mLogs)
            EXPECT_EQ(line.find("not registered"), std::string::npos) << line;
    }
}
