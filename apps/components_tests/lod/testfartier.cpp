#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

#include <components/lod/fartier.hpp>
#include <components/lod/viewlimits.hpp>

namespace
{
    using namespace Lod;

    constexpr std::uint64_t gib = 1024ull * 1024 * 1024;

    HardwareReport report(std::uint64_t deviceGib, std::uint64_t systemGib, bool software = false)
    {
        HardwareReport result;
        result.mDeviceMemoryBytes = deviceGib * gib;
        result.mSystemMemoryBytes = systemGib * gib;
        result.mSoftwareDevice = software;
        return result;
    }

    TEST(LodFarTier, a_software_device_is_the_minimal_tier_whatever_its_memory)
    {
        EXPECT_EQ(chooseFarTier(report(64, 128, true)), FarTier::Minimal);
    }

    TEST(LodFarTier, unknown_memory_is_the_minimal_tier)
    {
        EXPECT_EQ(chooseFarTier(HardwareReport{}), FarTier::Minimal);
        EXPECT_EQ(chooseFarTier(report(0, 32)), FarTier::Minimal);
        EXPECT_EQ(chooseFarTier(report(12, 0)), FarTier::Minimal);
    }

    TEST(LodFarTier, the_device_memory_picks_the_tier)
    {
        EXPECT_EQ(chooseFarTier(report(1, 64)), FarTier::Minimal);
        EXPECT_EQ(chooseFarTier(report(3, 64)), FarTier::Low);
        EXPECT_EQ(chooseFarTier(report(6, 64)), FarTier::Medium);
        EXPECT_EQ(chooseFarTier(report(12, 64)), FarTier::High);
    }

    TEST(LodFarTier, the_tier_boundaries_belong_to_the_larger_tier)
    {
        EXPECT_EQ(chooseFarTier(report(2, 64)), FarTier::Low);
        EXPECT_EQ(chooseFarTier(report(4, 64)), FarTier::Medium);
        EXPECT_EQ(chooseFarTier(report(8, 64)), FarTier::High);
    }

    TEST(LodFarTier, little_system_memory_lowers_the_tier_of_a_big_card)
    {
        EXPECT_EQ(chooseFarTier(report(12, 4)), FarTier::Minimal);
        EXPECT_EQ(chooseFarTier(report(12, 8)), FarTier::Low);
        EXPECT_EQ(chooseFarTier(report(12, 16)), FarTier::Medium);
        EXPECT_EQ(chooseFarTier(report(12, 32)), FarTier::High);
    }

    TEST(LodFarTier, the_tier_is_the_lower_of_the_two)
    {
        EXPECT_EQ(chooseFarTier(report(3, 32)), FarTier::Low);
        EXPECT_EQ(chooseFarTier(report(12, 8)), FarTier::Low);
    }

    TEST(LodFarTier, the_settings_never_get_cheaper_in_a_higher_tier)
    {
        const std::array tiers = { FarTier::Minimal, FarTier::Low, FarTier::Medium, FarTier::High };
        for (std::size_t i = 1; i < tiers.size(); ++i)
        {
            const FarTierSettings below = farTierSettings(tiers[i - 1]);
            const FarTierSettings above = farTierSettings(tiers[i]);
            EXPECT_GE(above.mFarLevels, below.mFarLevels) << i;
            EXPECT_LE(above.mTileThreshold, below.mTileThreshold) << i;
            EXPECT_GE(above.mMaxViewDistance, below.mMaxViewDistance) << i;
            EXPECT_GE(above.mResidentBudgetBytes, below.mResidentBudgetBytes) << i;
        }
    }

    TEST(LodFarTier, the_minimal_tier_is_the_weak_gpu_run_of_the_spec)
    {
        // Spec 0066, "Low-end hardware": far levels 3, a larger threshold.
        const FarTierSettings minimal = farTierSettings(FarTier::Minimal);
        EXPECT_EQ(minimal.mFarLevels, 3u);
        EXPECT_GE(minimal.mTileThreshold, 16.f);
    }

    TEST(LodFarTier, the_high_tier_is_the_whole_map_at_the_project_defaults)
    {
        const FarTierSettings high = farTierSettings(FarTier::High);
        EXPECT_EQ(high.mFarLevels, maxFarLevels);
        EXPECT_FLOAT_EQ(high.mTileThreshold, 8.f);
        EXPECT_FLOAT_EQ(high.mMaxViewDistance, defaultMaxViewDistance);
    }

