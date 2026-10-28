#pragma once

#include "kf_converter.hpp"

#include <openeden/contracts/game_contract.hpp>
#include <openeden/plugin.hpp>

#include <memory>

namespace compat_morrowind
{
    class MorrowindGameContract : public openeden::contracts::GameContract
    {
    public:
        [[nodiscard]] std::string_view getGameId() const noexcept override { return "morrowind"; }
        [[nodiscard]] std::string_view getGameDisplayName() const noexcept override
        {
            return "The Elder Scrolls III: Morrowind";
        }
        [[nodiscard]] float getUnitsPerMeter() const noexcept override { return 69.9912f; }
        [[nodiscard]] std::string_view getDefaultHumanoidSkeleton() const noexcept override
        {
            return "meshes/base_anim.nif";
        }
    };

    class MorrowindCompatPlugin : public openeden::sdk::Plugin
    {
    public:
        MorrowindCompatPlugin();
        ~MorrowindCompatPlugin() override;

        void registerServices(openeden::sdk::Registrar& registrar) override;
        bool resolve(const openeden::sdk::ServiceLocator& locator) override;
        void start(openeden::sdk::Host& host) override;
        void stop() override;

    private:
        MorrowindGameContract mGameContract;
        KfConverter mKfConverter;
    };
} // namespace compat_morrowind
