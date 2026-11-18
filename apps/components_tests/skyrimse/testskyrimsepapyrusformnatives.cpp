// Task 0321: the Skyrim pack answers the common natives (Actor.GetLevel, Actor.GetRace, FormList.*,
// ObjectReference.GetAngle*) through PapyrusFormQueries over its PapyrusGameData, and lists them as registered.
// Generated scripts and a fake game; no game data. Written first and run red against the stub library.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kRace = 0x01000C01;
    constexpr FormId kList = 0x01000D01;
    constexpr FormId kMember = 0x01000E02;
    constexpr double kPi = 3.14159265358979323846;

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
        FormId baseOf(FormId reference) const override { return reference == kActor ? kBase : 0; }
        bool actorLevel(FormId actor, std::int32_t& level) const override
        {
            if (actor != kBase && actor != kActor)
                return false;
            level = 17;
            return true;
        }
        bool actorRace(FormId actor, FormId& race) const override
        {
            if (actor != kBase && actor != kActor)
                return false;
            race = kRace;
            return true;
        }
        bool formListEntries(FormId list, std::vector<FormId>& out) const override
        {
            if (list != kList)
                return false;
            out = { 0x01000E01, kMember, 0x01000E03 };
            return true;
        }
        bool referenceRotation(FormId reference, double (&xyz)[3]) const override
        {
            if (reference != kActor)
                return false;
            xyz[0] = 0.0;
            xyz[1] = kPi / 2.0;
            xyz[2] = kPi;
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

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            for (const char* axis : { "GetAngleX", "GetAngleY", "GetAngleZ" })
                ref.function(axis, "Float").native();
            mData.add(ref);
            ScriptBuilder race("Race");
            race.parent("Form");
            mData.add(race);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("GetLevel", "Int").native();
            actor.function("GetRace", "Race").native();
            mData.add(actor);
            ScriptBuilder list("FormList");
            list.parent("Form");
            list.function("GetSize", "Int").native();
            list.function("HasForm", "Bool").native().param("akForm", "Form");
            mData.add(list);
            ScriptBuilder probe("Probe");
            probe.parent("Actor");
            probe.autoProperty("TheList", "FormList");
            probe.autoProperty("Member", "Form");
            probe.var("level", "Int");
            probe.var("race", "Race");
            probe.var("size", "Int");
            probe.var("has", "Bool");
            probe.var("angleY", "Float");
            probe.var("angleZ", "Float");
            probe.function("Run")
                .local("l", "Int")
                .local("r", "Race")
                .local("s", "Int")
                .local("h", "Bool")
                .local("y", "Float")
                .local("z", "Float")
                .callMethod("GetLevel", Id("self"), Id("l"))
                .assign(Id("level"), Id("l"))
                .callMethod("GetRace", Id("self"), Id("r"))
                .assign(Id("race"), Id("r"))
                .callMethod("GetSize", Id("::TheList_var"), Id("s"))
                .assign(Id("size"), Id("s"))
                .callMethod("HasForm", Id("::TheList_var"), Id("h"), { Id("::Member_var") })
                .assign(Id("has"), Id("h"))
                .callMethod("GetAngleY", Id("self"), Id("y"))
                .assign(Id("angleY"), Id("y"))
                .callMethod("GetAngleZ", Id("self"), Id("z"))
                .assign(Id("angleZ"), Id("z"));
            mData.add(probe);
            mData.mClasses[kActor] = "Actor";
            mData.mClasses[kList] = "FormList";
            mData.mClasses[kRace] = "Race";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->startGame();
        }
        PapyrusHost& host() { return *mHost; }
        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    vmad::Property objectProperty(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    TEST(SkyrimPapyrusFormNatives, a_script_on_an_actor_reads_level_race_list_and_angles_from_the_pack)
    {
        Rig rig;
        vmad::Script script;
        script.mName = "Probe";
        script.mProperties = { objectProperty("TheList", kList), objectProperty("Member", kMember) };
        ASSERT_EQ(rig.host().attachReference(kActor, kBase, true, { script }), 1u);
        for (int i = 0; i < 3; ++i)
            rig.host().tick(0.1f);
        ASSERT_TRUE(rig.host().callFunction(kActor, "Probe", "Run"));
        for (int i = 0; i < 3; ++i)
            rig.host().tick(0.1f);
        EXPECT_EQ(rig.host().numberVariable(kActor, "Probe", "level"), 17.0);
        EXPECT_EQ(rig.host().numberVariable(kActor, "Probe", "size"), 3.0);
        EXPECT_EQ(rig.host().numberVariable(kActor, "Probe", "has"), 1.0);
        EXPECT_NEAR(*rig.host().numberVariable(kActor, "Probe", "angleY"), 90.0, 1e-3);
        EXPECT_NEAR(*rig.host().numberVariable(kActor, "Probe", "angleZ"), 180.0, 1e-3);
        // none of them was left to the tolerant path
        const std::vector<std::string> tolerant = rig.host().tolerantNatives();
        for (const char* key : { "actor.getlevel", "actor.getrace", "formlist.getsize", "formlist.hasform",
                 "objectreference.getangley", "objectreference.getanglez" })
            EXPECT_EQ(std::count(tolerant.begin(), tolerant.end(), std::string(key)), 0) << key;
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusFormNatives, the_common_natives_are_listed_as_registered_and_the_cosmetic_ones_as_no_ops)
    {
        Rig rig;
        std::map<std::string, bool> listed;
        for (const auto& [key, noop] : rig.host().registeredNatives())
            listed[key] = noop;
        for (const char* key : { "actor.getlevel", "actor.getrace", "actorbase.getrace", "formlist.getat",
                 "formlist.getsize", "formlist.hasform", "objectreference.getanglex" })
        {
            ASSERT_TRUE(listed.count(key)) << key;
            EXPECT_FALSE(listed[key]) << key;
        }
        for (const char* key : { "sound.play", "effectshader.play", "game.shakecamera" })
        {
            ASSERT_TRUE(listed.count(key)) << key;
            EXPECT_TRUE(listed[key]) << key;
        }
        EXPECT_TRUE(listed.count("math.sqrt")) << "the library's pure natives are listed too";
        EXPECT_TRUE(listed.count("actor.hasspell")) << "so are the pack's own";
    }
}
