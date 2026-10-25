#include <components/plugin/lifecycle_manager.hpp>
#include <components/plugin/manifest.hpp>
#include <components/plugin/service_registry.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
#include <openeden/plugin.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using openeden::plugin::ContractRequirement;
    using openeden::plugin::LifecycleManager;
    using openeden::plugin::PluginManifest;
    using openeden::plugin::ServiceRegistry;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::Phase;
    using openeden::sdk::Plugin;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;
    using openeden::sdk::Version;

    // Sample contract for testing
    class TestContract
    {
    public:
        static constexpr std::string_view kId = "openeden.test";
        static constexpr Version kVersion{ 1, 0 };

        virtual ~TestContract() = default;
        virtual int compute(int a, int b) const = 0;
    };

    class TestContractImpl : public TestContract
    {
    public:
        int compute(int a, int b) const override { return a * b; }
    };

    class UnregisteredContract
    {
    public:
        static constexpr std::string_view kId = "openeden.unregistered";
        static constexpr Version kVersion{ 1, 0 };

        virtual ~UnregisteredContract() = default;
    };

    class MockHost : public Host
    {
    public:
        HostInfo mHostInfo{ openeden::sdk::kSdkVersion, "OpenEdenTestHost" };

        const HostInfo& getHostInfo() const override { return mHostInfo; }
    };

    PluginManifest makeManifest(std::string id, std::vector<ContractRequirement> providedContracts = {},
        std::vector<ContractRequirement> requiredContracts = {},
        std::vector<ContractRequirement> optionalContracts = {}, std::vector<std::string> loadAfter = {})
    {
        PluginManifest m;
        m.id = std::move(id);
        m.name = m.id;
        m.pluginVersion = "1.0.0";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        m.providedContracts = std::move(providedContracts);
        m.requiredContracts = std::move(requiredContracts);
        m.optionalContracts = std::move(optionalContracts);
        m.loadAfter = std::move(loadAfter);
        return m;
    }

    // Plugin that tracks lifecycle call sequence
    class TrackingPlugin : public Plugin
    {
    public:
        TrackingPlugin(std::string id, std::vector<std::string>& log, bool resolveSuccess = true)
            : mId(std::move(id))
            , mLog(log)
            , mResolveSuccess(resolveSuccess)
        {
        }

        void registerServices(Registrar& /*registrar*/) override { mLog.push_back(mId + "::registerServices"); }

        bool resolve(const ServiceLocator& /*locator*/) override
        {
            mLog.push_back(mId + "::resolve");
            return mResolveSuccess;
        }

        void start(Host& /*host*/) override { mLog.push_back(mId + "::start"); }

        void stop() override { mLog.push_back(mId + "::stop"); }

    private:
        std::string mId;
        std::vector<std::string>& mLog;
        bool mResolveSuccess;
    };

    // Provider plugin
    class ProviderPlugin : public Plugin
    {
    public:
        void registerServices(Registrar& registrar) override { registrar.provide<TestContract>(mImpl); }

        bool resolve(const ServiceLocator& /*locator*/) override { return true; }

        void start(Host& /*host*/) override {}

        void stop() override {}

        TestContractImpl mImpl;
    };

    // Consumer plugin
    class ConsumerPlugin : public Plugin
    {
    public:
        void registerServices(Registrar& /*registrar*/) override {}

        bool resolve(const ServiceLocator& locator) override
        {
            TestContract* found = locator.find<TestContract>();
            if (!found)
                return false;

            TestContract& required = locator.require<TestContract>();
            mCalculationResult = required.compute(6, 7);
            mResolvedSuccessfully = true;
            return true;
        }

        void start(Host& /*host*/) override {}

        void stop() override {}

        int mCalculationResult = 0;
        bool mResolvedSuccessfully = false;
    };

    // Direct ServiceRegistry tests
    TEST(ServiceRegistryTest, DirectRegistrationAndLookup)
    {
        ServiceRegistry registry;
        TestContractImpl impl;

        // Before registration, lookup should return nullptr / throw
        EXPECT_EQ(registry.find<TestContract>(), nullptr);
        EXPECT_THROW(registry.require<TestContract>(), std::exception);

        // Register implementation
        registry.provide<TestContract>(impl);

        // After registration, find returns pointer, require returns reference
        TestContract* found = registry.find<TestContract>();
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->compute(3, 4), 12);

        TestContract& required = registry.require<TestContract>();
        EXPECT_EQ(required.compute(4, 5), 20);

        // Unregistered contract still fails
        EXPECT_EQ(registry.find<UnregisteredContract>(), nullptr);
        EXPECT_THROW(registry.require<UnregisteredContract>(), std::exception);
    }

    // Lifecycle call sequence test: register -> resolve -> start in load order; stop in REVERSE order
    TEST(LifecycleTest, CallSequenceAndReverseStopOrder)
    {
        std::vector<std::string> log;
        MockHost host;
        LifecycleManager manager(host);

        // Plugin B requires contract from Plugin A -> load order must be A, then B
        const PluginManifest manifestA = makeManifest("pluginA", { { "openeden.test", { 1, 0 } } });
        const PluginManifest manifestB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 0 } } });

        // Add in reverse order
        manager.addPlugin(manifestB, std::make_unique<TrackingPlugin>("pluginB", log));
        manager.addPlugin(manifestA, std::make_unique<TrackingPlugin>("pluginA", log));

        EXPECT_TRUE(manager.initialize());

        const std::vector<std::string> expectedInitSequence{
            "pluginA::registerServices",
            "pluginB::registerServices",
            "pluginA::resolve",
            "pluginB::resolve",
            "pluginA::start",
            "pluginB::start",
        };
        EXPECT_EQ(log, expectedInitSequence);

        // Shutdown: stop must run in REVERSE load order (pluginB stopped before provider pluginA)
        manager.shutdown();

        const std::vector<std::string> expectedFullSequence{
            "pluginA::registerServices",
            "pluginB::registerServices",
            "pluginA::resolve",
            "pluginB::resolve",
            "pluginA::start",
            "pluginB::start",
            "pluginB::stop",
            "pluginA::stop",
        };
        EXPECT_EQ(log, expectedFullSequence);
    }

    // Contract lookup and invocation between provider and consumer
    TEST(LifecycleTest, ContractLookupAndUsageInLifecycle)
    {
        MockHost host;
        LifecycleManager manager(host);

        const PluginManifest manifestProvider = makeManifest("provider", { { "openeden.test", { 1, 0 } } });
        const PluginManifest manifestConsumer = makeManifest("consumer", {}, { { "openeden.test", { 1, 0 } } });

        auto provider = std::make_unique<ProviderPlugin>();
        auto consumer = std::make_unique<ConsumerPlugin>();
        ConsumerPlugin* consumerPtr = consumer.get();

        manager.addPlugin(manifestConsumer, std::move(consumer));
        manager.addPlugin(manifestProvider, std::move(provider));

        EXPECT_TRUE(manager.initialize());

        EXPECT_TRUE(consumerPtr->mResolvedSuccessfully);
        EXPECT_EQ(consumerPtr->mCalculationResult, 42); // 6 * 7

        manager.shutdown();
    }

    // Phase callback dispatch
    TEST(LifecycleTest, PhaseCallbackDispatchOrderAndParameters)
    {
        MockHost host;
        LifecycleManager manager(host);
        std::vector<std::string> callbackLog;

        float recordedDt = 0.0f;
        std::uint64_t recordedStep = 0;

        class CallbackPluginA : public Plugin
        {
        public:
            CallbackPluginA(std::vector<std::string>& log, float& dtOut, std::uint64_t& stepOut)
                : mLog(log)
                , mDt(dtOut)
                , mStep(stepOut)
            {
            }

            void registerServices(Registrar& registrar) override
            {
                // Register two callbacks for Phase::Input
                registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) { mLog.push_back("A::Input_1"); });
                registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) { mLog.push_back("A::Input_2"); });
                // Register fixed step callback
                registrar.registerPhaseCallback(Phase::FixedStepPrePhysics, [this](float dt, std::uint64_t step) {
                    mDt = dt;
                    mStep = step;
                    mLog.push_back("A::FixedStepPrePhysics");
                });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

            std::vector<std::string>& mLog;
            float& mDt;
            std::uint64_t& mStep;
        };

        class CallbackPluginB : public Plugin
        {
        public:
            CallbackPluginB(std::vector<std::string>& log)
                : mLog(log)
            {
            }

            void registerServices(Registrar& registrar) override
            {
                // Register one callback for Phase::Input
                registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) { mLog.push_back("B::Input_1"); });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

            std::vector<std::string>& mLog;
        };

        const PluginManifest manifestA = makeManifest("pluginA", { { "contractA", { 1, 0 } } });
        const PluginManifest manifestB = makeManifest("pluginB", {}, { { "contractA", { 1, 0 } } });

        manager.addPlugin(manifestA, std::make_unique<CallbackPluginA>(callbackLog, recordedDt, recordedStep));
        manager.addPlugin(manifestB, std::make_unique<CallbackPluginB>(callbackLog));

        EXPECT_TRUE(manager.initialize());

        // Run frame phase: executes in plugin load order, and registration order within plugin
        manager.runPhase(Phase::Input, 0.016f);
        const std::vector<std::string> expectedInputLog{ "A::Input_1", "A::Input_2", "B::Input_1" };
        EXPECT_EQ(callbackLog, expectedInputLog);

        callbackLog.clear();

        // Run fixed step phase: passes dt and stepIndex
        manager.runFixedStepPhase(Phase::FixedStepPrePhysics, 0.02f, 4242);
        EXPECT_FLOAT_EQ(recordedDt, 0.02f);
        EXPECT_EQ(recordedStep, 4242u);
        const std::vector<std::string> expectedFixedLog{ "A::FixedStepPrePhysics" };
        EXPECT_EQ(callbackLog, expectedFixedLog);

        manager.shutdown();
    }

    // Resolve returning false disables plugin, cascades to dependents, withdraws registrations, independent plugins
    // proceed
    TEST(LifecycleTest, ResolveReturningFalseDisablesPluginAndDependents)
    {
        std::vector<std::string> log;
        MockHost host;
        LifecycleManager manager(host);

        // Plugin A provides contractA
        const PluginManifest manifestA = makeManifest("pluginA", { { "contractA", { 1, 0 } } });
        // Plugin B requires contractA, provides contractB, but resolve() returns false
        const PluginManifest manifestB
            = makeManifest("pluginB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        // Plugin C requires contractB (depends on B)
        const PluginManifest manifestC = makeManifest("pluginC", {}, { { "contractB", { 1, 0 } } });
        // Plugin D is independent
        const PluginManifest manifestD = makeManifest("pluginD");

        manager.addPlugin(manifestA, std::make_unique<TrackingPlugin>("pluginA", log, true));
        manager.addPlugin(manifestB, std::make_unique<TrackingPlugin>("pluginB", log, false)); // returns false
        manager.addPlugin(manifestC, std::make_unique<TrackingPlugin>("pluginC", log, true));
        manager.addPlugin(manifestD, std::make_unique<TrackingPlugin>("pluginD", log, true));

        manager.initialize();

        // Plugin A and D should be active
        EXPECT_TRUE(manager.isPluginActive("pluginA"));
        EXPECT_TRUE(manager.isPluginActive("pluginD"));

        // Plugin B and C should NOT be active
        EXPECT_FALSE(manager.isPluginActive("pluginB"));
        EXPECT_FALSE(manager.isPluginActive("pluginC"));

        // Neither B nor C should have start() called
        for (const auto& entry : log)
        {
            EXPECT_NE(entry, "pluginB::start");
            EXPECT_NE(entry, "pluginC::start");
        }

        // A and D must have start() called
        EXPECT_TRUE(std::find(log.begin(), log.end(), "pluginA::start") != log.end());
        EXPECT_TRUE(std::find(log.begin(), log.end(), "pluginD::start") != log.end());

        manager.shutdown();
    }

    // Exception containment: throwing inside phase callback is caught, plugin marked faulted, other plugins proceed
    TEST(LifecycleTest, ExceptionContainmentInPhaseCallbacks)
    {
        MockHost host;
        LifecycleManager manager(host);
        std::vector<std::string> executionLog;

        class ThrowingPlugin : public Plugin
        {
        public:
            void registerServices(Registrar& registrar) override
            {
                registrar.registerPhaseCallback(
                    Phase::Input, [](float /*dt*/) { throw std::runtime_error("Simulated phase callback crash"); });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}
        };

        class SurvivingPlugin : public Plugin
        {
        public:
            SurvivingPlugin(std::vector<std::string>& log)
                : mLog(log)
            {
            }

            void registerServices(Registrar& registrar) override
            {
                registrar.registerPhaseCallback(
                    Phase::Input, [this](float /*dt*/) { mLog.push_back("SurvivingPlugin::executed"); });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

            std::vector<std::string>& mLog;
        };

        const PluginManifest manifestA = makeManifest("faultyPlugin");
        const PluginManifest manifestB = makeManifest("survivingPlugin");

        manager.addPlugin(manifestA, std::make_unique<ThrowingPlugin>());
        manager.addPlugin(manifestB, std::make_unique<SurvivingPlugin>(executionLog));

        EXPECT_TRUE(manager.initialize());

        // First runPhase: faultyPlugin throws, but exception must NOT propagate out of manager
        EXPECT_NO_THROW(manager.runPhase(Phase::Input, 0.016f));

        // Surviving plugin still received callback
        EXPECT_EQ(executionLog.size(), 1u);
        EXPECT_EQ(executionLog[0], "SurvivingPlugin::executed");

        // Faulty plugin must be marked faulted / disabled
        EXPECT_TRUE(manager.isPluginFaulted("faultyPlugin"));
        EXPECT_FALSE(manager.isPluginActive("faultyPlugin"));

        // Subsequent frame: faulty plugin must not be called; surviving plugin continues normally
        executionLog.clear();
        EXPECT_NO_THROW(manager.runPhase(Phase::Input, 0.016f));
        EXPECT_EQ(executionLog.size(), 1u);
        EXPECT_EQ(executionLog[0], "SurvivingPlugin::executed");

        manager.shutdown();
    }

    TEST(LifecycleTest, DestructionOrderFollowsReverseLoadOrder)
    {
        std::vector<std::string> destructLog;

        class DestructorTrackingPlugin : public Plugin
        {
        public:
            DestructorTrackingPlugin(std::string id, std::vector<std::string>& log)
                : mId(std::move(id))
                , mLog(log)
            {
            }

            ~DestructorTrackingPlugin() override { mLog.push_back(mId + "::~Plugin"); }

            void registerServices(Registrar& /*registrar*/) override {}
            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

        private:
            std::string mId;
            std::vector<std::string>& mLog;
        };

        MockHost host;
        {
            LifecycleManager manager(host);
            // Plugin B requires contract from Plugin A -> load order A, then B
            const PluginManifest manifestA = makeManifest("pluginA", { { "openeden.test", { 1, 0 } } });
            const PluginManifest manifestB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 0 } } });

            manager.addPlugin(manifestB, std::make_unique<DestructorTrackingPlugin>("pluginB", destructLog));
            manager.addPlugin(manifestA, std::make_unique<DestructorTrackingPlugin>("pluginA", destructLog));

            EXPECT_TRUE(manager.initialize());
            manager.shutdown();
        }

        // Destruction must occur in reverse load order (consumer pluginB destroyed before provider pluginA)
        const std::vector<std::string> expectedDestructSequence{
            "pluginB::~Plugin",
            "pluginA::~Plugin",
        };
        EXPECT_EQ(destructLog, expectedDestructSequence);
    }
}
