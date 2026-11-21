// Task 0326: Fort Hraggstad's kill count. GetRefTypeAliveCount (fn 592) counted a listed special reference as alive
// unless the placed record starts dead (task 0298); with a dead lookup (the session's kills) a killed reference stops
// counting.
#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/summons.hpp>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kWorldL = 0x0000003C;
    constexpr FormId kFort = 0x01000100;
    constexpr FormId kFortRefType = 0x01000300;
    constexpr FormId kBandit1 = 0x01000401;
    constexpr FormId kBandit2 = 0x01000402;
    constexpr FormId kBandit3 = 0x01000403;

    struct FortWorld
    {
        FortWorld()
        {
            Bytes special;
            for (const FormId ref : { kBandit1, kBandit2, kBandit3 })
                special.u32(kFortRefType).u32(ref).u32(kWorldL).u16(0).u16(0);
            Bytes record;
            record.append(sub4("EDID", Bytes().zstring("Fort")));
            record.append(sub4("LCSR", special));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("LCTN"), 0, record4("LCTN", 0u, kFort, record)));
            Content::Error error;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            LoadOrder o;
            std::string err;
            EXPECT_TRUE(o.add("Skyrim.esm", false, err));
            EXPECT_TRUE(o.add("test.esp", false, err));
            if (plugin == nullptr)
                return;
            EXPECT_TRUE(parseLocationRecords(*plugin, o, LocationLimits(), mRecords));
            LocationRecords copy = mRecords;
            mIndex.add(std::move(copy));
        }
        LocationRecords mRecords;
        LocationIndex mIndex;
        ActorIndex mActors;
        DialogueIndex mDialogue;
    };

    TEST(SkyrimFortKills, withNoDeadLookupEveryListedReferenceIsAlive)
    {
        FortWorld f;
        SkyrimConditionWorld world(f.mActors, f.mDialogue);
        world.setLocations(&f.mIndex);
        world.setCountRefTypes(true);
        EXPECT_EQ(world.refTypeAliveCount(kFort, kFortRefType), 3.f);
    }

    TEST(SkyrimFortKills, aReferenceTheSessionKilledStopsCounting)
    {
        FortWorld f;
        SkyrimConditionWorld world(f.mActors, f.mDialogue);
        world.setLocations(&f.mIndex);
        world.setCountRefTypes(true);
        std::set<FormId> dead{ kBandit1 };
        world.setDeadLookup([&dead](FormId ref) { return dead.count(ref) != 0; });
        EXPECT_EQ(world.refTypeAliveCount(kFort, kFortRefType), 2.f);
        dead.insert(kBandit2);
        dead.insert(kBandit3);
        EXPECT_EQ(world.refTypeAliveCount(kFort, kFortRefType), 0.f);
    }

    TEST(SkyrimFortKills, aKillOutsideTheListChangesNothing)
    {
        FortWorld f;
        SkyrimConditionWorld world(f.mActors, f.mDialogue);
        world.setLocations(&f.mIndex);
        world.setCountRefTypes(true);
        world.setDeadLookup([](FormId ref) { return ref == 0x01009999; });
        EXPECT_EQ(world.refTypeAliveCount(kFort, kFortRefType), 3.f);
    }

    TEST(SkyrimFortKills, theDeadLookupIsIgnoredWhileCountingIsOff)
    {
        FortWorld f;
        SkyrimConditionWorld world(f.mActors, f.mDialogue);
        world.setLocations(&f.mIndex);
        world.setDeadLookup([](FormId) { return true; });
        EXPECT_EQ(world.refTypeAliveCount(kFort, kFortRefType), 0.f);
    }
}

namespace
{
    using game_skyrimse::testKillMatches;

    TEST(SkyrimTestKillList, aPlainNameMatchesOnlyItself)
    {
        bool multi = true;
        EXPECT_TRUE(testKillMatches("cwbandit01", "cwbandit01", multi));
        EXPECT_FALSE(multi);
        EXPECT_FALSE(testKillMatches("cwbandit01", "cwbandit02", multi));
    }

    TEST(SkyrimTestKillList, aStarMatchesEveryNameWithThePrefix)
    {
        bool multi = false;
        EXPECT_TRUE(testKillMatches("cwbandit*", "cwbandit07", multi));
        EXPECT_TRUE(multi);
        EXPECT_TRUE(testKillMatches("cwbandit*", "cwbandit", multi));
        EXPECT_FALSE(testKillMatches("cwbandit*", "cwsoldier", multi));
    }

    TEST(SkyrimTestKillList, aCommaSeparatesNamesAndMakesTheAidAList)
    {
        bool multi = false;
        EXPECT_TRUE(testKillMatches("a,b", "b", multi));
        EXPECT_TRUE(multi);
        EXPECT_FALSE(testKillMatches("a,b", "c", multi));
        EXPECT_FALSE(testKillMatches("a,b", "", multi));
        EXPECT_FALSE(testKillMatches("", "a", multi));
    }
}

// ---- Location.GetRefTypeAliveCount (the script CW01Script.checkLocRefTypeAliveCount calls it after each bandit's
// death)
namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kPlace = 0x01000500;
    constexpr FormId kType = 0x01000501;
    constexpr FormId kHolder = 0x01000A01;

    class CountData final : public PapyrusGameData
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
        FormId baseOf(FormId) const override { return 0; }
        float refTypeAliveCount(FormId location, FormId refType) const override
        {
            return location == kPlace && refType == kType ? mAlive : -1.f;
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
        float mAlive = 4.f;
    };

    vmad::Property formProperty(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    TEST(SkyrimFortNative, locationGetRefTypeAliveCountAsksTheGameData)
    {
        CountData data;
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder location("Location");
        location.parent("Form");
        location.function("GetRefTypeAliveCount", "Int").native().param("akRefType", "Form");
        data.add(location);
        ScriptBuilder probe("Probe");
        probe.parent("Form");
        probe.autoProperty("Fort", "Location");
        probe.autoProperty("Kind", "Form");
        probe.var("alive", "Int");
        probe.function("Count")
            .local("n", "Int")
            .callMethod("GetRefTypeAliveCount", Id("::Fort_var"), Id("n"), { Id("::Kind_var") })
            .assign(Id("alive"), Id("n"));
        data.add(probe);
        data.mClasses[kHolder] = "Form";
        data.mClasses[kPlace] = "Location";
        data.mClasses[kType] = "Form";
        PapyrusHost host(data);
        std::vector<std::string> errors;
        host.setLog([&errors](bool error, const std::string& line) {
            if (error)
                errors.push_back(line);
        });
        host.startGame();
        vmad::Script s;
        s.mName = "Probe";
        s.mProperties = { formProperty("Fort", kPlace), formProperty("Kind", kType) };
        ASSERT_EQ(host.attachReference(kHolder, 0, false, { s }), 1u);
        for (int i = 0; i < 3; ++i)
            host.tick(0.1f);
        ASSERT_TRUE(host.callFunction(kHolder, "Probe", "Count"));
        for (int i = 0; i < 3; ++i)
            host.tick(0.1f);
        EXPECT_EQ(host.numberVariable(kHolder, "Probe", "alive"), std::optional<double>(4.0));
        EXPECT_TRUE(errors.empty());
    }
}
