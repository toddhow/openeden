#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/service_registry.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
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
    using openeden::extension::ContractRequirement;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::LifecycleManager;
    using openeden::extension::ServiceRegistry;
    using openeden::sdk::Extension;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::Phase;
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

    ExtensionManifest makeManifest(std::string id, std::vector<ContractRequirement> providedContracts = {},
        std::vector<ContractRequirement> requiredContracts = {},
        std::vector<ContractRequirement> optionalContracts = {}, std::vector<std::string> loadAfter = {})
    {
        ExtensionManifest m;
        m.id = std::move(id);
        m.name = m.id;
        m.extensionVersion = "1.0.0";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        m.providedContracts = std::move(providedContracts);
        m.requiredContracts = std::move(requiredContracts);
        m.optionalContracts = std::move(optionalContracts);
        m.loadAfter = std::move(loadAfter);
        return m;
    }

    // Extension that tracks lifecycle call sequence
    class TrackingExtension : public Extension
    {
    public:
        TrackingExtension(std::string id, std::vector<std::string>& log, bool resolveSuccess = true)
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

    // Provider extension
    class ProviderExtension : public Extension
    {
    public:
        void registerServices(Registrar& registrar) override { registrar.provide<TestContract>(mImpl); }

        bool resolve(const ServiceLocator& /*locator*/) override { return true; }

        void start(Host& /*host*/) override {}

        void stop() override {}

        TestContractImpl mImpl;
    };

    // Consumer extension
    class ConsumerExtension : public Extension
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

        // Extension B requires contract from Extension A -> load order must be A, then B
        const ExtensionManifest manifestA = makeManifest("extensionA", { { "openeden.test", { 1, 0 } } });
        const ExtensionManifest manifestB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 0 } } });

        // Add in reverse order
        manager.addExtension(manifestB, std::make_unique<TrackingExtension>("extensionB", log));
        manager.addExtension(manifestA, std::make_unique<TrackingExtension>("extensionA", log));

        EXPECT_TRUE(manager.initialize());

        const std::vector<std::string> expectedInitSequence{
            "extensionA::registerServices",
            "extensionB::registerServices",
            "extensionA::resolve",
            "extensionB::resolve",
            "extensionA::start",
            "extensionB::start",
        };
        EXPECT_EQ(log, expectedInitSequence);

        // Shutdown: stop must run in REVERSE load order (extensionB stopped before provider extensionA)
        manager.shutdown();

        const std::vector<std::string> expectedFullSequence{
            "extensionA::registerServices",
            "extensionB::registerServices",
            "extensionA::resolve",
            "extensionB::resolve",
            "extensionA::start",
            "extensionB::start",
            "extensionB::stop",
            "extensionA::stop",
        };
        EXPECT_EQ(log, expectedFullSequence);
    }

    // Contract lookup and invocation between provider and consumer
    TEST(LifecycleTest, ContractLookupAndUsageInLifecycle)
    {
        MockHost host;
        LifecycleManager manager(host);

        const ExtensionManifest manifestProvider = makeManifest("provider", { { "openeden.test", { 1, 0 } } });
        const ExtensionManifest manifestConsumer = makeManifest("consumer", {}, { { "openeden.test", { 1, 0 } } });

        auto provider = std::make_unique<ProviderExtension>();
        auto consumer = std::make_unique<ConsumerExtension>();
        ConsumerExtension* consumerPtr = consumer.get();

        manager.addExtension(manifestConsumer, std::move(consumer));
        manager.addExtension(manifestProvider, std::move(provider));

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

        class CallbackExtensionA : public Extension
        {
        public:
            CallbackExtensionA(std::vector<std::string>& log, float& dtOut)
                : mLog(log)
                , mDt(dtOut)
            {
            }

            void registerServices(Registrar& registrar) override
            {
                // Register two callbacks for Phase::Input
                registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) { mLog.push_back("A::Input_1"); });
                registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) { mLog.push_back("A::Input_2"); });
                // Register a callback for another phase that records its dt
                registrar.registerPhaseCallback(Phase::PostSimulation, [this](float dt) {
                    mDt = dt;
                    mLog.push_back("A::PostSimulation");
                });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

            std::vector<std::string>& mLog;
            float& mDt;
        };

        class CallbackExtensionB : public Extension
        {
        public:
            CallbackExtensionB(std::vector<std::string>& log)
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

        const ExtensionManifest manifestA = makeManifest("extensionA", { { "contractA", { 1, 0 } } });
        const ExtensionManifest manifestB = makeManifest("extensionB", {}, { { "contractA", { 1, 0 } } });

        manager.addExtension(manifestA, std::make_unique<CallbackExtensionA>(callbackLog, recordedDt));
        manager.addExtension(manifestB, std::make_unique<CallbackExtensionB>(callbackLog));

        EXPECT_TRUE(manager.initialize());

        // Run frame phase: executes in extension load order, and registration order within extension
        manager.runPhase(Phase::Input, 0.016f);
        const std::vector<std::string> expectedInputLog{ "A::Input_1", "A::Input_2", "B::Input_1" };
        EXPECT_EQ(callbackLog, expectedInputLog);

        callbackLog.clear();

        // Run another frame phase: passes dt, and runs only that phase's callbacks
        manager.runPhase(Phase::PostSimulation, 0.02f);
        EXPECT_FLOAT_EQ(recordedDt, 0.02f);
        const std::vector<std::string> expectedPostSimulationLog{ "A::PostSimulation" };
        EXPECT_EQ(callbackLog, expectedPostSimulationLog);

        manager.shutdown();
    }

    // Resolve returning false disables extension, cascades to dependents, withdraws registrations, independent
    // extensions proceed
    TEST(LifecycleTest, ResolveReturningFalseDisablesExtensionAndDependents)
    {
        std::vector<std::string> log;
        MockHost host;
        LifecycleManager manager(host);

        // Extension A provides contractA
        const ExtensionManifest manifestA = makeManifest("extensionA", { { "contractA", { 1, 0 } } });
        // Extension B requires contractA, provides contractB, but resolve() returns false
        const ExtensionManifest manifestB
            = makeManifest("extensionB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        // Extension C requires contractB (depends on B)
        const ExtensionManifest manifestC = makeManifest("extensionC", {}, { { "contractB", { 1, 0 } } });
        // Extension D is independent
        const ExtensionManifest manifestD = makeManifest("extensionD");

        manager.addExtension(manifestA, std::make_unique<TrackingExtension>("extensionA", log, true));
        manager.addExtension(manifestB, std::make_unique<TrackingExtension>("extensionB", log, false)); // returns false
        manager.addExtension(manifestC, std::make_unique<TrackingExtension>("extensionC", log, true));
        manager.addExtension(manifestD, std::make_unique<TrackingExtension>("extensionD", log, true));

        manager.initialize();

        // Extension A and D should be active
        EXPECT_TRUE(manager.isExtensionActive("extensionA"));
        EXPECT_TRUE(manager.isExtensionActive("extensionD"));

        // Extension B and C should NOT be active
        EXPECT_FALSE(manager.isExtensionActive("extensionB"));
        EXPECT_FALSE(manager.isExtensionActive("extensionC"));

        // Neither B nor C should have start() called
        for (const auto& entry : log)
        {
            EXPECT_NE(entry, "extensionB::start");
            EXPECT_NE(entry, "extensionC::start");
        }

        // A and D must have start() called
        EXPECT_TRUE(std::find(log.begin(), log.end(), "extensionA::start") != log.end());
        EXPECT_TRUE(std::find(log.begin(), log.end(), "extensionD::start") != log.end());

        manager.shutdown();
    }

    // Exception containment: throwing inside phase callback is caught, extension marked faulted, other extensions
    // proceed
    TEST(LifecycleTest, ExceptionContainmentInPhaseCallbacks)
    {
        MockHost host;
        LifecycleManager manager(host);
        std::vector<std::string> executionLog;

        class ThrowingExtension : public Extension
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

        class SurvivingExtension : public Extension
        {
        public:
            SurvivingExtension(std::vector<std::string>& log)
                : mLog(log)
            {
            }

            void registerServices(Registrar& registrar) override
            {
                registrar.registerPhaseCallback(
                    Phase::Input, [this](float /*dt*/) { mLog.push_back("SurvivingExtension::executed"); });
            }

            bool resolve(const ServiceLocator& /*locator*/) override { return true; }
            void start(Host& /*host*/) override {}
            void stop() override {}

            std::vector<std::string>& mLog;
        };

        const ExtensionManifest manifestA = makeManifest("faultyExtension");
        const ExtensionManifest manifestB = makeManifest("survivingExtension");

        manager.addExtension(manifestA, std::make_unique<ThrowingExtension>());
        manager.addExtension(manifestB, std::make_unique<SurvivingExtension>(executionLog));

        EXPECT_TRUE(manager.initialize());

        // First runPhase: faultyExtension throws, but exception must NOT propagate out of manager
        EXPECT_NO_THROW(manager.runPhase(Phase::Input, 0.016f));

        // Surviving extension still received callback
        EXPECT_EQ(executionLog.size(), 1u);
        EXPECT_EQ(executionLog[0], "SurvivingExtension::executed");

        // Faulty extension must be marked faulted / disabled
        EXPECT_TRUE(manager.isExtensionFaulted("faultyExtension"));
        EXPECT_FALSE(manager.isExtensionActive("faultyExtension"));

        // Subsequent frame: faulty extension must not be called; surviving extension continues normally
        executionLog.clear();
        EXPECT_NO_THROW(manager.runPhase(Phase::Input, 0.016f));
        EXPECT_EQ(executionLog.size(), 1u);
        EXPECT_EQ(executionLog[0], "SurvivingExtension::executed");

        manager.shutdown();
    }

    TEST(LifecycleTest, DestructionOrderFollowsReverseLoadOrder)
    {
        std::vector<std::string> destructLog;

        class DestructorTrackingExtension : public Extension
        {
        public:
            DestructorTrackingExtension(std::string id, std::vector<std::string>& log)
                : mId(std::move(id))
                , mLog(log)
            {
            }

            ~DestructorTrackingExtension() override { mLog.push_back(mId + "::~Extension"); }

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
            // Extension B requires contract from Extension A -> load order A, then B
            const ExtensionManifest manifestA = makeManifest("extensionA", { { "openeden.test", { 1, 0 } } });
            const ExtensionManifest manifestB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 0 } } });

            manager.addExtension(manifestB, std::make_unique<DestructorTrackingExtension>("extensionB", destructLog));
            manager.addExtension(manifestA, std::make_unique<DestructorTrackingExtension>("extensionA", destructLog));

            EXPECT_TRUE(manager.initialize());
            manager.shutdown();
        }

        // Destruction must occur in reverse load order (consumer extensionB destroyed before provider extensionA)
        const std::vector<std::string> expectedDestructSequence{
            "extensionB::~Extension",
            "extensionA::~Extension",
        };
        EXPECT_EQ(destructLog, expectedDestructSequence);
    }
}

