// A plugin built against the SDK only, compiled once per test ID (see apps/openmw_tests/CMakeLists.txt). It writes what
// happens to it to the file named by OPENEDEN_TEST_PLUGIN_LOG, so a test can read the order after the fact.
//
// Compile definitions:
//   OPENEDEN_TEST_PLUGIN_ID          the plugin's ID, as a string
//   OPENEDEN_TEST_PLUGIN_PROVIDES_A  provide the contract "openeden.test.a" v1.0
//   OPENEDEN_TEST_PLUGIN_REQUIRES_A  require that contract
//
// OPENEDEN_TEST_PLUGIN_THROW="<id>:start" or "<id>:phase" makes this plugin throw at that point.

#include <openeden/event_bus.hpp>
#include <openeden/logging.hpp>
#include <openeden/plugin.hpp>
#include <openeden/save.hpp>
#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

    // With OPENEDEN_TEST_PLUGIN_DATA, the plugin uses the data services, for tools/plugin_data_check/check.sh.
    constexpr const char* kChunkText = "chunk-from-plugin";
    constexpr std::uint32_t kChunkVersion = 3;

    class TestSaveHandler final : public openeden::sdk::SaveChunkHandler
    {
    public:
        [[nodiscard]] std::uint32_t currentVersion() const noexcept override { return kChunkVersion; }

        void writeSaveChunk(openeden::sdk::SaveChunkWriter& writer) override
        {
            writer.write(kChunkText, std::strlen(kChunkText));
        }

        void readSaveChunk(std::uint32_t chunkVersion, openeden::sdk::SaveChunkReader& reader) override
        {
            std::string text(reader.size(), '\0');
            reader.read(text.data(), text.size());
            record("chunk v" + std::to_string(chunkVersion) + " " + text);
        }
    };

    struct PingEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x54455354;

        std::int32_t count = 0;
        float value = 0.5f;
        bool flag = true;
        char text[16] = {};
    };

    struct UnmarkedEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x54455355;

        std::int32_t count = 0;
    };

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
                registrar.registerPhaseCallback(phase.mPhase, [this, event](float /*dt*/) {
                    record(event);
                    throwIfAsked("phase");
                    publishTestEvents();
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

            if (std::getenv("OPENEDEN_TEST_PLUGIN_DATA") != nullptr)
                startDataServices(host);
        }

        void stop() override
        {
            sSinkLogger = nullptr;
            record("stop");
            if (mSaves != nullptr)
                mSaves->unregisterHandler(kPluginId);
            if (mLogging != nullptr && mSink != nullptr)
                mLogging->removeSink(mSink);
            mSink.reset();
        }

    private:
        void startDataServices(openeden::sdk::Host& host)
        {
            const auto textOf = [&](std::string_view path) {
                const std::vector<std::uint8_t> bytes = host.getVfsService()->readFile(path);
                return std::string(bytes.begin(), bytes.end());
            };
            record("vfs " + textOf("openeden_test/hello.txt"));
            record("vfs-override " + textOf("openeden_test/overridden.txt"));

            openeden::sdk::SettingsService& settings = *host.getSettingsService();
            record("setting answer " + std::to_string(settings.getInt(kPluginId, "answer", -1)));
            record("setting broken " + std::to_string(settings.getInt(kPluginId, "broken", -1)));
            settings.setInt(kPluginId, "changed", 7);

            mSaveHandler = std::make_shared<TestSaveHandler>();
            mSaves = host.getSaveService();
            mSaves->registerHandler(kPluginId, mSaveHandler);

            mEvents = host.getEventBus();
            const openeden::sdk::ForwardedField fields[] = {
                openeden::sdk::forwardedField("count", &PingEvent::count, offsetof(PingEvent, count)),
                openeden::sdk::forwardedField("value", &PingEvent::value, offsetof(PingEvent, value)),
                openeden::sdk::forwardedField("flag", &PingEvent::flag, offsetof(PingEvent, flag)),
                openeden::sdk::forwardedField("text", &PingEvent::text, offsetof(PingEvent, text)),
            };
            mEvents->markForwardedToLua<PingEvent>(kPluginId, "ping", fields);
        }

        void publishTestEvents()
        {
            if (mEvents == nullptr || mPublished)
                return;
            mPublished = true;

            PingEvent event;
            event.count = 1;
            std::strcpy(event.text, "first");
            mEvents->publishImmediate(event);

            // Not valid UTF-8: dropped, and the events around it still arrive in order.
            PingEvent bad;
            bad.text[0] = '\xFF';
            mEvents->publishImmediate(bad);

            mEvents->publishImmediate(UnmarkedEvent{ 1 });

            event.count = 2;
            std::strcpy(event.text, "second");
            mEvents->publishImmediate(event);
        }

#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
        TestAImplementation mImplementation;
#endif
        openeden::sdk::LoggingService* mLogging = nullptr;
        std::shared_ptr<RecordingSink> mSink;
        std::shared_ptr<TestSaveHandler> mSaveHandler;
        openeden::sdk::SaveService* mSaves = nullptr;
        openeden::sdk::EventBus* mEvents = nullptr;
        bool mPublished = false;
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
