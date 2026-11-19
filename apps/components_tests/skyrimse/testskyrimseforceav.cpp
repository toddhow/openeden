// Task 0344: Actor.ForceActorValue, what C01QuestScript.FightStart calls to leave each
// ambusher at one point of health. It sets the actor's current value like SetActorValue does; before it was a tolerant
// no-op. Generated records and scripts, no game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

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
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kActor = 0x01000A01;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class Data final : public PapyrusGameData
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
            return form == kQuest ? "Quest" : form == kActor ? "Actor" : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
    };

    class Hooks final : public PapyrusWorldHooks
    {
    public:
        bool actorValue(FormId actor, std::string_view lowerName, double& current, double& maximum) override
        {
            if (actor != kActor || lowerName != "health")
                return false;
            current = mHealth;
            maximum = 75.0;
            return true;
        }
        bool setHealth(FormId actor, double health) override
        {
            if (actor != kActor)
                return false;
            mHealth = health;
            ++mSets;
            return true;
        }
        double mHealth = 75.0;
        int mSets = 0;
    };

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
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("ForceActorValue").native().param("asValueName", "String").param("afValue", "Float");
            mData.add(actor);
            ScriptBuilder fighter("Fighter");
            fighter.parent("Actor");
            fighter.function("OnGetUp")
                .param("akFurniture", "ObjectReference")
                .local("n", "None")
                .callMethod("ForceActorValue", Id("self"), Id("n"), { Str("health"), Flt(1.0f) });
            mData.add(fighter);
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q")));
            quest.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
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
            vmad::Script s;
            s.mName = "Fighter";
            EXPECT_EQ(mHost->attachReference(kActor, 0x01000B00, true, { s }), 1u);
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        Data mData;
        Hooks mHooks;
        QuestIndex mIndex;
        QuestState mState;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimForceActorValue, aScriptsForcedHealthIsTheActorsCurrentHealth)
    {
        Rig rig;
        rig.mHost->onGetUp(kActor, 0);
        for (int i = 0; i < 3; ++i)
            rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.mHooks.mSets, 1);
        EXPECT_DOUBLE_EQ(rig.mHooks.mHealth, 1.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }
}
