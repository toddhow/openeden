// Task 0202: SkyrimWorldView::describeBase, the description of a BASE form for PlaceAtMe (kind, editor id, model,
// bounds).
// Generated plugins in a scratch folder; no game data. Written first and run red against a stub that knows no base.
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

    constexpr std::uint32_t kStat = 0x00000801, kFurn = 0x00000802, kCell = 0x00000900, kRef = 0x00000A01;

    Bytes modelRecord(const char (&type)[5], std::uint32_t id, const std::string& editorId, const std::string& model)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(editorId)));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring(model)));
        return record4(type, 0, id, subs);
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), 0, records);
    }

    Bytes baseEsm()
    {
        Bytes bytes = tes4Header(1.7f, 1, "A", "B", 1, {}, 24, 44);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStat, "StoneWall", "architecture\\wall.nif")));
        bytes.append(topGroup("FURN", modelRecord("FURN", kFurn, "WoodChair", "furniture\\chair.nif")));
        Bytes cell;
        cell.append(sub4("EDID", Bytes().zstring("TestHall")));
        cell.append(sub4("DATA", Bytes().u16(1)));
        Bytes children;
        Bytes refSubs;
        refSubs.append(sub4("NAME", Bytes().u32(kStat)));
        refSubs.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        children.append(group4(kCell, 9, record4("REFR", 0, kRef, refSubs)));
        Bytes sub = record4("CELL", 0, kCell, cell);
        sub.append(group4(kCell, 6, children));
        bytes.append(topGroup("CELL", group4(kCell % 10, 2, group4((kCell / 10) % 10, 3, sub))));
        return bytes;
    }

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            mPath = fs::temp_directory_path()
                / ("oe_describebase_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
    };

    struct Loaded
    {
        TempDir dir;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        Loaded()
        {
            const fs::path p = dir.mPath / "base.esm";
            std::ofstream out(p, std::ios::binary);
            const std::string s = baseEsm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            out.close();
            view.load({ p }, problems);
        }
    };

    TEST(SkyrimDescribeBase, aStaticBaseIsDescribedWithItsModelBoundsAndEditorId)
    {
        Loaded l;
        ASSERT_TRUE(l.problems.empty());
        sdkgame::PlacedObject o;
        ASSERT_TRUE(l.view.describeBase(kStat, o));
        EXPECT_EQ(o.base, kStat);
        EXPECT_EQ(o.baseKind, sdkgame::BaseKind::Static);
        EXPECT_EQ(o.baseEditorId, "StoneWall");
        EXPECT_EQ(o.modelPath, "architecture\\wall.nif");
        EXPECT_TRUE(o.hasBounds);
        EXPECT_EQ(o.boundsMax.z, 20.f);
        EXPECT_EQ(o.scale, 1.f);
        EXPECT_FALSE(o.initiallyDisabled);
        EXPECT_EQ(o.position.x, 0.f);
    }

    TEST(SkyrimDescribeBase, aFurnitureBaseKeepsItsKind)
    {
        Loaded l;
        sdkgame::PlacedObject o;
        ASSERT_TRUE(l.view.describeBase(kFurn, o));
        EXPECT_EQ(o.baseKind, sdkgame::BaseKind::Furniture);
        EXPECT_EQ(o.modelPath, "furniture\\chair.nif");
    }

    TEST(SkyrimDescribeBase, aFormThatIsNotABaseIsRefused)
    {
        Loaded l;
        sdkgame::PlacedObject o;
        o.baseEditorId = "untouched";
        EXPECT_FALSE(l.view.describeBase(kRef, o)) << "a placed reference is not a base";
        EXPECT_FALSE(l.view.describeBase(0, o));
        EXPECT_FALSE(l.view.describeBase(0xDEADBEEF, o));
        EXPECT_EQ(o.baseEditorId, "untouched") << "a refusal leaves the output alone";
    }
}
