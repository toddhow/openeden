#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace
{
    using openeden::extension::LifecycleManager;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::Phase;

    class MockHost : public Host
    {
    public:
        HostInfo mHostInfo{ openeden::sdk::kSdkVersion, "OpenEdenEmptyHost" };

        const HostInfo& getHostInfo() const override { return mHostInfo; }
    };

    TEST(EmptyHostTest, InitializeWithZeroExtensionsSucceeds)
    {
        MockHost host;
        LifecycleManager manager(host);

        EXPECT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);

        manager.shutdown();
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);
    }

    TEST(EmptyHostTest, RunAllPhasesRepeatedlyWithoutExtensions)
    {
        MockHost host;
        LifecycleManager manager(host);

        ASSERT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);

        constexpr std::array<Phase, 6> framePhases = {
            Phase::Input,
            Phase::PreSimulation,
            Phase::PostSimulation,
            Phase::Animation,
            Phase::PreRender,
            Phase::Ui,
        };

        constexpr float frameDt = 1.0f / 60.0f;

        // Simulate 120 frames, verifying all 6 phases execute without error or crash
        for (int frame = 0; frame < 120; ++frame)
        {
            for (const Phase phase : framePhases)
            {
                EXPECT_NO_THROW(manager.runPhase(phase, frameDt));
            }
        }

        manager.shutdown();
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);
    }

    // The remaining phases keep their order once the fixed-step phases are gone (task 0019, design item 2).
    static_assert(Phase::Input < Phase::PreSimulation);
    static_assert(Phase::PreSimulation < Phase::PostSimulation);
    static_assert(Phase::PostSimulation < Phase::Animation);
    static_assert(Phase::Animation < Phase::PreRender);
    static_assert(Phase::PreRender < Phase::Ui);

    // SDK 3.0 (task 0031, stage C): the runtime editor hooks are gone, so Host has no editor accessor and a host
    // class needs to override only getHostInfo.
    template <class T>
    concept HasEditorAccessor = requires(T& host) { host.getEditorService(); };
    static_assert(!HasEditorAccessor<Host>);
    static_assert(!HasEditorAccessor<MockHost>);

    TEST(EmptyHostTest, HostImplHasEveryServiceExceptPhysicsUntilAWorldIsAttached)
    {
        openeden::extension::HostImpl host("TestOpenEdenHost");

        EXPECT_EQ(host.getHostInfo().hostName, "TestOpenEdenHost");
        EXPECT_EQ(host.getHostInfo().sdkVersion, openeden::sdk::kSdkVersion);

        EXPECT_NE(host.getEventBus(), nullptr);
        EXPECT_NE(host.getEntityManager(), nullptr);
        EXPECT_NE(host.getSettingsService(), nullptr);
        EXPECT_NE(host.getVfsService(), nullptr);
        EXPECT_NE(host.getLoggingService(), nullptr);
        EXPECT_NE(host.getProfilerService(), nullptr);
        EXPECT_NE(host.getScratchMemoryService(), nullptr);
        EXPECT_NE(host.getDebugDrawService(), nullptr);

        // The host never creates a world itself (decision 0003)
        EXPECT_EQ(host.getPhysicsQueryService(), nullptr);
    }

    TEST(EmptyHostTest, BareHostProvidesNoServices)
    {
        MockHost host;
        EXPECT_EQ(host.getEventBus(), nullptr);
        EXPECT_EQ(host.getPhysicsQueryService(), nullptr);
    }

    TEST(EmptyHostTest, ShutdownCompletesCleanlyAndIsIdempotent)
    {
        MockHost host;
        LifecycleManager manager(host);

        ASSERT_TRUE(manager.initialize());
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);

        // First shutdown
        EXPECT_NO_THROW(manager.shutdown());
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);

        // Calling shutdown again should complete cleanly
        EXPECT_NO_THROW(manager.shutdown());
        EXPECT_EQ(manager.getActiveExtensionCount(), 0u);
    }

    struct TestFrameEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 12345;
        int counter = 0;
    };

    TEST(EmptyHostTest, HostImplProcessesDeferredEventsAtPhaseBoundary)
    {
        openeden::extension::HostImpl host("TestOpenEdenHost");
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

    TEST(EmptyHostTest, HostImplDeliversDeferredEventsInFramePhaseOrder)
    {
        openeden::extension::HostImpl host("TestOpenEdenHost");
        LifecycleManager manager(host);

        std::vector<int> received;
        host.getEventBus()->subscribe<TestFrameEvent>(
            "test_subscriber", [&received](const TestFrameEvent& e) { received.push_back(e.counter); });

        // Published latest phase first: only the phase order decides the delivery order
        constexpr std::array<Phase, 6> framePhases = { Phase::Input, Phase::PreSimulation, Phase::PostSimulation,
            Phase::Animation, Phase::PreRender, Phase::Ui };
        for (int i = static_cast<int>(framePhases.size()) - 1; i >= 0; --i)
            host.getEventBus()->publishDeferred(TestFrameEvent{ i }, framePhases[static_cast<std::size_t>(i)]);

        for (const Phase phase : framePhases)
            manager.runPhase(phase, 0.016f);

        EXPECT_EQ(received, (std::vector<int>{ 0, 1, 2, 3, 4, 5 }));
    }
}
