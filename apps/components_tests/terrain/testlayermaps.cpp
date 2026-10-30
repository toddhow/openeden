// Tests for the renderer-free search for terrain normal and specular maps
// (task 0012, Step 2 (V4i), "Finding the maps"), following OpenMW's
// ESMTerrain::Storage::getLayerInfo at 442eec6e2f.
//
// Written from the spec alone. Assumptions and ambiguities:
// - findTerrainLayerMaps, TerrainLayerMaps and TerrainLayer live in namespace Terrain, declared in
//   components/terrain/layermaps.hpp, with the fields the spec names (layers, normalMaps; diffuseMap,
//   normalMapIndex, parallax, specular).
// - Candidate paths are built as vsgAdapters::autoMapPath does: the last '.' of the path replaced by
//   pattern + ".". The specular candidate uses AutoMapSettings::specularMapPattern (the caller fills
//   it with the terrain pattern, `_diffusespec`).
// - The exists callback is a lookup in a std::set of exact paths; no case folding or normalisation
//   inside findTerrainLayerMaps is assumed.
// - The normal-height map is tried before the plain normal map; when the normal-height map exists the
//   plain one is not used even if it also exists.
// - The specular map does not depend on the normal-map switch, and a layer's normal maps are looked
//   up from its original diffuse path, not from the specular replacement.
// - An empty path and a path with no '.' find nothing even if exists() says yes to every query, and
//   such a layer keeps its diffuse path unchanged.
// - normalMapIndex is the index into normalMaps; indices are assigned in layer order, starting at 0.

