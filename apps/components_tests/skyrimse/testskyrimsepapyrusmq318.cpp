// Task 0318 (the main quest after Dragon Rising): the two events the game's MQ104 and MQ105 player scripts wait for,
// OnLocationChange (the player enters Solitude, Windhelm or High Hrothgar) and OnObjectEquipped (the player equips a
// shout). Generated scripts and a fake game; no game data. Written first and run red against host functions that did
// nothing.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
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
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kStone = 0x01000A01; // a placed activator with a script
    constexpr FormId kNirya = 0x01000A02;
    constexpr FormId kPlayer = 0x14;
    constexpr FormId kShout = 0x01000C01;
    constexpr FormId kLocA = 0x01000D01;
    constexpr FormId kLocB = 0x01000D02;
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
    };

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.var("moves", "Int");
        s.var("equips", "Int");
        s.var("lastOld", "Form");
        s.var("lastNew", "Form");
        s.var("lastBase", "Form");
        s.function("OnLocationChange")
            .param("akOldLoc", "Form")
            .param("akNewLoc", "Form")
            .iadd(Id("moves"), Id("moves"), Int(1))
            .assign(Id("lastOld"), Id("akOldLoc"))
            .assign(Id("lastNew"), Id("akNewLoc"));
        s.function("OnObjectEquipped")
            .param("akBaseObject", "Form")
            .param("akReference", "ObjectReference")
            .iadd(Id("equips"), Id("equips"), Int(1))
            .assign(Id("lastBase"), Id("akBaseObject"));
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
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            mData.add(actor);
            mData.add(probeScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kStone] = "ObjectReference";
            mData.mClasses[kNirya] = "Actor";
            mData.mClasses[kPlayer] = "Actor";
            mData.mClasses[kShout] = "Shout";
            mData.mClasses[kLocA] = "Location";
            mData.mClasses[kLocB] = "Location";
            mData.mPositions[kStone] = { 1000.0, 2000.0, 300.0 };
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
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
        static vmad::Script probeOn()
        {
            vmad::Script s;
            s.mName = "Probe";
            return s;
        }
        void attach(FormId ref, bool actor)
        {
            ASSERT_EQ(mHost->attachReference(ref, kBase, actor, { probeOn() }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        std::optional<double> number(FormId ref, const char* name) { return mHost->numberVariable(ref, "Probe", name); }

        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimPapyrusMq318Events, aLocationChangeReachesTheActorsOnLocationChangeWithBothLocations)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onLocationChange(kNirya, kLocA, kLocB);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "moves"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastOld"), kLocA);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastNew"), kLocB);
    }

    TEST(SkyrimPapyrusMq318Events, aLocationChangeDoesNotReachAnotherActor)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onLocationChange(kPlayer, kLocA, kLocB);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "moves"), 0.0);
    }

    TEST(SkyrimPapyrusMq318Events, aShoutEquippedReachesTheActorsOnObjectEquippedWithTheShoutAsBaseObject)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onObjectEquipped(kNirya, kShout, 0);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "equips"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastBase"), kShout);
    }

    TEST(SkyrimPapyrusMq318Events, theEventsForAnActorWithNoScriptAreHarmless)
    {
        Rig rig;
        rig.mHost->onLocationChange(kPlayer, kLocA, kLocB);
        rig.mHost->onObjectEquipped(kPlayer, kShout, 0);
        rig.tick();
        EXPECT_TRUE(rig.mErrors.empty());
    }
}
