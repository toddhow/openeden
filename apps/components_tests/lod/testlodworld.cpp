// Task 0133 (milestone M-S2): where a LOD pack lives for a game pack's neutral world, and the fingerprint a neutral
// pack is checked against. Pure code.
#include <gtest/gtest.h>

#include <components/lod/lodworld.hpp>
#include <components/lod/tilebake.hpp>

#include <limits>
#include <set>
#include <string>

namespace
{
    using namespace Lod;

    NeutralWorldFacts skyrimFacts()
    {
        NeutralWorldFacts facts;
        facts.mGameId = "skyrimse";
        facts.mWorldEditorId = "WhiterunWorld";
        facts.mCellSizeUnits = 4096.f;
        facts.mVertsPerSide = 33;
        facts.mDefaultLandHeight = -2000.f;
        facts.mCellCount = 73723;
        return facts;
    }

    TEST(LodWorldPath, an_empty_game_id_is_morrowinds_legacy_place_whatever_the_world)
    {
        const std::filesystem::path root = "user/openeden/lod";
        EXPECT_EQ(terrainPackPath(root, "", ""), root / "terrain.edenpak");
        EXPECT_EQ(terrainPackPath(root, "", "Tamriel"), root / "terrain.edenpak");
        EXPECT_EQ(objectPackPath(root, "", ""), root / "objects.edenpak");
    }

    TEST(LodWorldPath, a_game_pack_world_gets_a_folder_of_its_own)
    {
        const std::filesystem::path root = "user/openeden/lod";
        EXPECT_EQ(terrainPackPath(root, "skyrimse", "WhiterunWorld"),
            root / "skyrimse" / "whiterunworld" / "terrain.edenpak");
        EXPECT_EQ(objectPackPath(root, "skyrimse", "Tamriel"), root / "skyrimse" / "tamriel" / "objects.edenpak");
    }

    TEST(LodWorldPath, a_neutral_path_never_equals_the_legacy_one_and_worlds_differ)
    {
        const std::filesystem::path root = "lod";
        EXPECT_NE(terrainPackPath(root, "skyrimse", "Tamriel"), terrainPackPath(root, "", ""));
        EXPECT_NE(terrainPackPath(root, "skyrimse", "Tamriel"), terrainPackPath(root, "skyrimse", "WhiterunWorld"));
        EXPECT_NE(terrainPackPath(root, "skyrimse", "Tamriel"), terrainPackPath(root, "fallout4", "Tamriel"));
    }

    TEST(LodWorldPath, a_hostile_name_cannot_leave_its_folder)
    {
        const std::filesystem::path root = "lod";
        const std::filesystem::path path = terrainPackPath(root, "..", "../../etc/passwd");
        std::set<std::string> parts;
        for (const auto& part : path)
            parts.insert(part.string());
        EXPECT_EQ(parts.count(".."), 0u);
        EXPECT_EQ(path.parent_path().parent_path().parent_path(), root);
        EXPECT_EQ(safePathComponent(""), "_");
        EXPECT_EQ(safePathComponent("a b/c\\d:e"), "a_b_c_d_e");
        EXPECT_EQ(safePathComponent("Caf\xC3\xA9"), "caf__");
        EXPECT_EQ(safePathComponent(std::string(200, 'x')).size(), 64u);
        EXPECT_EQ(safePathComponent("..."), "___");
    }

    TEST(LodWorldFingerprint, is_sixteen_lower_case_hex_digits_and_stable)
    {
        const std::string fp = neutralFingerprint(skyrimFacts());
        ASSERT_EQ(fp.size(), 16u);
        for (const char c : fp)
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << fp;
        EXPECT_EQ(fp, neutralFingerprint(skyrimFacts()));
    }

    TEST(LodWorldFingerprint, every_fact_changes_it)
    {
        const std::string base = neutralFingerprint(skyrimFacts());
        {
            auto f = skyrimFacts();
            f.mGameId = "other";
            EXPECT_NE(neutralFingerprint(f), base);
        }
        {
            auto f = skyrimFacts();
            f.mWorldEditorId = "Tamriel";
            EXPECT_NE(neutralFingerprint(f), base);
        }
        {
            auto f = skyrimFacts();
            f.mCellSizeUnits = 8192.f;
            EXPECT_NE(neutralFingerprint(f), base);
        }
        {
            auto f = skyrimFacts();
            f.mVertsPerSide = 65;
            EXPECT_NE(neutralFingerprint(f), base);
        }
        {
            auto f = skyrimFacts();
            f.mDefaultLandHeight = -1.f;
            EXPECT_NE(neutralFingerprint(f), base);
        }
        {
            auto f = skyrimFacts();
            f.mCellCount += 1;
            EXPECT_NE(neutralFingerprint(f), base);
        }
    }

    TEST(LodWorldFingerprint, the_game_and_world_names_do_not_run_together)
    {
        auto a = skyrimFacts();
        a.mGameId = "ab";
        a.mWorldEditorId = "c";
        auto b = skyrimFacts();
        b.mGameId = "a";
        b.mWorldEditorId = "bc";
        EXPECT_NE(neutralFingerprint(a), neutralFingerprint(b));
    }

