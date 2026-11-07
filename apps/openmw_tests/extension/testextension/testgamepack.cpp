// A game pack built against the SDK only, as a shared library (task 0084: the dynamic route of the game-pack tier).
// It writes what happens to it to the file named by OPENEDEN_TEST_EXTENSION_LOG, as testextension.cpp does.
//
// Compile definition:
//   OPENEDEN_TEST_GAME_PACK_ID  the pack's ID, as a string
//
// OPENEDEN_TEST_EXTENSION_THROW="<id>:phase" makes it throw in the GameSimulation phase.

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/records.hpp>
#include <openeden/game/save_chunks.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/game/ui.hpp>
#include <openeden/phases.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kPackId = OPENEDEN_TEST_GAME_PACK_ID;

    void record(std::string_view event)
    {
        static std::mutex mutex;
        const char* const path = std::getenv("OPENEDEN_TEST_EXTENSION_LOG");
        if (path == nullptr)
            return;
        const std::lock_guard lock(mutex);
        std::ofstream file(path, std::ios::app | std::ios::binary);
        file << kPackId << ' ' << event << '\n';
    }

    bool throwAsked()
    {
        const char* const value = std::getenv("OPENEDEN_TEST_EXTENSION_THROW");
        return value != nullptr && std::string(value) == std::string(kPackId) + ":phase";
    }

    class CounterChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        std::string_view chunkName() const override { return "counter"; }
        std::uint32_t chunkVersion() const override { return 1; }
        void write(openeden::sdk::ChunkWriter& writer) override { writer.writeValue(mCounter); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t) override { return reader.readValue(mCounter); }

        std::int32_t mCounter = 0;
    };

    class AnyLoader final : public openeden::sdk::ContentLoader
    {
    public:
        bool probe(const openeden::sdk::ContentHeader& header) const override { return header.gameTag == "TEST"; }
    };

    class TestGamePack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            registrar.registerPhaseCallback(openeden::sdk::Phase::GameSimulation, [](float) {
                record("phase GameSimulation");
                if (throwAsked())
                    throw std::runtime_error("test throw in a game phase");
            });
            registrar.registerPhaseCallback(
                openeden::sdk::Phase::PostSimulation, [](float) { record("phase PostSimulation"); });
            registrar.registerPhaseCallback(
                openeden::sdk::Phase::GameUpdateUi, [](float) { record("phase GameUpdateUi"); });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override
        {
            record("resolve");
            return true;
        }

        void start(openeden::sdk::Host& host) override
        {
            auto* chunks = host.getSaveChunkService();
            auto* records = host.getRecordService();
            auto* content = host.getContentService();
            auto* scripts = host.getScriptHostService();
            auto* ui = host.getUiService();
            if (chunks == nullptr || records == nullptr || content == nullptr || scripts == nullptr || ui == nullptr)
                throw std::runtime_error("a game service is missing");
            if (!chunks->registerHandler(mChunk)
                || !records->registerRecordType({ openeden::sdk::makeRecordTypeId('T', 'E', 'S', 'T'), "TEST", 16 })
                || !scripts->declareLanguage("testscript") || !ui->declareWindow("test_window", "Test"))
                throw std::runtime_error("a game service refused a registration");
            content->registerLoader(mLoader);
            record("start with every game service");
        }

        void stop() override { record("stop"); }

    private:
        CounterChunk mChunk;
        AnyLoader mLoader;
    };

    std::unique_ptr<openeden::sdk::Extension> createPack()
    {
        return std::make_unique<TestGamePack>();
    }

    const openeden::sdk::ExtensionDescriptor sDescriptor{ openeden::sdk::kSdkVersion, kPackId, &createPack };
}

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