    TEST(LodFarTier, every_tier_has_a_budget_and_at_least_one_level)
    {
        for (const FarTier tier : { FarTier::Minimal, FarTier::Low, FarTier::Medium, FarTier::High })
        {
            const FarTierSettings settings = farTierSettings(tier);
            EXPECT_GE(settings.mFarLevels, 1u);
            EXPECT_LE(settings.mFarLevels, maxFarLevels);
            EXPECT_GT(settings.mResidentBudgetBytes, 0u);
            EXPECT_GT(settings.mTileThreshold, 0.f);
            EXPECT_GT(settings.mMaxViewDistance, 0.f);
        }
    }

    // Task 0060, toddhow's decision of 2026-11-04 (option b): only the two weakest tiers evict far clusters.
    TEST(LodFarTier, only_the_minimal_and_low_tiers_evict_far_clusters)
    {
        EXPECT_TRUE(farTierSettings(FarTier::Minimal).mEvictFarClusters);
        EXPECT_TRUE(farTierSettings(FarTier::Low).mEvictFarClusters);
        EXPECT_FALSE(farTierSettings(FarTier::Medium).mEvictFarClusters);
        EXPECT_FALSE(farTierSettings(FarTier::High).mEvictFarClusters);
        // The default of a settings object nobody filled in is the safe one: nothing is evicted.
        EXPECT_FALSE(FarTierSettings{}.mEvictFarClusters);
    }

    TEST(LodResolveFarLevels, zero_and_negative_mean_the_tier)
    {
        EXPECT_EQ(resolveFarLevels(0, 4), 4u);
        EXPECT_EQ(resolveFarLevels(-3, 5), 5u);
    }

    TEST(LodResolveFarLevels, a_number_is_cut_to_the_levels_that_exist)
    {
        EXPECT_EQ(resolveFarLevels(1, 6), 1u);
        EXPECT_EQ(resolveFarLevels(3, 6), 3u);
        EXPECT_EQ(resolveFarLevels(6, 3), 6u);
        EXPECT_EQ(resolveFarLevels(7, 3), maxFarLevels);
        EXPECT_EQ(resolveFarLevels(1000000, 3), maxFarLevels);
        EXPECT_EQ(resolveFarLevels(std::numeric_limits<std::int32_t>::max(), 3), maxFarLevels);
    }

    TEST(LodResolveFarLevels, a_bad_tier_value_is_cut_too)
    {
        EXPECT_EQ(resolveFarLevels(0, 0), 1u);
        EXPECT_EQ(resolveFarLevels(0, 99), maxFarLevels);
    }

    TEST(LodFarLevelsWithinBudget, no_budget_keeps_every_wanted_level)
    {
        const std::array<std::uint64_t, 6> bytes = { 10, 20, 40, 80, 160, 320 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 0, 6), 6u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 0, 4), 4u);
    }

    TEST(LodFarLevelsWithinBudget, the_levels_are_added_while_their_sum_fits)
    {
        const std::array<std::uint64_t, 6> bytes = { 10, 20, 40, 80, 160, 320 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 70, 6), 3u); // 10 + 20 + 40 = 70
        EXPECT_EQ(farLevelsWithinBudget(bytes, 69, 6), 2u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 150, 6), 4u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 100000, 6), 6u);
    }

    TEST(LodFarLevelsWithinBudget, level_one_is_always_kept)
    {
        const std::array<std::uint64_t, 6> bytes = { 10, 20, 40, 80, 160, 320 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 5, 6), 1u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 1, 6), 1u);
    }

    TEST(LodFarLevelsWithinBudget, the_wanted_number_is_an_upper_bound)
    {
        const std::array<std::uint64_t, 6> bytes = { 10, 20, 40, 80, 160, 320 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 100000, 2), 2u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 100000, 0), 1u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 100000, 99), 6u);
    }

    TEST(LodFarLevelsWithinBudget, a_cost_list_that_is_shorter_than_wanted_limits_the_levels)
    {
        const std::array<std::uint64_t, 3> bytes = { 10, 20, 40 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 0, 6), 3u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, 100000, 6), 3u);
    }

    TEST(LodFarLevelsWithinBudget, unknown_costs_keep_the_wanted_levels)
    {
        EXPECT_EQ(farLevelsWithinBudget({}, 1000, 4), 4u);
        EXPECT_EQ(farLevelsWithinBudget({}, 0, 0), 1u);
    }

    TEST(LodFarLevelsWithinBudget, huge_costs_do_not_wrap_around)
    {
        const std::uint64_t big = std::numeric_limits<std::uint64_t>::max() / 2 + 1;
        const std::array<std::uint64_t, 3> bytes = { 1, big, big };
        // 1 + big fits a budget of big + 1; adding the third would wrap past 2^64 and look small.
        EXPECT_EQ(farLevelsWithinBudget(bytes, big + 1, 3), 2u);
        EXPECT_EQ(farLevelsWithinBudget(bytes, std::numeric_limits<std::uint64_t>::max(), 3), 2u);
    }

    TEST(LodFarLevelsWithinBudget, a_zero_cost_level_is_free_and_does_not_stop_the_count)
    {
        const std::array<std::uint64_t, 4> bytes = { 10, 0, 0, 100 };
        EXPECT_EQ(farLevelsWithinBudget(bytes, 10, 4), 3u);
    }
}

