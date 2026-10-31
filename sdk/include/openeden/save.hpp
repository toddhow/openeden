#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Writer interface for serializing a plugin's save game chunk.
     */
    class SaveChunkWriter
    {
    public:
        virtual ~SaveChunkWriter() = default;
        virtual void write(const void* data, std::size_t size) = 0;
    };

    /**
     * @brief Reader interface for deserializing a plugin's save game chunk.
     */
    class SaveChunkReader
    {
    public:
        virtual ~SaveChunkReader() = default;
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;
        virtual void read(void* dest, std::size_t size) = 0;
    };

    /**
     * @brief Handler for versioned plugin save game chunks with migration support.
     *
     * The game calls it on the main thread, between frames: it never runs on the save or load worker.
     */
    class SaveChunkHandler
    {
    public:
        virtual ~SaveChunkHandler() = default;
        [[nodiscard]] virtual std::uint32_t currentVersion() const noexcept = 0;
        virtual void writeSaveChunk(SaveChunkWriter& writer) = 0;
        virtual void readSaveChunk(std::uint32_t chunkVersion, SaveChunkReader& reader) = 0;
    };

    /**
     * @brief Service managing plugin save game chunks and unread chunk preservation.
     */
    class SaveService
    {
    public:
        virtual ~SaveService() = default;
        /**
         * @brief Registers a save chunk handler for a plugin.
         * Uses std::shared_ptr to allow plugins and the save service to safely share lifetime
         * during async save/load and late plugin unregistration.
         */
        virtual void registerHandler(std::string_view pluginId, std::shared_ptr<SaveChunkHandler> handler) = 0;
        virtual void unregisterHandler(std::string_view pluginId) = 0;
    };
} // namespace openeden::sdk
