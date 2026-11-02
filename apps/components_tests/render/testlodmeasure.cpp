// Task 0054 (stage S0): the pure parts of the LOD measurement tools. None of these tests needs a window, game data or
// network; the one that asks the machine's Vulkan loader says so and returns early where there is no device.
#include <gtest/gtest.h>

#include <components/render/devicereport.hpp>
#include <components/render/framecounters.hpp>
#include <components/render/processmemory.hpp>
#include <components/render/scenecensus.hpp>

#include <vsg/commands/Commands.h>
#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>

#include <string>
#include <vector>

namespace
{
    using Render::DeviceReport;
    using Render::QueueFamilySummary;

    constexpr uint32_t graphics = VK_QUEUE_GRAPHICS_BIT;
    constexpr uint32_t compute = VK_QUEUE_COMPUTE_BIT;
    constexpr uint32_t transfer = VK_QUEUE_TRANSFER_BIT;
    constexpr uint32_t sparse = VK_QUEUE_SPARSE_BINDING_BIT;

    // ---- queue families -------------------------------------------------------------------------------------------

    TEST(RenderLodMeasureQueues, one_universal_family_has_no_dedicated_queues)
    {
        DeviceReport r;
        Render::addQueueFamilyEntries(r, { QueueFamilySummary{ graphics | compute | transfer | sparse, 16 } });
        EXPECT_EQ(r.get("queue.familyCount"), "1");
        EXPECT_EQ(r.get("queue.family0"), "graphics,compute,transfer,sparse_binding x16");
        EXPECT_EQ(r.get("queue.graphics"), "present");
        EXPECT_EQ(r.get("queue.dedicatedCompute"), "absent");
        EXPECT_EQ(r.get("queue.dedicatedTransfer"), "absent");
        EXPECT_EQ(r.get("queue.sparseBinding"), "present");
    }

    TEST(RenderLodMeasureQueues, a_compute_only_and_a_transfer_only_family_are_dedicated)
    {
        DeviceReport r;
        Render::addQueueFamilyEntries(r,
            { QueueFamilySummary{ graphics | compute | transfer, 16 }, QueueFamilySummary{ compute | transfer, 8 },
                QueueFamilySummary{ transfer, 2 } });
        EXPECT_EQ(r.get("queue.familyCount"), "3");
        EXPECT_EQ(r.get("queue.family1"), "compute,transfer x8");
        EXPECT_EQ(r.get("queue.family2"), "transfer x2");
        EXPECT_EQ(r.get("queue.dedicatedCompute"), "present");
        EXPECT_EQ(r.get("queue.dedicatedTransfer"), "present");
        EXPECT_EQ(r.get("queue.sparseBinding"), "absent");
    }

    TEST(RenderLodMeasureQueues, no_families_gives_absent_entries_and_does_not_throw)
    {
        DeviceReport r;
        EXPECT_NO_THROW(Render::addQueueFamilyEntries(r, {}));
        EXPECT_EQ(r.get("queue.familyCount"), "0");
        EXPECT_EQ(r.get("queue.graphics"), "absent");
        EXPECT_EQ(r.get("queue.dedicatedCompute"), "absent");
    }

    TEST(RenderLodMeasureQueues, an_empty_flag_set_is_named_none)
    {
        EXPECT_EQ(Render::queueFlagNames(0), "none");
        EXPECT_EQ(Render::queueFlagNames(graphics), "graphics");
    }

    TEST(RenderLodMeasureQueues, the_new_keys_round_trip_through_the_json)
    {
        DeviceReport r;
        r.add("schema", "openeden.device-report/1");
        Render::addQueueFamilyEntries(r, { QueueFamilySummary{ graphics | compute, 4 } });
        r.add("memory.budget.heap0", Render::formatHeapBudget(11u * 1024 * 1024 * 1024, 512ull * 1024 * 1024));
        const std::string once = r.toJson();
        EXPECT_EQ(DeviceReport::parseJson(once).toJson(), once);
    }

    TEST(RenderLodMeasureQueues, heap_budget_text_is_in_mib)
    {
        EXPECT_EQ(Render::formatHeapBudget(11ull * 1024 * 1024 * 1024, 512ull * 1024 * 1024),
            "budget 11264 MiB, usage 512 MiB");
        EXPECT_EQ(Render::formatHeapBudget(0, 0), "budget 0 MiB, usage 0 MiB");
    }

    TEST(RenderLodMeasureQueues, the_real_probe_has_every_new_key)
    {
        DeviceReport r;
        ASSERT_NO_THROW(r = Render::collectDeviceReport(nullptr, nullptr, 0));
        if (r.get("device.name").empty())
            return; // no Vulkan device on this machine
        for (const char* key :
            { "queue.familyCount", "queue.graphics", "queue.dedicatedCompute", "queue.dedicatedTransfer",
                "queue.sparseBinding", "ext.memory_budget", "ext.memory_priority", "ext.mesh_shader",
                "feature.sparseBinding", "feature.sparseResidencyImage2D", "feature.sparseResidencyBuffer",
                "feature.sparseResidencyAliased", "feature.meshShader", "feature.taskShader",
                "feature.multiDrawIndirect", "feature.drawIndirectFirstInstance", "feature.pipelineStatisticsQuery",
                "limit.maxMemoryAllocationCount", "limit.maxImageDimension2D", "limit.maxStorageBufferRange" })
            EXPECT_FALSE(r.get(key).empty()) << key;
    }

