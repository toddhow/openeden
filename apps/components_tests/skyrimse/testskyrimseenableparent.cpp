// Task 0293: a placement that has an enable parent (XESP) starts in the state its parent starts in. The seven towns'
// garrisons (Imperial and Stormcloak guards, each enabled by a marker; only the Stormcloak marker starts enabled) were
// all drawn because the world view read only a placement's own InitiallyDisabled flag. Generated plugins only.
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_view.hpp>

#include <gtest/gtest.h>

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

    constexpr std::uint32_t kGrupTop = 0, kGrupWorld = 1, kGrupInteriorBlock = 2, kGrupInteriorSub = 3,
                            kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6, kGrupPersistent = 8,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kDisabled = 0x800;
    constexpr std::uint32_t kStat = 0x00000801, kCell = 0x00000900;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // A placement; parent 0 means none. `parentFlags` is the XESP flag byte (bit 0: the opposite of the parent).
    Bytes refr(
        std::uint32_t id, std::uint32_t flags, float x, float y, std::uint32_t parent = 0, std::uint8_t parentFlags = 0)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(kStat)));
        if (parent != 0)
            subs.append(sub4("XESP", Bytes().u32(parent).u8(parentFlags).u8(0).u8(0).u8(0)));
        subs.append(sub4("DATA", Bytes().f32(x).f32(y).f32(3).f32(0).f32(0).f32(0)));
        return record4("REFR", flags, id, subs);
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes cellRecord(std::uint32_t id, const std::string& editorId, std::uint16_t flags, const Bytes& extra = Bytes())
    {
        Bytes subs;
        subs.append(edid(editorId));
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

    Bytes interior(const Bytes& persistent, const Bytes& temporary)
    {
        Bytes sub = cellRecord(kCell, "Hall", CellFlags::Interior);
        sub.append(childrenGroup(kCell, persistent, temporary));
        return topGroup(
            "CELL", group4(kCell % 10, kGrupInteriorBlock, group4((kCell / 10) % 10, kGrupInteriorSub, sub)));
    }

    Bytes baseWith(const Bytes& cells)
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        Bytes stat;
        stat.append(edid("Wall"));
        stat.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        stat.append(sub4("MODL", Bytes().zstring("architecture\\wall.nif")));
        bytes.append(topGroup("STAT", record4("STAT", 0, kStat, stat)));
        bytes.append(cells);
        return bytes;
    }

    Bytes xclc(std::int32_t x, std::int32_t y)
    {
        return sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0));
    }

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_enableparent_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                    + std::to_string(counter++));
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
        explicit Loaded(const Bytes& plugin)
            : view(SkyrimWorldView::Options{}, nullptr)
        {
            view.load({ dir.write("a.esm", plugin) }, problems);
        }
    };

    bool disabled(const sdkgame::WorldCellContent& c, std::uint32_t id)
    {
        for (const sdkgame::PlacedObject& o : c.objects)
            if (o.id == id)
                return o.initiallyDisabled;
        ADD_FAILURE() << "no placement " << id;
        return false;
    }

    constexpr std::uint32_t kOffMarker = 0xA01, kOnMarker = 0xA02, kNoParent = 0xA03;

    TEST(SkyrimEnableParent, aChildOfAMarkerThatStartsDisabledStartsDisabledAndTheOtherGarrisonDoesNot)
    {
        Bytes temporary;
        temporary.append(refr(kOffMarker, kDisabled, 0, 0));
        temporary.append(refr(kOnMarker, 0, 10, 0));
        temporary.append(refr(0xB01, 0, 20, 0, kOffMarker)); // the Imperial guard
        temporary.append(refr(0xB02, 0, 30, 0, kOnMarker)); // the Stormcloak guard
        temporary.append(refr(kNoParent, 0, 40, 0)); // no parent: untouched
        Loaded l(baseWith(interior(Bytes(), temporary)));
        ASSERT_TRUE(l.problems.empty());
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(kCell, c, error)) << error;
        EXPECT_TRUE(disabled(c, kOffMarker));
        EXPECT_FALSE(disabled(c, kOnMarker));
        EXPECT_TRUE(disabled(c, 0xB01));
        EXPECT_FALSE(disabled(c, 0xB02));
        EXPECT_FALSE(disabled(c, kNoParent));
    }

    TEST(SkyrimEnableParent, theOppositeFlagInvertsTheParentsState)
    {
        Bytes temporary;
        temporary.append(refr(kOffMarker, kDisabled, 0, 0));
        temporary.append(refr(kOnMarker, 0, 10, 0));
        temporary.append(refr(0xB01, 0, 20, 0, kOffMarker, 1)); // opposite of a disabled parent: enabled
        temporary.append(refr(0xB02, 0, 30, 0, kOnMarker, 1)); // opposite of an enabled parent: disabled
        Loaded l(baseWith(interior(Bytes(), temporary)));
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(kCell, c, error)) << error;
        EXPECT_FALSE(disabled(c, 0xB01));
        EXPECT_TRUE(disabled(c, 0xB02));
    }

    TEST(SkyrimEnableParent, aChainOfParentsFollowsItsRootAndAPersistentParentCounts)
    {
        Bytes persistent;
        persistent.append(refr(kOffMarker, kDisabled | 0x400, 0, 0));
        Bytes temporary;
        temporary.append(refr(0xB01, 0, 20, 0, kOffMarker)); // child of the persistent marker
        temporary.append(refr(0xB02, 0, 30, 0, 0xB01)); // grandchild
        temporary.append(refr(0xB03, 0, 40, 0, 0xB02, 1)); // great grandchild, opposite
        Loaded l(baseWith(interior(persistent, temporary)));
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(kCell, c, error)) << error;
        EXPECT_TRUE(disabled(c, 0xB01));
        EXPECT_TRUE(disabled(c, 0xB02));
        EXPECT_FALSE(disabled(c, 0xB03));
    }

    TEST(SkyrimEnableParent, aParentThatIsNotInTheCellLeavesThePlacementAsItsOwnFlagsSay)
    {
        Bytes temporary;
        temporary.append(refr(0xB01, 0, 20, 0, 0x14)); // the player
        temporary.append(refr(0xB02, kDisabled, 30, 0, 0xFFFFF0)); // a reference this plugin does not hold
        Loaded l(baseWith(interior(Bytes(), temporary)));
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(kCell, c, error)) << error;
        EXPECT_FALSE(disabled(c, 0xB01));
        EXPECT_TRUE(disabled(c, 0xB02));
    }

    TEST(SkyrimEnableParent, aParentLoopDoesNotHangAndLeavesTheOwnFlags)
    {
        Bytes temporary;
        temporary.append(refr(0xB01, 0, 20, 0, 0xB02));
        temporary.append(refr(0xB02, kDisabled, 30, 0, 0xB01));
        Loaded l(baseWith(interior(Bytes(), temporary)));
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(kCell, c, error)) << error;
        EXPECT_EQ(c.objects.size(), 2u);
    }

    // An exterior: the marker is in the world's persistent cell, the guard in a square (the real towns' layout).
    TEST(SkyrimEnableParent, aPersistentExteriorMarkerDecidesASquaresGuards)
    {
        constexpr std::uint32_t world = 0x00000C00, pcell = 0x00000C01, g00 = 0x00000C10;
        Bytes persistent;
        persistent.append(refr(kOffMarker, kDisabled | 0x400, 100, 100));
        persistent.append(refr(kOnMarker, 0x400, 200, 100));
        Bytes worldChildren;
        Bytes persistentCell = cellRecord(pcell, "", 0);
        persistentCell.append(childrenGroup(pcell, persistent, Bytes()));
        worldChildren.append(persistentCell);
        Bytes temporary;
        temporary.append(refr(0xB01, 0, 120, 120, kOffMarker));
        temporary.append(refr(0xB02, 0, 130, 120, kOnMarker));
        Bytes sub = cellRecord(g00, "TestWorldX0Y0", 0, xclc(0, 0));
        sub.append(childrenGroup(g00, Bytes(), temporary));
        worldChildren.append(group4(0, kGrupExtBlock, group4(0, kGrupExtSub, sub)));
        Bytes both = record4("WRLD", 0, world, edid("TestWorld"));
        both.append(group4(world, kGrupWorld, worldChildren));
        Loaded l(baseWith(topGroup("WRLD", both)));
        ASSERT_TRUE(l.problems.empty());
        const auto cell = l.view.findCellByGrid(world, 0, 0);
        ASSERT_TRUE(cell.has_value());
        sdkgame::WorldCellContent c;
        std::string error;
        ASSERT_TRUE(l.view.loadCell(cell->id, c, error)) << error;
        EXPECT_TRUE(disabled(c, 0xB01));
        EXPECT_FALSE(disabled(c, 0xB02));
    }
}
