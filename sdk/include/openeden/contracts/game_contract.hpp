#pragma once

#include <openeden/version.hpp>
#include <span>
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
        static constexpr sdk::Version kVersion{ 1, 1 };

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

        // ---- Added in 1.1 (task 0085, move 1). Additive: the virtual functions above keep their places, and the
        // new ones have defaults meaning "not specified" (an empty value), so a pack written for 1.0 still compiles;
        // the engine keeps its own default for every value a pack leaves unspecified. A pack for another game must
        // answer all of them (they become pure when the second game pack has). ----

        /**
         * @brief True when this pack answers all four conventions below itself, so that an empty answer means "this
         * game has none" and the engine takes it as it is. False (the default, for a pack written for 1.0): an empty
         * answer means "not specified" and the engine keeps Morrowind's value for it. A pack of another game must
         * return true. Added before 1.1 was released, so it is not a version change.
         */
        [[nodiscard]] virtual bool specifiesAllConventions() const noexcept { return false; }

        /**
         * @brief The names of the humanoid skeleton's bones that the engine looks up by name, lower case.
         * Every view stays valid as long as the contract object does.
         */
        struct HumanoidBoneNames
        {
            std::string_view root; ///< the skeleton's root bone (Morrowind: "bip01"); empty: unspecified
            std::string_view rootFallback; ///< root of skeletons without `root`, e.g. creatures; empty: unspecified
            std::string_view head;
            std::string_view spine1;
            std::string_view spine2;
        };
        [[nodiscard]] virtual HumanoidBoneNames getHumanoidBoneNames() const noexcept { return {}; }

        /**
         * @brief Lower-case base-name prefixes of the game's actor skeleton model files. The model loader does not
         * optimise such a file because animations add keyframes to it afterwards.
         */
        [[nodiscard]] virtual std::span<const std::string_view> getSkeletonModelNamePrefixes() const noexcept
        {
            return {};
        }

        /**
         * @brief Appended to a body part record's id to name its first-person version.
         */
        [[nodiscard]] virtual std::string_view getFirstPersonBodyPartSuffix() const noexcept { return {}; }

        /**
         * @brief The first-person humanoid animation source file, as the animation context names it.
         */
        [[nodiscard]] virtual std::string_view getFirstPersonBaseAnimationFile() const noexcept { return {}; }
    };
} // namespace openeden::contracts