#include <components/terrain/layermaps.hpp>
#include <components/vsgadapters/nif/automaps.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    vsgAdapters::AutoMapSettings settings(bool normals, bool speculars)
    {
        vsgAdapters::AutoMapSettings result;
        result.autoUseNormalMaps = normals;
        result.autoUseSpecularMaps = speculars;
        result.normalMapPattern = "_n";
        result.normalHeightMapPattern = "_nh";
        result.specularMapPattern = "_diffusespec";
        return result;
    }

    std::function<bool(std::string_view)> existsIn(const std::set<std::string>& files)
    {
        return [files](std::string_view path) { return files.count(std::string(path)) != 0; };
    }

    Terrain::TerrainLayerMaps find(const std::vector<std::string>& layers, const vsgAdapters::AutoMapSettings& config,
        const std::set<std::string>& files)
    {
        return Terrain::findTerrainLayerMaps(layers, config, existsIn(files));
    }

    void expectPlain(const Terrain::TerrainLayer& layer, const std::string& diffuse)
    {
        EXPECT_EQ(layer.diffuseMap, diffuse);
        EXPECT_EQ(layer.normalMapIndex, -1);
        EXPECT_FALSE(layer.parallax);
        EXPECT_FALSE(layer.specular);
    }

    const std::set<std::string> allMaps = {
        "textures/tx_a.dds",
        "textures/tx_a_n.dds",
        "textures/tx_a_nh.dds",
        "textures/tx_a_diffusespec.dds",
        "textures/tx_b.dds",
        "textures/tx_b_n.dds",
        "textures/tx_b_nh.dds",
        "textures/tx_b_diffusespec.dds",
    };

    TEST(TerrainLayerMapsTest, defaultLayerHasNoMaps)
    {
        const Terrain::TerrainLayer layer;
        EXPECT_TRUE(layer.diffuseMap.empty());
        EXPECT_EQ(layer.normalMapIndex, -1);
        EXPECT_FALSE(layer.parallax);
        EXPECT_FALSE(layer.specular);
    }

    TEST(TerrainLayerMapsTest, bothSwitchesOffKeepsDiffuseAndFindsNothing)
    {
        const std::vector<std::string> paths = { "textures/tx_a.dds", "textures/tx_b.dds" };
        const auto result = find(paths, settings(false, false), allMaps);
        ASSERT_EQ(result.layers.size(), 2u);
        expectPlain(result.layers[0], "textures/tx_a.dds");
        expectPlain(result.layers[1], "textures/tx_b.dds");
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, bothSwitchesOffEvenWhenEverythingExists)
    {
        const std::vector<std::string> paths = { "textures/tx_a.dds" };
        const auto result
            = Terrain::findTerrainLayerMaps(paths, settings(false, false), [](std::string_view) { return true; });
        ASSERT_EQ(result.layers.size(), 1u);
        expectPlain(result.layers[0], "textures/tx_a.dds");
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, noLayersGivesNoLayers)
    {
        const auto result = find({}, settings(true, true), allMaps);
        EXPECT_TRUE(result.layers.empty());
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, normalHeightPreferredOverNormalWithParallax)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(true, false), allMaps);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a.dds");
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_TRUE(result.layers[0].parallax);
        EXPECT_FALSE(result.layers[0].specular);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_nh.dds");
    }

    TEST(TerrainLayerMapsTest, normalHeightOnly)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(true, false), { "textures/tx_a_nh.dds" });
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_TRUE(result.layers[0].parallax);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_nh.dds");
    }

    TEST(TerrainLayerMapsTest, normalOnlyHasNoParallax)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(true, false), { "textures/tx_a_n.dds" });
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a.dds");
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_FALSE(result.layers[0].parallax);
        EXPECT_FALSE(result.layers[0].specular);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_n.dds");
    }

    TEST(TerrainLayerMapsTest, noNormalMapFoundLeavesIndexUnset)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(true, false), { "textures/tx_a.dds" });
        ASSERT_EQ(result.layers.size(), 1u);
        expectPlain(result.layers[0], "textures/tx_a.dds");
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, sharedNormalMapIsDeduplicated)
    {
        // The same diffuse path twice (and a third layer between them) shares one normal map entry.
        const std::vector<std::string> paths = { "textures/tx_a.dds", "textures/tx_b.dds", "textures/tx_a.dds" };
        const auto result = find(paths, settings(true, false), { "textures/tx_a_n.dds", "textures/tx_b_n.dds" });
        ASSERT_EQ(result.layers.size(), 3u);
        ASSERT_EQ(result.normalMaps.size(), 2u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_n.dds");
        EXPECT_EQ(result.normalMaps[1], "textures/tx_b_n.dds");
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_EQ(result.layers[1].normalMapIndex, 1);
        EXPECT_EQ(result.layers[2].normalMapIndex, 0);
    }

    TEST(TerrainLayerMapsTest, sharedNormalHeightMapKeepsParallaxOnEveryLayer)
    {
        const std::vector<std::string> paths = { "textures/tx_a.dds", "textures/tx_a.dds" };
        const auto result = find(paths, settings(true, false), { "textures/tx_a_nh.dds" });
        ASSERT_EQ(result.layers.size(), 2u);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_EQ(result.layers[1].normalMapIndex, 0);
        EXPECT_TRUE(result.layers[0].parallax);
        EXPECT_TRUE(result.layers[1].parallax);
    }

    TEST(TerrainLayerMapsTest, distinctMapsGetSequentialIndicesInLayerOrder)
    {
        const std::vector<std::string> paths
            = { "textures/tx_c.dds", "textures/tx_none.dds", "textures/tx_a.dds", "textures/tx_b.dds" };
        const std::set<std::string> files = { "textures/tx_a_n.dds", "textures/tx_b_nh.dds", "textures/tx_c_n.dds" };
        const auto result = find(paths, settings(true, false), files);
        ASSERT_EQ(result.layers.size(), 4u);
        ASSERT_EQ(result.normalMaps.size(), 3u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_c_n.dds");
        EXPECT_EQ(result.normalMaps[1], "textures/tx_a_n.dds");
        EXPECT_EQ(result.normalMaps[2], "textures/tx_b_nh.dds");
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_EQ(result.layers[1].normalMapIndex, -1);
        EXPECT_EQ(result.layers[2].normalMapIndex, 1);
        EXPECT_EQ(result.layers[3].normalMapIndex, 2);
        EXPECT_FALSE(result.layers[0].parallax);
        EXPECT_FALSE(result.layers[1].parallax);
        EXPECT_FALSE(result.layers[2].parallax);
        EXPECT_TRUE(result.layers[3].parallax);
    }

    TEST(TerrainLayerMapsTest, specularReplacesDiffuseAndSetsSpecular)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(false, true), allMaps);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a_diffusespec.dds");
        EXPECT_TRUE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, -1);
        EXPECT_FALSE(result.layers[0].parallax);
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, missingSpecularKeepsDiffuse)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(false, true), { "textures/tx_a_n.dds" });
        ASSERT_EQ(result.layers.size(), 1u);
        expectPlain(result.layers[0], "textures/tx_a.dds");
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, specularAndNormalsTogether)
    {
        const auto result = find({ "textures/tx_a.dds" }, settings(true, true), allMaps);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a_diffusespec.dds");
        EXPECT_TRUE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_TRUE(result.layers[0].parallax);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        // Normal maps are named after the original diffuse map, not the specular replacement.
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_nh.dds");
    }

    TEST(TerrainLayerMapsTest, specularIndependentOfNormalsPerLayer)
    {
        // Layer a has only a specular map, layer b only a normal map.
        const std::vector<std::string> paths = { "textures/tx_a.dds", "textures/tx_b.dds" };
        const std::set<std::string> files = { "textures/tx_a_diffusespec.dds", "textures/tx_b_n.dds" };
        const auto result = find(paths, settings(true, true), files);
        ASSERT_EQ(result.layers.size(), 2u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a_diffusespec.dds");
        EXPECT_TRUE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, -1);
        EXPECT_EQ(result.layers[1].diffuseMap, "textures/tx_b.dds");
        EXPECT_FALSE(result.layers[1].specular);
        EXPECT_EQ(result.layers[1].normalMapIndex, 0);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_b_n.dds");
    }

    TEST(TerrainLayerMapsTest, normalsOffIgnoresNormalMapsButFindsSpecular)
    {
        const auto result = find({ "textures/tx_b.dds" }, settings(false, true), allMaps);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_TRUE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, -1);
        EXPECT_FALSE(result.layers[0].parallax);
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, specularOffIgnoresSpecularMaps)
    {
        const auto result = find({ "textures/tx_b.dds" }, settings(true, false), allMaps);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_b.dds");
        EXPECT_FALSE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
    }

    TEST(TerrainLayerMapsTest, layerCountAndOrderPreserved)
    {
        const std::vector<std::string> paths = { "textures/tx_b.dds", "textures/tx_none.dds", "textures/tx_a.dds",
            "textures/tx_b.dds", "textures/tx_none2.dds" };
        const auto result = find(paths, settings(true, true), allMaps);
        ASSERT_EQ(result.layers.size(), paths.size());
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_b_diffusespec.dds");
        EXPECT_EQ(result.layers[1].diffuseMap, "textures/tx_none.dds");
        EXPECT_EQ(result.layers[2].diffuseMap, "textures/tx_a_diffusespec.dds");
        EXPECT_EQ(result.layers[3].diffuseMap, "textures/tx_b_diffusespec.dds");
        EXPECT_EQ(result.layers[4].diffuseMap, "textures/tx_none2.dds");
        ASSERT_EQ(result.normalMaps.size(), 2u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_b_nh.dds");
        EXPECT_EQ(result.normalMaps[1], "textures/tx_a_nh.dds");
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_EQ(result.layers[1].normalMapIndex, -1);
        EXPECT_EQ(result.layers[2].normalMapIndex, 1);
        EXPECT_EQ(result.layers[3].normalMapIndex, 0);
        EXPECT_EQ(result.layers[4].normalMapIndex, -1);
    }

    TEST(TerrainLayerMapsTest, emptyPathFindsNothing)
    {
        const std::vector<std::string> paths = { "", "textures/tx_a.dds" };
        const auto result
            = Terrain::findTerrainLayerMaps(paths, settings(true, true), [](std::string_view) { return true; });
        ASSERT_EQ(result.layers.size(), 2u);
        expectPlain(result.layers[0], "");
        // The second layer still finds its maps (exists() says yes to everything, so normal-height wins).
        EXPECT_EQ(result.layers[1].diffuseMap, "textures/tx_a_diffusespec.dds");
        EXPECT_TRUE(result.layers[1].specular);
        EXPECT_EQ(result.layers[1].normalMapIndex, 0);
        EXPECT_TRUE(result.layers[1].parallax);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_nh.dds");
    }

    TEST(TerrainLayerMapsTest, pathWithoutDotFindsNothing)
    {
        const std::vector<std::string> paths = { "textures/tx_a" };
        const auto result
            = Terrain::findTerrainLayerMaps(paths, settings(true, true), [](std::string_view) { return true; });
        ASSERT_EQ(result.layers.size(), 1u);
        expectPlain(result.layers[0], "textures/tx_a");
        EXPECT_TRUE(result.normalMaps.empty());
    }

    TEST(TerrainLayerMapsTest, customPatterns)
    {
        vsgAdapters::AutoMapSettings config = settings(true, true);
        config.normalMapPattern = "_norm";
        config.normalHeightMapPattern = "_height";
        config.specularMapPattern = "_spc";
        const std::vector<std::string> paths = { "textures/tx_a.dds", "textures/tx_b.dds" };
        const std::set<std::string> files = {
            // Default-pattern files are present but must be ignored.
            "textures/tx_a_n.dds",
            "textures/tx_a_nh.dds",
            "textures/tx_a_diffusespec.dds",
            "textures/tx_a_norm.dds",
            "textures/tx_b_height.dds",
            "textures/tx_b_spc.dds",
        };
        const auto result = find(paths, config, files);
        ASSERT_EQ(result.layers.size(), 2u);
        EXPECT_EQ(result.layers[0].diffuseMap, "textures/tx_a.dds");
        EXPECT_FALSE(result.layers[0].specular);
        EXPECT_EQ(result.layers[0].normalMapIndex, 0);
        EXPECT_FALSE(result.layers[0].parallax);
        EXPECT_EQ(result.layers[1].diffuseMap, "textures/tx_b_spc.dds");
        EXPECT_TRUE(result.layers[1].specular);
        EXPECT_EQ(result.layers[1].normalMapIndex, 1);
        EXPECT_TRUE(result.layers[1].parallax);
        ASSERT_EQ(result.normalMaps.size(), 2u);
        EXPECT_EQ(result.normalMaps[0], "textures/tx_a_norm.dds");
        EXPECT_EQ(result.normalMaps[1], "textures/tx_b_height.dds");
    }

    TEST(TerrainLayerMapsTest, lastDotIsReplaced)
    {
        const std::vector<std::string> paths = { "data.dir/tx.a.dds" };
        const std::set<std::string> files = { "data.dir/tx.a_n.dds", "data.dir/tx.a_diffusespec.dds" };
        const auto result = find(paths, settings(true, true), files);
        ASSERT_EQ(result.layers.size(), 1u);
        EXPECT_EQ(result.layers[0].diffuseMap, "data.dir/tx.a_diffusespec.dds");
        EXPECT_TRUE(result.layers[0].specular);
        ASSERT_EQ(result.normalMaps.size(), 1u);
        EXPECT_EQ(result.normalMaps[0], "data.dir/tx.a_n.dds");
    }
}
