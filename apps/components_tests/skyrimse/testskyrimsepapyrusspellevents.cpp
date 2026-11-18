// Task 0292 (the College of Winterhold's tests): the spell events of the Papyrus host: OnHit with a spell as its source
// (on an actor, on an alias, on a scripted reference near where a shot ended), OnMagicEffectApply and OnSpellCast.
// Generated scripts and a fake game; no game data. Written first and run red against host functions that did nothing.
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
    constexpr FormId kMageLight = 0x01000C01;
    constexpr FormId kHeal = 0x01000C02;
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
        s.var("hits", "Int");
        s.var("applied", "Int");
        s.var("casts", "Int");
        s.var("lastSource", "Form");
        s.var("lastAggressor", "ObjectReference");
        s.var("lastEffect", "Form");
        s.var("lastCaster", "ObjectReference");
        s.var("lastSpell", "Form");
        s.function("OnHit")
            .param("akAggressor", "ObjectReference")
            .param("akSource", "Form")
            .param("akProjectile", "Form")
            .param("abPowerAttack", "Bool")
            .param("abSneakAttack", "Bool")
            .param("abBashAttack", "Bool")
            .param("abHitBlocked", "Bool")
            .iadd(Id("hits"), Id("hits"), Int(1))
            .assign(Id("lastSource"), Id("akSource"))
            .assign(Id("lastAggressor"), Id("akAggressor"));
        s.function("OnMagicEffectApply")
            .param("akCaster", "ObjectReference")
            .param("akEffect", "Form")
            .iadd(Id("applied"), Id("applied"), Int(1))
            .assign(Id("lastEffect"), Id("akEffect"))
            .assign(Id("lastCaster"), Id("akCaster"));
        s.function("OnSpellCast")
            .param("akSpell", "Form")
            .iadd(Id("casts"), Id("casts"), Int(1))
            .assign(Id("lastSpell"), Id("akSpell"));
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
            mData.mClasses[kMageLight] = "Spell";
            mData.mClasses[kHeal] = "MagicEffect";
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

    TEST(SkyrimPapyrusSpellEvents, aSpellHitOnAnActorReachesItsOnHitWithTheSpellAsSource)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onSpellHit(kPlayer, kMageLight, kNirya, 0.0, 0.0, 0.0);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "hits"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastSource"), kMageLight);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastAggressor"), kPlayer);
    }

    TEST(SkyrimPapyrusSpellEvents, aSpellThatEndedOnTheWorldReachesTheScriptedReferenceNearTheEndPoint)
    {
        Rig rig;
        rig.attach(kStone, false);
        rig.mHost->onSpellHit(kPlayer, kMageLight, 0, 1010.0, 1995.0, 310.0); // 12 units away
        rig.tick();
        EXPECT_EQ(rig.number(kStone, "hits"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kStone, "Probe", "lastSource"), kMageLight);
    }

    TEST(SkyrimPapyrusSpellEvents, aSpellThatEndedFarFromAScriptedReferenceDoesNotReachIt)
    {
        Rig rig;
        rig.attach(kStone, false);
        rig.mHost->onSpellHit(kPlayer, kMageLight, 0, 5000.0, 2000.0, 300.0);
        rig.tick();
        EXPECT_EQ(rig.number(kStone, "hits"), 0.0);
    }

    TEST(SkyrimPapyrusSpellEvents, anEffectAppliedReachesOnMagicEffectApplyWithCasterAndEffect)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onMagicEffectApplied(kPlayer, kNirya, kHeal);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "applied"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastEffect"), kHeal);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastCaster"), kPlayer);
    }

    TEST(SkyrimPapyrusSpellEvents, aSpellCastReachesTheCastersOnSpellCast)
    {
        Rig rig;
        rig.attach(kNirya, true);
        rig.mHost->onSpellCast(kNirya, kMageLight);
        rig.tick();
        EXPECT_EQ(rig.number(kNirya, "casts"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kNirya, "Probe", "lastSpell"), kMageLight);
    }

    TEST(SkyrimPapyrusSpellEvents, aHitOnAnActorWithNoScriptIsHarmless)
    {
        Rig rig;
        rig.mHost->onSpellHit(kPlayer, kMageLight, kNirya, 0.0, 0.0, 0.0);
        rig.mHost->onMagicEffectApplied(kPlayer, kNirya, kHeal);
        rig.mHost->onSpellCast(kPlayer, kMageLight);
        rig.tick();
        EXPECT_TRUE(rig.mErrors.empty());
    }
}
