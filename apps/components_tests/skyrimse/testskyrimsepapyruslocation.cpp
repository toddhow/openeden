// Task 0314: OnLocationChange. The game's TG01PlayerAliasScript (a script on a quest alias that holds the player) waits
// for the player to enter the Ratway and then moves Brynjolf to the Ragged Flagon; the host had no such event.
// Generated scripts and a fake game; no game data. Written first and run red against a stub `onLocationChange`.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kPlayer = 0x14;
    constexpr FormId kOtherRef = 0x01000A02;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kLocA = 0x01000C01;
    constexpr FormId kLocB = 0x01000C02;

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

    ScriptBuilder probe()
    {
        ScriptBuilder s("LocProbe");
        s.parent("ObjectReference");
        s.var("calls", "Int");
        s.var("oldLoc", "Form");
        s.var("newLoc", "Form");
        s.function("OnLocationChange")
            .param("akOldLoc", "Form")
            .param("akNewLoc", "Form")
            .iadd(Id("calls"), Id("calls"), Int(1))
            .assign(Id("oldLoc"), Id("akOldLoc"))
            .assign(Id("newLoc"), Id("akNewLoc"));
        return s;
    }

    namespace Op = PexAsm::Op;
    using PexAsm::Str;

    // The property of a script attached to a reference, read and written through a plain reference handle (the game's
    // TG01PlayerAlias does `pTG01QS.pTG01BrynMoved = 1` on a property typed as the quest's script).
    ScriptBuilder holder()
    {
        ScriptBuilder s("Holder");
        s.parent("ObjectReference");
        s.autoProperty("Moved", "Int");
        return s;
    }

    ScriptBuilder setter()
    {
        ScriptBuilder s("Setter");
        s.parent("ObjectReference");
        s.var("seen", "Int");
        s.function("Poke")
            .param("akTarget", "ObjectReference")
            .local("r", "Int")
            .op(Op::PropSet, { Str("Moved"), Id("akTarget"), Int(1) })
            .op(Op::PropGet, { Str("Moved"), Id("akTarget"), Id("r") })
            .assign(Id("seen"), Id("r"));
        return s;
    }

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            mData.add(probe());
            mData.add(holder());
            mData.add(setter());
            mData.mClasses[kPlayer] = "Actor";
            mData.mClasses[kOtherRef] = "ObjectReference";
            mData.mClasses[kLocA] = "Form";
            mData.mClasses[kLocB] = "Form";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->startGame();
        }
        void attach(FormId ref)
        {
            vmad::Script script;
            script.mName = "LocProbe";
            ASSERT_EQ(mHost->attachReference(ref, kBase, ref == kPlayer, { script }), 1u);
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        void tick()
        {
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        Data mData;
        std::unique_ptr<PapyrusHost> mHost;
    };

    TEST(SkyrimPapyrusLocation, a_script_on_the_player_hears_the_old_and_the_new_location)
    {
        Rig rig;
        rig.attach(kPlayer);
        rig.mHost->onLocationChange(kPlayer, kLocA, kLocB);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kPlayer, "LocProbe", "calls"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kPlayer, "LocProbe", "oldLoc"), kLocA);
        EXPECT_EQ(rig.mHost->formVariable(kPlayer, "LocProbe", "newLoc"), kLocB);
    }

    TEST(SkyrimPapyrusLocation, a_script_on_another_reference_does_not_hear_it)
    {
        Rig rig;
        rig.attach(kOtherRef);
        rig.mHost->onLocationChange(kPlayer, kLocA, kLocB);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kOtherRef, "LocProbe", "calls"), 0.0);
    }

    TEST(SkyrimPapyrusLocation, a_property_of_the_script_attached_to_a_reference_is_set_and_read_through_the_handle)
    {
        Rig rig;
        vmad::Script holderScript;
        holderScript.mName = "Holder";
        ASSERT_EQ(rig.mHost->attachReference(kOtherRef, kBase, false, { holderScript }), 1u);
        vmad::Script setterScript;
        setterScript.mName = "Setter";
        ASSERT_EQ(rig.mHost->attachReference(0x01000A03, kBase, false, { setterScript }), 1u);
        rig.mData.mClasses[0x01000A03] = "ObjectReference";
        rig.tick();
        const PapyrusHost::CallArgument args[] = { PapyrusHost::CallArgument::form(kOtherRef) };
        ASSERT_TRUE(rig.mHost->callFunctionWith(0x01000A03, "Setter", "Poke", args));
        EXPECT_EQ(rig.mHost->numberVariable(kOtherRef, "Holder", "Moved"), 1.0);
        EXPECT_EQ(rig.mHost->numberVariable(0x01000A03, "Setter", "seen"), 1.0);
    }

    TEST(SkyrimPapyrusLocation, every_change_is_sent_once)
    {
        Rig rig;
        rig.attach(kPlayer);
        rig.mHost->onLocationChange(kPlayer, kLocA, kLocB);
        rig.mHost->onLocationChange(kPlayer, kLocB, kLocA);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kPlayer, "LocProbe", "calls"), 2.0);
        EXPECT_EQ(rig.mHost->formVariable(kPlayer, "LocProbe", "newLoc"), kLocA);
    }
}
