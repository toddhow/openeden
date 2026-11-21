// Task 0289 (dungeon mechanics): the Papyrus host's animation natives (PlayAnimation, PlayAnimationAndWait,
// GetOpenState, SetOpen, the default activation of an object with no script). Generated scripts in the shape of the
// game's defaultPuzzlePillarScript and defaultPillarPuzzleLever; no game data. Written together with the code and
// checked by mutation (see the task's stage log), not run red against stubs.
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
    constexpr FormId kPillar = 0x01000A01;
    constexpr FormId kLever = 0x01000A02;
    constexpr FormId kGate = 0x01000A03;
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

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool playObjectAnimation(FormId reference, const std::string& event, double& seconds) override
        {
            mPlayed.push_back(std::to_string(reference) + ":" + event);
            seconds = mSeconds;
            return mCanPlay;
        }
        std::vector<std::string> mPlayed;
        double mSeconds = 2.0;
        bool mCanPlay = true;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("PlayAnimation", "Bool").native().param("asAnimation", "String");
        ref.function("PlayAnimationAndWait", "Bool").native().param("asAnimation", "String").param("asEvent", "String");
        ref.function("GetOpenState", "Int").native();
        ref.function("SetOpen").native().param("abOpen", "Bool");
        ref.function("Activate", "Bool").native().param("akActivator", "ObjectReference").param("abDefault", "Bool");
        ref.function("GetLinkedRef", "ObjectReference").native().param("apKeyword", "Form");
        data.add(ref);
    }

    // A puzzle pillar in the shape of defaultPuzzlePillarScript: on activation it turns and waits for the turn.
    ScriptBuilder pillarScript()
    {
        ScriptBuilder s("Pillar");
        s.parent("ObjectReference");
        s.var("turns", "Int");
        s.var("openState", "Int");
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("r", "Bool")
            .callMethod("PlayAnimationAndWait", Id("self"), Id("r"), { Str("Trigger01"), Str("Turned01") })
            .iadd(Id("turns"), Id("turns"), Int(1));
        s.function("Peek")
            .local("v", "Int")
            .callMethod("GetOpenState", Id("self"), Id("v"))
            .assign(Id("openState"), Id("v"));
        s.function("Play").local("r", "Bool").callMethod("PlayAnimation", Id("self"), Id("r"), { Str("Open") });
        s.function("Shut").local("r", "Bool").callMethod("PlayAnimation", Id("self"), Id("r"), { Str("Close") });
        s.function("SetItOpen").local("n", "None").callMethod("SetOpen", Id("self"), Id("n"), { Bool(true) });
        return s;
    }

    // A lever: activating it activates the linked reference (the gate) with itself as the activator.
    ScriptBuilder leverScript()
    {
        ScriptBuilder s("Lever");
        s.parent("ObjectReference");
        s.autoProperty("Gate", "ObjectReference");
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("r", "Bool")
            .callMethod("Activate", Id("::Gate_var"), Id("r"), { Id("self"), Bool(false) });
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

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(pillarScript());
            mData.add(leverScript());
            for (const FormId f : { kPillar, kLever, kGate })
                mData.mClasses[f] = "ObjectReference";
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
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void attach(FormId ref, vmad::Script script)
        {
            ASSERT_EQ(mHost->attachReference(ref, kBase, false, { std::move(script) }), 1u);
            tick();
        }
        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        std::optional<double> number(FormId ref, const char* script, const char* name)
        {
            return mHost->numberVariable(ref, script, name);
        }

        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    vmad::Script pillarOn()
    {
        vmad::Script s;
        s.mName = "Pillar";
        return s;
    }

    vmad::Script leverOn(FormId gate)
    {
        vmad::Script s;
        s.mName = "Lever";
        s.mProperties = { objProp("Gate", gate) };
        return s;
    }

    TEST(SkyrimDungeonNatives, a_pillar_turns_through_the_hook_and_the_script_waits_for_the_animation)
    {
        Rig rig;
        rig.attach(kPillar, pillarOn());
        rig.mHost->onActivate(kPillar, 0x14);
        rig.tick(3, 0.1f); // 0.3 s: the animation has been asked for and is still running
        ASSERT_EQ(rig.mHooks.mPlayed.size(), 1u);
        EXPECT_EQ(rig.mHooks.mPlayed[0], std::to_string(kPillar) + ":Trigger01");
        EXPECT_EQ(rig.number(kPillar, "Pillar", "turns"), 0.0) << "the script is parked for the animation's 2 s";
        rig.tick(30, 0.1f); // 3 s
        EXPECT_EQ(rig.number(kPillar, "Pillar", "turns"), 1.0);
    }

    TEST(SkyrimDungeonNatives, an_animation_the_world_cannot_play_does_not_make_the_script_wait)
    {
        Rig rig;
        rig.mHooks.mCanPlay = false;
        rig.attach(kPillar, pillarOn());
        rig.mHost->onActivate(kPillar, 0x14);
        rig.tick(3, 0.1f);
        EXPECT_EQ(rig.number(kPillar, "Pillar", "turns"), 1.0);
    }

    TEST(SkyrimDungeonNatives, without_hooks_the_animation_natives_are_tolerant)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        rig.attach(kPillar, pillarOn());
        rig.mHost->onActivate(kPillar, 0x14);
        rig.tick(3, 0.1f);
        EXPECT_EQ(rig.number(kPillar, "Pillar", "turns"), 1.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDungeonNatives, the_open_state_follows_the_open_and_close_animations)
    {
        Rig rig;
        rig.attach(kPillar, pillarOn());
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Peek"));
        rig.tick();
        EXPECT_EQ(rig.number(kPillar, "Pillar", "openState"), 3.0) << "closed until told otherwise";
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Play"));
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Peek"));
        rig.tick();
        EXPECT_EQ(rig.number(kPillar, "Pillar", "openState"), 1.0);
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Shut"));
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Peek"));
        rig.tick();
        EXPECT_EQ(rig.number(kPillar, "Pillar", "openState"), 3.0);
    }

    TEST(SkyrimDungeonNatives, SetOpen_plays_open_and_a_name_that_is_neither_open_nor_close_changes_no_state)
    {
        Rig rig;
        rig.attach(kPillar, pillarOn());
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "SetItOpen"));
        rig.tick();
        ASSERT_FALSE(rig.mHooks.mPlayed.empty());
        EXPECT_EQ(rig.mHooks.mPlayed.back(), std::to_string(kPillar) + ":Open");
        rig.mHost->onActivate(kPillar, 0x14);
        rig.tick(40, 0.1f);
        ASSERT_TRUE(rig.mHost->callFunction(kPillar, "Pillar", "Peek"));
        rig.tick();
        EXPECT_EQ(rig.number(kPillar, "Pillar", "openState"), 1.0) << "Trigger01 does not close it";
    }

    TEST(SkyrimDungeonNatives, a_lever_activates_a_gate_with_no_script_and_the_game_opens_then_closes_it)
    {
        Rig rig;
        rig.mData.mLinks[kLever] = kGate;
        rig.attach(kLever, leverOn(kGate));
        rig.mHost->onActivate(kLever, 0x14);
        rig.tick(5, 0.1f);
        ASSERT_EQ(rig.mHooks.mPlayed.size(), 1u);
        EXPECT_EQ(rig.mHooks.mPlayed[0], std::to_string(kGate) + ":Open");
        rig.mHost->onActivate(kLever, 0x14);
        rig.tick(5, 0.1f);
        ASSERT_EQ(rig.mHooks.mPlayed.size(), 2u);
        EXPECT_EQ(rig.mHooks.mPlayed[1], std::to_string(kGate) + ":Close");
    }

    TEST(SkyrimDungeonNatives, an_object_with_a_script_of_its_own_is_not_opened_by_the_default_activation)
    {
        Rig rig;
        rig.attach(kPillar, pillarOn());
        rig.attach(kLever, leverOn(kPillar));
        rig.mHost->onActivate(kLever, 0x14);
        rig.tick(5, 0.1f);
        // the pillar got OnActivate (it plays Trigger01); nothing played Open for it
        ASSERT_EQ(rig.mHooks.mPlayed.size(), 1u);
        EXPECT_EQ(rig.mHooks.mPlayed[0], std::to_string(kPillar) + ":Trigger01");
    }

    TEST(SkyrimDungeonNatives, hasInstances_says_whether_a_reference_has_scripts)
    {
        Rig rig;
        rig.attach(kPillar, pillarOn());
        EXPECT_TRUE(rig.mHost->hasInstances(kPillar));
        EXPECT_FALSE(rig.mHost->hasInstances(kGate));
    }
}
