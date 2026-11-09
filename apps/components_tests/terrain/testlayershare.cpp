// Task 0080 (stage d, fourth slice): when the LOD terrain's Builder may use the live Builder's layer arrays instead of
// building a second set. The arrays hold the layers' diffuse, normal, material and height files and their per-layer
// tables, so they are interchangeable exactly when the layer list and everything that decides which files are found
// and how they are read are equal.

#include <components/terrain/layermaps.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using Terrain::TerrainMapSettings;

    TerrainMapSettings mapsOn()
    {
        TerrainMapSettings settings;
        settings.exists = [](std::string_view) { return false; };
        settings.autoMaps.autoUseNormalMaps = true;
        return settings;
    }

    const std::vector<std::string> layers{ "textures/a.dds", "textures/b.dds" };

    TEST(TerrainLayerShare, equal_inputs_share)
    {
        EXPECT_TRUE(Terrain::layerArraysShareable(layers, mapsOn(), layers, mapsOn()));
    }

    TEST(TerrainLayerShare, equal_inputs_without_maps_share)
    {
        EXPECT_TRUE(Terrain::layerArraysShareable(layers, TerrainMapSettings{}, layers, TerrainMapSettings{}));
    }

    TEST(TerrainLayerShare, another_layer_list_does_not_share)
    {
        std::vector<std::string> other = layers;
        other[1] = "textures/c.dds";
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), other, mapsOn()));
    }

    TEST(TerrainLayerShare, the_same_layers_in_another_order_do_not_share)
    {
        const std::vector<std::string> reversed{ layers[1], layers[0] };
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), reversed, mapsOn()));
    }

    TEST(TerrainLayerShare, a_shorter_layer_list_does_not_share)
    {
        const std::vector<std::string> shorter{ layers[0] };
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), shorter, mapsOn()));
    }

    TEST(TerrainLayerShare, maps_on_against_maps_off_do_not_share)
    {
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, TerrainMapSettings{}));
    }

    TEST(TerrainLayerShare, pbr_decides)
    {
        TerrainMapSettings other = mapsOn();
        other.pbr = true;
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, other));
    }

    TEST(TerrainLayerShare, parallax_quality_and_scale_decide)
    {
        TerrainMapSettings quality = mapsOn();
        quality.parallaxQuality = 2;
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, quality));
        TerrainMapSettings scale = mapsOn();
        scale.parallaxScale = 2.f;
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, scale));
    }

    TEST(TerrainLayerShare, the_map_patterns_decide)
    {
        TerrainMapSettings pattern = mapsOn();
        pattern.autoMaps.normalMapPattern = "_normal";
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, pattern));
        TerrainMapSettings height = mapsOn();
        height.autoMaps.autoUseHeightMaps = true;
        EXPECT_FALSE(Terrain::layerArraysShareable(layers, mapsOn(), layers, height));
    }

    TEST(TerrainLayerShare, settings_that_do_not_touch_the_arrays_do_not_matter)
    {
        // The pipeline's own options (wetness, cover, point shadows, G-buffer outputs) and the exists callback's
        // identity change the shader, not the arrays.
        TerrainMapSettings other = mapsOn();
        other.pointShadow = true;
        other.aoTerrain = true;
        other.giSky = true;
        other.gbufferOutputs = 3;
        other.wetness.enabled = true;
        other.cover.enabled = true;
        EXPECT_TRUE(Terrain::layerArraysShareable(layers, mapsOn(), layers, other));
    }

    TEST(TerrainLayerShare, symmetric)
    {
        TerrainMapSettings other = mapsOn();
        other.pbr = true;
        EXPECT_EQ(Terrain::layerArraysShareable(layers, mapsOn(), layers, other),
            Terrain::layerArraysShareable(layers, other, layers, mapsOn()));
    }

    TEST(TerrainLayerShare, adoption_never_waits_for_a_source_that_is_still_building)
    {
        EXPECT_FALSE(Terrain::mayAdoptTiles(true, true, false));
        EXPECT_TRUE(Terrain::mayAdoptTiles(true, true, true));
    }

    TEST(TerrainLayerShare, adoption_without_sharing_ignores_the_source)
    {
        EXPECT_TRUE(Terrain::mayAdoptTiles(true, false, false));
        EXPECT_TRUE(Terrain::mayAdoptTiles(true, false, true));
    }

    TEST(TerrainLayerShare, adoption_needs_the_tiles)
    {
        EXPECT_FALSE(Terrain::mayAdoptTiles(false, false, true));
        EXPECT_FALSE(Terrain::mayAdoptTiles(false, true, true));
    }
}
