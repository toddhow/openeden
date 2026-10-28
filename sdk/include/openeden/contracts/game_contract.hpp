#pragma once

#include <openeden/version.hpp>
#include <string_view>

namespace openeden::contracts
{
    /**
     * @brief Versioned contract provided by a game compatibility pack.
     *
     * Identifies the active game ruleset (e.g. "morrowind") and provides
     * game-specific conventions such as world unit scale and default skeleton paths.
     */
    class GameContract
    {
    public:
        static constexpr std::string_view kId = "openeden.game";
        static constexpr sdk::Version kVersion{ 1, 0 };

        virtual ~GameContract() = default;

        /**
         * @brief Canonical lower-case identifier for the game ruleset (e.g. "morrowind").
         */
        [[nodiscard]] virtual std::string_view getGameId() const noexcept = 0;

        /**
         * @brief Human-readable display name for the game (e.g. "The Elder Scrolls III: Morrowind").
         */
        [[nodiscard]] virtual std::string_view getGameDisplayName() const noexcept = 0;

        /**
         * @brief World distance units per meter (e.g. 69.9912f for Morrowind).
         */
        [[nodiscard]] virtual float getUnitsPerMeter() const noexcept = 0;

        /**
         * @brief Default relative path to the humanoid base animation/skeleton model.
         */
        [[nodiscard]] virtual std::string_view getDefaultHumanoidSkeleton() const noexcept = 0;
    };
} // namespace openeden::contracts
