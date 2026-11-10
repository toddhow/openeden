// Task 0086, world model option 1: the Skyrim SE world store behind the pack (SkyrimWorldView) and the SDK's WorldView
// contract. Generated plugins only, written to a scratch folder; no game data. The store must keep no reference in its
// index, read a cell on demand from every plugin that touches it, and follow the load order for overrides.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#ifndef OPENEDEN_WORLDVIEW_RIG
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#endif
#include <openeden/extension.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/host.hpp>
#include <openeden/pack_kind.hpp>

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

    class MapFiles final : public FileSource
    {
    public:
        std::map<std::string, std::string> mFiles;
        mutable int mAsked = 0;
        bool read(std::string_view path, std::string& bytes) const override
        {
            ++mAsked;
            const auto it = mFiles.find(std::string(path));
            if (it == mFiles.end())
                return false;
            bytes = it->second;
            return true;
        }
    };

    // A .STRINGS table: count, data size, (id, offset) directory, zero-terminated strings.
    std::string stringsTable(const std::vector<std::pair<std::uint32_t, std::string>>& entries)
    {
        Bytes data;
        Bytes directory;
        for (const auto& [id, text] : entries)
        {
            directory.u32(id).u32(static_cast<std::uint32_t>(data.size()));
            data.zstring(text);
        }
        Bytes all;
        all.u32(static_cast<std::uint32_t>(entries.size())).u32(static_cast<std::uint32_t>(data.size()));
        all.append(directory).append(data);
        return all.str();
    }

    // base.esm: a static and a furniture, an interior cell with two references (one temporary, one persistent).
    constexpr std::uint32_t kStatA = 0x00000801, kFurn = 0x00000802, kCellInterior = 0x00000900, kRefA = 0x00000A01,
                            kRefB = 0x00000A02;

    Bytes baseEsm()
    {
        Bytes bytes = header(Content::sRecMaster);
        Bytes bases = modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif");
        bytes.append(topGroup("STAT", bases));
        bytes.append(topGroup("FURN", modelRecord("FURN", kFurn, "WoodChair", "furniture\\chair.nif")));
        bytes.append(
            interiorCellGroup(kCellInterior, cellRecord(kCellInterior, "TestHall", "The Hall", CellFlags::Interior),
                refr(kRefA, kStatA, 0x400, 100, 200), refr(kRefB, kFurn, 0, 5, 6, 2.0f)));
        return bytes;
    }

    // Finds a placed object by its id.
    const sdkgame::PlacedObject* byId(const sdkgame::WorldCellContent& c, std::uint32_t id)
    {
        for (const sdkgame::PlacedObject& o : c.objects)
            if (o.id == id)
                return &o;
        return nullptr;
    }

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

    // ---- the index and the lazy read -------------------------------------------------------------------------------

    TEST(SkyrimWorldView, theIndexHoldsNoReferencesAndALoadReadsThemOnDemand)
    {
        Loaded l({ { "base.esm", baseEsm() } });
        ASSERT_TRUE(l.problems.empty());
        EXPECT_EQ(l.view.cellCount(), 1u);
        EXPECT_EQ(l.view.stats().mReferencesInIndex, 0u);
        EXPECT_EQ(l.view.stats().mCellReads, 0u);
        EXPECT_EQ(l.view.stats().mReferencesDecoded, 0u);
        EXPECT_EQ(l.view.stats().mBaseForms, 2u);

        const auto cell = l.view.findCellByEditorId("TestHall");
        ASSERT_TRUE(cell.has_value());
        sdkgame::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, content, error)) << error;
        EXPECT_EQ(l.view.stats().mCellReads, 1u);
        EXPECT_EQ(l.view.stats().mReferencesDecoded, 2u);
        ASSERT_EQ(content.objects.size(), 2u);
        EXPECT_EQ(content.cell.objectCount, 2u);
        EXPECT_EQ(content.unresolvedBases, 0u);
        EXPECT_EQ(content.cell.name, "The Hall");
        EXPECT_TRUE(content.cell.interior);

        const sdkgame::PlacedObject* a = byId(content, kRefA);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->base, kStatA);
        EXPECT_EQ(a->baseKind, sdkgame::BaseKind::Static);
        EXPECT_EQ(a->baseEditorId, "StoneWall");
        EXPECT_EQ(a->modelPath, "architecture\\wall.nif");
        EXPECT_TRUE(a->hasBounds);
        EXPECT_EQ(a->boundsMin, (sdkgame::WorldVec3{ -10, -10, 0 }));
        EXPECT_EQ(a->boundsMax, (sdkgame::WorldVec3{ 10, 10, 20 }));
        EXPECT_TRUE(a->persistent);
        EXPECT_EQ(a->position, (sdkgame::WorldVec3{ 100, 200, 3 }));
        EXPECT_FLOAT_EQ(a->rotation.x, 0.5f);
        EXPECT_FLOAT_EQ(a->scale, 1.0f);

        const sdkgame::PlacedObject* b = byId(content, kRefB);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->baseKind, sdkgame::BaseKind::Furniture);
        EXPECT_FLOAT_EQ(b->scale, 2.0f);
        EXPECT_FALSE(b->persistent);
        // Objects come in placement id order.
        EXPECT_LT(content.objects[0].id, content.objects[1].id);
    }

    TEST(SkyrimWorldView, twoMaterialisationsOfACellGiveTheSameObjects)
    {
        SkyrimWorldView::Options options;
        options.mMaxCachedCells = 1;
        Loaded l({ { "base.esm", baseEsm() } }, options);
        const auto cell = l.view.findCellByEditorId("testhall");
        ASSERT_TRUE(cell.has_value());
        sdkgame::WorldCellContent first, second;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, first, error)) << error;
        ASSERT_TRUE(l.view.loadCell(cell->id, second, error)) << error;
        EXPECT_EQ(l.view.stats().mCacheHits, 1u); // the second ask came from the cache
        EXPECT_EQ(first.objects, second.objects);

        // A fresh view over the same files reads it again and gets the same thing.
        SkyrimWorldView again(options, nullptr);
        std::vector<std::string> problems;
        again.load({ l.dir.mPath / "base.esm" }, problems);
        sdkgame::WorldCellContent third;
        ASSERT_TRUE(again.loadCell(cell->id, third, error)) << error;
        EXPECT_EQ(first.objects, third.objects);
        EXPECT_EQ(again.stats().mCacheHits, 0u);
    }

    // ---- the load order ------------------------------------------------------------------------------------------

    Bytes overrideEsp()
    {
        // over.esp has base.esm as its master: ids with the top byte 0 are base.esm's, top byte 1 are its own.
        Bytes bytes = header(0, { { "base.esm", 0 } });
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\newwall.nif")));
        // Overrides the cell with its own children: moves A, deletes B and adds C (an id of over.esp itself).
        bytes.append(
            interiorCellGroup(kCellInterior, cellRecord(kCellInterior, "TestHall", "The Hall", CellFlags::Interior),
                refr(kRefA, kStatA, 0x400, 777, 888),
                Bytes().append(refr(kRefB, kFurn, kDeleted)).append(refr(0x01000B01, kFurn, 0, 9, 9))));
        return bytes;
    }

    TEST(SkyrimWorldView, aLaterPluginWinsPerPlacementAndADeletedPlacementGoes)
    {
        Loaded l({ { "base.esm", baseEsm() }, { "over.esp", overrideEsp() } });
        ASSERT_TRUE(l.problems.empty()) << l.problems.front();
        EXPECT_EQ(l.view.cellCount(), 1u);
        const auto cell = l.view.findCellByEditorId("TestHall");
        ASSERT_TRUE(cell.has_value());
        sdkgame::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, content, error)) << error;
        ASSERT_EQ(content.objects.size(), 2u); // A (moved) and C; B deleted
        const sdkgame::PlacedObject* a = byId(content, kRefA);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->position.x, 777.0f);
        EXPECT_EQ(byId(content, kRefB), nullptr);
        const sdkgame::PlacedObject* c = byId(content, 0x01000B01);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(c->baseKind, sdkgame::BaseKind::Furniture);
        EXPECT_EQ(c->position.x, 9.0f);
        // The base form is the later plugin's.
        EXPECT_EQ(a->modelPath, "architecture\\newwall.nif");
        EXPECT_EQ(l.view.stats().mCellReads, 2u); // one read per plugin that touches the cell
    }

    TEST(SkyrimWorldView, theLoadOrderDecidesTheWinnerNotTheFileName)
    {
        // The same two plugins in the other order: base.esm last, so its version of A and B wins.
        Loaded l({ { "over.esp", overrideEsp() }, { "base.esm", baseEsm() } });
        // over.esp names base.esm as a master that comes AFTER it: its ids into that master cannot resolve.
        const auto cell = l.view.findCellByEditorId("TestHall");
        ASSERT_TRUE(cell.has_value());
        sdkgame::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, content, error)) << error;
        EXPECT_GT(l.view.stats().mUnresolvedFormIds, 0u);
    }

    TEST(SkyrimWorldView, aDeletedWinningCellLeavesTheList)
    {
        Bytes del = header(0, { { "base.esm", 0 } });
        Bytes cellRec = record4("CELL", kDeleted, kCellInterior, edid("TestHall"));
        Bytes sub = cellRec;
        del.append(topGroup("CELL",
            group4(kCellInterior % 10, kGrupInteriorBlock, group4((kCellInterior / 10) % 10, kGrupInteriorSub, sub))));
        Loaded l({ { "base.esm", baseEsm() }, { "del.esp", del } });
        EXPECT_EQ(l.view.cellCount(), 0u);
        EXPECT_FALSE(l.view.findCellByEditorId("TestHall").has_value());
    }

    // ---- exteriors -------------------------------------------------------------------------------------------------

    TEST(SkyrimWorldView, aPersistentExteriorPlacementShowsInTheSquareItStandsIn)
    {
        constexpr std::uint32_t world = 0x00000C00, pcell = 0x00000C01, g00 = 0x00000C10, g10 = 0x00000C11;
        Bytes persistent;
        persistent.append(refr(0x00000D01, kStatA, 0x400, 100, 100)); // grid (0, 0)
        persistent.append(refr(0x00000D02, kStatA, 0x400, 5000, 100)); // grid (1, 0)
        persistent.append(refr(0x00000D03, kStatA, 0x400, -100, 100)); // grid (-1, 0): no such cell here
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif")));
        bytes.append(worldGroup(world, "TestWorld", pcell, persistent,
            { { g00, 0, 0, "TestWorldX0Y0", refr(0x00000D10, kStatA, 0, 10, 10) },
                { g10, 1, 0, "TestWorldX1Y0",
                    refr(0x00000D11, kStatA, 0, 4100, 10).append(refr(0x00000D12, kStatA, 0, 4200, 20)) } }));
        Loaded l({ { "world.esm", bytes } });
        ASSERT_TRUE(l.problems.empty());
        EXPECT_EQ(l.view.worldCount(), 1u);
        EXPECT_EQ(l.view.cellCount(), 3u); // the persistent cell and two squares
        const auto w = l.view.findWorldByEditorId("testworld");
        ASSERT_TRUE(w.has_value());
        EXPECT_EQ(w->id, world);

        const auto c00 = l.view.findCellByGrid(world, 0, 0);
        const auto c10 = l.view.findCellByGrid(world, 1, 0);
        ASSERT_TRUE(c00.has_value());
        ASSERT_TRUE(c10.has_value());
        EXPECT_FALSE(c00->interior);
        EXPECT_EQ(c00->world, world);
        EXPECT_FALSE(l.view.findCellByGrid(world, 5, 5).has_value());

        sdkgame::WorldCellContent a, b;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(c00->id, a, error)) << error;
        ASSERT_TRUE(l.view.loadCell(c10->id, b, error)) << error;
        ASSERT_EQ(a.objects.size(), 2u); // its own temporary + the persistent one at x = 100
        EXPECT_NE(byId(a, 0x00000D10), nullptr);
        EXPECT_NE(byId(a, 0x00000D01), nullptr);
        ASSERT_EQ(b.objects.size(), 3u);
        EXPECT_NE(byId(b, 0x00000D02), nullptr);
        EXPECT_EQ(byId(b, 0x00000D03), nullptr);
        EXPECT_EQ(a.cell.objectCount, 2u);

        // The persistent cell itself lists everything it holds.
        const auto all = l.view.findCellByGrid(world, 0, 0);
        std::vector<sdkgame::WorldCellInfo> list;
        l.view.listCells(0, 10, list);
        ASSERT_EQ(list.size(), 3u);
        EXPECT_FALSE(list[0].hasGrid); // the world's persistent cell first
        EXPECT_EQ(list[1].gridX, 0);
        EXPECT_EQ(list[2].gridX, 1);
        sdkgame::WorldCellContent p;
        ASSERT_TRUE(l.view.loadCell(list[0].id, p, error)) << error;
        EXPECT_EQ(p.objects.size(), 3u);
    }

    // ---- owner, lock and teleport through the store (task 0099 stage N4)
    // -----------------------------------------------

    Bytes refrWithComponents(
        std::uint32_t id, std::uint32_t base, std::uint32_t flags, float x, float y, const Bytes& extra)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        subs.append(sub4("DATA", Bytes().f32(x).f32(y).f32(3).f32(0).f32(0).f32(0)));
        subs.append(extra);
        return record4("REFR", flags, id, subs);
    }

    Bytes xtelBytes(std::uint32_t door)
    {
        return sub4("XTEL", Bytes().u32(door).f32(1).f32(2).f32(3).f32(0).f32(0).f32(0).u32(0));
    }

    TEST(SkyrimWorldViewComponents, aPersistentExteriorDoorCarriesItsComponentsIntoTheSquareItStandsIn)
    {
        constexpr std::uint32_t world = 0x00000C00, pcell = 0x00000C01, g00 = 0x00000C10, g10 = 0x00000C11;
        const Bytes extra = Bytes().append(sub4("XOWN", Bytes().u32(0x00013BB8))).append(xtelBytes(0x00000B99));
        Bytes persistent;
        persistent.append(refrWithComponents(0x00000D02, kStatA, 0x400, 5000, 100, extra)); // grid (1, 0)
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif")));
        bytes.append(worldGroup(world, "TestWorld", pcell, persistent,
            { { g00, 0, 0, "TestWorldX0Y0", refr(0x00000D10, kStatA, 0, 10, 10) },
                { g10, 1, 0, "TestWorldX1Y0", refr(0x00000D11, kStatA, 0, 4100, 10) } }));
        Loaded l({ { "world.esm", bytes } });
        ASSERT_TRUE(l.problems.empty());
        const auto c10 = l.view.findCellByGrid(world, 1, 0);
        const auto c00 = l.view.findCellByGrid(world, 0, 0);
        ASSERT_TRUE(c10 && c00);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(l.view, c10->id, cell, error)) << error;
        const sdkgame::WorldObject* door = nullptr;
        for (const sdkgame::WorldObject& o : cell.objects)
            if (o.id == 0x00000D02)
                door = &o;
        ASSERT_NE(door, nullptr); // borrowed from the world's persistent cell
        ASSERT_TRUE(door->owner.has_value());
        EXPECT_EQ(door->owner->owner, 0x00013BB8u);
        ASSERT_TRUE(door->teleport.has_value());
        EXPECT_EQ(door->teleport->destinationDoor, 0x00000B99u);
        EXPECT_EQ(door->teleport->destinationCell, sdkgame::kNoWorldId); // the destination door is in no loaded cell
        EXPECT_EQ(l.view.stats().mComponents.mTeleportCellsUnknown, 1u);
        // The square next door does not see it.
        ASSERT_TRUE(loadNeutralCell(l.view, c00->id, cell, error)) << error;
        for (const sdkgame::WorldObject& o : cell.objects)
            EXPECT_NE(o.id, 0x00000D02u);
    }

    TEST(SkyrimWorldViewComponents, theIndexFindsTeleportDoorsWithoutReadingAnyCellAndALaterPluginWins)
    {
        constexpr std::uint32_t kHall = 0x00000900, kYard = 0x00000910, kDoorHall = 0x00000B01, kDoorYard = 0x00000B99;
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif")));
        Bytes both = bytes;
        both.append(interiorCellGroup(kHall, cellRecord(kHall, "TestHall", "", CellFlags::Interior), Bytes(),
            refrWithComponents(kDoorHall, kStatA, 0, 1, 1, xtelBytes(kDoorYard))));
        both.append(interiorCellGroup(kYard, cellRecord(kYard, "TestYard", "", CellFlags::Interior), Bytes(),
            refrWithComponents(kDoorYard, kStatA, 0, 1, 1, xtelBytes(kDoorHall))));
        // over.esp moves the yard door into a third cell of its own.
        Bytes over = header(0, { { "base.esm", 0 } });
        constexpr std::uint32_t kAnnex = 0x01000900;
        over.append(interiorCellGroup(kAnnex, cellRecord(kAnnex, "TestAnnex", "", CellFlags::Interior), Bytes(),
            refrWithComponents(kDoorYard, kStatA, 0, 1, 1, xtelBytes(kDoorHall))));
        Loaded one({ { "base.esm", both } });
        ASSERT_TRUE(one.problems.empty());
        EXPECT_EQ(one.view.stats().mCellReads, 0u); // the index pass read no cell
        EXPECT_EQ(one.view.stats().mTeleportDoorsIndexed, 2u);
        EXPECT_EQ(one.view.teleportDoorCells().at(kDoorHall), kHall);
        EXPECT_EQ(one.view.teleportDoorCells().at(kDoorYard), kYard);

        Loaded two({ { "base.esm", both }, { "over.esp", over } });
        ASSERT_TRUE(two.problems.empty());
        EXPECT_EQ(two.view.stats().mTeleportDoorsIndexed, 3u);
        EXPECT_EQ(two.view.teleportDoorCells().at(kDoorYard), kAnnex); // the later plugin wins
        EXPECT_EQ(two.view.teleportDoorCells().at(kDoorHall), kHall);
    }

    // ---- lists, names and lookups ----------------------------------------------------------------------------------

    TEST(SkyrimWorldView, interiorCellsListByEditorIdThenExteriorsByWorldAndGrid)
    {
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "w.nif")));
        bytes.append(interiorCellGroup(0x900, cellRecord(0x900, "Zeta", "", CellFlags::Interior), Bytes(), Bytes()));
        bytes.append(interiorCellGroup(0x901, cellRecord(0x901, "alpha", "", CellFlags::Interior), Bytes(), Bytes()));
        bytes.append(interiorCellGroup(0x902, cellRecord(0x902, "Beta", "", CellFlags::Interior), Bytes(), Bytes()));
        bytes.append(worldGroup(0xC00, "W", 0xC01, Bytes(),
            { { 0xC10, 2, 1, "W21", Bytes() }, { 0xC11, 1, 1, "W11", Bytes() }, { 0xC12, 3, 0, "W30", Bytes() } }));
        Loaded l({ { "a.esm", bytes } });
        std::vector<sdkgame::WorldCellInfo> list;
        EXPECT_EQ(l.view.listCells(0, 100, list), 7u);
        ASSERT_EQ(list.size(), 7u);
        EXPECT_EQ(list[0].editorId, "alpha");
        EXPECT_EQ(list[1].editorId, "Beta");
        EXPECT_EQ(list[2].editorId, "Zeta");
        EXPECT_FALSE(list[3].hasGrid); // the persistent cell
        EXPECT_EQ(list[4].editorId, "W30"); // y = 0
        EXPECT_EQ(list[5].editorId, "W11"); // y = 1, x = 1
        EXPECT_EQ(list[6].editorId, "W21");

        std::vector<sdkgame::WorldCellInfo> page;
        EXPECT_EQ(l.view.listCells(2, 2, page), 2u);
        EXPECT_EQ(page[0].editorId, "Zeta");
        EXPECT_EQ(l.view.listCells(100, 5, page), 0u);
        EXPECT_FALSE(l.view.findCellByEditorId("nothing").has_value());
        EXPECT_TRUE(l.view.findCellByEditorId("ZETA").has_value());
        EXPECT_EQ(l.view.gameId(), "skyrimse");
    }

    TEST(SkyrimWorldView, namesOfALocalisedPluginComeFromItsStringTable)
    {
        Bytes bytes = header(Content::sRecMaster | Content::sRecLocalized);
        bytes.append(interiorCellGroup(
            0x900, cellRecord(0x900, "Named", "", CellFlags::Interior, Bytes(), true, 77), Bytes(), Bytes()));
        MapFiles files;
        files.mFiles["strings\\loc_english.strings"] = stringsTable({ { 77, "Localised Hall" }, { 5, "x" } });
        Loaded l({ { "loc.esm", bytes } }, {}, &files);
        const auto cell = l.view.findCellByEditorId("named");
        ASSERT_TRUE(cell.has_value());
        EXPECT_EQ(cell->name, "Localised Hall");
        EXPECT_EQ(l.view.stats().mStringTablesMissing, 0u);

        // No table: the plugin still loads, the name is empty and the miss is counted.
        MapFiles none;
        Loaded m({ { "loc.esm", bytes } }, {}, &none);
        const auto bare = m.view.findCellByEditorId("named");
        ASSERT_TRUE(bare.has_value());
        EXPECT_TRUE(bare->name.empty());
        EXPECT_EQ(m.view.stats().mStringTablesMissing, 1u);
    }

    TEST(SkyrimWorldView, aPlacementOfAMissingBaseIsCountedNotDropped)
    {
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(interiorCellGroup(
            0x900, cellRecord(0x900, "Lonely", "", CellFlags::Interior), Bytes(), refr(0xA01, 0x00000999, 0, 1, 2)));
        Loaded l({ { "m.esm", bytes } });
        const auto cell = l.view.findCellByEditorId("lonely");
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, c, error)) << error;
        ASSERT_EQ(c.objects.size(), 1u);
        EXPECT_EQ(c.objects[0].baseKind, sdkgame::BaseKind::Unknown);
        EXPECT_EQ(c.unresolvedBases, 1u);
        EXPECT_TRUE(c.objects[0].modelPath.empty());
    }

    TEST(SkyrimWorldView, nonStaticBasesMapToTheirKindsAndModels)
    {
        Bytes bytes = header(Content::sRecMaster);
        bytes.append(topGroup("DOOR", modelRecord("DOOR", 0x801, "D", "d.nif")));
        bytes.append(topGroup("CONT", modelRecord("CONT", 0x802, "C", "c.nif")));
        bytes.append(topGroup("ACTI", modelRecord("ACTI", 0x803, "A", "a.nif")));
        bytes.append(topGroup("TREE", modelRecord("TREE", 0x804, "T", "t.nif")));
        bytes.append(topGroup("LIGH", modelRecord("LIGH", 0x805, "L", "l.nif")));
        bytes.append(topGroup("MSTT", modelRecord("MSTT", 0x806, "M", "m.nif")));
        bytes.append(topGroup("FLOR", modelRecord("FLOR", 0x807, "F", "f.nif")));
        Bytes misc;
        misc.append(edid("Coin")).append(sub4("MODL", Bytes().zstring("coin.nif")));
        misc.append(sub4("DATA", Bytes().u32(5).f32(0.5f)));
        bytes.append(topGroup("MISC", record4("MISC", 0, 0x808, misc)));
        Bytes npc;
        npc.append(edid("Guard"));
        bytes.append(topGroup("NPC_", record4("NPC_", 0, 0x809, npc)));
        Bytes refs;
        for (std::uint32_t i = 0; i < 9; ++i)
            refs.append(refr(0xB00 + i, 0x801 + i));
        bytes.append(interiorCellGroup(0x900, cellRecord(0x900, "Kinds", "", CellFlags::Interior), Bytes(), refs));
        Loaded l({ { "k.esm", bytes } });
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(l.view.findCellByEditorId("kinds")->id, c, error)) << error;
        ASSERT_EQ(c.objects.size(), 9u);
        const sdkgame::BaseKind expected[]
            = { sdkgame::BaseKind::Door, sdkgame::BaseKind::Container, sdkgame::BaseKind::Activator,
                  sdkgame::BaseKind::Tree, sdkgame::BaseKind::Light, sdkgame::BaseKind::MovableStatic,
                  sdkgame::BaseKind::Flora, sdkgame::BaseKind::Item, sdkgame::BaseKind::Actor };
        for (std::size_t i = 0; i < 9; ++i)
            EXPECT_EQ(c.objects[i].baseKind, expected[i]) << i;
        EXPECT_EQ(c.objects[7].modelPath, "coin.nif");
        EXPECT_TRUE(c.objects[8].modelPath.empty());
        EXPECT_EQ(c.unresolvedBases, 0u);
    }

    // ---- hostile and damaged input ---------------------------------------------------------------------------------

    TEST(SkyrimWorldView, aFileThatIsNotASkyrimPluginOrDoesNotExistIsRefusedWithItsName)
    {
        TempDir dir;
        SkyrimWorldView view;
        std::string error;
        EXPECT_FALSE(view.addPlugin(dir.mPath / "missing.esm", error));
        EXPECT_NE(error.find("missing.esm"), std::string::npos);

        Bytes morrowind = tes3Header(1.3f, 1, "a", "b", 0, {});
        const fs::path tes3 = dir.write("morrowind.esm", morrowind);
        error.clear();
        EXPECT_FALSE(view.addPlugin(tes3, error));
        EXPECT_NE(error.find("morrowind.esm"), std::string::npos);

        const fs::path junk = dir.write("junk.esm", Bytes().text("not a plugin at all, just text"));
        error.clear();
        EXPECT_FALSE(view.addPlugin(junk, error));
        EXPECT_EQ(view.stats().mPluginsRefused, 3u);
        view.finish();
        EXPECT_EQ(view.cellCount(), 0u);
    }

    TEST(SkyrimWorldView, aDuplicatePluginNameIsRefused)
    {
        TempDir dir;
        const fs::path a = dir.write("base.esm", baseEsm());
        std::filesystem::create_directories(dir.mPath / "other");
        std::ofstream((dir.mPath / "other" / "base.esm"), std::ios::binary) << baseEsm().str();
        SkyrimWorldView view;
        std::string error;
        EXPECT_TRUE(view.addPlugin(a, error));
        EXPECT_FALSE(view.addPlugin(dir.mPath / "other" / "base.esm", error));
        EXPECT_NE(error.find("base.esm"), std::string::npos);
    }

    TEST(SkyrimWorldView, aFileThatShrinksAfterIndexingFailsTheCellCleanly)
    {
        Loaded l({ { "base.esm", baseEsm() } });
        const auto cell = l.view.findCellByEditorId("TestHall");
        ASSERT_TRUE(cell.has_value());
        // Cut the file in the middle of the cell's children group.
        const fs::path path = l.dir.mPath / "base.esm";
        const auto size = fs::file_size(path);
        fs::resize_file(path, size - 40);
        sdkgame::WorldCellContent c;
        std::string error;
        EXPECT_FALSE(l.view.loadCell(cell->id, c, error));
        EXPECT_FALSE(error.empty());
        EXPECT_NE(error.find("base.esm"), std::string::npos);
        EXPECT_TRUE(c.objects.empty());
        // And an unknown id is an error too.
        EXPECT_FALSE(l.view.loadCell(0xDEAD, c, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimWorldView, aDamagedPluginKeepsTheRecordsBeforeTheDamage)
    {
        Bytes bytes = baseEsm();
        std::string s = bytes.str();
        s.resize(s.size() - 20); // cut inside the last group
        TempDir dir;
        Bytes cut;
        cut.text(s);
        const fs::path p = dir.write("cut.esm", cut);
        SkyrimWorldView view;
        std::string error;
        // The header and the first top groups are intact: the plugin is taken, the damage is reported.
        (void)view.addPlugin(p, error);
        view.finish();
        EXPECT_FALSE(view.errors().empty());
    }

    TEST(SkyrimWorldView, theObjectCapDropsAndCounts)
    {
        SkyrimWorldView::Options options;
        options.mMaxObjectsPerCell = 1;
        Loaded l({ { "base.esm", baseEsm() } }, options);
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(l.view.findCellByEditorId("TestHall")->id, c, error)) << error;
        EXPECT_EQ(c.objects.size(), 1u);
        EXPECT_EQ(l.view.stats().mObjectsDropped, 1u);
    }

    TEST(SkyrimWorldView, theSameFilesGiveTheSameListsInAnyRun)
    {
        Loaded a({ { "base.esm", baseEsm() }, { "over.esp", overrideEsp() } });
        Loaded b({ { "base.esm", baseEsm() }, { "over.esp", overrideEsp() } });
        std::vector<sdkgame::WorldCellInfo> la, lb;
        a.view.listCells(0, 100, la);
        b.view.listCells(0, 100, lb);
        EXPECT_EQ(la, lb);
        sdkgame::WorldCellContent ca, cb;
        std::string error;
        ASSERT_TRUE(a.view.loadCell(la[0].id, ca, error));
        ASSERT_TRUE(b.view.loadCell(lb[0].id, cb, error));
        EXPECT_EQ(ca.objects, cb.objects);
    }

#ifndef OPENEDEN_WORLDVIEW_RIG
    // ---- the service slot ------------------------------------------------------------------------------------------

    class ViewProbe : public openeden::sdk::Extension
    {
    public:
        explicit ViewProbe(SkyrimWorldView& view, sdkgame::WorldViewService*& seen)
            : mView(view)
            , mSeen(seen)
        {
        }
        void registerServices(openeden::sdk::Registrar&) override {}
        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }
        void start(openeden::sdk::Host& host) override
        {
            mSeen = host.getWorldViewService();
            if (mSeen != nullptr)
                mSeen->registerWorldView(mView);
        }
        void stop() override
        {
            if (mSeen != nullptr)
                mSeen->withdrawWorldView(mView);
        }

    private:
        SkyrimWorldView& mView;
        sdkgame::WorldViewService*& mSeen;
    };

    openeden::extension::ExtensionManifest manifestOf(const std::string& id, openeden::sdk::PackKind kind)
    {
        openeden::extension::ExtensionManifest m;
        m.version = kind == openeden::sdk::PackKind::Game ? 2 : 1;
        m.id = id;
        m.name = id;
        m.extensionVersion = "1.0.0";
        m.kind = std::string(openeden::sdk::toString(kind));
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    TEST(SkyrimWorldViewService, onlyAGamePackGetsTheSlotAndTheViewIsReachableThroughIt)
    {
        {
            openeden::extension::HostImpl host;
            openeden::extension::LifecycleManager manager(host);
            SkyrimWorldView view;
            sdkgame::WorldViewService* seen = nullptr;
            manager.addExtension(
                manifestOf("game", openeden::sdk::PackKind::Game), std::make_unique<ViewProbe>(view, seen));
            ASSERT_TRUE(manager.initialize());
            ASSERT_NE(seen, nullptr);
            EXPECT_EQ(seen->worldView(), &view);
            EXPECT_EQ(host.worldView().worldView(), &view);
            sdkgame::WorldView* viaService = seen->worldView();
            EXPECT_EQ(viaService->gameId(), "skyrimse");
            EXPECT_EQ(viaService->cellCount(), 0u);
            manager.shutdown();
            EXPECT_EQ(host.worldView().worldView(), nullptr);
        }
        {
            openeden::extension::HostImpl host;
            openeden::extension::LifecycleManager manager(host);
            SkyrimWorldView view;
            sdkgame::WorldViewService* seen = reinterpret_cast<sdkgame::WorldViewService*>(1);
            manager.addExtension(
                manifestOf("feat", openeden::sdk::PackKind::Feature), std::make_unique<ViewProbe>(view, seen));
            ASSERT_TRUE(manager.initialize());
            EXPECT_EQ(seen, nullptr);
            EXPECT_EQ(host.worldView().worldView(), nullptr);
            EXPECT_FALSE(manager.getCapabilityDenials().empty());
        }
    }
#endif
}
