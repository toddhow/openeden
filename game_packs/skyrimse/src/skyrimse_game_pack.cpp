#include "game_skyrimse/skyrimse_game_pack.hpp"

#include "game_skyrimse/neutral_world.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/world_view.hpp"

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>

#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <components/files/conversion.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        // Owns the content loader it registers (the host keeps a reference until stop()), the world view and the
        // neutral world source over it (the source is declared after the view it refers to).
        class SkyrimGamePack final : public openeden::sdk::Extension
        {
        public:
            void registerServices(openeden::sdk::Registrar&) override {}

            bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

            // The null game rules (design doc game-packs.md 5.5): the pack identifies Skyrim SE content, then refuses
            // the session. A game pack's failure in start() is a session error that names it and refuses saving, so
            // the player sees the message at the main menu; the editor and tools never start the pack and run.
            void start(openeden::sdk::Host& host) override
            {
                openeden::sdk::ContentService* const content = host.getContentService();
                if (content == nullptr)
                    throw std::runtime_error("the content service is missing");
                content->registerLoader(mLoader);
                // The read-only world view (openeden/game/world_view.hpp). It starts empty: the plugins that fill it
                // come from the game's content list, which no host hands to a pack yet (task 0086 stage log); a host
                // that knows the load order calls addPlugin / load on it.
                if (openeden::sdk::game::WorldViewService* const worldViews = host.getWorldViewService())
                {
                    worldViews->registerWorldView(mWorldView);
                    mWorldViews = worldViews;
                }
                // The neutral world source (task 0103, stage N3a-1): when the host reads sources (the engine with
                // [Game] neutral world), the pack hands it the source over its world view and the session goes on,
                // with no rules; the world view is still empty until a host loads plugins into it (the plugin list
                // reaches no pack yet). A host that does not read sources gets the old behaviour below.
                openeden::sdk::LoggingService* const logging = host.getLoggingService();
                openeden::sdk::Logger* const logger = logging != nullptr ? logging->getLogger(kPackId) : nullptr;
                openeden::sdk::game::WorldSourceService* const sources = host.getWorldSourceService();
                if (sources != nullptr && sources->consumerEnabled())
                {
                    // Task 0107 (stage N3a-2b): the view is filled from the session's content list before the source
                    // is registered over it. The list, the VFS for the string tables and the language setting all come
                    // from the host; none of this runs when the host does not read sources.
                    loadSessionContent(host, *sources, logger);
                    std::string error;
                    if (!sources->registerSource(mCellSource, error))
                        throw std::runtime_error("the world source was refused: " + error);
                    mWorldSources = sources;
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "Skyrim SE world source registered; the rules are not implemented, so only the world is "
                            "available");
                    probeCell(logger);
                    return;
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Warning, kRulesNotImplementedMessage);
                throw std::runtime_error(std::string(kRulesNotImplementedMessage));
            }

            void stop() override
            {
                withdrawWorldSource();
                withdrawWorldView();
            }

            ~SkyrimGamePack() override
            {
                withdrawWorldSource();
                withdrawWorldView();
            }

        private:
            // Builds the world view from the plugins of the session that this pack's content loader accepts. Plugins
            // the view refuses are named once and left out.
            void loadSessionContent(openeden::sdk::Host& host, openeden::sdk::game::WorldSourceService& sources,
                openeden::sdk::Logger* logger)
            {
                auto say = [logger](openeden::sdk::LogLevel level, const std::string& text) {
                    if (logger != nullptr)
                        logger->log(level, text);
                };
                SkyrimWorldView::Options options;
                if (const openeden::sdk::SettingsService* settings = host.getSettingsService())
                {
                    const std::string wanted
                        = settings->getString(kPackId, kStringTableLanguageKey, kDefaultStringTableLanguage);
                    bool usedDefault = false;
                    options.mLanguage = sanitiseStringTableLanguage(wanted, usedDefault);
                    if (usedDefault)
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kStringTableLanguageKey)
                                + "' is not a language name (2 to 32 ASCII letters); using " + options.mLanguage);
                }
                if (const openeden::sdk::VfsService* vfs = host.getVfsService())
                {
                    mFileSource = std::make_unique<VfsFileSource>(*vfs);
                }
                if (!mWorldView.configure(std::move(options), mFileSource.get()))
                    return;

                const auto started = std::chrono::steady_clock::now();
                std::vector<openeden::sdk::game::SessionContentFile> files;
                const std::size_t given = sources.sessionContent(files);
                std::size_t taken = 0;
                for (const openeden::sdk::game::SessionContentFile& file : files)
                {
                    std::string error;
                    if (mWorldView.addPlugin(Files::pathFromUnicodeString(file.path), error))
                        ++taken;
                    else
                        say(openeden::sdk::LogLevel::Warning, "plugin left out: " + error);
                }
                if (given == 0)
                    return; // an empty view, as before this stage
                mWorldView.finish();
                const SkyrimWorldView::Stats& stats = mWorldView.stats();
                // The pack's start() runs on the main thread inside Game::Game with no message pumping: the time is
                // what the freeze monitor (5 s) would see.
                const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                say(openeden::sdk::LogLevel::Info,
                    "world view built from the session: " + std::to_string(taken) + " of " + std::to_string(given)
                        + " plugins, " + std::to_string(stats.mCells) + " cells, " + std::to_string(stats.mWorlds)
                        + " worlds, " + std::to_string(stats.mBaseForms) + " base forms, "
                        + std::to_string(stats.mStringTablesMissing) + " string tables missing, language "
                        + mWorldView.language() + ", " + std::to_string(millis.count()) + " ms");
            }

            // A test aid: OPENEDEN_SKYRIMSE_PROBE_CELL names an editor id; the log says whether the cell is found and
            // how many objects it has. It never changes behaviour.
            void probeCell(openeden::sdk::Logger* logger)
            {
                const char* const wanted = std::getenv("OPENEDEN_SKYRIMSE_PROBE_CELL");
                if (wanted == nullptr || *wanted == '\0' || logger == nullptr)
                    return;
                const openeden::sdk::game::WorldId id = mCellSource.findCellByEditorId(wanted);
                if (id == openeden::sdk::game::kNoWorldId)
                {
                    logger->log(openeden::sdk::LogLevel::Info, "probe cell found: no");
                    return;
                }
                openeden::sdk::game::WorldCell cell;
                std::string error;
                if (!mCellSource.buildCell(id, cell, error))
                {
                    logger->log(openeden::sdk::LogLevel::Info, "probe cell found: yes, but it did not build");
                    return;
                }
                logger->log(openeden::sdk::LogLevel::Info,
                    "probe cell found: yes, " + std::to_string(cell.objects.size()) + " objects, "
                        + std::to_string(cell.droppedObjects) + " dropped");
            }

            void withdrawWorldSource()
            {
                if (mWorldSources != nullptr)
                    mWorldSources->withdrawSource(mCellSource);
                mWorldSources = nullptr;
            }

            void withdrawWorldView()
            {
                if (mWorldViews != nullptr)
                    mWorldViews->withdrawWorldView(mWorldView);
                mWorldViews = nullptr;
            }

            SkyrimContentLoader mLoader;
            std::unique_ptr<VfsFileSource> mFileSource; // declared before the view that may refer to it
            SkyrimWorldView mWorldView;
            SkyrimCellSource mCellSource{ mWorldView };
            openeden::sdk::game::WorldViewService* mWorldViews = nullptr;
            openeden::sdk::game::WorldSourceService* mWorldSources = nullptr;
        };
    }

    std::unique_ptr<openeden::sdk::Extension> createSkyrimGamePack()
    {
        return std::make_unique<SkyrimGamePack>();
    }
}

#ifndef OPENEDEN_SKYRIMSE_NO_ENTRY
static const openeden::sdk::ExtensionDescriptor sDescriptor{ openeden::sdk::kSdkVersion, game_skyrimse::kPackId,
    &game_skyrimse::createSkyrimGamePack };

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
#endif
