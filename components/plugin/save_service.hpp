#pragma once

#include <openeden/save.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of save game chunks and unread chunk preservation.
     */
    class SaveServiceImpl : public openeden::sdk::SaveService
    {
    public:
        SaveServiceImpl() = default;
        ~SaveServiceImpl() override = default;

        // Non-copyable
        SaveServiceImpl(const SaveServiceImpl&) = delete;
        SaveServiceImpl& operator=(const SaveServiceImpl&) = delete;

        // Movable
        SaveServiceImpl(SaveServiceImpl&&) noexcept = default;
        SaveServiceImpl& operator=(SaveServiceImpl&&) noexcept = default;

        void registerHandler(
            std::string_view pluginId, std::shared_ptr<openeden::sdk::SaveChunkHandler> handler) override;

        void unregisterHandler(std::string_view pluginId) override;

        /**
         * @brief Serializes all active and unread chunks into binary save data.
         */
        [[nodiscard]] std::vector<std::uint8_t> serialize() const;

        /**
         * @brief Deserializes binary save data, dispatching to handlers and preserving unread chunks.
         */
        bool deserialize(const std::vector<std::uint8_t>& data);

        /**
         * @brief Checks if save data contains a preserved unread chunk for a missing plugin.
         */
        [[nodiscard]] bool hasUnreadChunk(std::string_view pluginId) const;

        /**
         * @brief Returns the raw preserved bytes for an unread chunk.
         */
        [[nodiscard]] std::vector<std::uint8_t> getUnreadChunkRawBytes(std::string_view pluginId) const;

        /**
         * @brief Returns the list of plugin IDs with preserved unread chunks.
         */
        [[nodiscard]] std::vector<std::string> getUnreadChunkPluginIds() const;

    private:
        struct ChunkData
        {
            std::uint32_t version = 0;
            std::vector<std::uint8_t> bytes;
        };

        std::map<std::string, std::shared_ptr<openeden::sdk::SaveChunkHandler>, std::less<>> mHandlers;
        std::map<std::string, ChunkData, std::less<>> mUnreadChunks;
    };
} // namespace openeden::plugin
