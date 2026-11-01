#include <gtest/gtest.h>

#include <components/render/devicereport.hpp>

#include <stdexcept>
#include <string>

namespace
{
    using Render::DeviceReport;

    DeviceReport sample()
    {
        DeviceReport r;
        r.add("schema", "openeden.device-report/1");
        r.add("device.name", "Test \"GPU\" \\ 9000\t\xC3\x85");
        r.add("timestamps", true);
        r.add("feature.rayQuery", false);
        r.add("subgroup.size", "unknown (needs Vulkan 1.1)");
        r.add("control", std::string("a\nb\x01"));
        return r;
    }

    TEST(RenderDeviceReportTest, keeps_entries_in_insertion_order)
    {
        const DeviceReport r = sample();
        ASSERT_EQ(r.entries().size(), 6u);
        EXPECT_EQ(r.entries()[0].first, "schema");
        EXPECT_EQ(r.entries()[5].first, "control");
    }

    TEST(RenderDeviceReportTest, a_string_literal_is_text_and_not_a_flag)
    {
        DeviceReport r;
        r.add("name", "present-looking text");
        EXPECT_EQ(r.get("name"), "present-looking text");
        r.add("a", true);
        r.add("b", false);
        EXPECT_EQ(r.get("a"), "present");
        EXPECT_EQ(r.get("b"), "absent");
    }

    TEST(RenderDeviceReportTest, get_of_a_missing_key_is_empty)
    {
        EXPECT_TRUE(sample().get("nope").empty());
    }

    TEST(RenderDeviceReportTest, json_round_trips_byte_for_byte)
    {
        const std::string once = sample().toJson();
        const DeviceReport parsed = DeviceReport::parseJson(once);
        EXPECT_EQ(parsed.toJson(), once);
        const DeviceReport original = sample();
        ASSERT_EQ(parsed.entries().size(), original.entries().size());
        for (std::size_t i = 0; i < parsed.entries().size(); ++i)
            EXPECT_EQ(parsed.entries()[i], original.entries()[i]);
    }

    TEST(RenderDeviceReportTest, json_has_the_canonical_shape)
    {
        DeviceReport r;
        r.add("a", "x");
        r.add("b", true);
        EXPECT_EQ(r.toJson(), "{\n  \"a\": \"x\",\n  \"b\": \"present\"\n}\n");
    }

    TEST(RenderDeviceReportTest, an_empty_report_round_trips)
    {
        const std::string text = DeviceReport().toJson();
        EXPECT_EQ(DeviceReport::parseJson(text).toJson(), text);
    }

    TEST(RenderDeviceReportTest, parse_refuses_text_that_is_not_an_object_of_strings)
    {
        EXPECT_THROW(DeviceReport::parseJson("[1]"), std::runtime_error);
        EXPECT_THROW(DeviceReport::parseJson("{\"a\": 1}"), std::runtime_error);
        EXPECT_THROW(DeviceReport::parseJson("{"), std::runtime_error);
    }

    TEST(RenderDeviceReportTest, absent_and_unknown_are_distinct_readable_prefixes)
    {
        DeviceReport r;
        r.add("feature.rayQuery", false);
        r.add("subgroup.size", "unknown (needs Vulkan 1.1)");
        EXPECT_EQ(r.get("feature.rayQuery").substr(0, 6), "absent");
        EXPECT_EQ(r.get("subgroup.size").substr(0, 7), "unknown");
    }

    TEST(RenderDeviceReportTest, collecting_without_a_game_device_does_not_throw_and_names_every_item)
    {
        // This asks the machine's Vulkan loader. Where it has no device the report says so and stops; where it has
        // one, every item research note 0008 names has a key, present, absent or unknown, never missing.
        DeviceReport r;
        EXPECT_NO_THROW(r = Render::collectDeviceReport(nullptr, nullptr, 0));
        ASSERT_FALSE(r.entries().empty());
        EXPECT_EQ(r.get("schema"), "openeden.device-report/1");
        if (r.get("device.name").empty())
            return;
        for (const char* key : { "timestamps", "ext.create_renderpass2", "ext.depth_stencil_resolve",
                 "ext.push_descriptor", "ext.descriptor_indexing", "ext.acceleration_structure", "ext.ray_query",
                 "surface.hdr10_st2084", "subgroup.stages", "subgroup.operations", "feature.shaderFloat16",
                 "feature.storageBuffer16BitAccess", "feature.shaderInt16", "feature.multiview", "feature.rayQuery",
                 "feature.accelerationStructure", "feature.descriptorIndexing", "memory.deviceLocalMiB" })
            EXPECT_FALSE(r.get(key).empty()) << key;
    }
}
