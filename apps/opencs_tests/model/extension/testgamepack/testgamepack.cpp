// A game pack built against the SDK only, as a shared library, for Eden Studio's host tests (task 0173, stage c). It
// behaves as the Skyrim SE pack does in the one way that matters here (game_packs/skyrimse/src/skyrimse_game_pack.cpp,
// start()): it registers a content loader and, when the host reads world sources, a world source named after the pack;
// when the host does not, it throws. Unlike the real pack it reads no data: the one cell it builds lists the names of
// the content files the host gave it, so a test can see what reached the pack.
//
// Compile definition:
//   OPENEDEN_TEST_GAME_PACK_ID  the pack's ID, as a string; it is also the game id of the world source
//
// Environment:
//   OPENEDEN_TEST_PACK_THROW=1  start() throws "test game pack asked to fail"
//
// The cell: id 1, an interior whose editor id is "Content:" followed by the content names joined by '|'.

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kPackId = OPENEDEN_TEST_GAME_PACK_ID;

    class Tes4Loader final : public openeden::sdk::ContentLoader
    {
    public:
        bool probe(const openeden::sdk::ContentHeader& header) const override { return header.gameTag == "TES4"; }
    };

    class ContentListSource final : public openeden::sdk::game::WorldCellSource
    {
    public:
        explicit ContentListSource(std::string editorId)
            : mEditorId(std::move(editorId))
        {
        }

        std::string_view gameId() const override { return kPackId; }
        std::size_t cellCount() const override { return 1; }
        std::size_t listCells(
            std::size_t first, std::size_t, std::vector<openeden::sdk::game::WorldId>& out) const override
        {
            if (first > 0)
            {
                return 0;
            }
            out.push_back(1);
            return 1;
        }
        bool buildCell(
            openeden::sdk::game::WorldId id, openeden::sdk::game::WorldCell& out, std::string& error) override
        {
            if (id != 1)
            {
                error = "no such cell";
                return false;
            }
            out = {};
            out.id = 1;
            out.editorId = mEditorId;
            out.interior = true;
            return true;
        }
        double unitsPerMetre() const override { return 70.0; }

    private:
        std::string mEditorId;
    };

    class TestGamePack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar&) override {}

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            const char* const fail = std::getenv("OPENEDEN_TEST_PACK_THROW");
            if (fail != nullptr && std::string(fail) == "1")
            {
                throw std::runtime_error("test game pack asked to fail");
            }
            openeden::sdk::ContentService* const content = host.getContentService();
            if (content == nullptr)
            {
                throw std::runtime_error("the content service is missing");
            }
            content->registerLoader(mLoader);

            openeden::sdk::game::WorldSourceService* const sources = host.getWorldSourceService();
            if (sources == nullptr || !sources->consumerEnabled())
            {
                throw std::runtime_error("test game pack rules are not implemented");
            }
            std::vector<openeden::sdk::game::SessionContentFile> files;
            sources->sessionContent(files);
            std::string editorId = "Content:";
            for (std::size_t i = 0; i < files.size(); ++i)
            {
                editorId += (i == 0 ? "" : "|") + files[i].name;
            }
            mSource = std::make_unique<ContentListSource>(editorId);
            std::string error;
            if (!sources->registerSource(*mSource, error))
            {
                throw std::runtime_error("the world source was refused: " + error);
            }
            mSources = sources;
        }

        void stop() override { withdraw(); }

        ~TestGamePack() override { withdraw(); }

    private:
        void withdraw()
        {
            if (mSources != nullptr && mSource != nullptr)
            {
                mSources->withdrawSource(*mSource);
            }
            mSources = nullptr;
        }

        Tes4Loader mLoader;
        std::unique_ptr<ContentListSource> mSource;
        openeden::sdk::game::WorldSourceService* mSources = nullptr;
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
