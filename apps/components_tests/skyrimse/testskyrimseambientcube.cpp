// Task 0355 stage e: the interior ambient cube. A Skyrim interior's XCLL carries six ambient colours (bytes 40 to 63:
// +X, -X, +Y, -Y, +Z, -Z) and its lighting template (LTMP -> LGTM) a DALC with six more; a cell that takes its ambient
// from the template (inherit flag 0) takes the template's cube. Generated plugins only; the pack hands the cube out
// through the optional AmbientCubeSource interface.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/ambient_cube_source.hpp>
#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

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
    constexpr std::uint32_t kTemplate = 0x00000C00, kTemplateNoDalc = 0x00000C01, kOwn = 0x00000C10,
                            kInherits = 0x00000C20, kInheritsNoDalc = 0x00000C30, kShort = 0x00000C40,
                            kZero = 0x00000C50, kNoXcll = 0x00000C60;

    using Rgb = std::array<std::uint8_t, 3>;
    using Cube = std::array<Rgb, 6>;

    const Cube kOwnCube
        = { { { 10, 11, 12 }, { 20, 21, 22 }, { 30, 31, 32 }, { 40, 41, 42 }, { 50, 51, 52 }, { 60, 61, 62 } } };
    const Cube kTemplateCube = { { { 110, 111, 112 }, { 120, 121, 122 }, { 130, 131, 132 }, { 140, 141, 142 },
        { 150, 151, 152 }, { 160, 161, 162 } } };
    const Cube kNoCube = {};

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    void colour(Bytes& d, const Rgb& c)
    {
        d.u8(c[0]).u8(c[1]).u8(c[2]).u8(0);
    }

    // An XCLL of `size` bytes (64 or 92): three colours and the fog distances at the front, the cube at 40, the inherit
    // word at 88.
    Bytes xcll(const Cube& cube, std::uint32_t inherit, std::size_t size = 92)
    {
        Bytes d;
        colour(d, { 1, 2, 3 });
        colour(d, { 4, 5, 6 });
        colour(d, { 7, 8, 9 });
        d.f32(0.0f).f32(1000.0f);
        while (d.size() < 40)
            d.u8(0);
        for (const Rgb& c : cube)
            colour(d, c);
        while (d.size() < size)
            d.u8(0);
        if (size == 92)
            for (int i = 0; i < 4; ++i)
                d.str()[88 + i] = static_cast<char>((inherit >> (8 * i)) & 0xff);
        return d;
    }

    Bytes dalc(const Cube& cube)
    {
        Bytes d;
        for (const Rgb& c : cube)
            colour(d, c);
        d.u8(0).u8(0).u8(0).u8(0).f32(1.0f); // the specular colour and the fresnel power: 32 bytes in all
        return d;
    }

    Bytes lgtm(std::uint32_t id, const std::string& name, bool withDalc)
    {
        Bytes subs;
        subs.append(edid(name));
        subs.append(sub4("DATA", xcll(kNoCube, 0)));
        if (withDalc)
            subs.append(sub4("DALC", dalc(kTemplateCube)));
        return record4("LGTM", 0, id, subs);
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

    Bytes withTemplate(const Bytes& xcllSub, std::uint32_t tmpl)
    {
        Bytes b = xcllSub;
        b.append(sub4("LTMP", Bytes().u32(tmpl)));
        return b;
    }

    Bytes esm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        Bytes templates = lgtm(kTemplate, "TplWithDalc", true);
        templates.append(lgtm(kTemplateNoDalc, "TplNoDalc", false));
        bytes.append(group4(Content::fourCC("LGTM"), kGrupTop, templates));
        Bytes cells;
        cells.append(interiorCell(kOwn, "OwnCube", sub4("XCLL", xcll(kOwnCube, 0))));
        cells.append(
            interiorCell(kInherits, "InheritsCube", withTemplate(sub4("XCLL", xcll(kOwnCube, 0x1)), kTemplate)));
        cells.append(interiorCell(
            kInheritsNoDalc, "InheritsNoDalc", withTemplate(sub4("XCLL", xcll(kOwnCube, 0x1)), kTemplateNoDalc)));
        cells.append(interiorCell(kShort, "ShortXcll", sub4("XCLL", xcll(kOwnCube, 0, 64))));
        cells.append(interiorCell(kZero, "ZeroCube", sub4("XCLL", xcll(kNoCube, 0))));
        cells.append(interiorCell(kNoXcll, "NoXcll", Bytes()));
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
                / ("oe_skyrim_ambcube_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "ambcube.esm";
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
        // Loads the cell the way the engine does, then asks the pack for its cube.
        bool cube(const char* editorId, sdkgame::AmbientCube& out)
        {
            const auto info = view.findCellByEditorId(editorId);
            sdkgame::WorldCell cell;
            std::string error;
            if (!info || !loadNeutralCell(view, info->id, cell, error))
                return false;
            auto* source = dynamic_cast<sdkgame::AmbientCubeSource*>(&view);
            return source != nullptr && source->ambientCubeForCell(info->id, out);
        }
    };

    // ---- the rule on the records ----------------------------------------------------------------------------

    TEST(SkyrimAmbientCubeRule, theCellsOwnCubeIsKeptWithoutTheInheritFlag)
    {
        CellLightingData own;
        own.mCube = kOwnCube;
        own.mHasCube = true;
        CellLightingData tpl;
        tpl.mCube = kTemplateCube;
        tpl.mHasCube = true;
        EXPECT_EQ(applyLightingTemplate(own, &tpl).mCube, kOwnCube);
        EXPECT_EQ(applyLightingTemplate(own, nullptr).mCube, kOwnCube);
    }

    TEST(SkyrimAmbientCubeRule, theAmbientInheritFlagTakesTheTemplatesCube)
    {
        CellLightingData own;
        own.mCube = kOwnCube;
        own.mHasCube = true;
        own.mInherit = LightingInherit::Ambient;
        CellLightingData tpl;
        tpl.mCube = kTemplateCube;
        tpl.mHasCube = true;
        const CellLightingData out = applyLightingTemplate(own, &tpl);
        EXPECT_EQ(out.mCube, kTemplateCube);
        EXPECT_TRUE(out.mHasCube);
    }

    TEST(SkyrimAmbientCubeRule, aTemplateWithNoCubeLeavesTheCellsOwn)
    {
        CellLightingData own;
        own.mCube = kOwnCube;
        own.mHasCube = true;
        own.mInherit = LightingInherit::Ambient;
        CellLightingData tpl; // no DALC
        EXPECT_EQ(applyLightingTemplate(own, &tpl).mCube, kOwnCube);
    }

    // ---- through the records and the view ---------------------------------------------------------------------

    TEST(SkyrimAmbientCubeView, aCellsXcllGivesItsSixColoursInTheOrderPlusXMinusXPlusYMinusYPlusZMinusZ)
    {
        Loaded l;
        sdkgame::AmbientCube c;
        ASSERT_TRUE(l.cube("OwnCube", c));
        EXPECT_EQ(c.colour, kOwnCube);
    }

    TEST(SkyrimAmbientCubeView, theShortXcllCarriesTheCubeToo)
    {
        Loaded l;
        sdkgame::AmbientCube c;
        ASSERT_TRUE(l.cube("ShortXcll", c));
        EXPECT_EQ(c.colour, kOwnCube);
    }

    TEST(SkyrimAmbientCubeView, aCellThatInheritsItsAmbientGetsTheTemplatesDalc)
    {
        Loaded l;
        sdkgame::AmbientCube c;
        ASSERT_TRUE(l.cube("InheritsCube", c));
        EXPECT_EQ(c.colour, kTemplateCube);
    }

    TEST(SkyrimAmbientCubeView, aTemplateWithoutADalcLeavesTheCellsOwnCube)
    {
        Loaded l;
        sdkgame::AmbientCube c;
        ASSERT_TRUE(l.cube("InheritsNoDalc", c));
        EXPECT_EQ(c.colour, kOwnCube);
    }

    TEST(SkyrimAmbientCubeView, anAllZeroCubeOrNoXcllIsNoCube)
    {
        Loaded l;
        sdkgame::AmbientCube c;
        EXPECT_FALSE(l.cube("ZeroCube", c));
        EXPECT_FALSE(l.cube("NoXcll", c));
    }

    TEST(SkyrimAmbientCubeView, aCellThatWasNeverLoadedHasNoCube)
    {
        Loaded l;
        auto* source = dynamic_cast<sdkgame::AmbientCubeSource*>(&l.view);
        ASSERT_NE(source, nullptr);
        sdkgame::AmbientCube c;
        EXPECT_FALSE(source->ambientCubeForCell(0x00ABCDEF, c));
    }

    TEST(SkyrimAmbientCubeView, theCellSourceForwardsTheQuestion)
    {
        Loaded l;
        SkyrimCellSource source(l.view);
        auto* asCube = dynamic_cast<sdkgame::AmbientCubeSource*>(&source);
        ASSERT_NE(asCube, nullptr);
        const auto info = l.view.findCellByEditorId("OwnCube");
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(info && loadNeutralCell(l.view, info->id, cell, error));
        sdkgame::AmbientCube c;
        ASSERT_TRUE(asCube->ambientCubeForCell(info->id, c));
        EXPECT_EQ(c.colour, kOwnCube);
    }

    TEST(SkyrimAmbientCubeView, theSdkVersionIsADraftZeroOne)
    {
        EXPECT_EQ(sdkgame::kAmbientCubeSourceVersion.major, 0);
        EXPECT_EQ(sdkgame::kAmbientCubeSourceVersion.minor, 1);
    }
}
