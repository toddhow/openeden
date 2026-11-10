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
                    std::string error;
                    if (!sources->registerSource(mCellSource, error))
                        throw std::runtime_error("the world source was refused: " + error);
                    mWorldSources = sources;
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "Skyrim SE world source registered; the rules are not implemented, so only the world is "
                            "available");
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