    TEST(LodWorldFingerprint, pinned_value_so_a_change_of_the_recipe_is_noticed)
    {
        // Computed independently in Python (FNV-1a 64 over length-prefixed text and little-endian numbers) and then checked
        // against the implementation; changing the recipe makes every neutral pack stale on purpose.
        EXPECT_EQ(neutralFingerprint(skyrimFacts()), "3cc3a638b7464738");
    }

    TEST(LodWorldName, joins_game_world_and_fingerprint)
    {
        const NeutralWorldFacts facts = skyrimFacts();
        EXPECT_EQ(neutralWorldspaceName(facts), "skyrimse:WhiterunWorld:" + neutralFingerprint(facts));
    }

    TileManifest manifestFor(const NeutralWorldFacts& facts)
    {
        TileBakeSettings base;
        base.mCellSize = 4096;
        base.mVerticesPerCell = 33;
        return makeTerrainManifest(base, TerrainBakePlan{}, true, neutralWorldspaceName(facts));
    }

    TEST(LodWorldMismatch, a_manifest_a_bake_would_write_now_matches)
    {
        const NeutralWorldFacts facts = skyrimFacts();
        EXPECT_EQ(describeNeutralPackMismatch(manifestFor(facts), neutralWorldspaceName(facts), 4096, 33, 256), "");
    }

    TEST(LodWorldMismatch, each_difference_is_named)
    {
        const NeutralWorldFacts facts = skyrimFacts();
        const std::string name = neutralWorldspaceName(facts);
        {
            NeutralWorldFacts other = facts;
            other.mCellCount += 1;
            EXPECT_NE(describeNeutralPackMismatch(manifestFor(other), name, 4096, 33, 256).find("worldspace"),
                std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mWorldspaces[0].mCellSize = 8192;
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("cell size"), std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mGridSize = 17;
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("grid size"), std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mAlbedoSize = 0;
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("albedo"), std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mBakerVersion += 1;
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("baker version"), std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mLayoutVersion += 1;
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("layout version"), std::string::npos);
        }
        {
            TileManifest m = manifestFor(facts);
            m.mWorldspaces.push_back(m.mWorldspaces[0]);
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("worldspace"), std::string::npos);
            m.mWorldspaces.clear();
            EXPECT_NE(describeNeutralPackMismatch(m, name, 4096, 33, 256).find("worldspace"), std::string::npos);
        }
    }

    TEST(LodWorldMismatch, a_morrowind_pack_is_never_a_neutral_match)
    {
        const NeutralWorldFacts facts = skyrimFacts();
        TileBakeSettings base; // Morrowind's: cell 8192
        const TileManifest morrowind = makeTerrainManifest(base, TerrainBakePlan{}, true, "Morrowind");
        EXPECT_NE(describeNeutralPackMismatch(morrowind, neutralWorldspaceName(facts), 4096, 33, 256), "");
    }

    TEST(LodWorldAlbedo, morrowinds_cell_keeps_its_256_and_skyrims_gets_the_same_ground_per_texel)
    {
        EXPECT_EQ(albedoSizeForCell(8192.f), 256u);
        EXPECT_EQ(albedoSizeForCell(4096.f), 128u);
    }

    TEST(LodWorldAlbedo, other_sizes_round_down_to_a_power_of_two_between_32_and_256)
    {
        EXPECT_EQ(albedoSizeForCell(2048.f), 64u);
        EXPECT_EQ(albedoSizeForCell(1024.f), 32u);
        EXPECT_EQ(albedoSizeForCell(512.f), 32u);
        EXPECT_EQ(albedoSizeForCell(16384.f), 256u);
        EXPECT_EQ(albedoSizeForCell(5000.f), 128u); // 156 rounds down
    }

    TEST(LodWorldAlbedo, an_unusable_size_gives_the_default)
    {
        EXPECT_EQ(albedoSizeForCell(0.f), 256u);
        EXPECT_EQ(albedoSizeForCell(-5.f), 256u);
        EXPECT_EQ(albedoSizeForCell(std::numeric_limits<float>::quiet_NaN()), 256u);
        EXPECT_EQ(albedoSizeForCell(1e12f), 256u);
    }

    TEST(LodWorldExtent, no_tiles_is_an_empty_box)
    {
        EXPECT_TRUE(extentOfTiles({}).empty());
    }

    TEST(LodWorldExtent, the_box_is_the_union_of_the_tile_squares)
    {
        // A level 1 tile whose south-west cell is (-4, 6) covers cells -4..-3 by 6..7; a level 3 tile at (8, -16) covers
        // 8..15 by -16..-9.
        const std::vector<TileId> tiles{ *tileContainingCell(0, 1, -4, 6), *tileContainingCell(0, 3, 8, -16) };
        const LandExtent extent = extentOfTiles(tiles);
        ASSERT_FALSE(extent.empty());
        EXPECT_EQ(extent.mMinX, -4);
        EXPECT_EQ(extent.mMaxX, 15);
        EXPECT_EQ(extent.mMinY, -16);
        EXPECT_EQ(extent.mMaxY, 7);
    }

    TEST(LodWorldExtent, an_invalid_tile_is_skipped)
    {
        TileId bad;
        bad.mLevel = 200;
        const std::vector<TileId> tiles{ bad, *tileContainingCell(0, 2, 4, 4) };
        const LandExtent extent = extentOfTiles(tiles);
        ASSERT_FALSE(extent.empty());
        EXPECT_EQ(extent.mMinX, 4);
        EXPECT_EQ(extent.mMaxX, 7);
    }
}
