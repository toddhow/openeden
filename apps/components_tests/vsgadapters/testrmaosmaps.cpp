// Tests for the `_rmaos` search of the tier 3 material map (task 0044, D1). Written from the spec before the
// implementation.

#include <components/render/materialmath.hpp>
#include <components/vsgadapters/nif/automaps.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <string>

namespace
{
    using namespace vsgAdapters;

    std::function<bool(std::string_view)> existing(std::set<std::string> files)
    {
        return [files](std::string_view path) { return files.count(std::string(path)) > 0; };
    }

    TEST(VsgRmaosMapsTest, the_default_rmaos_pattern_is_the_named_constant)
    {
        const AutoMapSettings settings;
        EXPECT_EQ(settings.rmaosMapPattern, std::string(Render::Pbr::rmaosMapSuffix));
        // The orm pattern is untouched.
        EXPECT_EQ(settings.materialMapPattern, std::string(Render::Pbr::materialMapSuffix));
    }

    TEST(VsgRmaosMapsTest, an_rmaos_file_is_found_with_its_layout)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings, existing({ "textures/tx_a_rmaos.dds" }));
        EXPECT_EQ(maps.materialMap, "textures/tx_a_rmaos.dds");
        EXPECT_EQ(maps.materialLayout, Render::Pbr::MapLayout::Rmaos);
    }

    TEST(VsgRmaosMapsTest, rmaos_wins_over_orm_when_both_exist)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps(
            "textures/tx_a.dds", "", settings, existing({ "textures/tx_a_orm.dds", "textures/tx_a_rmaos.dds" }));
        EXPECT_EQ(maps.materialMap, "textures/tx_a_rmaos.dds");
        EXPECT_EQ(maps.materialLayout, Render::Pbr::MapLayout::Rmaos);
    }

    TEST(VsgRmaosMapsTest, orm_is_still_found_with_the_orm_layout)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings, existing({ "textures/tx_a_orm.dds" }));
        EXPECT_EQ(maps.materialMap, "textures/tx_a_orm.dds");
        EXPECT_EQ(maps.materialLayout, Render::Pbr::MapLayout::Orm);
    }

    TEST(VsgRmaosMapsTest, nothing_is_read_with_the_setting_off)
    {
        AutoMapSettings settings;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings, existing({ "textures/tx_a_rmaos.dds" }));
        EXPECT_TRUE(maps.materialMap.empty());
    }

    TEST(VsgRmaosMapsTest, rmaos_is_found_beside_the_normal_map_without_touching_it)
    {
        AutoMapSettings settings;
        settings.autoUseNormalMaps = true;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps(
            "textures/tx_a.dds", "", settings, existing({ "textures/tx_a_n.dds", "textures/tx_a_rmaos.dds" }));
        EXPECT_EQ(maps.normalMap, "textures/tx_a_n.dds");
        EXPECT_EQ(maps.materialMap, "textures/tx_a_rmaos.dds");
    }

    TEST(VsgRmaosMapsTest, a_path_with_no_dot_finds_nothing)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        EXPECT_TRUE(findAutoMaps("noext", "", settings, existing({ "noext_rmaos" })).materialMap.empty());
    }
}
