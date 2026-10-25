#include <components/plugin/host_impl.hpp>
#include <components/plugin/lifecycle_manager.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <array>

namespace
{
    using openeden::plugin::LifecycleManager;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::Phase;

    class MockHost : public Host
    {
    public:
        HostInfo mHostInfo{ openeden::sdk::kSdkVersion, "OpenEdenEmptyHost" };

        const HostInfo& getHostInfo() const override { return mHostInfo; }
    };

    TEST(EmptyHostTest, InitializeWithZeroPluginsSucceeds)
    {
        MockHost host;
        LifecycleManager manager(host);

        EXPECT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActivePluginCount(), 0u);

        manager.shutdown();
        EXPECT_EQ(manager.getActivePluginCount(), 0u);
    }

    TEST(EmptyHostTest, RunAllPhasesRepeatedlyWithoutPlugins)
    {
        MockHost host;
        LifecycleManager manager(host);

        ASSERT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActivePluginCount(), 0u);

        constexpr std::array<Phase, 6> framePhases = {
            Phase::Input,
            Phase::PreSimulation,
            Phase::PostSimulation,
            Phase::Animation,
            Phase::PreRender,
            Phase::Ui,
        };

        constexpr std::array<Phase, 2> fixedStepPhases = {
            Phase::FixedStepPrePhysics,
            Phase::FixedStepPostPhysics,
        };

        constexpr float frameDt = 1.0f / 60.0f;
        constexpr float fixedDt = 1.0f / 60.0f;
        std::uint64_t stepIndex = 0;

        // Simulate 120 frames with fixed steps, verifying all 8 phases execute without error or crash
        for (int frame = 0; frame < 120; ++frame)
        {
            EXPECT_NO_THROW(manager.runPhase(Phase::Input, frameDt));
            EXPECT_NO_THROW(manager.runPhase(Phase::PreSimulation, frameDt));

            // Fixed step physics phases
            EXPECT_NO_THROW(manager.runFixedStepPhase(Phase::FixedStepPrePhysics, fixedDt, stepIndex));
            EXPECT_NO_THROW(manager.runFixedStepPhase(Phase::FixedStepPostPhysics, fixedDt, stepIndex));
            ++stepIndex;

            EXPECT_NO_THROW(manager.runPhase(Phase::PostSimulation, frameDt));
            EXPECT_NO_THROW(manager.runPhase(Phase::Animation, frameDt));
            EXPECT_NO_THROW(manager.runPhase(Phase::PreRender, frameDt));
            EXPECT_NO_THROW(manager.runPhase(Phase::Ui, frameDt));
        }

        // Test running all phases directly in sequence as well
        for (const Phase phase : framePhases)
        {
            EXPECT_NO_THROW(manager.runPhase(phase, frameDt));
        }
        for (const Phase phase : fixedStepPhases)
        {
            EXPECT_NO_THROW(manager.runFixedStepPhase(phase, fixedDt, stepIndex++));
        }

        manager.shutdown();
        EXPECT_EQ(manager.getActivePluginCount(), 0u);
    }

    TEST(EmptyHostTest, ShutdownCompletesCleanlyAndIsIdempotent)
    {
        MockHost host;
        LifecycleManager manager(host);

        ASSERT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActivePluginCount(), 0u);

        // First shutdown
        EXPECT_NO_THROW(manager.shutdown());
        EXPECT_EQ(manager.getActivePluginCount(), 0u);

        // Calling shutdown again should complete cleanly
        EXPECT_NO_THROW(manager.shutdown());
        EXPECT_EQ(manager.getActivePluginCount(), 0u);
    }

    struct TestFrameEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 12345;
        int counter = 0;
    };

    TEST(EmptyHostTest, HostImplProcessesDeferredEventsAtPhaseBoundary)
    {
        openeden::plugin::HostImpl host("TestOpenEdenHost");
        LifecycleManager manager(host);

        int receivedValue = 0;
        host.getEventBus()->subscribe<TestFrameEvent>(
            "test_subscriber", [&receivedValue](const TestFrameEvent& e) { receivedValue = e.counter; });

        // Queue deferred event for Phase::PreSimulation
        host.getEventBus()->publishDeferred(TestFrameEvent{ 999 }, Phase::PreSimulation);
        EXPECT_EQ(receivedValue, 0);

        // Run different phase: event remains queued
        manager.runPhase(Phase::Input, 0.016f);
        EXPECT_EQ(receivedValue, 0);

        // Run target phase: event processed at phase boundary
        manager.runPhase(Phase::PreSimulation, 0.016f);
        EXPECT_EQ(receivedValue, 999);
    }
}
