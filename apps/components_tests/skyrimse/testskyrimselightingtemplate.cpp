// Task 0303: a cell's interior lighting after its lighting template (LTMP, a LGTM record). Generated plugins only.
// A 92 byte XCLL ends with inherit flags: the values the cell takes from the template instead of its own, which are
// stale (Dawnguard HQ stores a bright green ambient and inherits it).
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
    constexpr std::uint32_t kTemplate = 0x00000B00, kCellAll = 0x00000B10, kCellAmbient = 0x00000B20,
                            kCellNoFlags = 0x00000B30, kCellShort = 0x00000B40, kCellMissing = 0x00000B50,
                            kCellNoTemplate = 0x00000B60, kCellFog = 0x00000B70;

    using Rgb = std::array<std::uint8_t, 3>;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // The first 20 bytes of XCLL and LGTM DATA: ambient, directional, fog colour (R G B A each), fog near and far.
    void prefix(Bytes& d, Rgb amb, Rgb dir, Rgb fog, float nearDistance, float farDistance)
    {
        d.u8(amb[0]).u8(amb[1]).u8(amb[2]).u8(0).u8(dir[0]).u8(dir[1]).u8(dir[2]).u8(0);
        d.u8(fog[0]).u8(fog[1]).u8(fog[2]).u8(0).f32(nearDistance).f32(farDistance);
    }

    // The cell's own XCLL: stale values; size 92 with the inherit word at offset 88, or 64 bytes (no flags).
    Bytes cellXcll(std::uint32_t inherit, std::size_t size = 92)
    {
        Bytes d;
        prefix(d, { 98, 238, 143 }, { 58, 68, 47 }, { 155, 162, 140 }, 1400.0f, 9000.0f);
        while (d.size() < std::min<std::size_t>(size, 88))
            d.u8(0);
        if (size == 92)
            d.u32(inherit);
        while (d.size() < size)
            d.u8(0);
        return d;
    }

    Bytes templateRecord()
    {
        Bytes data;
        prefix(data, { 41, 41, 33 }, { 78, 88, 67 }, { 121, 129, 105 }, 550.0f, 6000.0f);
        while (data.size() < 92)
            data.u8(0);
        Bytes subs;
        subs.append(edid("TestTemplate"));
        subs.append(sub4("DATA", data));
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

    Bytes ltmp(std::uint32_t id)
    {
        return sub4("LTMP", Bytes().u32(id));
    }

    Bytes esm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(group4(Content::fourCC("LGTM"), kGrupTop, templateRecord()));
        Bytes cells;
        Bytes all = sub4("XCLL", cellXcll(0x1f));
        all.append(ltmp(kTemplate));
        cells.append(interiorCell(kCellAll, "AllInherited", all));
        Bytes amb = sub4("XCLL", cellXcll(LightingInherit::Ambient));
        amb.append(ltmp(kTemplate));
        cells.append(interiorCell(kCellAmbient, "AmbientInherited", amb));
        Bytes none = sub4("XCLL", cellXcll(0));
        none.append(ltmp(kTemplate));
        cells.append(interiorCell(kCellNoFlags, "NothingInherited", none));
        Bytes shortXcll = sub4("XCLL", cellXcll(0, 64));
        shortXcll.append(ltmp(kTemplate));
        cells.append(interiorCell(kCellShort, "ShortXcll", shortXcll));
        Bytes missing = sub4("XCLL", cellXcll(0x1f));
        missing.append(ltmp(0x00000BEE));
        cells.append(interiorCell(kCellMissing, "MissingTemplate", missing));
        cells.append(interiorCell(kCellNoTemplate, "NoTemplate", sub4("XCLL", cellXcll(0x1f))));
        Bytes fog
            = sub4("XCLL", cellXcll(LightingInherit::FogColor | LightingInherit::FogNear | LightingInherit::FogFar));
        fog.append(ltmp(kTemplate));
        cells.append(interiorCell(kCellFog, "FogInherited", fog));
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop, cells));
        return bytes;
    }

    struct Plugin
    {
        fs::path mDir;
        fs::path mFile;
        Plugin()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_ltmp_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "ltmp.esm";
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
        bool lighting(const char* editorId, sdkgame::CellLighting& out)
        {
            const auto info = view.findCellByEditorId(editorId);
            sdkgame::WorldCell cell;
            std::string error;
            if (!info || !loadNeutralCell(view, info->id, cell, error) || !cell.lighting)
                return false;
            out = *cell.lighting;
            return true;
        }
    };

    CellLightingData own(std::uint32_t inherit)
    {
        CellLightingData d;
        d.mAmbient = { 1, 2, 3 };
        d.mDirectional = { 4, 5, 6 };
        d.mFogColor = { 7, 8, 9 };
        d.mFogNear = 100.0f;
        d.mFogFar = 4000.0f;
        d.mInherit = inherit;
        return d;
    }

    CellLightingData tmpl()
    {
        CellLightingData d;
        d.mAmbient = { 11, 12, 13 };
        d.mDirectional = { 14, 15, 16 };
        d.mFogColor = { 17, 18, 19 };
        d.mFogNear = 500.0f;
        d.mFogFar = 6000.0f;
        return d;
    }

    // ---- the pure rule ---------------------------------------------------------------------------------------

    TEST(SkyrimLightingTemplateRule, noTemplateLeavesTheCellsOwnValues)
    {
        const CellLightingData out = applyLightingTemplate(own(0x1f), nullptr);
        EXPECT_EQ(out.mAmbient, (Rgb{ 1, 2, 3 }));
        EXPECT_EQ(out.mFogFar, 4000.0f);
    }

    TEST(SkyrimLightingTemplateRule, noFlagsLeavesTheCellsOwnValues)
    {
        const CellLightingData t = tmpl();
        const CellLightingData out = applyLightingTemplate(own(0), &t);
        EXPECT_EQ(out.mAmbient, (Rgb{ 1, 2, 3 }));
        EXPECT_EQ(out.mDirectional, (Rgb{ 4, 5, 6 }));
        EXPECT_EQ(out.mFogColor, (Rgb{ 7, 8, 9 }));
        EXPECT_EQ(out.mFogNear, 100.0f);
        EXPECT_EQ(out.mFogFar, 4000.0f);
    }

    TEST(SkyrimLightingTemplateRule, eachFlagTakesItsOwnValueAndNothingElse)
    {
        const CellLightingData t = tmpl();
        {
            const CellLightingData out = applyLightingTemplate(own(LightingInherit::Ambient), &t);
            EXPECT_EQ(out.mAmbient, (Rgb{ 11, 12, 13 }));
            EXPECT_EQ(out.mDirectional, (Rgb{ 4, 5, 6 }));
            EXPECT_EQ(out.mFogColor, (Rgb{ 7, 8, 9 }));
            EXPECT_EQ(out.mFogNear, 100.0f);
            EXPECT_EQ(out.mFogFar, 4000.0f);
        }
        {
            const CellLightingData out = applyLightingTemplate(own(LightingInherit::Directional), &t);
            EXPECT_EQ(out.mAmbient, (Rgb{ 1, 2, 3 }));
            EXPECT_EQ(out.mDirectional, (Rgb{ 14, 15, 16 }));
        }
        {
            const CellLightingData out = applyLightingTemplate(own(LightingInherit::FogColor), &t);
            EXPECT_EQ(out.mFogColor, (Rgb{ 17, 18, 19 }));
            EXPECT_EQ(out.mFogNear, 100.0f);
        }
        {
            const CellLightingData out = applyLightingTemplate(own(LightingInherit::FogNear), &t);
            EXPECT_EQ(out.mFogNear, 500.0f);
            EXPECT_EQ(out.mFogFar, 4000.0f);
            EXPECT_EQ(out.mFogColor, (Rgb{ 7, 8, 9 }));
        }
        {
            const CellLightingData out = applyLightingTemplate(own(LightingInherit::FogFar), &t);
            EXPECT_EQ(out.mFogFar, 6000.0f);
            EXPECT_EQ(out.mFogNear, 100.0f);
        }
    }

    TEST(SkyrimLightingTemplateRule, flagsWeDoNotUseAreIgnored)
    {
        const CellLightingData t = tmpl();
        const CellLightingData out = applyLightingTemplate(own(0x7e0), &t); // rotation, fade, clip, power, max, fade
        EXPECT_EQ(out.mAmbient, (Rgb{ 1, 2, 3 }));
        EXPECT_EQ(out.mFogFar, 4000.0f);
    }

    // ---- through the records and the view -----------------------------------------------------------------------

    TEST(SkyrimLightingTemplateView, aCellThatInheritsEverythingGetsTheTemplatesValues)
    {
        Loaded l;
        sdkgame::CellLighting c;
        ASSERT_TRUE(l.lighting("AllInherited", c));
        EXPECT_EQ(c.ambient, (Rgb{ 41, 41, 33 }));
        EXPECT_EQ(c.directional, (Rgb{ 78, 88, 67 }));
        EXPECT_EQ(c.fogColor, (Rgb{ 121, 129, 105 }));
        EXPECT_EQ(c.fogNear, 550.0);
        EXPECT_EQ(c.fogFar, 6000.0);
    }

    TEST(SkyrimLightingTemplateView, onlyTheFlaggedValuesComeFromTheTemplate)
    {
        Loaded l;
        sdkgame::CellLighting c;
        ASSERT_TRUE(l.lighting("AmbientInherited", c));
        EXPECT_EQ(c.ambient, (Rgb{ 41, 41, 33 }));
        EXPECT_EQ(c.directional, (Rgb{ 58, 68, 47 }));
        EXPECT_EQ(c.fogColor, (Rgb{ 155, 162, 140 }));
        EXPECT_EQ(c.fogNear, 1400.0);
        ASSERT_TRUE(l.lighting("FogInherited", c));
        EXPECT_EQ(c.ambient, (Rgb{ 98, 238, 143 }));
        EXPECT_EQ(c.fogColor, (Rgb{ 121, 129, 105 }));
        EXPECT_EQ(c.fogNear, 550.0);
        EXPECT_EQ(c.fogFar, 6000.0);
    }

    TEST(SkyrimLightingTemplateView, noFlagsNoTemplateAShortRecordAndAMissingTemplateKeepTheOwnValues)
    {
        Loaded l;
        for (const char* name : { "NothingInherited", "ShortXcll", "MissingTemplate", "NoTemplate" })
        {
            sdkgame::CellLighting c;
            ASSERT_TRUE(l.lighting(name, c)) << name;
            EXPECT_EQ(c.ambient, (Rgb{ 98, 238, 143 })) << name;
            EXPECT_EQ(c.fogFar, 9000.0) << name;
        }
    }
}
