// Task 0299 (dungeon traps): what the Papyrus host does with a trap script's Weapon.Fire(source, ammo): it reaches the
// world hook with the weapon, the source reference, the ammunition and the place and facing of the source. Generated
// scripts in the shape of the game's DartTrap; no game data. Written before the code and run red against the old host.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
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
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kPort = 0x01000A01;
    constexpr FormId kOtherPort = 0x01000A02;
    constexpr FormId kWeapon = 0x01000C01;
    constexpr FormId kAmmo = 0x01000C02;
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
        bool referenceRotation(FormId reference, double (&xyz)[3]) const override
        {
            const auto it = mRotations.find(reference);
            if (it == mRotations.end())
                return false;
            xyz[0] = it->second[0];
            xyz[1] = it->second[1];
            xyz[2] = it->second[2];
            return true;
        }
        bool initiallyDead(FormId) const override { return false; }
        FormId linkedRef(FormId reference, FormId keyword) const override
        {
            const auto it = mLinks.find(reference);
            return keyword == 0 && it != mLinks.end() ? it->second : 0;
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
        std::map<FormId, std::array<double, 3>> mPositions;
        std::map<FormId, std::array<double, 3>> mRotations;
        std::map<FormId, FormId> mLinks;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool fireWeapon(const WeaponFireRequest& request) override
        {
            mFired.push_back(request);
            return mCanFire;
        }
        std::vector<WeaponFireRequest> mFired;
        bool mCanFire = true;
    };

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

    // A dart port in the shape of DartTrap.fireTrap: on activation it fires its weapon from itself with its ammunition.
    ScriptBuilder dartScript()
    {
        ScriptBuilder s("Dart");
        s.parent("ObjectReference");
        s.autoProperty("dartWeapon", "Weapon");
        s.autoProperty("dartAmmo", "Ammo");
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("n", "None")
            .callMethod("Fire", Id("::dartWeapon_var"), Id("n"), { Id("self"), Id("::dartAmmo_var") });
        return s;
    }

    // The same, but the source is a property (another reference, or none).
    ScriptBuilder sourceDartScript()
    {
        ScriptBuilder s("SourceDart");
        s.parent("ObjectReference");
        s.autoProperty("dartWeapon", "Weapon");
        s.autoProperty("dartAmmo", "Ammo");
        s.autoProperty("source", "ObjectReference");
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .local("n", "None")
            .callMethod("Fire", Id("::dartWeapon_var"), Id("n"), { Id("::source_var"), Id("::dartAmmo_var") });
        return s;
    }

    // A port in the shape of TrapBase's use of the chain: it asks for its n-th linked reference.
    ScriptBuilder chainScript()
    {
        ScriptBuilder s("Chain");
        s.parent("ObjectReference");
        s.var("got", "Int");
        s.function("Pick")
            .param("n", "Int")
            .local("r", "ObjectReference")
            .local("v", "Int")
            .callMethod("GetNthLinkedRef", Id("self"), Id("r"), { Id("n") })
            .callMethod("GetFormID", Id("r"), Id("v"))
            .assign(Id("got"), Id("v"));
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
            ref.function("GetNthLinkedRef", "ObjectReference").native().param("aiLinkedRef", "Int");
            ref.function("SetAngle").native().param("afX", "Float").param("afY", "Float").param("afZ", "Float");
            mData.add(ref);
            ScriptBuilder weapon("Weapon");
            weapon.parent("Form");
            weapon.function("Fire").native().param("akSource", "ObjectReference").param("akAmmo", "Ammo");
            mData.add(weapon);
            ScriptBuilder ammo("Ammo");
            ammo.parent("Form");
            mData.add(ammo);
            mData.add(dartScript());
            mData.add(sourceDartScript());
            mData.add(chainScript());
            mData.mClasses[kPort] = "ObjectReference";
            mData.mClasses[kOtherPort] = "ObjectReference";
            mData.mClasses[kWeapon] = "Weapon";
            mData.mClasses[kAmmo] = "Ammo";
            mData.mPositions[kPort] = { 100.0, 200.0, 300.0 };
            mData.mRotations[kPort] = { 0.0, 0.0, 1.5 };
            mData.mPositions[kOtherPort] = { -5.0, 6.0, 7.0 };
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
        vmad::Script dartOn(FormId weapon = kWeapon, FormId ammo = kAmmo)
        {
            vmad::Script s;
            s.mName = "Dart";
            s.mProperties = { objProp("dartWeapon", weapon), objProp("dartAmmo", ammo) };
            return s;
        }

        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimDungeonTraps, a_scripts_Weapon_Fire_reaches_the_world_with_the_weapon_the_source_the_ammo_and_the_place)
    {
        Rig rig;
        rig.attach(kPort, rig.dartOn());
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        ASSERT_EQ(rig.mHooks.mFired.size(), 1u);
        const PapyrusWorldHooks::WeaponFireRequest& r = rig.mHooks.mFired[0];
        EXPECT_EQ(r.mWeapon, kWeapon);
        EXPECT_EQ(r.mSource, kPort);
        EXPECT_EQ(r.mAmmo, kAmmo);
        EXPECT_DOUBLE_EQ(r.mPosition[0], 100.0);
        EXPECT_DOUBLE_EQ(r.mPosition[1], 200.0);
        EXPECT_DOUBLE_EQ(r.mPosition[2], 300.0);
        EXPECT_DOUBLE_EQ(r.mAngles[0], 0.0);
        EXPECT_DOUBLE_EQ(r.mAngles[1], 0.0);
        EXPECT_DOUBLE_EQ(r.mAngles[2], 1.5) << "the data's own angles";
    }

    TEST(SkyrimDungeonTraps, a_script_that_turned_the_source_wins_over_the_data_angles)
    {
        Rig rig;
        ScriptBuilder turner("Turner");
        turner.parent("ObjectReference");
        turner.function("Turn")
            .local("n", "None")
            .callMethod("SetAngle", Id("self"), Id("n"),
                { PexAsm::Flt(15.f), PexAsm::Flt(30.f), PexAsm::Flt(90.f) });
        rig.mData.add(turner);
        vmad::Script turnerOn;
        turnerOn.mName = "Turner";
        ASSERT_EQ(rig.mHost->attachReference(kPort, kBase, false, { turnerOn, rig.dartOn() }), 2u);
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kPort, "Turner", "Turn"));
        rig.tick();
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        ASSERT_EQ(rig.mHooks.mFired.size(), 1u);
        // SetAngle takes degrees; the host keeps radians
        EXPECT_NEAR(rig.mHooks.mFired[0].mAngles[0], 15.0 * 3.14159265358979 / 180.0, 1e-5);
        EXPECT_NEAR(rig.mHooks.mFired[0].mAngles[1], 30.0 * 3.14159265358979 / 180.0, 1e-5);
        EXPECT_NEAR(rig.mHooks.mFired[0].mAngles[2], 90.0 * 3.14159265358979 / 180.0, 1e-5);
    }

    TEST(SkyrimDungeonTraps, a_source_with_no_known_place_is_not_fired_from)
    {
        Rig rig;
        rig.mData.mPositions.erase(kPort);
        rig.attach(kPort, rig.dartOn());
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        EXPECT_TRUE(rig.mHooks.mFired.empty());
    }

    TEST(SkyrimDungeonTraps, a_source_with_no_data_angles_faces_straight_ahead)
    {
        Rig rig;
        rig.mData.mRotations.erase(kPort);
        rig.attach(kPort, rig.dartOn());
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        ASSERT_EQ(rig.mHooks.mFired.size(), 1u);
        EXPECT_DOUBLE_EQ(rig.mHooks.mFired[0].mAngles[2], 0.0);
    }

    TEST(SkyrimDungeonTraps, no_source_means_no_shot)
    {
        Rig rig;
        vmad::Script s;
        s.mName = "SourceDart";
        s.mProperties = { objProp("dartWeapon", kWeapon), objProp("dartAmmo", kAmmo) };
        rig.attach(kPort, s);
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        EXPECT_TRUE(rig.mHooks.mFired.empty());
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDungeonTraps, another_reference_can_be_the_source)
    {
        Rig rig;
        vmad::Script s;
        s.mName = "SourceDart";
        s.mProperties
            = { objProp("dartWeapon", kWeapon), objProp("dartAmmo", kAmmo), objProp("source", kOtherPort) };
        rig.attach(kPort, s);
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        ASSERT_EQ(rig.mHooks.mFired.size(), 1u);
        EXPECT_EQ(rig.mHooks.mFired[0].mSource, kOtherPort);
        EXPECT_DOUBLE_EQ(rig.mHooks.mFired[0].mPosition[0], -5.0);
    }

    TEST(SkyrimDungeonTraps, without_world_hooks_the_shot_is_tolerated)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        rig.attach(kPort, rig.dartOn());
        rig.mHost->onActivate(kPort, 0x14);
        rig.tick();
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDungeonTraps, a_world_that_cannot_fire_is_logged_once_per_port)
    {
        Rig rig;
        rig.mHooks.mCanFire = false;
        rig.attach(kPort, rig.dartOn());
        for (int i = 0; i < 3; ++i)
        {
            rig.mHost->onActivate(kPort, 0x14);
            rig.tick();
        }
        EXPECT_EQ(rig.mHooks.mFired.size(), 3u);
        int notes = 0;
        for (const std::string& line : rig.mLogs)
            if (line.find("could not be fired") != std::string::npos)
                ++notes;
        EXPECT_EQ(notes, 1);
    }

    TEST(SkyrimDungeonTraps, GetNthLinkedRef_follows_the_unkeyworded_chain_and_answers_none_past_its_end)
    {
        Rig rig;
        constexpr FormId kThird = 0x01000A03;
        rig.mData.mClasses[kThird] = "ObjectReference";
        rig.mData.mLinks[kPort] = kOtherPort;
        rig.mData.mLinks[kOtherPort] = kThird;
        vmad::Script chain;
        chain.mName = "Chain";
        rig.attach(kPort, chain);
        const auto pick = [&](int n) {
            const std::array<PapyrusHost::CallArgument, 1> args{ PapyrusHost::CallArgument::integer(n) };
            EXPECT_TRUE(rig.mHost->callFunctionWith(kPort, "Chain", "Pick", args));
            rig.tick();
            return rig.mHost->numberVariable(kPort, "Chain", "got");
        };
        EXPECT_EQ(pick(1), static_cast<double>(kOtherPort));
        EXPECT_EQ(pick(2), static_cast<double>(kThird));
        EXPECT_EQ(pick(3), 0.0);
        EXPECT_EQ(pick(0), 0.0) << "index 0 is the object itself in the script's own convention, not a link";
    }
}

namespace
{
    using namespace game_skyrimse;
}
