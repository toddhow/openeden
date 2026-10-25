#include <components/plugin/host_impl.hpp>
#include <components/plugin/profiler_service.hpp>
#include <openeden/host.hpp>
#include <openeden/profiling.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    using openeden::plugin::HostImpl;
    using openeden::plugin::ProfilerServiceImpl;
    using openeden::sdk::ProfilerService;
    using openeden::sdk::ProfileScope;
    using openeden::sdk::ProfileStats;

    TEST(ProfilerServiceTest, BeginEndScopeTimingPerPlugin)
    {
        ProfilerServiceImpl profiler;

        EXPECT_FALSE(profiler.hasScope("creativemotion", "SolveIK"));

        profiler.beginScope("creativemotion", "SolveIK");
        // Simulate a tiny amount of work
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        profiler.endScope("creativemotion", "SolveIK");

        EXPECT_TRUE(profiler.hasScope("creativemotion", "SolveIK"));

        const ProfileStats stats = profiler.getScopeStats("creativemotion", "SolveIK");
        EXPECT_EQ(stats.callCount, 1u);
        EXPECT_GT(stats.totalTimeMs, 0.0);
        EXPECT_GE(stats.maxTimeMs, stats.minTimeMs);
        EXPECT_DOUBLE_EQ(stats.avgTimeMs, stats.totalTimeMs);
    }

    TEST(ProfilerServiceTest, ProfileScopeRaiiHelper)
    {
        ProfilerServiceImpl profiler;

        {
            ProfileScope scope(profiler, "creativemotion", "BlendTrees");
            // Work within scope
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        EXPECT_TRUE(profiler.hasScope("creativemotion", "BlendTrees"));
        const ProfileStats stats = profiler.getScopeStats("creativemotion", "BlendTrees");
        EXPECT_EQ(stats.callCount, 1u);
        EXPECT_GT(stats.totalTimeMs, 0.0);
    }

    TEST(ProfilerServiceTest, ProfileScopeMoveSemantics)
    {
        ProfilerServiceImpl profiler;

        {
            ProfileScope scope1(profiler, "test_plugin", "MovableScope");
            ProfileScope scope2 = std::move(scope1);
            // scope2 should own the scope and end it upon destruction
        }

        const ProfileStats stats = profiler.getScopeStats("test_plugin", "MovableScope");
        // Should have ended exactly once, not twice
        EXPECT_EQ(stats.callCount, 1u);
    }

    TEST(ProfilerServiceTest, PerPluginAggregationCalculations)
    {
        ProfilerServiceImpl profiler;

        // Record 4 deterministic samples: 2.0ms, 4.0ms, 6.0ms, 8.0ms
        profiler.recordSample("plugin_physics", "NarrowPhase", 2.0);
        profiler.recordSample("plugin_physics", "NarrowPhase", 4.0);
        profiler.recordSample("plugin_physics", "NarrowPhase", 6.0);
        profiler.recordSample("plugin_physics", "NarrowPhase", 8.0);

        const ProfileStats stats = profiler.getScopeStats("plugin_physics", "NarrowPhase");
        EXPECT_EQ(stats.callCount, 4u);
        EXPECT_DOUBLE_EQ(stats.totalTimeMs, 20.0);
        EXPECT_DOUBLE_EQ(stats.minTimeMs, 2.0);
        EXPECT_DOUBLE_EQ(stats.maxTimeMs, 8.0);
        EXPECT_DOUBLE_EQ(stats.avgTimeMs, 5.0);
    }

    TEST(ProfilerServiceTest, PerPluginScopeIsolation)
    {
        ProfilerServiceImpl profiler;

        // Two distinct plugins using the same scope name
        profiler.recordSample("plugin_alpha", "SimulateStep", 10.0);
        profiler.recordSample("plugin_alpha", "SimulateStep", 14.0);

        profiler.recordSample("plugin_beta", "SimulateStep", 1.0);
        profiler.recordSample("plugin_beta", "SimulateStep", 2.0);
        profiler.recordSample("plugin_beta", "SimulateStep", 3.0);

        const ProfileStats alphaStats = profiler.getScopeStats("plugin_alpha", "SimulateStep");
        EXPECT_EQ(alphaStats.callCount, 2u);
        EXPECT_DOUBLE_EQ(alphaStats.totalTimeMs, 24.0);
        EXPECT_DOUBLE_EQ(alphaStats.avgTimeMs, 12.0);

        const ProfileStats betaStats = profiler.getScopeStats("plugin_beta", "SimulateStep");
        EXPECT_EQ(betaStats.callCount, 3u);
        EXPECT_DOUBLE_EQ(betaStats.totalTimeMs, 6.0);
        EXPECT_DOUBLE_EQ(betaStats.avgTimeMs, 2.0);

        // Scopes within same plugin with different names are also isolated
        profiler.recordSample("plugin_alpha", "RenderPass", 5.0);
        EXPECT_EQ(profiler.getScopeStats("plugin_alpha", "RenderPass").callCount, 1u);
        EXPECT_EQ(profiler.getScopeStats("plugin_alpha", "SimulateStep").callCount, 2u);
    }

    TEST(ProfilerServiceTest, FrameResetSemantics)
    {
        ProfilerServiceImpl profiler;

        profiler.recordSample("plugin_fx", "ParticleUpdate", 15.0);
        EXPECT_EQ(profiler.getScopeStats("plugin_fx", "ParticleUpdate").callCount, 1u);

        // Frame boundary reset clears per-frame aggregation
        profiler.resetFrame();

        const ProfileStats resetStats = profiler.getScopeStats("plugin_fx", "ParticleUpdate");
        EXPECT_EQ(resetStats.callCount, 0u);
        EXPECT_DOUBLE_EQ(resetStats.totalTimeMs, 0.0);
    }

    TEST(ProfilerServiceTest, ZeroAllocationScopeTimingHotPath)
    {
        ProfilerServiceImpl profiler;

        // Simulate 10,000 scope timing calls on a hot path
        for (int i = 0; i < 10000; ++i)
        {
            profiler.beginScope("creativemotion", "HotPathLoop");
            profiler.endScope("creativemotion", "HotPathLoop");
        }

        const ProfileStats stats = profiler.getScopeStats("creativemotion", "HotPathLoop");
        EXPECT_EQ(stats.callCount, 10000u);
        EXPECT_GE(stats.totalTimeMs, 0.0);
    }

    TEST(ProfilerServiceTest, GracefulHandlingOfMismatchedOrUnknownScopes)
    {
        ProfilerServiceImpl profiler;

        // Calling endScope without beginScope should not crash
        EXPECT_NO_THROW(profiler.endScope("unknown_plugin", "never_begun"));

        // Querying non-existent scope returns empty stats cleanly
        EXPECT_FALSE(profiler.hasScope("unknown_plugin", "never_begun"));
        const ProfileStats emptyStats = profiler.getScopeStats("unknown_plugin", "never_begun");
        EXPECT_EQ(emptyStats.callCount, 0u);
        EXPECT_DOUBLE_EQ(emptyStats.totalTimeMs, 0.0);
    }

    TEST(ProfilerServiceTest, ConcurrentWorkerThreadsProfiling)
    {
        ProfilerServiceImpl profiler;

        constexpr int kNumThreads = 4;
        constexpr int kIterationsPerThread = 1000;
        std::vector<std::thread> workers;
        workers.reserve(kNumThreads);

        for (int t = 0; t < kNumThreads; ++t)
        {
            workers.emplace_back([&profiler]() {
                for (int i = 0; i < kIterationsPerThread; ++i)
                {
                    profiler.beginScope("creativemotion", "SolveIK");
                    profiler.endScope("creativemotion", "SolveIK");
                }
            });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }

        const ProfileStats stats = profiler.getScopeStats("creativemotion", "SolveIK");
        EXPECT_EQ(stats.callCount, static_cast<std::uint64_t>(kNumThreads * kIterationsPerThread));
        EXPECT_GE(stats.totalTimeMs, 0.0);
    }

    TEST(ProfilerServiceTest, HostImplWiresProfilerService)
    {
        HostImpl host("TestHost");
        ProfilerService* profiler = host.getProfilerService();

        ASSERT_NE(profiler, nullptr);

        profiler->recordSample("host_plugin", "HostInit", 3.5);
        const ProfileStats stats = profiler->getScopeStats("host_plugin", "HostInit");
        EXPECT_EQ(stats.callCount, 1u);
        EXPECT_DOUBLE_EQ(stats.totalTimeMs, 3.5);
    }
}
