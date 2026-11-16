// Task 0256 (Skyrim word walls): the trigger volumes (a sphere primitive, XPRM), the tracker that tells a script the
// player went in or out, the view cone behind ObjectReference.HasLOS, the Papyrus natives a word wall's scripts call, the
// merge of a base form's script properties into a reference's, and the message a learned word shows. Generated plugins
// and scripts; no game data. Written first and run red against stubs (see the task's stage log).
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/shouts.hpp>
#include <game_skyrimse/shouts_service.hpp>
#include <game_skyrimse/triggers.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <limits>
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
    using namespace game_skyrimse::shouts;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kInf = std::numeric_limits<double>::infinity();

    TriggerVolume sphere(double x, double y, double z, double radius, FormId cell = 0x100, bool interior = true)
    {
        TriggerVolume v;
        v.mType = kPrimitiveSphere;
        v.mBounds[0] = radius;
        v.mBounds[1] = radius;
        v.mBounds[2] = radius;
        v.mPosition[0] = x;
        v.mPosition[1] = y;
        v.mPosition[2] = z;
        v.mCell = cell;
        v.mInterior = interior;
        return v;
    }

    TriggerPlace inInterior(FormId cell)
    {
        TriggerPlace p;
        p.mKnown = true;
        p.mInterior = true;
        p.mCell = cell;
        return p;
    }

    TriggerPlace inExterior()
    {
        TriggerPlace p;
        p.mKnown = true;
        return p;
    }

    // ---- the volume -------------------------------------------------------------------------------------------------

    TEST(SkyrimTriggerVolume, aSphereHoldsThePointsWithinItsRadiusAndItsSurface)
    {
        const TriggerVolume v = sphere(100, 200, 300, 50);
        EXPECT_TRUE(volumeContains(v, 100, 200, 300));
        EXPECT_TRUE(volumeContains(v, 150, 200, 300)); // on the surface
        EXPECT_TRUE(volumeContains(v, 100, 200, 260));
        EXPECT_FALSE(volumeContains(v, 150.01, 200, 300));
        EXPECT_FALSE(volumeContains(v, 130, 230, 330)); // 3 x 30 x 30 = 52 units out
        EXPECT_FALSE(volumeContains(v, 0, 0, 0));
    }

    TEST(SkyrimTriggerVolume, onlyASphereWithAPositiveFiniteRadiusIsSupported)
    {
        EXPECT_TRUE(triggerSupported(sphere(0, 0, 0, 1)));
        TriggerVolume box = sphere(0, 0, 0, 100);
        box.mType = 1;
        EXPECT_FALSE(triggerSupported(box));
        EXPECT_FALSE(volumeContains(box, 0, 0, 0));
        EXPECT_FALSE(triggerSupported(sphere(0, 0, 0, 0)));
        EXPECT_FALSE(triggerSupported(sphere(0, 0, 0, -5)));
        EXPECT_FALSE(triggerSupported(sphere(0, 0, 0, kInf)));
        EXPECT_FALSE(triggerSupported(sphere(std::nan(""), 0, 0, 5)));
    }

    TEST(SkyrimTriggerVolume, aPointThatIsNotFiniteIsNeverInside)
    {
        const TriggerVolume v = sphere(0, 0, 0, 1000);
        EXPECT_FALSE(volumeContains(v, std::nan(""), 0, 0));
        EXPECT_FALSE(volumeContains(v, 0, kInf, 0));
    }

    // ---- the view cone ----------------------------------------------------------------------------------------------

    TEST(SkyrimTriggerView, aTargetAheadIsSeenAndOneBehindIsNot)
    {
        // heading 0 faces +Y
        EXPECT_TRUE(inViewCone(0, 0, 0, 0.f, 0.f, 0, 500, 0, kViewHalfAngle));
        EXPECT_FALSE(inViewCone(0, 0, 0, 0.f, 0.f, 0, -500, 0, kViewHalfAngle));
        // a quarter turn to the right (heading pi / 2) faces +X
        EXPECT_TRUE(inViewCone(0, 0, 0, static_cast<float>(kPi / 2), 0.f, 500, 0, 0, kViewHalfAngle));
        EXPECT_FALSE(inViewCone(0, 0, 0, static_cast<float>(kPi / 2), 0.f, 0, 500, 0, kViewHalfAngle));
    }

    TEST(SkyrimTriggerView, theConeHasTheGivenHalfAngle)
    {
        const double inside = kViewHalfAngle - 0.05;
        const double outside = kViewHalfAngle + 0.05;
        EXPECT_TRUE(inViewCone(0, 0, 0, 0.f, 0.f, 1000 * std::sin(inside), 1000 * std::cos(inside), 0, kViewHalfAngle));
        EXPECT_FALSE(
            inViewCone(0, 0, 0, 0.f, 0.f, 1000 * std::sin(outside), 1000 * std::cos(outside), 0, kViewHalfAngle));
    }

    TEST(SkyrimTriggerView, thePitchLooksUpAndAtTheEyeNotTheFeet)
    {
        EXPECT_TRUE(inViewCone(0, 0, 0, 0.f, static_cast<float>(kPi / 2 - 0.1), 0, 10, 1000, kViewHalfAngle));
        EXPECT_FALSE(inViewCone(0, 0, 0, 0.f, 0.f, 0, 10, 1000, kViewHalfAngle)); // straight above, looking level
        EXPECT_TRUE(inViewCone(0, 0, 0, 0.f, 0.f, 0, 0, 0, kViewHalfAngle)); // a target at the eye is in view
    }

    TEST(SkyrimTriggerView, aNonFiniteInputIsNotInView)
    {
        EXPECT_FALSE(inViewCone(0, 0, 0, std::nanf(""), 0.f, 0, 500, 0, kViewHalfAngle));
        EXPECT_FALSE(inViewCone(0, 0, 0, 0.f, 0.f, 0, std::nan(""), 0, kViewHalfAngle));
    }

    // ---- the tracker ------------------------------------------------------------------------------------------------

    TEST(SkyrimTriggerTracker, goingInAndOutGivesOneEdgeEachAndNothingBetween)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 100)));
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 500, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        t.update(inInterior(0x100), 50, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_EQ(edges[0].mReference, 0x500u);
        EXPECT_TRUE(edges[0].mEntered);
        EXPECT_TRUE(t.inside(0x500));
        edges.clear();
        t.update(inInterior(0x100), 60, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        t.update(inInterior(0x100), 400, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_FALSE(edges[0].mEntered);
        EXPECT_FALSE(t.inside(0x500));
    }

    TEST(SkyrimTriggerTracker, edgesComeInAscendingReferenceOrder)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x900, sphere(0, 0, 0, 100)));
        ASSERT_TRUE(t.add(0x300, sphere(0, 0, 0, 100)));
        ASSERT_TRUE(t.add(0x600, sphere(0, 0, 0, 100)));
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 3u);
        EXPECT_EQ(edges[0].mReference, 0x300u);
        EXPECT_EQ(edges[1].mReference, 0x600u);
        EXPECT_EQ(edges[2].mReference, 0x900u);
    }

    TEST(SkyrimTriggerTracker, anInteriorVolumeCountsOnlyInItsOwnCellAndAnExteriorOneOnlyOutdoors)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 100, 0x100, true)));
        ASSERT_TRUE(t.add(0x600, sphere(0, 0, 0, 100, 0x200, false)));
        std::vector<TriggerEdge> edges;
        // the player stands at the same coordinates in ANOTHER interior: neither volume holds him
        t.update(inInterior(0x777), 0, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        // in the first interior: only the interior volume
        t.update(inInterior(0x100), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_EQ(edges[0].mReference, 0x500u);
        edges.clear();
        // outdoors: the interior volume lets go and the exterior one takes him
        t.update(inExterior(), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 2u);
        EXPECT_EQ(edges[0].mReference, 0x500u);
        EXPECT_FALSE(edges[0].mEntered);
        EXPECT_EQ(edges[1].mReference, 0x600u);
        EXPECT_TRUE(edges[1].mEntered);
    }

    TEST(SkyrimTriggerTracker, anUnknownPlaceChangesNothing)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 100)));
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 0, 0, 0, edges);
        edges.clear();
        t.update(TriggerPlace(), 5000, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        EXPECT_TRUE(t.inside(0x500));
    }

    TEST(SkyrimTriggerTracker, resetInsideReportsTheVolumesHeStandsInAgain)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 100)));
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        edges.clear();
        t.resetInside();
        EXPECT_FALSE(t.inside(0x500));
        t.update(inInterior(0x100), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_TRUE(edges[0].mEntered);
    }

    TEST(SkyrimTriggerTracker, anUnsupportedVolumeIsRefusedAndCountedAndNeverFires)
    {
        TriggerTracker t;
        TriggerVolume box = sphere(0, 0, 0, 100);
        box.mType = 1;
        EXPECT_FALSE(t.add(0x500, box));
        EXPECT_EQ(t.unsupported(), 1u);
        EXPECT_EQ(t.count(), 0u);
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 0, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
    }

    TEST(SkyrimTriggerTracker, addingAReferenceAgainReplacesItsVolumeAndKeepsTheInsideFlag)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 100)));
        std::vector<TriggerEdge> edges;
        t.update(inInterior(0x100), 0, 0, 0, edges);
        edges.clear();
        ASSERT_TRUE(t.add(0x500, sphere(0, 0, 0, 200)));
        EXPECT_EQ(t.count(), 1u);
        t.update(inInterior(0x100), 150, 0, 0, edges);
        EXPECT_TRUE(edges.empty()); // still inside the larger volume: no second enter
        EXPECT_TRUE(t.inside(0x500));
    }

    TEST(SkyrimTriggerTracker, theTrackerHoldsAtMostItsLimit)
    {
        TriggerTracker t;
        for (std::size_t i = 0; i < TriggerTracker::kMaxTriggers; ++i)
            ASSERT_TRUE(t.add(static_cast<FormId>(0x1000 + i), sphere(0, 0, 0, 10)));
        EXPECT_FALSE(t.add(0x900000, sphere(0, 0, 0, 10)));
        EXPECT_EQ(t.count(), TriggerTracker::kMaxTriggers);
        EXPECT_TRUE(t.add(0x1000, sphere(0, 0, 0, 20))); // replacing is still allowed
    }

    // ---- XPRM in a placed reference --------------------------------------------------------------------------------

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes pluginWithRef(const Bytes& refSubs)
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes cellSubs;
        cellSubs.append(sub4("EDID", Bytes().zstring("Wall")));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        Bytes sub = record4("CELL", 0, cellId, cellSubs);
        sub.append(group4(
            cellId, kGrupCellChildren, group4(cellId, kGrupTemporary, record4("REFR", 0, 0x01000900, refSubs))));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop,
            group4(cellId % 10, kGrupInteriorBlock, group4((cellId / 10) % 10, kGrupInteriorSub, sub))));
        return bytes;
    }

    Bytes xprm(float bounds, std::uint32_t type)
    {
        Bytes b;
        b.f32(bounds).f32(bounds).f32(bounds).f32(1.f).f32(1.f).f32(0.f).f32(0.15f).u32(type);
        return b;
    }

    TEST(SkyrimWordWallRecords, aReferenceKeepsItsPrimitive)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(0x0005095E)));
        subs.append(sub4("XPRM", xprm(2064.78f, 2)));
        subs.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(pluginWithRef(subs).str(), "test.esp", order(), ParseLimits(), plugin, error))
            << error.describe();
        ASSERT_EQ(plugin.mCells.size(), 1u);
        ASSERT_EQ(plugin.mCells[0].mTemporary.size(), 1u);
        const PlacedRef& ref = plugin.mCells[0].mTemporary[0];
        ASSERT_TRUE(ref.mPrimitive.has_value());
        EXPECT_EQ(ref.mPrimitive->mType, 2u);
        EXPECT_FLOAT_EQ(ref.mPrimitive->mBounds[0], 2064.78f);
        EXPECT_FLOAT_EQ(ref.mPrimitive->mBounds[2], 2064.78f);
        EXPECT_EQ(plugin.mOddSubrecords, 0u);
    }

    TEST(SkyrimWordWallRecords, aReferenceWithNoPrimitiveHasNone)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(0x00001234)));
        subs.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(pluginWithRef(subs).str(), "test.esp", order(), ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mCells.size(), 1u);
        ASSERT_EQ(plugin.mCells[0].mTemporary.size(), 1u);
        EXPECT_FALSE(plugin.mCells[0].mTemporary[0].mPrimitive.has_value());
    }

    TEST(SkyrimWordWallRecords, aPrimitiveOfTheWrongSizeIsCountedAndDropped)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(0x0005095E)));
        subs.append(sub4("XPRM", Bytes().f32(1).f32(2).f32(3)));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(pluginWithRef(subs).str(), "test.esp", order(), ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mCells.size(), 1u);
        ASSERT_EQ(plugin.mCells[0].mTemporary.size(), 1u);
        EXPECT_FALSE(plugin.mCells[0].mTemporary[0].mPrimitive.has_value());
        EXPECT_EQ(plugin.mOddSubrecords, 1u);
    }

    // ---- a base form's script properties fill the reference's --------------------------------------------------------

    vmad::Property intProp(const std::string& name, std::int32_t value)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 3;
        p.mValue.mKind = vmad::ValueKind::Int;
        p.mValue.mInteger = value;
        return p;
    }

    vmad::Script scriptWith(const std::string& name, std::vector<vmad::Property> props)
    {
        vmad::Script s;
        s.mName = name;
        s.mProperties = std::move(props);
        return s;
    }

    TEST(SkyrimWordWallScripts, aScriptBothTheReferenceAndTheBaseCarryGetsTheBasesMissingProperties)
    {
        const std::vector<vmad::Script> own = { scriptWith("WordWallTriggerScript", { intProp("myWord", 1) }) };
        const std::vector<vmad::Script> base
            = { scriptWith("wordwalltriggerscript", { intProp("myWord", 9), intProp("outerRadius", 1200) }) };
        const std::vector<vmad::Script> all = vmad::combineScripts(own, &base);
        ASSERT_EQ(all.size(), 1u);
        ASSERT_EQ(all[0].mProperties.size(), 2u);
        EXPECT_EQ(all[0].mProperties[0].mName, "myWord");
        EXPECT_EQ(all[0].mProperties[0].mValue.mInteger, 1) << "the reference's value wins";
        EXPECT_EQ(all[0].mProperties[1].mName, "outerRadius");
        EXPECT_EQ(all[0].mProperties[1].mValue.mInteger, 1200) << "the base fills what the reference leaves out";
    }

    TEST(SkyrimWordWallScripts, thePropertyNamesAreComparedWithoutCase)
    {
        const std::vector<vmad::Script> own = { scriptWith("Scr", { intProp("MyWord", 1) }) };
        const std::vector<vmad::Script> base = { scriptWith("Scr", { intProp("myword", 9) }) };
        const std::vector<vmad::Script> all = vmad::combineScripts(own, &base);
        ASSERT_EQ(all.size(), 1u);
        ASSERT_EQ(all[0].mProperties.size(), 1u);
        EXPECT_EQ(all[0].mProperties[0].mValue.mInteger, 1);
    }

    // ---- the natives and events a word wall's scripts use -----------------------------------------------------------

    constexpr FormId kWallRef = 0x01000A00;
    constexpr FormId kOtherRef = 0x01000A01;
    constexpr FormId kMarker = 0x01000A10;
    constexpr FormId kPlayer = 0x14;

    class WallData final : public PapyrusGameData
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
            if (form == kMarker || form == kWallRef || form == kOtherRef)
                return "ObjectReference";
            return std::string();
        }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            if (reference != kMarker)
                return false;
            x = mMarker[0];
            y = mMarker[1];
            z = mMarker[2];
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
        double mMarker[3] = { 0, 500, 120 };
    };

    ScriptBuilder wallScript()
    {
        ScriptBuilder s("TestWall");
        s.parent("ObjectReference");
        s.var("enters", "Int");
        s.var("leaves", "Int");
        s.var("who", "ObjectReference");
        s.var("sees", "Bool");
        s.var("dist", "Float", Flt(-1));
        s.autoProperty("Marker", "ObjectReference");
        s.function("OnTriggerEnter")
            .param("akActionRef", "ObjectReference")
            .iadd(Id("enters"), Id("enters"), Int(1))
            .assign(Id("who"), Id("akActionRef"));
        s.function("OnTriggerLeave")
            .param("akActionRef", "ObjectReference")
            .iadd(Id("leaves"), Id("leaves"), Int(1));
        s.function("Probe")
            .local("p", "Actor")
            .callStatic("Game", "GetPlayer", Id("p"))
            .local("b", "Bool")
            .callMethod("HasLOS", Id("p"), Id("b"), { Id("::Marker_var") })
            .assign(Id("sees"), Id("b"))
            .local("d", "Float")
            .callMethod("GetDistance", Id("p"), Id("d"), { Id("::Marker_var") })
            .assign(Id("dist"), Id("d"));
        return s;
    }

    void addWallClasses(WallData& data)
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
        actor.function("HasLOS", "Bool").native().param("akTarget", "ObjectReference");
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("GetPlayer", "Actor").global().native();
        data.add(game);
    }

    vmad::Property markerProp()
    {
        vmad::Property p;
        p.mName = "Marker";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = kMarker;
        p.mValue.mAlias = -1;
        return p;
    }

    struct WallRig
    {
        WallRig()
        {
            addWallClasses(mData);
            mData.add(wallScript());
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) {
                if (error)
                    mErrors.push_back(line);
            });
            vmad::Script s;
            s.mName = "TestWall";
            s.mProperties = { markerProp() };
            EXPECT_EQ(mHost->attachReference(kWallRef, 0x5095E, false, { s }), 1u);
            EXPECT_EQ(mHost->attachReference(kOtherRef, 0x5095E, false, { s }), 1u);
            mHost->tick(0.1f);
        }
        ~WallRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        static PapyrusPlayerView view(double x, double y, double z, float heading = 0.f, float pitch = 0.f,
            TriggerPlace place = inInterior(0x100))
        {
            PapyrusPlayerView v;
            v.mKnown = true;
            v.mX = x;
            v.mY = y;
            v.mZ = z;
            v.mHeading = heading;
            v.mPitch = pitch;
            v.mPlace = place;
            return v;
        }
        double number(FormId ref, const char* name) const
        {
            return mHost->numberVariable(ref, "TestWall", name).value_or(-99);
        }

        WallData mData;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimWordWallHost, theScriptOnAVolumeGetsOneEnterAndOneLeaveWithThePlayer)
    {
        WallRig rig;
        ASSERT_TRUE(rig.mHost->addTrigger(kWallRef, sphere(0, 0, 0, 100)));
        EXPECT_EQ(rig.mHost->triggerCount(), 1u);
        rig.mHost->setPlayerView(WallRig::view(500, 0, 0));
        EXPECT_EQ(rig.mHost->updateTriggers(), 0u);
        rig.mHost->setPlayerView(WallRig::view(30, 0, 0));
        EXPECT_EQ(rig.mHost->updateTriggers(), 1u);
        EXPECT_EQ(rig.mHost->updateTriggers(), 0u); // no second enter while he stays
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.number(kWallRef, "enters"), 1.0);
        EXPECT_EQ(rig.number(kWallRef, "leaves"), 0.0);
        const std::optional<FormId> who = rig.mHost->formVariable(kWallRef, "TestWall", "who");
        ASSERT_TRUE(who.has_value());
        EXPECT_EQ(*who, kPlayer);
        rig.mHost->setPlayerView(WallRig::view(900, 0, 0));
        EXPECT_EQ(rig.mHost->updateTriggers(), 1u);
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.number(kWallRef, "enters"), 1.0);
        EXPECT_EQ(rig.number(kWallRef, "leaves"), 1.0);
        EXPECT_EQ(rig.mHost->counters().mEvents.at("ontriggerenter"), 1u);
        EXPECT_EQ(rig.mHost->counters().mEvents.at("ontriggerleave"), 1u);
    }

    TEST(SkyrimWordWallHost, aReferenceThatIsNotAVolumeGetsNoTriggerEvent)
    {
        WallRig rig;
        ASSERT_TRUE(rig.mHost->addTrigger(kWallRef, sphere(0, 0, 0, 100)));
        rig.mHost->setPlayerView(WallRig::view(0, 0, 0));
        (void)rig.mHost->updateTriggers();
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.number(kOtherRef, "enters"), 0.0);
        EXPECT_EQ(rig.number(kWallRef, "enters"), 1.0);
    }

    TEST(SkyrimWordWallHost, resettingTheTriggersMakesTheNextUpdateEnterAgain)
    {
        WallRig rig;
        ASSERT_TRUE(rig.mHost->addTrigger(kWallRef, sphere(0, 0, 0, 100)));
        rig.mHost->setPlayerView(WallRig::view(0, 0, 0));
        EXPECT_EQ(rig.mHost->updateTriggers(), 1u);
        rig.mHost->resetTriggers();
        EXPECT_EQ(rig.mHost->updateTriggers(), 1u);
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.number(kWallRef, "enters"), 2.0);
    }

    TEST(SkyrimWordWallHost, noPlayerViewMeansNoEvents)
    {
        WallRig rig;
        ASSERT_TRUE(rig.mHost->addTrigger(kWallRef, sphere(0, 0, 0, 100)));
        EXPECT_EQ(rig.mHost->updateTriggers(), 0u);
        const PapyrusPlayerView unknown;
        rig.mHost->setPlayerView(unknown);
        EXPECT_EQ(rig.mHost->updateTriggers(), 0u);
    }

    TEST(SkyrimWordWallHost, hasLOSIsTrueWhenThePlayerLooksAtTheMarkerAndFalseWhenHeLooksAway)
    {
        WallRig rig;
        // the player at the origin faces +Y (heading 0); the marker is 500 units ahead at the eye's height
        rig.mHost->setPlayerView(WallRig::view(0, 0, 0, 0.f, 0.f));
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "TestWall", "Probe"));
        EXPECT_EQ(rig.number(kWallRef, "sees"), 1.0);
        rig.mHost->setPlayerView(WallRig::view(0, 0, 0, static_cast<float>(kPi), 0.f)); // turned round
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "TestWall", "Probe"));
        EXPECT_EQ(rig.number(kWallRef, "sees"), 0.0);
    }

    TEST(SkyrimWordWallHost, hasLOSIsFalseWhenThePlayerViewIsNotKnown)
    {
        WallRig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "TestWall", "Probe"));
        EXPECT_EQ(rig.number(kWallRef, "sees"), 0.0);
    }

    TEST(SkyrimWordWallHost, thePlayersDistanceToAReferenceUsesTheViewsPosition)
    {
        WallRig rig;
        rig.mHost->setPlayerView(WallRig::view(0, 100, 0));
        ASSERT_TRUE(rig.mHost->callFunction(kWallRef, "TestWall", "Probe"));
        // the marker is at (0, 500, 120): 400 along Y and 120 up
        EXPECT_NEAR(rig.number(kWallRef, "dist"), std::sqrt(400.0 * 400.0 + 120.0 * 120.0), 0.01);
    }

    // ---- the message a learned word shows ---------------------------------------------------------------------------

    constexpr FormId kFus = 0x13E08, kRo = 0x13E09, kDah = 0x13E0A, kShout = 0x13E07;

    struct ShoutRig
    {
        ShoutRig()
        {
            ShoutRecord shout;
            shout.mCommon.mId = kShout;
            shout.mCommon.mEditorId = "UnrelentingForceShout";
            shout.mHasData = true;
            shout.mWordCount = 3;
            shout.mWords[0] = { kFus, 0x100, 15.f };
            shout.mWords[1] = { kRo, 0x101, 20.f };
            shout.mWords[2] = { kDah, 0x102, 45.f };
            const auto word = [](FormId id, const char* name) {
                WordRecord w;
                w.mCommon.mId = id;
                w.mCommon.mEditorId = name;
                return w;
            };
            mIndex.add({ shout }, { word(kFus, "WordFus"), word(kRo, "WordRo"), word(kDah, "WordDah") });
            Hooks hooks;
            hooks.mIndex = [this]() -> const ShoutIndex* { return &mIndex; };
            mService = std::make_unique<ShoutService>(std::move(hooks));
        }
        ShoutIndex mIndex;
        std::unique_ptr<ShoutService> mService;
    };

    TEST(SkyrimWordWallShouts, learningANewWordAnnouncesItByName)
    {
        ShoutRig rig;
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        ASSERT_EQ(rig.mService->state().noticeCount(), 1u);
        const std::string text = rig.mService->state().noticeAt(1);
        EXPECT_NE(text.find("Word of Power"), std::string::npos) << text;
        EXPECT_NE(text.find("Fus"), std::string::npos) << text;
        EXPECT_EQ(text.find("WordFus"), std::string::npos) << "the editor id's Word prefix is not shown: " << text;
    }

    TEST(SkyrimWordWallShouts, aWordAlreadyLearnedAnnouncesNothingTheSecondTime)
    {
        ShoutRig rig;
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        EXPECT_EQ(rig.mService->state().noticeCount(), 1u);
    }

    TEST(SkyrimWordWallShouts, aWordTheRecordsDoNotKnowAnnouncesNothing)
    {
        ShoutRig rig;
        EXPECT_FALSE(rig.mService->teachWord(0x20000));
        EXPECT_EQ(rig.mService->state().noticeCount(), 0u);
    }

    TEST(SkyrimWordWallShouts, aLearnedWordCanThenBeUnlockedWithADragonSoulAndShouted)
    {
        ShoutRig rig;
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        rig.mService->state().addSouls(1);
        EXPECT_TRUE(rig.mService->state().unlock(kFus));
        const ShoutRecord* shout = rig.mIndex.shout(kShout);
        ASSERT_NE(shout, nullptr);
        EXPECT_EQ(rig.mService->state().availableWords(*shout), 1u);
    }

    TEST(SkyrimWordWallShouts, aSoulUnlocksALearnedWordByItsEditorIdAndIsSpent)
    {
        ShoutRig rig;
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        rig.mService->state().addSouls(2);
        EXPECT_TRUE(rig.mService->unlockWithSoul("WordFus"));
        EXPECT_TRUE(rig.mService->isWordUnlocked(kFus));
        EXPECT_EQ(rig.mService->state().souls(), 1u);
    }

    TEST(SkyrimWordWallShouts, aWordNotLearnedYetCannotBeUnlockedAndNoSoulIsSpent)
    {
        ShoutRig rig;
        rig.mService->state().addSouls(1);
        EXPECT_FALSE(rig.mService->unlockWithSoul("WordFus"));
        EXPECT_FALSE(rig.mService->isWordUnlocked(kFus));
        EXPECT_EQ(rig.mService->state().souls(), 1u);
    }

    TEST(SkyrimWordWallShouts, withNoSoulOrAnUnknownNameOrAnUnlockedWordNothingHappens)
    {
        ShoutRig rig;
        ASSERT_TRUE(rig.mService->teachWord(kFus));
        EXPECT_FALSE(rig.mService->unlockWithSoul("WordFus")); // no soul
        rig.mService->state().addSouls(2);
        EXPECT_FALSE(rig.mService->unlockWithSoul("WordNope"));
        EXPECT_FALSE(rig.mService->unlockWithSoul(""));
        EXPECT_TRUE(rig.mService->unlockWithSoul("WordFus"));
        EXPECT_FALSE(rig.mService->unlockWithSoul("WordFus")); // already unlocked: the second soul stays
        EXPECT_EQ(rig.mService->state().souls(), 1u);
    }
}
