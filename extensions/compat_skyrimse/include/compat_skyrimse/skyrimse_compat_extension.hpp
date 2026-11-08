#pragma once

#include "scene_options.hpp"

#include <openeden/contracts/game_contract.hpp>
#include <openeden/extension.hpp>

#include <array>

namespace compat_skyrimse
{
    /// openeden.game for Skyrim Special Edition: the facts a tool needs to place and read its data. Values are from
    /// recall of the game's layout (RECALLED-VERIFY) unless a comment says the installed data confirmed them.
    class SkyrimGameContract : public openeden::contracts::GameContract
    {
    public:
        [[nodiscard]] std::string_view getGameId() const noexcept override { return "skyrimse"; }
        [[nodiscard]] std::string_view getGameDisplayName() const noexcept override
        {
            return "The Elder Scrolls V: Skyrim Special Edition";
        }
        /// Both games use a unit of about 1.43 cm; the value is Constants::UnitsPerMeter, Morrowind's, not measured here.
        [[nodiscard]] float getUnitsPerMeter() const noexcept override { return 69.99125109f; }
        [[nodiscard]] std::string_view getDefaultHumanoidSkeleton() const noexcept override
        {
            return defaultMaleSkeleton();
        }
        [[nodiscard]] HumanoidBoneNames getHumanoidBoneNames() const noexcept override
        {
            return { "npc root [root]", "", "npc head [head]", "npc spine [spn0]", "npc spine1 [spn1]" };
        }
        [[nodiscard]] std::span<const std::string_view> getSkeletonModelNamePrefixes() const noexcept override
        {
            static constexpr std::array<std::string_view, 2> sPrefixes{ "skeleton", "skeletonbeast" };
            return sPrefixes;
        }
        /// Skyrim has no body part records, so there is no first-person variant of one.
        [[nodiscard]] std::string_view getFirstPersonBodyPartSuffix() const noexcept override { return {}; }
        [[nodiscard]] std::string_view getFirstPersonBaseAnimationFile() const noexcept override
        {
            return "meshes/actors/character/_1stperson/skeleton.nif";
        }
    };

    class SkyrimCompatExtension : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override;
        bool resolve(const openeden::sdk::ServiceLocator& locator) override;
        void start(openeden::sdk::Host& host) override;
        void stop() override;

    private:
        SkyrimGameContract mGameContract;
    };
}
