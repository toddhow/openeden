// Task 0296 (milestone M-S7): the start of the Bards College questline. The pack's side, no game data: a script's
// ObjectReference.AddToMap marks that map marker discovered (Discovery::add, the hook, the native), which the first leg's
// stage fragment (MS05 stage 75: Alias_MS05DeadMansRespiteMapmarker.GetRef().AddToMap(false)) needs.
#include <game_skyrimse/map.hpp>
#include <game_skyrimse/map_markers.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::map;
    using PexAsm::Bool;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kWorld = 0x01000100;

    Marker marker(FormId ref, float x, float y, std::uint8_t flags = 0)
    {
        Marker m;
        m.mRef = ref;
        m.mWorld = kWorld;
        m.mX = x;
        m.mY = y;
        m.mFlags = flags;
        m.mType = 1;
        m.mName.mPresent = true;
        m.mName.mText = "M";
        return m;
    }

    MarkerIndex indexOf(std::vector<Marker> markers)
    {
        MarkerRecords records;
        records.mMarkers = std::move(markers);
        MarkerIndex index;
        index.add(std::move(records));
        index.finish();
        return index;
    }

    TEST(SkyrimBardsMap, addDiscoversOneKnownMarkerAndSaysSo)
    {
        const MarkerIndex index = indexOf({ marker(0x10, 0, 0), marker(0x20, 9000, 9000) });
        Discovery state;
        const std::uint64_t before = state.revision();
        EXPECT_TRUE(state.add(index, 0x20));
        EXPECT_TRUE(state.discovered(0x20));
        EXPECT_FALSE(state.discovered(0x10)) << "only the named marker";
        EXPECT_GT(state.revision(), before);
        EXPECT_EQ(state.count(), 1u);
    }

    TEST(SkyrimBardsMap, addTwiceOrForAnUnknownMarkerChangesNothing)
    {
        const MarkerIndex index = indexOf({ marker(0x10, 0, 0) });
        Discovery state;
        ASSERT_TRUE(state.add(index, 0x10));
        const std::uint64_t revision = state.revision();
        EXPECT_FALSE(state.add(index, 0x10)) << "already discovered";
        EXPECT_FALSE(state.add(index, 0x99)) << "not a map marker";
        EXPECT_FALSE(state.add(index, 0));
        EXPECT_EQ(state.revision(), revision);
        EXPECT_EQ(state.count(), 1u);
    }

    TEST(SkyrimBardsMap, addKeepsTheFoundListSortedForTheSaveChunk)
    {
        const MarkerIndex index = indexOf({ marker(0x10, 0, 0), marker(0x20, 1, 1), marker(0x30, 2, 2) });
        Discovery state;
        ASSERT_TRUE(state.add(index, 0x30));
        ASSERT_TRUE(state.add(index, 0x10));
        ASSERT_TRUE(state.add(index, 0x20));
        EXPECT_EQ(state.found(), (std::vector<FormId>{ 0x10, 0x20, 0x30 }));
    }

    // ---- the hook and the native -----------------------------------------------------------------------------------

    TEST(SkyrimBardsHooks, packWorldHooksForwardsAddToMapAndRefusesWithoutIt)
    {
        PackWorldEnv none;
        PackWorldHooks refusing(std::move(none));
        EXPECT_FALSE(refusing.addToMap(0x10));
        std::vector<FormId> seen;
        PackWorldEnv env;
        env.addToMap = [&seen](FormId ref) {
            seen.push_back(ref);
            return ref == 0x10;
        };
        PackWorldHooks hooks(std::move(env));
        EXPECT_TRUE(hooks.addToMap(0x10));
        EXPECT_FALSE(hooks.addToMap(0x11));
        EXPECT_EQ(seen, (std::vector<FormId>{ 0x10, 0x11 }));
    }

    constexpr FormId kRef = 0x01000A01;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kMarkerRef = 0x01000C01;

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
        FormId baseOf(FormId) const override { return 0; }
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

    class MapHooks final : public PapyrusWorldHooks
    {
    public:
        bool addToMap(FormId reference) override
        {
            mAdded.push_back(reference);
            return true;
        }
        std::vector<FormId> mAdded;
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

    struct Rig
    {
        explicit Rig(bool withHooks = true)
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            ref.function("AddToMap").native().param("abAllowFastTravel", "Bool");
            mData.add(ref);
            ScriptBuilder caller("MarkerCaller");
            caller.parent("ObjectReference");
            caller.autoProperty("Marker", "ObjectReference");
            caller.function("Reveal")
                .local("n", "None")
                .callMethod("AddToMap", Id("::Marker_var"), Id("n"), { Bool(false) });
            mData.add(caller);
            mData.mClasses[kRef] = "ObjectReference";
            mData.mClasses[kMarkerRef] = "ObjectReference";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            if (withHooks)
                mHost->setWorldHooks(&mHooks);
            mHost->startGame();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        FakeData mData;
        MapHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs, mErrors;
    };

    TEST(SkyrimBardsPapyrus, addToMapReachesTheHookWithTheMarkerReference)
    {
        Rig rig;
        vmad::Script s;
        s.mName = "MarkerCaller";
        s.mProperties = { objProp("Marker", kMarkerRef) };
        ASSERT_EQ(rig.mHost->attachReference(kRef, kBase, true, { s }), 1u);
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "MarkerCaller", "Reveal"));
        rig.tick();
        EXPECT_EQ(rig.mHooks.mAdded, (std::vector<FormId>{ kMarkerRef }));
    }

    TEST(SkyrimBardsPapyrus, addToMapWithNoHooksIsHarmless)
    {
        Rig rig(false);
        vmad::Script s;
        s.mName = "MarkerCaller";
        s.mProperties = { objProp("Marker", kMarkerRef) };
        ASSERT_EQ(rig.mHost->attachReference(kRef, kBase, true, { s }), 1u);
        rig.tick();
        EXPECT_TRUE(rig.mHost->callFunction(kRef, "MarkerCaller", "Reveal"));
        rig.tick();
        EXPECT_TRUE(rig.mErrors.empty());
    }
}