    // ---- frame counters -------------------------------------------------------------------------------------------

    TEST(RenderLodMeasureCounters, none_registered_writes_nothing_so_the_stats_file_is_as_before)
    {
        Render::FrameCounters counters;
        EXPECT_TRUE(counters.empty());
        EXPECT_EQ(counters.headerText(), "");
        EXPECT_EQ(counters.lineText(), "");
    }

    TEST(RenderLodMeasureCounters, header_and_line_have_one_column_per_counter_in_order)
    {
        Render::FrameCounters counters;
        double rss = 100.5;
        counters.add("rss mib", [&rss] { return rss; });
        counters.add("cells", [] { return 3.0; });
        EXPECT_FALSE(counters.empty());
        EXPECT_EQ(counters.headerText(), " ctr_rss_mib ctr_cells");
        EXPECT_EQ(counters.lineText(), " 100.5 3");
        rss = 250.0;
        EXPECT_EQ(counters.lineText(), " 250 3");
    }

    TEST(RenderLodMeasureCounters, large_values_are_written_in_full)
    {
        Render::FrameCounters counters;
        counters.add("triangles", [] { return 12345678.0; });
        EXPECT_EQ(counters.lineText(), " 12345678");
    }

    // ---- process memory -------------------------------------------------------------------------------------------

    TEST(RenderLodMeasureMemory, statm_text_gives_the_resident_pages_times_the_page_size)
    {
        EXPECT_EQ(Render::parseStatmResidentBytes("1000 250 40 1 0 300 0\n", 4096), 250u * 4096u);
    }

    TEST(RenderLodMeasureMemory, bad_statm_text_gives_zero)
    {
        EXPECT_EQ(Render::parseStatmResidentBytes("", 4096), 0u);
        EXPECT_EQ(Render::parseStatmResidentBytes("only", 4096), 0u);
        EXPECT_EQ(Render::parseStatmResidentBytes("12 abc", 4096), 0u);
        EXPECT_EQ(Render::parseStatmResidentBytes("1 99999999999999999999999", 4096), 0u); // too big for 64 bits
    }

    TEST(RenderLodMeasureMemory, this_process_has_a_working_set)
    {
        EXPECT_GT(Render::processWorkingSetBytes(), 1024u * 1024u);
    }

    // ---- scene census ---------------------------------------------------------------------------------------------

    TEST(RenderLodMeasureCensus, an_empty_graph_counts_nothing)
    {
        Render::SceneCensus census;
        vsg::Group::create()->accept(census);
        EXPECT_EQ(census.draws(), 0u);
        EXPECT_EQ(census.triangles(), 0u);
    }

    TEST(RenderLodMeasureCensus, an_indexed_draw_counts_its_triangles_times_its_instances)
    {
        auto draw = vsg::VertexIndexDraw::create();
        draw->indexCount = 30;
        draw->instanceCount = 2;
        Render::SceneCensus census;
        draw->accept(census);
        EXPECT_EQ(census.draws(), 1u);
        EXPECT_EQ(census.triangles(), 20u);
    }

    TEST(RenderLodMeasureCensus, plain_and_command_draws_are_counted)
    {
        auto group = vsg::Group::create();
        auto vertexDraw = vsg::VertexDraw::create();
        vertexDraw->vertexCount = 9;
        vertexDraw->instanceCount = 1;
        group->addChild(vertexDraw);
        auto commands = vsg::Commands::create();
        commands->addChild(vsg::Draw::create(6, 2, 0, 0));
        commands->addChild(vsg::DrawIndexed::create(12, 1, 0, 0, 0));
        group->addChild(commands);
        Render::SceneCensus census;
        group->accept(census);
        EXPECT_EQ(census.draws(), 3u);
        EXPECT_EQ(census.triangles(), 3u + 4u + 4u);
    }

    TEST(RenderLodMeasureCensus, nested_groups_are_walked_and_a_switched_off_child_is_not)
    {
        auto on = vsg::VertexIndexDraw::create();
        on->indexCount = 3;
        on->instanceCount = 1;
        auto off = vsg::VertexIndexDraw::create();
        off->indexCount = 300;
        off->instanceCount = 1;
        auto sw = vsg::Switch::create();
        sw->addChild(true, on);
        sw->addChild(false, off);
        auto inner = vsg::Group::create();
        inner->addChild(sw);
        auto root = vsg::Group::create();
        root->addChild(inner);
        root->addChild(inner); // shared nodes are drawn once per use
        Render::SceneCensus census;
        root->accept(census);
        EXPECT_EQ(census.draws(), 2u);
        EXPECT_EQ(census.triangles(), 2u);
    }

    TEST(RenderLodMeasureCensus, reset_clears_the_totals)
    {
        auto draw = vsg::VertexIndexDraw::create();
        draw->indexCount = 3;
        draw->instanceCount = 1;
        Render::SceneCensus census;
        draw->accept(census);
        census.reset();
        EXPECT_EQ(census.draws(), 0u);
        EXPECT_EQ(census.triangles(), 0u);
    }
}
