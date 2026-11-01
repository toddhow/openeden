// Tests for the terrain layers' `_rmaos` search (task 0044, D4). Written from the spec before the implementation.

#include <components/terrain/layermaps.hpp>
#include <components/vsgadapters/nif/automaps.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{
    using Render::Pbr::MapLayout;

    std::function<bool(std::string_view)> existsIn(std::set<std::string> files)
    {
        return [files](std::string_view path) { return files.count(std::string(path)) != 0; };
    }

    vsgAdapters::AutoMapSettings withMaterial()
    {
        vsgAdapters::AutoMapSettings s;
        s.autoUseMaterialMaps = true;
        return s;
    }

    TEST(TerrainRmaosTest, a_layer_with_an_rmaos_file_gets_it_and_the_layout)
    {
        const auto maps = Terrain::findTerrainLayerMaps(
            { "textures/a.dds", "textures/b.dds" }, withMaterial(), existsIn({ "textures/a_rmaos.dds" }));
        ASSERT_EQ(maps.layers.size(), 2u);
        ASSERT_EQ(maps.materialMaps.size(), 1u);
        EXPECT_EQ(maps.materialMaps[0], "textures/a_rmaos.dds");
        EXPECT_EQ(maps.layers[0].materialMapIndex, 0);
        EXPECT_EQ(maps.layers[0].materialLayout, MapLayout::Rmaos);
        EXPECT_EQ(maps.layers[1].materialMapIndex, -1);
    }

    TEST(TerrainRmaosTest, rmaos_wins_over_orm_and_orm_is_still_found)
    {
        const auto maps = Terrain::findTerrainLayerMaps({ "textures/a.dds", "textures/b.dds" }, withMaterial(),
            existsIn({ "textures/a_rmaos.dds", "textures/a_orm.dds", "textures/b_orm.dds" }));
        ASSERT_EQ(maps.layers.size(), 2u);
        EXPECT_EQ(maps.materialMaps[maps.layers[0].materialMapIndex], "textures/a_rmaos.dds");
        EXPECT_EQ(maps.layers[0].materialLayout, MapLayout::Rmaos);
        EXPECT_EQ(maps.materialMaps[maps.layers[1].materialMapIndex], "textures/b_orm.dds");
        EXPECT_EQ(maps.layers[1].materialLayout, MapLayout::Orm);
    }

    TEST(TerrainRmaosTest, a_file_shared_by_layers_is_listed_once)
    {
        const auto maps = Terrain::findTerrainLayerMaps(
            { "textures/a.dds", "textures/a.dds" }, withMaterial(), existsIn({ "textures/a_rmaos.dds" }));
        EXPECT_EQ(maps.materialMaps.size(), 1u);
        EXPECT_EQ(maps.layers[0].materialMapIndex, maps.layers[1].materialMapIndex);
    }

    TEST(TerrainRmaosTest, nothing_is_searched_with_the_setting_off_or_for_an_empty_path)
    {
        vsgAdapters::AutoMapSettings off;
        const auto none
            = Terrain::findTerrainLayerMaps({ "textures/a.dds" }, off, existsIn({ "textures/a_rmaos.dds" }));
        EXPECT_TRUE(none.materialMaps.empty());
        EXPECT_EQ(none.layers[0].materialMapIndex, -1);
        const auto empty = Terrain::findTerrainLayerMaps({ "" }, withMaterial(), existsIn({ "_rmaos" }));
        EXPECT_TRUE(empty.materialMaps.empty());
    }

    TEST(TerrainRmaosTest, the_material_search_does_not_change_the_normal_search)
    {
        auto s = withMaterial();
        s.autoUseNormalMaps = true;
        const auto maps = Terrain::findTerrainLayerMaps(
            { "textures/a.dds" }, s, existsIn({ "textures/a_n.dds", "textures/a_rmaos.dds" }));
        ASSERT_EQ(maps.normalMaps.size(), 1u);
        EXPECT_EQ(maps.layers[0].normalMapIndex, 0);
        EXPECT_EQ(maps.layers[0].diffuseMap, "textures/a.dds");
        EXPECT_EQ(maps.layers[0].materialMapIndex, 0);
    }

    TEST(TerrainRmaosTest, the_settings_say_the_maps_are_wanted_with_the_material_maps_alone)
    {
        Terrain::TerrainMapSettings s;
        s.exists = [](std::string_view) { return true; };
        EXPECT_FALSE(s.enabled());
        s.autoMaps.autoUseMaterialMaps = true;
        EXPECT_TRUE(s.enabled());
    }

    TEST(TerrainRmaosTest, the_layer_flags_pack_the_three_indices_and_the_bits)
    {
        using namespace Terrain;
        const uint32_t flags = packLayerFlags(5, true, false, 7, MapLayout::Rmaos);
        EXPECT_EQ(flags & layerNormalMask, 5u);
        EXPECT_NE(flags & layerParallaxFlag, 0u);
        EXPECT_EQ(flags & layerSpecularFlag, 0u);
        EXPECT_EQ((flags >> layerMaterialShift) & layerMaterialMask, 7u);
        EXPECT_NE(flags & layerRmaosFlag, 0u);

        const uint32_t none = packLayerFlags(-1, false, true, -1, MapLayout::Orm);
        EXPECT_EQ(none & layerNormalMask, layerNormalMask); // "no normal map"
        EXPECT_NE(none & layerSpecularFlag, 0u);
        EXPECT_EQ((none >> layerMaterialShift) & layerMaterialMask, layerMaterialMask); // "no material map"
        EXPECT_EQ(none & layerRmaosFlag, 0u);
    }

    TEST(TerrainRmaosTest, a_layer_without_a_normal_map_never_keeps_the_parallax_bit)
    {
        using namespace Terrain;
        EXPECT_EQ(packLayerFlags(-1, true, false, -1, MapLayout::Orm) & layerParallaxFlag, 0u);
    }
}
