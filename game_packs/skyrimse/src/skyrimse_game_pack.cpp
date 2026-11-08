#include "game_skyrimse/skyrimse_game_pack.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>

#include <stdexcept>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        // Owns the content loader it registers (the host keeps a reference until stop()).
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
                if (openeden::sdk::LoggingService* const logging = host.getLoggingService())
                {
                    if (openeden::sdk::Logger* const logger = logging->getLogger(kPackId))
                        logger->log(openeden::sdk::LogLevel::Warning, kRulesNotImplementedMessage);
                }
                throw std::runtime_error(std::string(kRulesNotImplementedMessage));
            }

            void stop() override {}

        private:
            SkyrimContentLoader mLoader;
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