// ---- LifecycleManager::getFailures (task 0018): what the player is told ---------------------------------------------

namespace
{
    enum class FailAt
    {
        Nowhere,
        Start,
        Phase,
    };

    constexpr const char* kStartFailureText = "failure text from start";
    constexpr const char* kPhaseFailureText = "failure text from a phase";

    // Provides the contract if told to, and throws where it is told to.
    class FailingExtension : public Extension
    {
    public:
        FailingExtension(FailAt failAt, bool provides)
            : mFailAt(failAt)
            , mProvides(provides)
        {
        }

        void registerServices(Registrar& registrar) override
        {
            if (mProvides)
                registrar.provide<TestContract>(mImplementation);
            registrar.registerPhaseCallback(Phase::Input, [this](float /*dt*/) {
                if (mFailAt == FailAt::Phase)
                    throw std::runtime_error(kPhaseFailureText);
            });
        }

        bool resolve(const ServiceLocator& /*locator*/) override { return true; }

        void start(Host& /*host*/) override
        {
            if (mFailAt == FailAt::Start)
                throw std::runtime_error(kStartFailureText);
        }

        void stop() override {}

    private:
        FailAt mFailAt;
        bool mProvides;
        TestContractImpl mImplementation;
    };

    const ContractRequirement kFailureTestContract{ "openeden.test", { 1, 0 } };

