// Task 0292: the scripts of a world's persistent placements (the Winterhold bridge's trigger box is one) reach the
// square they stand in: scriptsOfCell lists them with the square's own, as loadCell lists the placements. Generated
// plugins; no game data. Written first and run red against a scriptsOfCell that listed only the square's own.
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;
    namespace fs = std::filesystem;

    constexpr std::uint32_t kGrupTop = 0, kGrupWorld = 1, kGrupInteriorBlock = 2, kGrupInteriorSub = 3,
                            kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6, kGrupPersistent = 8,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kDeleted = 0x20;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes refr(
        std::uint32_t id, std::uint32_t base, std::uint32_t flags = 0, float x = 1, float y = 2, float scale = -1.0f)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        if (scale > 0)
            subs.append(sub4("XSCL", Bytes().f32(scale)));
        subs.append(sub4("DATA", Bytes().f32(x).f32(y).f32(3).f32(0.5f).f32(0.25f).f32(0.125f)));
        return record4("REFR", flags, id, subs);
    }

    Bytes modelRecord(const char (&type)[5], std::uint32_t id, const std::string& editorId, const std::string& model)
    {
        Bytes subs;
        subs.append(edid(editorId));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring(model)));
        return record4(type, 0, id, subs);
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes cellRecord(std::uint32_t id, const std::string& editorId, const std::string& name, std::uint16_t flags,
        const Bytes& extra = Bytes(), bool localizedName = false, std::uint32_t nameId = 0)
    {
        Bytes subs;
        subs.append(edid(editorId));
        if (localizedName)
            subs.append(sub4("FULL", Bytes().u32(nameId)));
        else if (!name.empty())
            subs.append(sub4("FULL", Bytes().zstring(name)));
        subs.append(sub4("DATA", Bytes().u16(flags)));
        subs.append(extra);
        return record4("CELL", 0, id, subs);
    }

    Bytes childrenGroup(std::uint32_t cellId, const Bytes& persistent, const Bytes& temporary)
    {
        Bytes children;
        if (persistent.size() > 0)
            children.append(group4(cellId, kGrupPersistent, persistent));
        if (temporary.size() > 0)
            children.append(group4(cellId, kGrupTemporary, temporary));
        return group4(cellId, kGrupCellChildren, children);
    }

    // CELL top group > block > sub-block > cell record + children group.
    Bytes interiorCellGroup(std::uint32_t cellId, const Bytes& cellRec, const Bytes& persistent, const Bytes& temporary)
    {
        Bytes sub = cellRec;
        sub.append(childrenGroup(cellId, persistent, temporary));
        return topGroup(
            "CELL", group4(cellId % 10, kGrupInteriorBlock, group4((cellId / 10) % 10, kGrupInteriorSub, sub)));
    }

    Bytes header(std::uint32_t flags = 0, const std::vector<Master>& masters = {})
    {
        return tes4Header(1.7f, flags, "A", "B", 1, masters, 24, 44);
    }

    Bytes xclc(std::int32_t x, std::int32_t y)
    {
        return sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0));
    }

    struct GridCell
    {
        std::uint32_t id;
        std::int32_t x, y;
        std::string editorId;
        Bytes temporary;
    };

    // WRLD top group with the world's children group: the persistent cell, then one block > sub-block per grid cell.
    Bytes worldGroup(std::uint32_t worldId, const std::string& worldEditorId, std::uint32_t persistentCellId,
        const Bytes& persistentRefs, const std::vector<GridCell>& cells)
    {
        Bytes worldChildren;
        Bytes persistentCell = cellRecord(persistentCellId, "", "", 0);
        persistentCell.append(childrenGroup(persistentCellId, persistentRefs, Bytes()));
        worldChildren.append(persistentCell);
        for (const GridCell& c : cells)
        {
            Bytes sub = cellRecord(c.id, c.editorId, "", 0, xclc(c.x, c.y));
            sub.append(childrenGroup(c.id, Bytes(), c.temporary));
            worldChildren.append(
                group4(static_cast<std::uint32_t>(((c.x >> 5) << 16) | ((c.y >> 5) & 0xFFFF)), kGrupExtBlock,
                    group4(static_cast<std::uint32_t>(((c.x >> 3) << 16) | ((c.y >> 3) & 0xFFFF)), kGrupExtSub, sub)));
        }
        Bytes worldRecord = record4("WRLD", 0, worldId, edid(worldEditorId));
        Bytes both = worldRecord;
        both.append(group4(worldId, kGrupWorld, worldChildren));
        return topGroup("WRLD", both);
    }

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_worldview_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
        fs::path write(const std::string& name, const Bytes& bytes) const
        {
            const fs::path p = mPath / name;
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    struct Loaded
    {
        TempDir dir;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        explicit Loaded(const std::vector<std::pair<std::string, Bytes>>& plugins,
            SkyrimWorldView::Options options = {}, const FileSource* files = nullptr)
            : view(std::move(options), files)
        {
            std::vector<fs::path> paths;
            for (const auto& [name, bytes] : plugins)
                paths.push_back(dir.write(name, bytes));
            view.load(paths, problems);
        }
    };

    // a REFR carrying one script "Scr" (VMAD: version 5, object format 2, one script, no properties)
    Bytes refrWithScript(std::uint32_t id, std::uint32_t base, std::uint32_t flags, float x, float y)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        Bytes vmad;
        vmad.u16(5).u16(2).u16(1);
        vmad.u16(3).text("Scr").u8(0).u16(0);
        subs.append(sub4("VMAD", vmad));
        subs.append(sub4("DATA", Bytes().f32(x).f32(y).f32(3).f32(0).f32(0).f32(0)));
        return record4("REFR", flags, id, subs);
    }

    bool hasScript(const std::vector<SkyrimWorldView::ReferenceScripts>& list, FormId reference)
    {
        for (const SkyrimWorldView::ReferenceScripts& s : list)
            if (s.mReference == reference && !s.mScripts.empty() && s.mScripts[0].mName == "Scr")
                return true;
        return false;
    }

    TEST(SkyrimWorldViewScripts, aPersistentPlacementsScriptsReachTheSquareItStandsIn)
    {
        constexpr std::uint32_t world = 0x00000C00, pcell = 0x00000C01, g00 = 0x00000C10, g10 = 0x00000C11,
                                stat = 0x00000801;
        Bytes persistent;
        persistent.append(refrWithScript(0x00000D01, stat, 0x400, 100, 100)); // grid (0, 0)
        persistent.append(refrWithScript(0x00000D02, stat, 0x400, 5000, 100)); // grid (1, 0)
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("STAT", modelRecord("STAT", stat, "StoneWall", "architecture\wall.nif")));
        bytes.append(worldGroup(world, "TestWorld", pcell, persistent,
            { { g00, 0, 0, "TestWorldX0Y0", refrWithScript(0x00000D10, stat, 0, 10, 10) },
                { g10, 1, 0, "TestWorldX1Y0", refr(0x00000D11, stat, 0, 4100, 10) } }));
        Loaded l({ { "world.esm", bytes } });
        ASSERT_TRUE(l.problems.empty());
        const std::vector<SkyrimWorldView::ReferenceScripts> a = l.view.scriptsOfCell(g00);
        EXPECT_TRUE(hasScript(a, 0x00000D10)) << "the square's own";
        EXPECT_TRUE(hasScript(a, 0x00000D01)) << "the persistent one standing in it";
        EXPECT_FALSE(hasScript(a, 0x00000D02)) << "the persistent one in the next square";
        const std::vector<SkyrimWorldView::ReferenceScripts> b = l.view.scriptsOfCell(g10);
        EXPECT_TRUE(hasScript(b, 0x00000D02));
        EXPECT_FALSE(hasScript(b, 0x00000D01));
        EXPECT_FALSE(hasScript(b, 0x00000D10));
        // each reference once, however often the cell is asked
        EXPECT_EQ(l.view.scriptsOfCell(g00).size(), 2u);
    }
}
