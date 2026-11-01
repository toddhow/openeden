// Tests for Render::Pbr's material override files and table (task 0027, tier 2).
//
// Written from the spec alone, without reading the implementation.

#include <components/render/materialoverrides.hpp>

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace Render::Pbr;

    // The canonical form: two-space indent, fixed key order, LF, a final newline.
    const std::string canonicalSample = R"({
  "format": "openeden.material",
  "version": 1,
  "materials": [
    {
      "match": {
        "texture": "textures/tx_example.dds"
      },
      "roughness": 0.4,
      "metalness": 1,
      "reflectance": 0.5,
      "occlusion": 0.75,
      "subsurface": false
    },
    {
      "match": {
        "model": "meshes/x/test.nif"
      },
      "subsurface": true
    }
  ]
}
)";

    std::optional<OverrideSet> parse(const std::string& text, OverrideError& error)
    {
        return parseOverrides(text, "test.json", error);
    }

    TEST(RenderMaterialOverridesTest, parses_the_canonical_sample)
    {
        OverrideError error;
        const auto set = parse(canonicalSample, error);
        ASSERT_TRUE(set.has_value()) << error.describe();
        ASSERT_EQ(set->entries.size(), 2u);
        const auto& a = set->entries[0];
        EXPECT_EQ(a.kind, OverrideEntry::Kind::Texture);
        EXPECT_EQ(a.path, "textures/tx_example.dds");
        EXPECT_DOUBLE_EQ(*a.values.roughness, 0.4);
        EXPECT_DOUBLE_EQ(*a.values.metalness, 1.0);
        EXPECT_DOUBLE_EQ(*a.values.reflectance, 0.5);
        EXPECT_DOUBLE_EQ(*a.values.occlusion, 0.75);
        EXPECT_FALSE(*a.values.subsurface);
        const auto& b = set->entries[1];
        EXPECT_EQ(b.kind, OverrideEntry::Kind::Model);
        EXPECT_FALSE(b.values.roughness.has_value());
        EXPECT_TRUE(*b.values.subsurface);
    }

    TEST(RenderMaterialOverridesTest, canonical_text_round_trips_byte_for_byte)
    {
        OverrideError error;
        const auto set = parse(canonicalSample, error);
        ASSERT_TRUE(set.has_value());
        EXPECT_EQ(writeCanonical(*set), canonicalSample);
        const auto again = parse(writeCanonical(*set), error);
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(writeCanonical(*again), canonicalSample);
    }

    TEST(RenderMaterialOverridesTest, non_canonical_input_is_written_canonically)
    {
        const std::string messy
            = R"({"version":1,"format":"openeden.material","materials":[{"subsurface":true,"roughness":0.4,"match":{"model":"a.nif"}}]})";
        OverrideError error;
        const auto set = parse(messy, error);
        ASSERT_TRUE(set.has_value()) << error.describe();
        const std::string written = writeCanonical(*set);
        EXPECT_EQ(written.find('\r'), std::string::npos);
        EXPECT_EQ(written.back(), '\n');
        EXPECT_LT(written.find("\"format\""), written.find("\"version\""));
        EXPECT_LT(written.find("\"match\""), written.find("\"roughness\""));
        EXPECT_LT(written.find("\"roughness\""), written.find("\"subsurface\""));
        // Writing again changes nothing.
        const auto reparsed = parse(written, error);
        ASSERT_TRUE(reparsed.has_value());
        EXPECT_EQ(writeCanonical(*reparsed), written);
    }

    TEST(RenderMaterialOverridesTest, awkward_numbers_round_trip)
    {
        OverrideSet set;
        OverrideEntry entry;
        entry.kind = OverrideEntry::Kind::Texture;
        entry.path = "textures/a.dds";
        entry.values.roughness = 0.1;
        entry.values.metalness = 1.0 / 3.0;
        entry.values.occlusion = 0.0;
        set.entries.push_back(entry);
        const std::string text = writeCanonical(set);
        OverrideError error;
        const auto back = parse(text, error);
        ASSERT_TRUE(back.has_value()) << error.describe();
        EXPECT_DOUBLE_EQ(*back->entries[0].values.roughness, 0.1);
        EXPECT_DOUBLE_EQ(*back->entries[0].values.metalness, 1.0 / 3.0);
        EXPECT_EQ(writeCanonical(*back), text);
    }

    // ---- errors, all located

    void expectError(const std::string& text, const std::string& fieldPart, int line = -1)
    {
        OverrideError error;
        const auto set = parseOverrides(text, "bad.json", error);
        EXPECT_FALSE(set.has_value()) << text;
        EXPECT_EQ(error.file, "bad.json");
        EXPECT_NE(error.field.find(fieldPart), std::string::npos)
            << "field was '" << error.field << "', wanted '" << fieldPart << "'";
        EXPECT_FALSE(error.message.empty());
        if (line >= 0)
        {
            EXPECT_EQ(error.line, line);
        }
        EXPECT_NE(error.describe().find("bad.json"), std::string::npos);
    }

    TEST(RenderMaterialOverridesTest, malformed_json_is_located)
    {
        OverrideError error;
        EXPECT_FALSE(parseOverrides("{\n  \"format\": \n", "bad.json", error).has_value());
        EXPECT_EQ(error.file, "bad.json");
        EXPECT_GE(error.line, 1);
        EXPECT_FALSE(error.message.empty());
    }

    TEST(RenderMaterialOverridesTest, wrong_format_and_version)
    {
        expectError(R"({"format":"other","version":1,"materials":[]})", "format");
        expectError(R"({"format":"openeden.material","version":2,"materials":[]})", "version");
        expectError(R"({"format":"openeden.material","materials":[]})", "version");
        expectError(R"({"format":"openeden.material","version":1})", "materials");
    }

    TEST(RenderMaterialOverridesTest, entries_need_exactly_one_match_key_and_a_value)
    {
        const std::string head = R"({"format":"openeden.material","version":1,"materials":[)";
        expectError(head + R"({"roughness":0.5}]})", "match");
        expectError(head + R"({"match":{},"roughness":0.5}]})", "match");
        expectError(head + R"({"match":{"texture":"a","model":"b"},"roughness":0.5}]})", "match");
        expectError(head + R"({"match":{"texture":"a"}}]})", "materials[0]");
        expectError(head + R"({"match":{"texture":""},"roughness":0.5}]})", "match");
    }

    TEST(RenderMaterialOverridesTest, values_are_range_checked)
    {
        const std::string head = R"({"format":"openeden.material","version":1,"materials":[{"match":{"texture":"a"},)";
        expectError(head + R"("roughness":0.01}]})", "roughness");
        expectError(head + R"("roughness":1.5}]})", "roughness");
        expectError(head + R"("metalness":-0.1}]})", "metalness");
        expectError(head + R"("reflectance":2}]})", "reflectance");
        expectError(head + R"("occlusion":"x"}]})", "occlusion");
        expectError(head + R"("subsurface":1}]})", "subsurface");
    }

    TEST(RenderMaterialOverridesTest, unknown_keys_are_refused)
    {
        expectError(R"({"format":"openeden.material","version":1,"materials":[],"extra":1})", "extra");
        expectError(
            R"({"format":"openeden.material","version":1,"materials":[{"match":{"texture":"a"},"roughness":0.5,"gloss":1}]})",
            "gloss");
    }

    TEST(RenderMaterialOverridesTest, error_line_points_at_the_offending_key)
    {
        const std::string text
            = "{\n"
              "  \"format\": \"openeden.material\",\n"
              "  \"version\": 1,\n"
              "  \"materials\": [\n"
              "    {\n"
              "      \"match\": { \"texture\": \"a\" },\n"
              "      \"roughness\": 7\n"
              "    }\n"
              "  ]\n"
              "}\n";
        expectError(text, "roughness", 7);
    }

    // ---- paths and the table

    TEST(RenderMaterialOverridesTest, normalizePath_lowercases_and_uses_forward_slashes)
    {
        EXPECT_EQ(normalizePath("Textures\\TX_Example.DDS"), "textures/tx_example.dds");
        EXPECT_EQ(normalizePath("meshes/a/b.nif"), "meshes/a/b.nif");
        EXPECT_EQ(normalizePath(""), "");
    }

    OverrideEntry entry(OverrideEntry::Kind kind, const std::string& path)
    {
        OverrideEntry e;
        e.kind = kind;
        e.path = path;
        return e;
    }

    TEST(RenderMaterialOverridesTest, empty_table_changes_nothing)
    {
        OverrideTable table;
        EXPECT_TRUE(table.empty());
        Surface base;
        base.roughness = 0.33f;
        const Surface out = table.resolve("meshes/a.nif", "textures/a.dds", base);
        EXPECT_FLOAT_EQ(out.roughness, 0.33f);
        EXPECT_FLOAT_EQ(out.metalness, 0.f);
        EXPECT_FALSE(out.subsurface);
    }

    TEST(RenderMaterialOverridesTest, a_texture_entry_applies_to_its_texture_only)
    {
        OverrideSet set;
        auto e = entry(OverrideEntry::Kind::Texture, "textures/Gold.dds");
        e.values.metalness = 1.0;
        e.values.roughness = 0.2;
        set.entries.push_back(e);
        OverrideTable table;
        table.add(set);
        EXPECT_FALSE(table.empty());
        const Surface hit = table.resolve("meshes/a.nif", "TEXTURES\\gold.dds", Surface{});
        EXPECT_FLOAT_EQ(hit.metalness, 1.f);
        EXPECT_NEAR(hit.roughness, 0.2f, 1e-6f);
        EXPECT_FLOAT_EQ(hit.reflectance, 0.5f); // not set: falls through to the base
        const Surface miss = table.resolve("meshes/a.nif", "textures/silver.dds", Surface{});
        EXPECT_FLOAT_EQ(miss.metalness, 0.f);
    }

    TEST(RenderMaterialOverridesTest, a_model_entry_applies_to_every_texture_of_the_model)
    {
        OverrideSet set;
        auto e = entry(OverrideEntry::Kind::Model, "meshes/Props/Anvil.nif");
        e.values.metalness = 0.9;
        set.entries.push_back(e);
        OverrideTable table;
        table.add(set);
        EXPECT_NEAR(table.resolve("meshes/props/anvil.nif", "textures/a.dds", Surface{}).metalness, 0.9f, 1e-6f);
        EXPECT_NEAR(table.resolve("meshes/props/anvil.nif", "textures/b.dds", Surface{}).metalness, 0.9f, 1e-6f);
        EXPECT_FLOAT_EQ(table.resolve("meshes/props/other.nif", "textures/a.dds", Surface{}).metalness, 0.f);
    }

    TEST(RenderMaterialOverridesTest, texture_beats_model_and_later_beats_earlier)
    {
        OverrideSet set;
        auto model = entry(OverrideEntry::Kind::Model, "meshes/a.nif");
        model.values.roughness = 0.9;
        model.values.metalness = 0.5;
        auto texture = entry(OverrideEntry::Kind::Texture, "textures/a.dds");
        texture.values.roughness = 0.1;
        // The texture entry comes first in the file, the model entry after it: the texture still wins.
        set.entries = { texture, model };
        OverrideTable table;
        table.add(set);
        const Surface s = table.resolve("meshes/a.nif", "textures/a.dds", Surface{});
        EXPECT_NEAR(s.roughness, 0.1f, 1e-6f);
        EXPECT_NEAR(s.metalness, 0.5f, 1e-6f); // from the model: the texture did not set it

        OverrideSet second;
        auto later = entry(OverrideEntry::Kind::Texture, "textures/a.dds");
        later.values.roughness = 0.7;
        second.entries = { later };
        table.add(second);
        EXPECT_NEAR(table.resolve("meshes/a.nif", "textures/a.dds", Surface{}).roughness, 0.7f, 1e-6f);
    }

    TEST(RenderMaterialOverridesTest, markSubsurfaceModel_is_the_lowest_layer)
    {
        OverrideTable table;
        table.markSubsurfaceModel("Meshes\\B\\Head.nif");
        EXPECT_FALSE(table.empty());
        EXPECT_TRUE(table.resolve("meshes/b/head.nif", "textures/skin.dds", Surface{}).subsurface);
        EXPECT_FALSE(table.resolve("meshes/b/hand.nif", "textures/skin.dds", Surface{}).subsurface);

        OverrideSet set;
        auto e = entry(OverrideEntry::Kind::Model, "meshes/b/head.nif");
        e.values.subsurface = false;
        set.entries.push_back(e);
        table.add(set);
        EXPECT_FALSE(table.resolve("meshes/b/head.nif", "textures/skin.dds", Surface{}).subsurface);
    }

    TEST(RenderMaterialOverridesTest, resolve_clamps_its_result)
    {
        OverrideTable table;
        Surface base;
        base.roughness = 5.f;
        base.metalness = -2.f;
        const Surface s = table.resolve("m", "t", base);
        EXPECT_FLOAT_EQ(s.roughness, 1.f);
        EXPECT_FLOAT_EQ(s.metalness, 0.f);
    }

    TEST(RenderMaterialOverridesTest, marking_and_resolving_from_two_threads_is_safe)
    {
        OverrideTable table;
        std::thread writer([&table] {
            for (int i = 0; i < 2000; ++i)
                table.markSubsurfaceModel("meshes/part" + std::to_string(i % 50) + ".nif");
        });
        std::thread reader([&table] {
            for (int i = 0; i < 2000; ++i)
                table.resolve("meshes/part" + std::to_string(i % 50) + ".nif", "textures/a.dds", Surface{});
        });
        writer.join();
        reader.join();
        EXPECT_TRUE(table.resolve("meshes/part3.nif", "t", Surface{}).subsurface);
    }

    TEST(RenderMaterialOverridesTest, odd_top_levels_and_versions_are_errors_not_crashes)
    {
        OverrideError error;
        EXPECT_FALSE(parseOverrides("[]", "bad.json", error).has_value());
        EXPECT_EQ(error.file, "bad.json");
        EXPECT_FALSE(
            parseOverrides(R"({"format":"openeden.material","version":4294967297,"materials":[]})", "bad.json", error)
                .has_value());
        EXPECT_NE(error.field.find("version"), std::string::npos);
        EXPECT_FALSE(parseOverrides(R"({"format":"openeden.material","version":1.5,"materials":[]})", "bad.json", error)
                         .has_value());
        EXPECT_FALSE(parseOverrides(R"({"format":"openeden.material","version":1,"materials":[3]})", "bad.json", error)
                         .has_value());
        EXPECT_FALSE(parseOverrides(R"({"format":5,"version":1,"materials":[]})", "bad.json", error).has_value());
    }

    TEST(RenderMaterialOverridesTest, resolveValues_leaves_unset_values_unset_and_strips_folders)
    {
        OverrideSet set;
        auto e = entry(OverrideEntry::Kind::Texture, "textures/a.dds");
        e.values.metalness = 0.5;
        set.entries.push_back(e);
        OverrideTable table;
        table.add(set);
        const auto values = table.resolveValues("meshes/x.nif", "a.dds"); // the folder is optional on both sides
        EXPECT_FALSE(values.roughness.has_value());
        EXPECT_FALSE(values.subsurface.has_value());
        ASSERT_TRUE(values.metalness.has_value());
        EXPECT_DOUBLE_EQ(*values.metalness, 0.5);
        EXPECT_FALSE(table.resolveValues("x.nif", "b.dds").metalness.has_value());
    }
}
