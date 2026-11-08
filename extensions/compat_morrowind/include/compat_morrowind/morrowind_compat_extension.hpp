#pragma once

#include "kf_converter.hpp"

#include <openeden/contracts/game_contract.hpp>
#include <openeden/extension.hpp>

#include <array>
#include <memory>
#include <span>
#include <string_view>

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
        // Exactly Constants::UnitsPerMeter (components/misc/constants.hpp) so that asking the contract gives the
        // same float as the engine's fallback; a test pins both (task 0085, move 1). The pack may not include the core.
        [[nodiscard]] float getUnitsPerMeter() const noexcept override { return 69.99125109f; }
        [[nodiscard]] std::string_view getDefaultHumanoidSkeleton() const noexcept override
        {
            return "meshes/base_anim.nif";
        }

        // Morrowind's conventions (task 0085, move 1). The engine's own defaults (Misc::GameConventions) equal these;
        // a test pins that.
        [[nodiscard]] bool specifiesAllConventions() const noexcept override { return true; }
        [[nodiscard]] HumanoidBoneNames getHumanoidBoneNames() const noexcept override
        {
            return { "bip01", "root bone", "bip01 head", "bip01 spine1", "bip01 spine2" };
        }
        [[nodiscard]] std::span<const std::string_view> getSkeletonModelNamePrefixes() const noexcept override
        {
            static constexpr std::array<std::string_view, 2> sPrefixes{ "base_anim", "skin" };
            return sPrefixes;
        }
        [[nodiscard]] std::string_view getFirstPersonBodyPartSuffix() const noexcept override { return ".1st"; }
        [[nodiscard]] std::string_view getFirstPersonBaseAnimationFile() const noexcept override
        {
            return "xbase_anim.1st.nif";
        }
    };

    class MorrowindCompatExtension : public openeden::sdk::Extension
    {
    public:
        MorrowindCompatExtension();
        ~MorrowindCompatExtension() override;

        void registerServices(openeden::sdk::Registrar& registrar) override;
        bool resolve(const openeden::sdk::ServiceLocator& locator) override;
        void start(openeden::sdk::Host& host) override;
        void stop() override;

    private:
        MorrowindGameContract mGameContract;
        KfConverter mKfConverter;
    };
} // namespace compat_morrowind
