// Tests for the data side of parallax height maps (task 0045): the `_p` search for objects and terrain layers, the
// `displacementScale` key of the material files, and a generated BC4 DDS. Written from the spec
// (docs/tasks/0045-rendering-parallax-height-maps.md) before the implementation. No GPU, window or game data.

#include <components/render/materialoverrides.hpp>
#include <components/render/parallaxmath.hpp>
#include <components/terrain/layermaps.hpp>
#include <components/vsgadapters/nif/automaps.hpp>

#include <vsg/core/Data.h>
#include <vsg/io/Options.h>
#include <vsg/io/mem_stream.h>
#include <vsgXchange/images.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Pbr;

    std::function<bool(std::string_view)> existsIn(std::set<std::string> files)
    {
        return [files](std::string_view path) { return files.count(std::string(path)) != 0; };
    }

    TEST(RenderParallaxDataTest, the_height_suffix_is_the_true_pbr_one)
    {
        EXPECT_EQ(std::string(Render::Parallax::heightMapSuffix), "_p");
        const vsgAdapters::AutoMapSettings settings;
        EXPECT_FALSE(settings.autoUseHeightMaps);
        EXPECT_EQ(settings.heightMapPattern, "_p");
    }

    TEST(RenderParallaxDataTest, an_object_height_file_is_found_only_when_asked_for)
    {
        const auto exists = existsIn({ "textures/tx_a_p.dds", "textures/tx_a_n.dds" });
        vsgAdapters::AutoMapSettings settings;
        settings.autoUseNormalMaps = true;
        EXPECT_TRUE(vsgAdapters::findAutoMaps("textures/tx_a.dds", "", settings, exists).heightMap.empty());
        settings.autoUseHeightMaps = true;
        EXPECT_EQ(
            vsgAdapters::findAutoMaps("textures/tx_a.dds", "", settings, exists).heightMap, "textures/tx_a_p.dds");
        // No file, nothing.
        EXPECT_TRUE(vsgAdapters::findAutoMaps("textures/tx_b.dds", "", settings, exists).heightMap.empty());
        // The normal map is found as before.
        EXPECT_EQ(
            vsgAdapters::findAutoMaps("textures/tx_a.dds", "", settings, exists).normalMap, "textures/tx_a_n.dds");
    }

    TEST(RenderParallaxDataTest, a_height_file_alone_does_not_need_the_other_settings)
    {
        vsgAdapters::AutoMapSettings settings;
        settings.autoUseHeightMaps = true;
        const auto maps
            = vsgAdapters::findAutoMaps("textures/tx_a.dds", "", settings, existsIn({ "textures/tx_a_p.dds" }));
        EXPECT_EQ(maps.heightMap, "textures/tx_a_p.dds");
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_TRUE(maps.materialMap.empty());
    }

    TEST(RenderParallaxDataTest, a_height_file_pulls_in_its_normal_map_for_the_tangent_frame)
    {
        const auto exists = existsIn({ "textures/tx_a_p.dds", "textures/tx_a_n.dds", "textures/tx_b_n.dds" });
        vsgAdapters::AutoMapSettings settings;
        settings.autoUseHeightMaps = true; // normal maps are not asked for
        EXPECT_EQ(
            vsgAdapters::findAutoMaps("textures/tx_a.dds", "", settings, exists).normalMap, "textures/tx_a_n.dds");
        // Without a height file the old rule holds: no normal map is looked for.
        EXPECT_TRUE(vsgAdapters::findAutoMaps("textures/tx_b.dds", "", settings, exists).normalMap.empty());
        // The model's own bump texture is not taken for it.
        EXPECT_TRUE(
            vsgAdapters::findAutoMaps("textures/tx_a.dds", "textures/tx_a_n.dds", settings, exists).normalMap.empty());
    }

    TEST(RenderParallaxDataTest, terrain_layers_list_each_height_file_once_and_the_others_have_minus_one)
    {
        vsgAdapters::AutoMapSettings settings;
        settings.autoUseHeightMaps = true;
        const auto maps = Terrain::findTerrainLayerMaps(
            { "textures/a.dds", "textures/b.dds", "textures/a.dds" }, settings, existsIn({ "textures/a_p.dds" }));
        ASSERT_EQ(maps.layers.size(), 3u);
        ASSERT_EQ(maps.heightMaps.size(), 1u);
        EXPECT_EQ(maps.heightMaps[0], "textures/a_p.dds");
        EXPECT_EQ(maps.layers[0].heightMapIndex, 0);
        EXPECT_EQ(maps.layers[1].heightMapIndex, -1);
        EXPECT_EQ(maps.layers[2].heightMapIndex, 0);
    }

    TEST(RenderParallaxDataTest, terrain_layers_look_for_no_height_file_when_not_asked)
    {
        const vsgAdapters::AutoMapSettings settings;
        const auto maps
            = Terrain::findTerrainLayerMaps({ "textures/a.dds" }, settings, existsIn({ "textures/a_p.dds" }));
        EXPECT_TRUE(maps.heightMaps.empty());
        EXPECT_EQ(maps.layers[0].heightMapIndex, -1);
    }

    TEST(RenderParallaxDataTest, the_terrain_settings_ask_for_maps_when_only_heights_are_wanted)
    {
        Terrain::TerrainMapSettings s;
        s.exists = existsIn({});
        EXPECT_FALSE(s.enabled());
        s.autoMaps.autoUseHeightMaps = true;
        EXPECT_TRUE(s.enabled());
    }

    const std::string displacementSample = R"({
  "format": "openeden.material",
  "version": 1,
  "materials": [
    {
      "match": {
        "texture": "textures/tx_wall.dds"
      },
      "roughnessScale": 1.5,
      "displacementScale": 0.5
    },
    {
      "match": {
        "model": "meshes/x/test.nif"
      },
      "displacementScale": 2
    }
  ]
}
)";

    TEST(RenderParallaxDataTest, the_displacement_key_parses_and_writes_back_byte_for_byte)
    {
        OverrideError error;
        const auto set = parseOverrides(displacementSample, "t.json", error);
        ASSERT_TRUE(set.has_value()) << error.describe();
        ASSERT_EQ(set->entries.size(), 2u);
        EXPECT_DOUBLE_EQ(*set->entries[0].values.displacementScale, 0.5);
        EXPECT_DOUBLE_EQ(*set->entries[1].values.displacementScale, 2.0);
        EXPECT_FALSE(set->entries[1].values.roughnessScale.has_value());
        EXPECT_EQ(writeCanonical(*set), displacementSample);
    }

    TEST(RenderParallaxDataTest, a_bad_displacement_gets_a_located_error)
    {
        const auto bad = [](const std::string& entry) {
            const std::string text
                = R"({"format": "openeden.material", "version": 1, "materials": [{"match": {"texture": "a.dds"}, )"
                + entry + "}]}";
            OverrideError error;
            const auto set = parseOverrides(text, "t.json", error);
            EXPECT_FALSE(set.has_value()) << entry;
            return error;
        };
        EXPECT_EQ(bad(R"("displacementScale": 0)").field, "materials[0].displacementScale");
        EXPECT_EQ(bad(R"("displacementScale": -1)").field, "materials[0].displacementScale");
        EXPECT_EQ(bad(R"("displacementScale": 9)").field, "materials[0].displacementScale");
        EXPECT_EQ(bad(R"("displacementScale": "x")").field, "materials[0].displacementScale");
    }

    TEST(RenderParallaxDataTest, the_displacement_merges_like_the_other_values)
    {
        OverrideSet models, textures;
        OverrideEntry m;
        m.kind = OverrideEntry::Kind::Model;
        m.path = "meshes/a.nif";
        m.values.displacementScale = 2.0;
        models.entries.push_back(m);
        OverrideEntry t;
        t.kind = OverrideEntry::Kind::Texture;
        t.path = "textures/b.dds";
        t.values.displacementScale = 1.0;
        textures.entries.push_back(t);
        OverrideTable table;
        table.add(models);
        table.add(textures);
        EXPECT_DOUBLE_EQ(*table.resolveValues("meshes/a.nif", "").displacementScale, 2.0);
        EXPECT_DOUBLE_EQ(*table.resolveValues("meshes/a.nif", "textures/b.dds").displacementScale, 1.0);
        EXPECT_FALSE(table.resolveValues("meshes/other.nif", "").displacementScale.has_value());
    }

    // A 4 by 4 DDS holding one BC4 block (8 bytes), with a DX10 header or the legacy `BC4U` code.
    std::vector<uint8_t> makeBc4(bool dx10)
    {
        std::vector<uint8_t> bytes(4 + 124 + (dx10 ? 20 : 0) + 8, 0);
        const auto put = [&](size_t at, uint32_t v) { std::memcpy(&bytes[at], &v, 4); };
        std::memcpy(&bytes[0], "DDS ", 4);
        put(4, 124);
        put(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);
        put(12, 4);
        put(16, 4);
        put(20, 8);
        put(28, 1);
        put(76, 32);
        put(80, 0x4);
        std::memcpy(&bytes[84], dx10 ? "DX10" : "BC4U", 4);
        put(108, 0x1000);
        if (dx10)
        {
            put(128, 80); // DXGI_FORMAT_BC4_UNORM
            put(132, 3);
            put(140, 1);
        }
        return bytes;
    }

    vsg::ref_ptr<vsg::Data> readBytes(const std::vector<uint8_t>& bytes)
    {
        vsg::mem_stream in(bytes.data(), bytes.size());
        auto options = vsg::Options::create();
        options->extensionHint = ".dds";
        vsgXchange::dds reader;
        auto object = reader.read(in, options);
        return vsg::ref_ptr<vsg::Data>(object ? object.cast<vsg::Data>() : nullptr);
    }

    TEST(RenderParallaxDataTest, a_generated_bc4_file_reads_as_a_one_channel_block_format_with_either_header)
    {
        for (bool dx10 : { true, false })
        {
            auto data = readBytes(makeBc4(dx10));
            ASSERT_TRUE(data.valid()) << dx10;
            EXPECT_EQ(data->properties.format, VK_FORMAT_BC4_UNORM_BLOCK) << dx10;
        }
    }
}