    void addProvider(LifecycleManager& manager, const std::string& id, FailAt failAt)
    {
        manager.addExtension(
            makeManifest(id, { kFailureTestContract }), std::make_unique<FailingExtension>(failAt, true));
    }

    void addDependant(LifecycleManager& manager, const std::string& id)
    {
        manager.addExtension(
            makeManifest(id, {}, { kFailureTestContract }), std::make_unique<FailingExtension>(FailAt::Nowhere, false));
    }

    void addStandalone(LifecycleManager& manager, const std::string& id, FailAt failAt = FailAt::Nowhere)
    {
        manager.addExtension(makeManifest(id), std::make_unique<FailingExtension>(failAt, false));
    }

    void expectComplete(const LifecycleManager::Failure& failure)
    {
        EXPECT_FALSE(failure.stage.empty()) << failure.extensionId;
        EXPECT_FALSE(failure.reason.empty()) << failure.extensionId;
    }

    TEST(LifecycleFailures, HealthyExtensionsHaveNoFailuresAtAnyPoint)
    {
        MockHost host;
        LifecycleManager manager(host);
        addProvider(manager, "provider", FailAt::Nowhere);
        addDependant(manager, "dependant");
        addStandalone(manager, "standalone");

        EXPECT_TRUE(manager.getFailures().empty());
        manager.initialize();
        EXPECT_TRUE(manager.getFailures().empty());
        manager.runPhase(Phase::Input, 0.016f);
        manager.runPhase(Phase::Input, 0.016f);
        EXPECT_TRUE(manager.getFailures().empty());
        manager.shutdown();
        EXPECT_TRUE(manager.getFailures().empty());
    }

