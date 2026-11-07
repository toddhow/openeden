#pragma once

#include <cstdint>
#include <string_view>

namespace openeden::sdk
{
    /// What a content loader sees of a plugin file before it is read: the game it names and its format version.
    struct ContentHeader
    {
        std::string_view gameTag;
        std::uint32_t formatVersion = 0;
    };

    /**
     * @brief A game pack's decoder for plugin files (docs/design/game-packs.md section 5.2). In this SDK version only
     * probing is part of the contract; the loading calls come with task 0085.
     */
    class ContentLoader
    {
    public:
        virtual ~ContentLoader() = default;

        /// Whether this loader accepts a plugin with this header.
        [[nodiscard]] virtual bool probe(const ContentHeader& header) const = 0;
    };

    /**
     * @brief Game packs only. Main thread only.
     */
    class ContentService
    {
    public:
        virtual ~ContentService() = default;

        /// The loader must outlive the pack's stop(). Loaders are asked in registration order.
        virtual void registerLoader(ContentLoader& loader) = 0;

        /// The first registered loader that accepts the header, or null.
        [[nodiscard]] virtual ContentLoader* findLoader(const ContentHeader& header) const = 0;
    };
} // namespace openeden::sdk
