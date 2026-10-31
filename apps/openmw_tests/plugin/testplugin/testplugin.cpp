// A plugin built against the SDK only, compiled once per test ID (see apps/openmw_tests/CMakeLists.txt). It writes what
// happens to it to the file named by OPENEDEN_TEST_PLUGIN_LOG, so a test can read the order after the fact.
//
// Compile definitions:
//   OPENEDEN_TEST_PLUGIN_ID          the plugin's ID, as a string
//   OPENEDEN_TEST_PLUGIN_PROVIDES_A  provide the contract "openeden.test.a" v1.0
//   OPENEDEN_TEST_PLUGIN_REQUIRES_A  require that contract
//
// OPENEDEN_TEST_PLUGIN_THROW="<id>:start" or "<id>:phase" makes this plugin throw at that point.

#include <openeden/logging.hpp>
#include <openeden/plugin.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kPluginId = OPENEDEN_TEST_PLUGIN_ID;
    // The answer the provider gives and the consumer checks, to prove a contract call crosses between libraries.
    [[maybe_unused]] constexpr int kContractAnswer = 42;

    void record(std::string_view event)
    {
        // A sink can be called from any thread that logs.
        static std::mutex mutex;
        const char* const path = std::getenv("OPENEDEN_TEST_PLUGIN_LOG");
        if (path == nullptr)
            return;
        const std::lock_guard lock(mutex);
        std::ofstream file(path, std::ios::app | std::ios::binary);
        file << kPluginId << ' ' << event << '\n';
    }

    void throwIfAsked(std::string_view stage)
    {
        const char* const value = std::getenv("OPENEDEN_TEST_PLUGIN_THROW");
        if (value == nullptr)
            return;
        const std::string wanted = std::string(kPluginId) + ':' + std::string(stage);
        if (wanted == value)
            throw std::runtime_error("test throw " + wanted);
    }

    class TestAContract
    {
    public:
        static constexpr std::string_view kId = "openeden.test.a";
        static constexpr openeden::sdk::Version kVersion{ 1, 0 };

        virtual ~TestAContract() = default;
        [[nodiscard]] virtual int answer() const = 0;
    };

#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
    class TestAImplementation final : public TestAContract
    {
    public:
        [[nodiscard]] int answer() const override { return kContractAnswer; }
    };
#endif

    // Set while started. With OPENEDEN_TEST_PLUGIN_SINK_LOGS, the sink logs while it receives a line, which must not hang.
    openeden::sdk::Logger* sSinkLogger = nullptr;

    class RecordingSink final : public openeden::sdk::LogSink
    {
    public:
        void log(std::string_view /*pluginId*/, openeden::sdk::LogLevel /*level*/, std::string_view message) override
        {
            // Engine log lines arrive with their line ending.
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            record("sink " + std::string(message));
            if (sSinkLogger != nullptr && std::getenv("OPENEDEN_TEST_PLUGIN_SINK_LOGS") != nullptr)
                sSinkLogger->info("line from inside the sink");
        }
    };

    struct PhaseName
    {
        openeden::sdk::Phase mPhase;
        const char* mName;
    };

    constexpr PhaseName kPhases[] = {
        { openeden::sdk::Phase::Input, "Input" },
        { openeden::sdk::Phase::PreSimulation, "PreSimulation" },
        { openeden::sdk::Phase::PostSimulation, "PostSimulation" },
        { openeden::sdk::Phase::Animation, "Animation" },
        { openeden::sdk::Phase::PreRender, "PreRender" },
        { openeden::sdk::Phase::Ui, "Ui" },
    };

    class TestPlugin final : public openeden::sdk::Plugin
    {
    public:
        ~TestPlugin() override { record("destroy"); }

        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            record("register");
#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
            registrar.provide<TestAContract>(mImplementation);
#endif
            for (const PhaseName& phase : kPhases)
            {
                const std::string event = std::string("phase ") + phase.mName;
                registrar.registerPhaseCallback(phase.mPhase, [event](float /*dt*/) {
                    record(event);
                    throwIfAsked("phase");
                });
            }
        }

        bool resolve(const openeden::sdk::ServiceLocator& locator) override
        {
            record("resolve");
#ifdef OPENEDEN_TEST_PLUGIN_REQUIRES_A
            const TestAContract* const contract = locator.find<TestAContract>();
            if (contract == nullptr || contract->answer() != kContractAnswer)
                return false;
#else
            static_cast<void>(locator);
#endif
            return true;
        }

        void start(openeden::sdk::Host& host) override
        {
            record("start");
            throwIfAsked("start");

            mLogging = host.getLoggingService();
            if (mLogging == nullptr)
                return;
            // The sink goes first so that it sees this plugin's own line too.
            mSink = std::make_shared<RecordingSink>();
            mLogging->addSink(mSink);
            if (openeden::sdk::Logger* const logger = mLogging->getLogger(kPluginId))
            {
                sSinkLogger = logger;
                logger->info("hello from plugin");
            }
            throwIfAsked("start-after-sink");
        }

        void stop() override
        {
            sSinkLogger = nullptr;
            record("stop");
            if (mLogging != nullptr && mSink != nullptr)
                mLogging->removeSink(mSink);
            mSink.reset();
        }

    private:
#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
        TestAImplementation mImplementation;
#endif
        openeden::sdk::LoggingService* mLogging = nullptr;
        std::shared_ptr<RecordingSink> mSink;
    };

    std::unique_ptr<openeden::sdk::Plugin> createPlugin()
    {
        return std::make_unique<TestPlugin>();
    }

    const openeden::sdk::PluginDescriptor sDescriptor{ openeden::sdk::kSdkVersion, kPluginId, &createPlugin };
}

extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
