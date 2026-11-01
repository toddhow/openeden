// Tests for the tier 3 material map search (task 0027, "Tier 3"): `<diffuse name>_orm` next to the diffuse texture,
// found only with the setting on. Written from the spec alone, without reading the implementation.

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

    TEST(VsgMaterialMapsTest, the_default_pattern_is_the_proposed_suffix)
    {
        const AutoMapSettings settings;
        EXPECT_EQ(settings.materialMapPattern, std::string(Render::Pbr::materialMapSuffix));
        EXPECT_FALSE(settings.autoUseMaterialMaps);
    }

    TEST(VsgMaterialMapsTest, nothing_is_read_with_the_setting_off)
    {
        AutoMapSettings settings;
        settings.autoUseNormalMaps = true;
        settings.autoUseSpecularMaps = true;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings, existing({ "textures/tx_a_orm.dds" }));
        EXPECT_TRUE(maps.materialMap.empty());
    }

    TEST(VsgMaterialMapsTest, the_map_next_to_the_diffuse_texture_is_found)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings, existing({ "textures/tx_a_orm.dds" }));
        EXPECT_EQ(maps.materialMap, "textures/tx_a_orm.dds");
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_TRUE(maps.specularMap.empty());
    }

    TEST(VsgMaterialMapsTest, a_missing_file_or_an_empty_path_finds_nothing)
    {
        AutoMapSettings settings;
        settings.autoUseMaterialMaps = true;
        EXPECT_TRUE(findAutoMaps("textures/tx_a.dds", "", settings, existing({})).materialMap.empty());
        EXPECT_TRUE(findAutoMaps("", "", settings, existing({ "_orm" })).materialMap.empty());
        EXPECT_TRUE(findAutoMaps("noextension", "", settings, existing({ "noextension_orm" })).materialMap.empty());
    }

    TEST(VsgMaterialMapsTest, it_is_independent_of_the_normal_and_specular_search)
    {
        AutoMapSettings settings;
        settings.autoUseNormalMaps = true;
        settings.autoUseSpecularMaps = true;
        settings.autoUseMaterialMaps = true;
        const auto maps = findAutoMaps("textures/tx_a.dds", "", settings,
            existing({ "textures/tx_a_n.dds", "textures/tx_a_spec.dds", "textures/tx_a_orm.dds" }));
        EXPECT_EQ(maps.normalMap, "textures/tx_a_n.dds");
        EXPECT_EQ(maps.specularMap, "textures/tx_a_spec.dds");
        EXPECT_EQ(maps.materialMap, "textures/tx_a_orm.dds");
    }
}
