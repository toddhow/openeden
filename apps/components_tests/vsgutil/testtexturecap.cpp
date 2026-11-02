// Tests for the runtime texture downscaler's decisions (task 0049, stage a): vsgUtil::TextureCap. Pure numbers, no
// image, device or file. Written from the spec before the implementation.

#include <components/vsgutil/texturecap.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace vsgUtil::TextureCap;

    TEST(TextureCapSanitise, zero_and_negative_mean_no_cap)
    {
        EXPECT_EQ(sanitiseCap(0), 0);
        EXPECT_EQ(sanitiseCap(-1), 0);
        EXPECT_EQ(sanitiseCap(-4096), 0);
    }

    TEST(TextureCapSanitise, small_values_rise_to_sixteen)
    {
        EXPECT_EQ(sanitiseCap(1), 16);
        EXPECT_EQ(sanitiseCap(15), 16);
        EXPECT_EQ(sanitiseCap(16), 16);
    }

    TEST(TextureCapSanitise, rounds_down_to_a_power_of_two)
    {
        EXPECT_EQ(sanitiseCap(17), 16);
        EXPECT_EQ(sanitiseCap(100), 64);
        EXPECT_EQ(sanitiseCap(1023), 512);
        EXPECT_EQ(sanitiseCap(1024), 1024);
        EXPECT_EQ(sanitiseCap(1500), 1024);
        EXPECT_EQ(sanitiseCap(4096), 4096);
    }

    TEST(TextureCapSanitise, large_values_stop_at_8192)
    {
        EXPECT_EQ(sanitiseCap(8192), 8192);
        EXPECT_EQ(sanitiseCap(9000), 8192);
        EXPECT_EQ(sanitiseCap(100000), 8192);
    }

    TEST(TextureCapLevels, square_power_of_two_block_compressed)
    {
        // 2048 with a 12 level chain.
        EXPECT_EQ(levelsToDrop(2048, 2048, 12, 4, 1024), 1);
        EXPECT_EQ(levelsToDrop(2048, 2048, 12, 4, 512), 2);
        EXPECT_EQ(levelsToDrop(2048, 2048, 12, 4, 256), 3);
        EXPECT_EQ(levelsToShrink(2048, 2048, 12, 4, 256), 0);
    }

    TEST(TextureCapLevels, cap_zero_or_at_or_over_the_longest_side_changes_nothing)
    {
        EXPECT_EQ(levelsToDrop(2048, 2048, 12, 4, 0), 0);
        EXPECT_EQ(levelsToDrop(1024, 1024, 11, 4, 1024), 0);
        EXPECT_EQ(levelsToDrop(512, 256, 10, 4, 1024), 0);
        EXPECT_EQ(levelsToShrink(512, 256, 10, 1, 1024), 0);
        EXPECT_EQ(levelsToShrink(2048, 2048, 1, 1, 0), 0);
    }

    TEST(TextureCapLevels, the_longest_side_decides_for_a_non_square_image)
    {
        const Plan plan = planLevels(2048, 512, 12, 4, 512);
        EXPECT_EQ(plan.drop, 2); // 512 by 128
        EXPECT_EQ(plan.shrink, 0);
    }

    TEST(TextureCapLevels, non_power_of_two_sizes_halve_with_floor)
    {
        EXPECT_EQ(levelsToDrop(1500, 1000, 11, 1, 1024), 1); // 750 by 500
        EXPECT_EQ(levelsToDrop(2049, 2049, 12, 1, 1024), 1); // level 1 is 1024
        EXPECT_EQ(levelsToDrop(2050, 2050, 12, 1, 1024), 2); // level 1 is 1025: still over the cap
        EXPECT_EQ(levelsToDrop(3000, 3000, 12, 1, 1024), 2); // 1500, then 750
    }

    TEST(TextureCapLevels, the_two_calls_agree_with_planLevels)
    {
        const Plan plan = planLevels(2048, 2048, 2, 1, 256);
        EXPECT_EQ(plan.drop, levelsToDrop(2048, 2048, 2, 1, 256));
        EXPECT_EQ(plan.shrink, levelsToShrink(2048, 2048, 2, 1, 256));
    }

    TEST(TextureCapLevels, a_short_chain_gives_what_it_has_and_the_rest_is_shrink)
    {
        // Three levels (2048, 1024, 512): two can be dropped, the third halving (to 256) must be averaged.
        const Plan plan = planLevels(2048, 2048, 3, 1, 256);
        EXPECT_EQ(plan.drop, 2);
        EXPECT_EQ(plan.shrink, 1);
    }

    TEST(TextureCapLevels, a_single_level_image_is_all_shrink)
    {
        const Plan plan = planLevels(1024, 1024, 1, 1, 256);
        EXPECT_EQ(plan.drop, 0);
        EXPECT_EQ(plan.shrink, 2);
    }

    TEST(TextureCapLevels, a_block_compressed_level_is_never_smaller_than_one_block)
    {
        // 2048 by 16, cap 256: three halvings would leave 256 by 2, under a 4 by 4 block. Two leave 512 by 4.
        EXPECT_EQ(levelsToDrop(2048, 16, 12, 4, 256), 2);
        EXPECT_EQ(levelsToShrink(2048, 16, 12, 4, 256), 0);
        // Uncompressed: down to one texel is allowed.
        EXPECT_EQ(levelsToDrop(2048, 16, 12, 1, 256), 3);
        // A square block compressed image stops at 4 by 4.
        EXPECT_EQ(levelsToDrop(64, 64, 7, 4, 2), 4);
        EXPECT_EQ(levelsToDrop(64, 64, 7, 1, 2), 5);
    }

    TEST(TextureCapLevels, a_side_already_under_a_block_is_kept_as_it_is)
    {
        EXPECT_EQ(levelsToDrop(32, 2, 6, 4, 16), 0);
        EXPECT_EQ(levelsToDrop(2, 2, 2, 4, 1), 0);
    }

    TEST(TextureCapBudget, the_table_bands_for_objects_and_terrain)
    {
        EXPECT_EQ(capFromBudget(2048, Category::Object), 512);
        EXPECT_EQ(capFromBudget(2048, Category::Terrain), 256);
        EXPECT_EQ(capFromBudget(4096, Category::Object), 1024);
        EXPECT_EQ(capFromBudget(4096, Category::Terrain), 512);
        EXPECT_EQ(capFromBudget(8192, Category::Object), 2048);
        EXPECT_EQ(capFromBudget(8192, Category::Terrain), 1024);
        EXPECT_EQ(capFromBudget(12288, Category::Object), 0);
        EXPECT_EQ(capFromBudget(12288, Category::Terrain), 2048);
    }

    TEST(TextureCapBudget, a_band_starts_at_its_lower_bound)
    {
        EXPECT_EQ(capFromBudget(3071, Category::Object), 512);
        EXPECT_EQ(capFromBudget(3072, Category::Object), 1024);
        EXPECT_EQ(capFromBudget(6143, Category::Object), 1024);
        EXPECT_EQ(capFromBudget(6144, Category::Object), 2048);
        EXPECT_EQ(capFromBudget(10239, Category::Object), 2048);
        EXPECT_EQ(capFromBudget(10240, Category::Object), 0);
        EXPECT_EQ(capFromBudget(3071, Category::Terrain), 256);
        EXPECT_EQ(capFromBudget(3072, Category::Terrain), 512);
        EXPECT_EQ(capFromBudget(10239, Category::Terrain), 1024);
        EXPECT_EQ(capFromBudget(10240, Category::Terrain), 2048);
    }

    TEST(TextureCapBudget, unknown_memory_and_exempt_categories_give_no_cap)
    {
        EXPECT_EQ(capFromBudget(0, Category::Object), 0);
        EXPECT_EQ(capFromBudget(0, Category::Terrain), 0);
        for (Category category : { Category::None, Category::Ui, Category::Effect })
            EXPECT_EQ(capFromBudget(2048, category), 0);
    }

    TEST(TextureCapEffective, everything_off_gives_no_cap)
    {
        const Caps caps;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 0);
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 0);
    }

    TEST(TextureCapEffective, the_global_cap_applies_to_terrain_and_objects)
    {
        Caps caps;
        caps.global = 1024;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 1024);
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 1024);
    }

    TEST(TextureCapEffective, a_category_setting_beats_the_global_one)
    {
        Caps caps;
        caps.global = 1024;
        caps.terrain = 256;
        caps.object = 2048;
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 256);
        EXPECT_EQ(effectiveCap(Category::Object, caps), 2048);
    }

    TEST(TextureCapEffective, a_category_setting_alone_leaves_the_other_category_uncapped)
    {
        Caps caps;
        caps.terrain = 512;
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 512);
        EXPECT_EQ(effectiveCap(Category::Object, caps), 0);
    }

    TEST(TextureCapEffective, ui_effect_and_none_are_never_capped)
    {
        Caps caps;
        caps.global = 16;
        caps.terrain = 16;
        caps.object = 16;
        caps.autoBudget = true;
        caps.deviceLocalMiB = 1024;
        for (Category category : { Category::None, Category::Ui, Category::Effect })
            EXPECT_EQ(effectiveCap(category, caps), 0);
    }

    TEST(TextureCapEffective, settings_are_sanitised)
    {
        Caps caps;
        caps.global = 1500;
        caps.terrain = 5;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 1024);
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 16);
    }

    TEST(TextureCapEffective, auto_alone_uses_the_table)
    {
        Caps caps;
        caps.autoBudget = true;
        caps.deviceLocalMiB = 8192;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 2048);
        EXPECT_EQ(effectiveCap(Category::Terrain, caps), 1024);
    }

    TEST(TextureCapEffective, auto_never_raises_a_cap_the_user_set)
    {
        Caps caps;
        caps.autoBudget = true;
        caps.deviceLocalMiB = 8192; // objects 2048
        caps.global = 512;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 512);
        caps.global = 4096;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 2048);
        caps.object = 1024;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 1024);
    }

    TEST(TextureCapEffective, auto_with_no_cap_in_its_band_keeps_the_explicit_cap)
    {
        Caps caps;
        caps.autoBudget = true;
        caps.deviceLocalMiB = 16384; // objects: no cap
        EXPECT_EQ(effectiveCap(Category::Object, caps), 0);
        caps.object = 1024;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 1024);
    }

    TEST(TextureCapEffective, auto_without_known_memory_finds_no_cap)
    {
        Caps caps;
        caps.autoBudget = true;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 0);
    }

    TEST(TextureCapEffective, auto_off_ignores_the_memory)
    {
        Caps caps;
        caps.deviceLocalMiB = 1024;
        EXPECT_EQ(effectiveCap(Category::Object, caps), 0);
    }

    TEST(TextureCapBytes, bc1_chain_against_hand_computed_values)
    {
        // Level bytes: (side / 4) * (side / 4) * 8 for 2048, 1024, ... 4, then 8 for each of 2 and 1.
        EXPECT_EQ(bytesOfChain(2048, 2048, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 1), 2097152u);
        EXPECT_EQ(bytesOfChain(2048, 2048, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 2), 2621440u);
        EXPECT_EQ(bytesOfChain(2048, 2048, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 3), 2752512u);
        EXPECT_EQ(bytesOfChain(2048, 2048, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 12), 2796216u);
    }

    TEST(TextureCapBytes, bc7_and_rgba8)
    {
        EXPECT_EQ(bytesOfChain(256, 256, VK_FORMAT_BC7_UNORM_BLOCK, 1), 65536u);
        EXPECT_EQ(bytesOfChain(256, 256, VK_FORMAT_BC7_UNORM_BLOCK, 9), 87408u);
        EXPECT_EQ(bytesOfChain(4, 4, VK_FORMAT_R8G8B8A8_UNORM, 3), 64u + 16u + 4u);
        EXPECT_EQ(bytesOfChain(4, 2, VK_FORMAT_R8G8B8A8_UNORM, 2), 32u + 8u);
        EXPECT_EQ(bytesOfChain(8, 8, VK_FORMAT_R8_UNORM, 1), 64u);
    }

    TEST(TextureCapBytes, a_block_format_rounds_up_to_whole_blocks)
    {
        EXPECT_EQ(bytesOfChain(5, 5, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 1), 4u * 8u);
        EXPECT_EQ(bytesOfChain(1, 1, VK_FORMAT_BC7_UNORM_BLOCK, 1), 16u);
    }

    TEST(TextureCapBytes, no_levels_or_an_unknown_format_is_zero)
    {
        EXPECT_EQ(bytesOfChain(256, 256, VK_FORMAT_BC7_UNORM_BLOCK, 0), 0u);
        EXPECT_EQ(bytesOfChain(256, 256, VK_FORMAT_UNDEFINED, 1), 0u);
    }

    TEST(TextureCapDevice, local_heaps_are_summed_in_mib)
    {
        VkPhysicalDeviceMemoryProperties memory{};
        memory.memoryHeapCount = 3;
        memory.memoryHeaps[0].size = 8ull * 1024 * 1024 * 1024;
        memory.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        memory.memoryHeaps[1].size = 16ull * 1024 * 1024 * 1024;
        memory.memoryHeaps[1].flags = 0;
        memory.memoryHeaps[2].size = 256ull * 1024 * 1024;
        memory.memoryHeaps[2].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        EXPECT_EQ(deviceLocalMiB(memory), 8192u + 256u);
    }

    TEST(TextureCapDevice, no_heaps_is_zero)
    {
        VkPhysicalDeviceMemoryProperties memory{};
        EXPECT_EQ(deviceLocalMiB(memory), 0u);
    }

    TEST(TextureCapSettings, caps_are_kept_for_the_loader_threads)
    {
        const Caps before = getCaps();
        Caps caps;
        caps.global = 512;
        caps.terrain = 256;
        caps.object = 1024;
        caps.autoBudget = true;
        caps.deviceLocalMiB = 4096;
        setCaps(caps);
        const Caps read = getCaps();
        EXPECT_EQ(read.global, 512);
        EXPECT_EQ(read.terrain, 256);
        EXPECT_EQ(read.object, 1024);
        EXPECT_TRUE(read.autoBudget);
        EXPECT_EQ(read.deviceLocalMiB, 4096u);
        setCaps(before);
    }
}