    TEST(LifecycleFailures, ThrowInStartGivesAnEntryForTheExtensionThenOneForItsDependant)
    {
        MockHost host;
        LifecycleManager manager(host);
        addProvider(manager, "provider", FailAt::Start);
        addDependant(manager, "dependant");
        addStandalone(manager, "standalone");

        manager.initialize();

        const auto& failures = manager.getFailures();
        ASSERT_EQ(failures.size(), 2u);

        EXPECT_EQ(failures[0].extensionId, "provider");
        expectComplete(failures[0]);
        EXPECT_NE(failures[0].stage.find("start"), std::string::npos) << failures[0].stage;
        EXPECT_NE(failures[0].reason.find(kStartFailureText), std::string::npos) << failures[0].reason;

        EXPECT_EQ(failures[1].extensionId, "dependant");
        expectComplete(failures[1]);

        EXPECT_TRUE(manager.isExtensionActive("standalone"));
    }

    TEST(LifecycleFailures, ThrowInAPhaseAddsOneEntryWhenItHasNoDependants)
    {
        MockHost host;
        LifecycleManager manager(host);
        addStandalone(manager, "faulty", FailAt::Phase);
        addStandalone(manager, "healthy");

        manager.initialize();
        EXPECT_TRUE(manager.getFailures().empty());

        manager.runPhase(Phase::Input, 0.016f);

        const auto& failures = manager.getFailures();
        ASSERT_EQ(failures.size(), 1u);
        EXPECT_EQ(failures[0].extensionId, "faulty");
        expectComplete(failures[0]);
        EXPECT_NE(failures[0].reason.find(kPhaseFailureText), std::string::npos) << failures[0].reason;

        // Disabled now: later frames must not add another entry, and shutting down must not either.
        manager.runPhase(Phase::Input, 0.016f);
        manager.runPhase(Phase::Input, 0.016f);
        EXPECT_EQ(manager.getFailures().size(), 1u);
        manager.shutdown();
        EXPECT_EQ(manager.getFailures().size(), 1u);
    }

    TEST(LifecycleFailures, ThrowInAPhaseAlsoGivesADependantAnEntryAfterTheExtension)
    {
        MockHost host;
        LifecycleManager manager(host);
        addProvider(manager, "provider", FailAt::Phase);
        addDependant(manager, "dependant");

        manager.initialize();
        manager.runPhase(Phase::Input, 0.016f);

        const auto& failures = manager.getFailures();
        ASSERT_EQ(failures.size(), 2u);
        EXPECT_EQ(failures[0].extensionId, "provider");
        EXPECT_EQ(failures[1].extensionId, "dependant");
        expectComplete(failures[0]);
        expectComplete(failures[1]);
    }

    TEST(LifecycleFailures, FailuresKeepTheOrderTheyHappenedIn)
    {
        MockHost host;
        LifecycleManager manager(host);
        // In load order the phase failure comes first, but the start failure happens first.
        addStandalone(manager, "a_fails_in_phase", FailAt::Phase);
        addStandalone(manager, "b_fails_in_start", FailAt::Start);

        manager.initialize();
        manager.runPhase(Phase::Input, 0.016f);

        const auto& failures = manager.getFailures();
        ASSERT_EQ(failures.size(), 2u);
        EXPECT_EQ(failures[0].extensionId, "b_fails_in_start");
        EXPECT_EQ(failures[1].extensionId, "a_fails_in_phase");
    }
}
