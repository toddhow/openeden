#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief The tier of a pack, from the manifest's "kind" (docs/design/game-packs.md section 4).
     *
     * Feature extensions and compatibility packs are ordinary extensions (decisions 0003 and 0005). Only a game pack is
     * handed the game services (save chunks, record types, content loaders, script host, windows) and runs the game
     * phases.
     */
    enum class PackKind : std::uint8_t
    {
        Feature,
        Compatibility,
        Game
    };

    inline constexpr std::string_view kKindFeature = "feature";
    inline constexpr std::string_view kKindCompatibility = "compatibility";
    inline constexpr std::string_view kKindGame = "game";

    /// Nullopt for a name that is not one of the three manifest kinds.
    [[nodiscard]] constexpr std::optional<PackKind> parsePackKind(std::string_view name) noexcept
    {
        if (name == kKindFeature)
            return PackKind::Feature;
        if (name == kKindCompatibility)
            return PackKind::Compatibility;
        if (name == kKindGame)
            return PackKind::Game;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::string_view toString(PackKind kind) noexcept
    {
        switch (kind)
        {
            case PackKind::Feature:
                return kKindFeature;
            case PackKind::Compatibility:
                return kKindCompatibility;
            case PackKind::Game:
                return kKindGame;
        }
        return kKindFeature;
    }

    /// The services and phases only a game pack may use.
    [[nodiscard]] constexpr bool hasGameCapabilities(PackKind kind) noexcept
    {
        return kind == PackKind::Game;
    }
} // namespace openeden::sdk