namespace
{
    using namespace Lod;

    TEST(LodFarTierSetting, auto_and_the_four_names_parse_in_any_case)
    {
        EXPECT_TRUE(parseFarTierSetting("auto").mAuto);
        EXPECT_TRUE(parseFarTierSetting(" AUTO\t").mAuto);
        const std::array<std::pair<const char*, FarTier>, 4> names = { { { "minimal", FarTier::Minimal },
            { "Low", FarTier::Low }, { "MEDIUM", FarTier::Medium }, { " high ", FarTier::High } } };
        for (const auto& [text, tier] : names)
        {
            const FarTierSetting setting = parseFarTierSetting(text);
            EXPECT_FALSE(setting.mAuto) << text;
            EXPECT_EQ(setting.mTier, tier) << text;
            EXPECT_TRUE(setting.mValid) << text;
        }
    }

    TEST(LodFarTierSetting, empty_is_auto_and_valid)
    {
        const FarTierSetting setting = parseFarTierSetting("");
        EXPECT_TRUE(setting.mAuto);
        EXPECT_TRUE(setting.mValid);
    }

    TEST(LodFarTierSetting, anything_else_is_auto_and_not_valid)
    {
        for (const char* text : { "ultra", "3", "low medium", "hig", "-", "autos" })
        {
            const FarTierSetting setting = parseFarTierSetting(text);
            EXPECT_TRUE(setting.mAuto) << text;
            EXPECT_FALSE(setting.mValid) << text;
        }
    }

    TEST(LodFarTierSetting, a_named_tier_ignores_the_hardware)
    {
        FarTierSetting setting;
        setting.mAuto = false;
        setting.mTier = FarTier::Low;
        EXPECT_EQ(resolveFarTier(setting, report(64, 128)), FarTier::Low);
    }

    TEST(LodFarTierSetting, auto_asks_the_report)
    {
        EXPECT_EQ(resolveFarTier(FarTierSetting{}, report(64, 128)), FarTier::High);
        EXPECT_EQ(resolveFarTier(FarTierSetting{}, report(3, 128)), FarTier::Low);
        EXPECT_EQ(resolveFarTier(FarTierSetting{}, report(64, 128, true)), FarTier::Minimal);
    }

    TEST(LodEffectiveFarSettings, a_tier_only_makes_tiles_coarser)
    {
        FarTierSettings tier;
        tier.mTileThreshold = 32;
        EXPECT_FLOAT_EQ(effectiveTileThreshold(8, tier), 32.f);
        EXPECT_FLOAT_EQ(effectiveTileThreshold(64, tier), 64.f);
        EXPECT_FLOAT_EQ(effectiveTileThreshold(32, tier), 32.f);
    }

    TEST(LodEffectiveFarSettings, the_cap_is_the_smaller_of_setting_and_tier)
    {
        FarTierSettings tier;
        tier.mMaxViewDistance = 150000;
        EXPECT_FLOAT_EQ(effectiveMaxViewDistance(600000, tier), 150000.f);
        EXPECT_FLOAT_EQ(effectiveMaxViewDistance(100000, tier), 100000.f);
        EXPECT_FLOAT_EQ(effectiveMaxViewDistance(0, tier), 150000.f);
        EXPECT_FLOAT_EQ(effectiveMaxViewDistance(-5, tier), 150000.f);
    }

