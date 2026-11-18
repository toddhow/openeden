// Task 0282 (word walls and combat): Actor.IsInCombat is answered from the world hooks (the combat AI's engaged state),
// so a script that asks "is the player fighting?" gets a real answer. The word wall's own rule, which the shipped
// WordWallTriggerScript.StartFX applies, is written out in a generated script: it teaches only when the player is
// within 300 units of the look target and not in combat, or within 200 units whatever he is doing. Generated scripts; no
// game data. Written first and run red against a stub (see the task's stage log).
#include <game_skyrimse/papyrus_host.hpp>

#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kWallRef = 0x01000B00;
    constexpr FormId kMarker = 0x01000B10;
    constexpr FormId kPlayer = 0x14;
    constexpr FormId kNpc = 0x01000B20;

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
            if (form == kNpc)
                return "Actor";
            return form == kMarker || form == kWallRef ? "ObjectReference" : std::string();
        }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            if (reference != kMarker)
                return false;
            x = 0;
            y = 0;
            z = 0;
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
    };

    class FightHooks final : public PapyrusWorldHooks
    {
    public:
        bool inCombat(FormId actor) override
        {
            mAsked.push_back(actor);
            const auto it = mFighting.find(actor);
            return it != mFighting.end() && it->second;
        }
        std::map<FormId, bool> mFighting;
        std::vector<FormId> mAsked;
    };

    ScriptBuilder wall()
    {
        ScriptBuilder s("FightWall");
        s.parent("ObjectReference");
        s.var("combat", "Bool");
        s.var("npcCombat", "Bool");
        s.var("learned", "Int");
        s.autoProperty("Marker", "ObjectReference");
        s.autoProperty("Npc", "Actor");
        s.function("Ask")
            .local("p", "Actor")
            .callStatic("Game", "GetPlayer", Id("p"))
            .local("c", "Bool")
            .callMethod("IsInCombat", Id("p"), Id("c"))
            .assign(Id("combat"), Id("c"))
            .local("n", "Bool")
            .callMethod("IsInCombat", Id("::Npc_var"), Id("n"))
            .assign(Id("npcCombat"), Id("n"));
        // the shipped StartFX condition: dist < 300 && !IsInCombat() || dist <= 200 (jumps are relative, in instructions)
        s.function("TryTeach")
            .local("p", "Actor")
            .local("d", "Float")
            .local("c", "Bool")
            .local("closeBy", "Bool")
            .local("inRange", "Bool")
            .assign(Id("learned"), Int(0))
            .callStatic("Game", "GetPlayer", Id("p"))
            .callMethod("GetDistance", Id("p"), Id("d"), { Id("::Marker_var") })
            .callMethod("IsInCombat", Id("p"), Id("c"))
            .binary(PexAsm::Op::CompareLe, Id("closeBy"), Id("d"), Flt(200))
            .jmpt(Id("closeBy"), 4)
            .binary(PexAsm::Op::CompareLt, Id("inRange"), Id("d"), Flt(300))
            .jmpf(Id("inRange"), 3)
            .jmpt(Id("c"), 2)
            .assign(Id("learned"), Int(1));
        return s;
    }

    void addClasses(Data& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("GetDistance", "Float").native().param("akOther", "ObjectReference");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("IsInCombat", "Bool").native();
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("GetPlayer", "Actor").global().native();
        data.add(game);
    }

    vmad::Property objectProp(const char* name, FormId form)
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
            mData.add(wall());
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) {
                if (error)
                    mErrors.push_back(line);
            });
            mHost->setWorldHooks(&mHooks);
            vmad::Script s;
            s.mName = "FightWall";
            s.mProperties = { objectProp("Marker", kMarker), objectProp("Npc", kNpc) };
            EXPECT_EQ(mHost->attachReference(kWallRef, 0x5095E, false, { s }), 1u);
            mHost->tick(0.1f);
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void standAt(double distance)
        {
            PapyrusPlayerView v;
            v.mKnown = true;
            v.mX = 0;
            v.mY = distance;
            v.mZ = 0;
            mHost->setPlayerView(v);
        }
        double number(const char* name) const
        {
            return mHost->numberVariable(kWallRef, "FightWall", name).value_or(-99);
        }
        Data mData;
        FightHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimWordWallCombat, isInCombatAsksTheWorldAboutThePlayerAndAboutAnActor)
    {
        Rig rig;
        rig.mHooks.mFighting[kPlayer] = true;
        rig.mHooks.mFighting[kNpc] = false;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "Ask"));
        EXPECT_EQ(rig.number("combat"), 1.0);
        EXPECT_EQ(rig.number("npcCombat"), 0.0);
        rig.mHooks.mFighting[kPlayer] = false;
        rig.mHooks.mFighting[kNpc] = true;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "Ask"));
        EXPECT_EQ(rig.number("combat"), 0.0);
        EXPECT_EQ(rig.number("npcCombat"), 1.0);
    }

    TEST(SkyrimWordWallCombat, isInCombatIsNoLongerATolerantNative)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "Ask"));
        for (const std::string& name : rig.mHost->tolerantNatives())
            EXPECT_NE(name, "actor.isincombat");
    }

    TEST(SkyrimWordWallCombat, withNoWorldHooksNobodyIsInCombat)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "Ask"));
        EXPECT_EQ(rig.number("combat"), 0.0);
        EXPECT_EQ(rig.number("npcCombat"), 0.0);
    }

    TEST(SkyrimWordWallCombat, theWallsRuleTeachesAtMiddleRangeOnlyWhenNobodyFights)
    {
        Rig rig;
        rig.standAt(250);
        rig.mHooks.mFighting[kPlayer] = false;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "TryTeach"));
        EXPECT_EQ(rig.number("learned"), 1.0) << "calm and within 300";
        rig.mHooks.mFighting[kPlayer] = true;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "TryTeach"));
        EXPECT_EQ(rig.number("learned"), 0.0) << "fighting at 250: refused";
    }

    TEST(SkyrimWordWallCombat, withinTwoHundredUnitsTheWallTeachesEvenInAFight)
    {
        Rig rig;
        rig.standAt(150);
        rig.mHooks.mFighting[kPlayer] = true;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "TryTeach"));
        EXPECT_EQ(rig.number("learned"), 1.0);
    }

    TEST(SkyrimWordWallCombat, beyondThreeHundredUnitsTheWallNeverTeaches)
    {
        Rig rig;
        rig.standAt(350);
        rig.mHooks.mFighting[kPlayer] = false;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "FightWall", "TryTeach"));
        EXPECT_EQ(rig.number("learned"), 0.0);
    }
}
