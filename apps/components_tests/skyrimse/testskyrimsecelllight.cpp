// Task 0355 stages f and j: an interior cell's directional rotation and fade, clip distance and light fade distances
// (CellLightSettings), after its lighting template (LTMP, a LGTM record). Generated plugins only; written before the
// code, the first run against a stub that answered nothing.
//
// XCLL layout used (xEdit's, RECALLED-VERIFY; the value ranges of the five masters agree with it, see the task doc):
// 0 ambient, 4 directional, 8 fog colour (R G B A), 12 fog near, 16 fog far, 20 rotation XY (i32), 24 rotation Z (i32),
// 28 directional fade, 32 clip distance, 36 fog power, 40 ambient cube (24 bytes), 64 specular colour, 68 fresnel,
// 72 far fog colour, 76 fog max, 80 light fade begin, 84 light fade end, 88 inherit flags. A 64 byte XCLL stops before
// the specular colour.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/cell_light_source.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;
    namespace fs = std::filesystem;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6;
    constexpr std::uint32_t kTemplate = 0x00000C00, kCellOwn = 0x00000C10, kCellAll = 0x00000C20,
                            kCellRotation = 0x00000C30, kCellShort = 0x00000C40, kCellMissing = 0x00000C50,
                            kCellNoXcll = 0x00000C60, kCellFade = 0x00000C70, kCellClip = 0x00000C80,
                            kCellLightFade = 0x00000C90;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    struct Words
    {
        std::int32_t rotXY = 0, rotZ = 0;
        float fade = 0.f, clip = 0.f, fadeBegin = 0.f, fadeEnd = 0.f;
    };

    // An XCLL / LGTM DATA body of `size` bytes (64 or 92): colours and fog filler, then the words under test.
    Bytes lightingBody(const Words& w, std::uint32_t inherit, std::size_t size)
    {
        Bytes d;
        d.u32(0x00403020).u32(0x00605040).u32(0x00908070).f32(100.f).f32(2000.f); // 0 to 19
        d.u32(static_cast<std::uint32_t>(w.rotXY)).u32(static_cast<std::uint32_t>(w.rotZ)); // 20, 24
        d.f32(w.fade).f32(w.clip); // 28, 32
        while (d.size() < std::min<std::size_t>(80, size))
            d.u8(0);
        if (size == 92)
        {
            d.f32(w.fadeBegin).f32(w.fadeEnd); // 80, 84
            d.u32(inherit); // 88
        }
        while (d.size() < size)
            d.u8(0);
        return d;
    }

    Bytes templateRecord()
    {
        Words w;
        w.rotXY = 45;
        w.rotZ = 270;
        w.fade = 0.5f;
        w.clip = 12000.f;
        w.fadeBegin = 4000.f;
        w.fadeEnd = 5000.f;
        Bytes subs;
        subs.append(edid("TestTemplate"));
        subs.append(sub4("DATA", lightingBody(w, 0, 92)));
        return record4("LGTM", 0, kTemplate, subs);
    }

    Bytes interiorCell(std::uint32_t id, const std::string& name, const Bytes& extraSubs)
    {
        Bytes cellSubs;
        cellSubs.append(edid(name));
        cellSubs.append(sub4("FULL", Bytes().zstring(name)));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        cellSubs.append(extraSubs);
        Bytes sub = record4("CELL", 0, id, cellSubs);
        sub.append(group4(id, kGrupCellChildren, Bytes()));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    Bytes cellWith(const Words& w, std::uint32_t inherit, std::uint32_t templateId, std::size_t size = 92)
    {
        Bytes subs = sub4("XCLL", lightingBody(w, inherit, size));
        if (templateId != 0)
            subs.append(sub4("LTMP", Bytes().u32(templateId)));
        return subs;
    }

    Words ownWords()
    {
        Words w;
        w.rotXY = 10;
        w.rotZ = 20;
        w.fade = 0.25f;
        w.clip = 3000.f;
        w.fadeBegin = 100.f;
        w.fadeEnd = 200.f;
        return w;
    }

    Bytes esm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(group4(Content::fourCC("LGTM"), kGrupTop, templateRecord()));
        Bytes cells;
        cells.append(interiorCell(kCellOwn, "OwnValues", cellWith(ownWords(), 0, kTemplate)));
        cells.append(interiorCell(kCellAll, "AllInherited",
            cellWith(ownWords(),
                LightingInherit::Rotation | LightingInherit::DirectionalFade | LightingInherit::ClipDistance
                    | LightingInherit::LightFade,
                kTemplate)));
        cells.append(interiorCell(
            kCellRotation, "RotationInherited", cellWith(ownWords(), LightingInherit::Rotation, kTemplate)));
        cells.append(interiorCell(kCellShort, "ShortXcll", cellWith(ownWords(), 0, kTemplate, 64)));
        cells.append(interiorCell(kCellMissing, "MissingTemplate", cellWith(ownWords(), 0xffffffffu, 0x00000CEE)));
        cells.append(interiorCell(kCellNoXcll, "NoXcll", Bytes()));
        cells.append(interiorCell(
            kCellFade, "FadeInherited", cellWith(ownWords(), LightingInherit::DirectionalFade, kTemplate)));
        cells.append(
            interiorCell(kCellClip, "ClipInherited", cellWith(ownWords(), LightingInherit::ClipDistance, kTemplate)));
        cells.append(interiorCell(
            kCellLightFade, "LightFadeInherited", cellWith(ownWords(), LightingInherit::LightFade, kTemplate)));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop, cells));
        return bytes;
    }

    struct Plugin
    {
        fs::path mDir;
        fs::path mFile;
        Plugin()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            mDir = fs::temp_directory_path() / ("oe_skyrim_celllight_" + std::to_string(stamp));
            fs::create_directories(mDir);
            mFile = mDir / "celllight.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = esm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        ~Plugin()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
    };

    struct Loaded
    {
        Plugin plugin;
        SkyrimWorldView view;
        Loaded()
        {
            std::vector<std::string> problems;
            view.load({ plugin.mFile }, problems);
            EXPECT_TRUE(problems.empty());
        }
        bool settings(const char* editorId, sdkgame::CellLightSettings& out)
        {
            const auto info = view.findCellByEditorId(editorId);
            return info && view.lightSettingsForCell(info->id, out);
        }
    };

    // ---- the pure rule -----------------------------------------------------------------------------------------

    CellLightingData ownData(std::uint32_t inherit)
    {
        CellLightingData d;
        d.mRotationXY = 1;
        d.mRotationZ = 2;
        d.mDirectionalFade = 0.1f;
        d.mClipDistance = 3.f;
        d.mLightFadeBegin = 4.f;
        d.mLightFadeEnd = 5.f;
        d.mInherit = inherit;
        return d;
    }

    CellLightingData templateData()
    {
        CellLightingData d;
        d.mRotationXY = 91;
        d.mRotationZ = 92;
        d.mDirectionalFade = 0.9f;
        d.mClipDistance = 93.f;
        d.mLightFadeBegin = 94.f;
        d.mLightFadeEnd = 95.f;
        return d;
    }

    TEST(SkyrimCellLightRule, noFlagsAndNoTemplateLeaveTheCellsOwnWords)
    {
        const CellLightingData t = templateData();
        for (const CellLightingData* tmpl : { &t, static_cast<const CellLightingData*>(nullptr) })
        {
            const CellLightingData out = applyLightingTemplate(ownData(tmpl == nullptr ? 0xffffffffu : 0), tmpl);
            EXPECT_EQ(out.mRotationXY, 1);
            EXPECT_EQ(out.mRotationZ, 2);
            EXPECT_EQ(out.mDirectionalFade, 0.1f);
            EXPECT_EQ(out.mClipDistance, 3.f);
            EXPECT_EQ(out.mLightFadeBegin, 4.f);
            EXPECT_EQ(out.mLightFadeEnd, 5.f);
        }
    }

    TEST(SkyrimCellLightRule, theRotationFlagTakesBothAnglesAndNothingElse)
    {
        const CellLightingData t = templateData();
        const CellLightingData out = applyLightingTemplate(ownData(LightingInherit::Rotation), &t);
        EXPECT_EQ(out.mRotationXY, 91);
        EXPECT_EQ(out.mRotationZ, 92);
        EXPECT_EQ(out.mDirectionalFade, 0.1f);
        EXPECT_EQ(out.mClipDistance, 3.f);
        EXPECT_EQ(out.mLightFadeBegin, 4.f);
    }

    TEST(SkyrimCellLightRule, theFadeClipAndLightFadeFlagsEachTakeTheirOwnWords)
    {
        const CellLightingData t = templateData();
        {
            const CellLightingData out = applyLightingTemplate(ownData(LightingInherit::DirectionalFade), &t);
            EXPECT_EQ(out.mDirectionalFade, 0.9f);
            EXPECT_EQ(out.mRotationXY, 1);
            EXPECT_EQ(out.mClipDistance, 3.f);
        }
        {
            const CellLightingData out = applyLightingTemplate(ownData(LightingInherit::ClipDistance), &t);
            EXPECT_EQ(out.mClipDistance, 93.f);
            EXPECT_EQ(out.mDirectionalFade, 0.1f);
            EXPECT_EQ(out.mLightFadeEnd, 5.f);
        }
        {
            const CellLightingData out = applyLightingTemplate(ownData(LightingInherit::LightFade), &t);
            EXPECT_EQ(out.mLightFadeBegin, 94.f);
            EXPECT_EQ(out.mLightFadeEnd, 95.f);
            EXPECT_EQ(out.mClipDistance, 3.f);
        }
    }

    TEST(SkyrimCellLightRule, theFlagBitsAreTheOnesTheDataShows)
    {
        EXPECT_EQ(LightingInherit::Rotation, 0x20u);
        EXPECT_EQ(LightingInherit::DirectionalFade, 0x40u);
        EXPECT_EQ(LightingInherit::ClipDistance, 0x80u);
        EXPECT_EQ(LightingInherit::LightFade, 0x400u);
    }

    // ---- through the records and the view ------------------------------------------------------------------------

    TEST(SkyrimCellLightView, aCellWithNoFlagsGivesItsOwnWords)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(l.settings("OwnValues", s));
        EXPECT_EQ(s.rotationXY, 10.0);
        EXPECT_EQ(s.rotationZ, 20.0);
        EXPECT_EQ(s.directionalFade, 0.25);
        EXPECT_EQ(s.clipDistance, 3000.0);
        EXPECT_EQ(s.lightFadeBegin, 100.0);
        EXPECT_EQ(s.lightFadeEnd, 200.0);
    }

    TEST(SkyrimCellLightView, aCellThatInheritsEverythingGetsTheTemplatesWords)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(l.settings("AllInherited", s));
        EXPECT_EQ(s.rotationXY, 45.0);
        EXPECT_EQ(s.rotationZ, 270.0);
        EXPECT_EQ(s.directionalFade, 0.5);
        EXPECT_EQ(s.clipDistance, 12000.0);
        EXPECT_EQ(s.lightFadeBegin, 4000.0);
        EXPECT_EQ(s.lightFadeEnd, 5000.0);
    }

    TEST(SkyrimCellLightView, eachFlagTakesOnlyItsOwnWordsThroughTheView)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(l.settings("RotationInherited", s));
        EXPECT_EQ(s.rotationXY, 45.0);
        EXPECT_EQ(s.rotationZ, 270.0);
        EXPECT_EQ(s.directionalFade, 0.25);
        ASSERT_TRUE(l.settings("FadeInherited", s));
        EXPECT_EQ(s.directionalFade, 0.5);
        EXPECT_EQ(s.rotationXY, 10.0);
        ASSERT_TRUE(l.settings("ClipInherited", s));
        EXPECT_EQ(s.clipDistance, 12000.0);
        EXPECT_EQ(s.lightFadeEnd, 200.0);
        ASSERT_TRUE(l.settings("LightFadeInherited", s));
        EXPECT_EQ(s.lightFadeBegin, 4000.0);
        EXPECT_EQ(s.lightFadeEnd, 5000.0);
        EXPECT_EQ(s.clipDistance, 3000.0);
    }

    TEST(SkyrimCellLightView, aShortXcllHasTheRotationFadeAndClipButNoLightFade)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(l.settings("ShortXcll", s));
        EXPECT_EQ(s.rotationXY, 10.0);
        EXPECT_EQ(s.rotationZ, 20.0);
        EXPECT_EQ(s.directionalFade, 0.25);
        EXPECT_EQ(s.clipDistance, 3000.0);
        EXPECT_EQ(s.lightFadeBegin, 0.0);
        EXPECT_EQ(s.lightFadeEnd, 0.0);
    }

    TEST(SkyrimCellLightView, aMissingTemplateLeavesTheCellsOwnWordsEvenWithEveryFlag)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(l.settings("MissingTemplate", s));
        EXPECT_EQ(s.rotationXY, 10.0);
        EXPECT_EQ(s.clipDistance, 3000.0);
        EXPECT_EQ(s.lightFadeEnd, 200.0);
    }

    TEST(SkyrimCellLightView, aCellWithNoLightingOrAnUnknownCellGivesNothing)
    {
        Loaded l;
        sdkgame::CellLightSettings s;
        s.clipDistance = 7.0;
        EXPECT_FALSE(l.settings("NoXcll", s));
        EXPECT_EQ(s.clipDistance, 0.0); // reset to the defaults
        EXPECT_FALSE(l.view.lightSettingsForCell(0x00000DDD, s));
        EXPECT_FALSE(l.view.lightSettingsForCell(0, s));
    }

    TEST(SkyrimCellLightView, theCellSourceForwardsToTheView)
    {
        Loaded l;
        SkyrimCellSource source(l.view);
        const auto info = l.view.findCellByEditorId("AllInherited");
        ASSERT_TRUE(info.has_value());
        sdkgame::CellLightSettings s;
        ASSERT_TRUE(source.lightSettingsForCell(info->id, s));
        EXPECT_EQ(s.clipDistance, 12000.0);
    }
}