    TEST(LodResolveFarTextureSize, zero_is_the_tier_and_a_number_is_a_power_of_two)
    {
        EXPECT_EQ(resolveFarTextureSize(0, 256), 256u);
        EXPECT_EQ(resolveFarTextureSize(-1, 128), 128u);
        EXPECT_EQ(resolveFarTextureSize(512, 128), 512u);
        EXPECT_EQ(resolveFarTextureSize(300, 128), 256u);
        EXPECT_EQ(resolveFarTextureSize(17, 128), 16u);
        EXPECT_EQ(resolveFarTextureSize(5, 128), 16u);
        EXPECT_EQ(resolveFarTextureSize(100000, 128), 8192u);
    }

    TEST(LodResolveFarTextureSize, the_tier_value_is_made_valid_too)
    {
        EXPECT_EQ(resolveFarTextureSize(0, 0), 16u);
        EXPECT_EQ(resolveFarTextureSize(0, 300), 256u);
    }
}

namespace
{
    using namespace Lod;

    TEST(LodClusterBudget, bytes_follow_the_vertex_and_index_counts)
    {
        EXPECT_EQ(clusterResidentBytes(0, 0), 0u);
        EXPECT_EQ(clusterResidentBytes(1000, 0), 1000u * clusterBytesPerVertex);
        EXPECT_EQ(clusterResidentBytes(0, 3000), 3000u * clusterBytesPerIndex);
        EXPECT_EQ(clusterResidentBytes(1000, 3000), 1000u * clusterBytesPerVertex + 3000u * clusterBytesPerIndex);
    }

    TEST(LodClusterBudget, the_per_vertex_cost_is_what_the_arrays_and_the_decoded_cluster_hold)
    {
        EXPECT_EQ(clusterBytesPerVertex, 18u + 12u + 12u + 16u + 8u);
        EXPECT_EQ(clusterBytesPerIndex, 8u);
    }

    TEST(LodClusterBudget, huge_counts_saturate_instead_of_wrapping)
    {
        const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
        EXPECT_EQ(clusterResidentBytes(max, 0), max);
        EXPECT_EQ(clusterResidentBytes(max / 2, max / 2), max);
    }

    TEST(LodClusterBudget, no_budget_admits_everything)
    {
        EXPECT_TRUE(admitCluster(0, 1000, 0));
        EXPECT_TRUE(admitCluster(1ull << 40, 1ull << 40, 0));
    }

    TEST(LodClusterBudget, a_cluster_is_admitted_while_the_sum_fits)
    {
        EXPECT_TRUE(admitCluster(600, 400, 1000));
        EXPECT_FALSE(admitCluster(600, 401, 1000));
        EXPECT_TRUE(admitCluster(0, 1000, 1000));
    }

    TEST(LodClusterBudget, the_first_cluster_is_admitted_even_over_budget)
    {
        EXPECT_TRUE(admitCluster(0, 5000, 1000));
        EXPECT_FALSE(admitCluster(1, 5000, 1000));
    }

    TEST(LodClusterBudget, the_sum_does_not_wrap_around)
    {
        const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
        EXPECT_FALSE(admitCluster(max - 10, 100, max));
        EXPECT_TRUE(admitCluster(max - 100, 100, max));
    }

    TEST(LodFarTier, the_terrain_tile_budget_grows_with_the_tier_and_every_tier_has_one)
    {
        // Task 0060: the drawn terrain tiles have a budget of their own, next to the clusters' (starting values).
        const FarTier tiers[] = { FarTier::Minimal, FarTier::Low, FarTier::Medium, FarTier::High };
        std::uint64_t below = 0;
        for (const FarTier tier : tiers)
        {
            const std::uint64_t budget = farTierSettings(tier).mTerrainBudgetBytes;
            EXPECT_GT(budget, 0u);
            EXPECT_GE(budget, below);
            below = budget;
        }
        EXPECT_EQ(farTierSettings(FarTier::Minimal).mTerrainBudgetBytes, 16ull * 1024 * 1024);
        EXPECT_EQ(farTierSettings(FarTier::High).mTerrainBudgetBytes, 64ull * 1024 * 1024);
    }
}
