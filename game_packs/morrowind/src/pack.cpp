#include <game_morrowind/pack.hpp>

#include <game_morrowind/game_rules.hpp>

#include <openeden/game/game_rules.hpp>
#include <openeden/pack_kind.hpp>

namespace game_morrowind
{
    namespace
    {
        class MorrowindPack final : public openeden::sdk::Extension
        {
        public:
            void registerServices(openeden::sdk::Registrar& registrar) override
            {
                mRules = makeGameRules();
                registrar.provide<openeden::sdk::game::GameRules>(*mRules);
            }

            bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

            // Nothing runs in the frame phases yet: Morrowind's mechanics are still the core's (task 0085)
            void start(openeden::sdk::Host&) override {}

            void stop() override {}

        private:
            std::unique_ptr<openeden::sdk::game::GameRules> mRules;
        };
    }

    openeden::extension::ExtensionManifest packManifest()
    {
        openeden::extension::ExtensionManifest manifest;
        manifest.version = 2;
        manifest.id = "morrowind";
        manifest.name = "Morrowind";
        manifest.extensionVersion = "1.0.0";
        manifest.kind = std::string(openeden::sdk::kKindGame);
        manifest.sdkVersion = openeden::sdk::kSdkVersion;
        manifest.providedContracts.push_back(
            { std::string(openeden::sdk::game::GameRules::kId), openeden::sdk::game::GameRules::kVersion });
        manifest.licence = "GPL-3.0-or-later";
        return manifest;
    }

    std::unique_ptr<openeden::sdk::Extension> makePackExtension()
    {
        return std::make_unique<MorrowindPack>();
    }
}
