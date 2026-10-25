#include "save_service.hpp"

#include <cstring>
#include <iostream>

namespace openeden::plugin
{
    namespace
    {
        constexpr std::uint32_t kSaveMagic = 0x4544454E; // "EDEN"
        constexpr std::uint32_t kSaveVersion = 1;

        void append32LE(std::vector<std::uint8_t>& out, std::uint32_t val)
        {
            out.push_back(static_cast<std::uint8_t>(val & 0xFF));
            out.push_back(static_cast<std::uint8_t>((val >> 8) & 0xFF));
            out.push_back(static_cast<std::uint8_t>((val >> 16) & 0xFF));
            out.push_back(static_cast<std::uint8_t>((val >> 24) & 0xFF));
        }

        bool read32LE(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint32_t& val)
        {
            if (offset + 4 > data.size())
                return false;
            val = static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
                | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
            offset += 4;
            return true;
        }

        class MemoryChunkWriter : public openeden::sdk::SaveChunkWriter
        {
        public:
            void write(const void* data, std::size_t size) override
            {
                if (data && size > 0)
                {
                    const auto* bytes = static_cast<const std::uint8_t*>(data);
                    mBuffer.insert(mBuffer.end(), bytes, bytes + size);
                }
            }

            [[nodiscard]] const std::vector<std::uint8_t>& buffer() const noexcept { return mBuffer; }

        private:
            std::vector<std::uint8_t> mBuffer;
        };

        class MemoryChunkReader : public openeden::sdk::SaveChunkReader
        {
        public:
            MemoryChunkReader(const std::uint8_t* data, std::size_t size)
                : mData(data)
                , mSize(size)
            {
            }

            [[nodiscard]] std::size_t size() const noexcept override { return mSize; }

            void read(void* dest, std::size_t size) override
            {
                if (!dest || size == 0)
                    return;

                const std::size_t toCopy = std::min(size, mSize - mOffset);
                if (toCopy > 0)
                {
                    std::memcpy(dest, mData + mOffset, toCopy);
                    mOffset += toCopy;
                }
            }

        private:
            const std::uint8_t* mData = nullptr;
            std::size_t mSize = 0;
            std::size_t mOffset = 0;
        };
    } // namespace

    void SaveServiceImpl::registerHandler(
        std::string_view pluginId, std::shared_ptr<openeden::sdk::SaveChunkHandler> handler)
    {
        const std::string pid(pluginId);
        mHandlers[pid] = std::move(handler);

        // If we had a preserved unread chunk for this plugin, attempt to consume it now
        auto it = mUnreadChunks.find(pid);
        if (it != mUnreadChunks.end() && mHandlers[pid])
        {
            bool readSucceeded = false;
            try
            {
                MemoryChunkReader reader(it->second.bytes.data(), it->second.bytes.size());
                mHandlers[pid]->readSaveChunk(it->second.version, reader);
                readSucceeded = true;
            }
            catch (const std::exception& e)
            {
                std::cerr << "OpenEden SaveService: Exception consuming preserved chunk for plugin '" << pid
                          << "': " << e.what() << '\n';
            }
            catch (...)
            {
                std::cerr << "OpenEden SaveService: Unknown exception consuming preserved chunk for plugin '" << pid
                          << "'\n";
            }

            // Only remove from unread pool if successfully consumed without error
            if (readSucceeded)
            {
                mUnreadChunks.erase(it);
            }
            else
            {
                // Handler failed to read its save chunk: withdraw handler so preserved chunk is not overwritten
                mHandlers.erase(pid);
            }
        }
    }

    void SaveServiceImpl::unregisterHandler(std::string_view pluginId)
    {
        mHandlers.erase(std::string(pluginId));
    }

    std::vector<std::uint8_t> SaveServiceImpl::serialize() const
    {
        std::vector<std::uint8_t> out;

        // Header: Magic + Format Version (Little-Endian)
        append32LE(out, kSaveMagic);
        append32LE(out, kSaveVersion);

        // Count total chunks: active handlers + unread chunks not in active handlers
        std::map<std::string, ChunkData, std::less<>> allChunks;

        for (const auto& [pid, handler] : mHandlers)
        {
            if (handler)
            {
                try
                {
                    MemoryChunkWriter writer;
                    handler->writeSaveChunk(writer);
                    allChunks[pid] = { handler->currentVersion(), writer.buffer() };
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden SaveService: Exception writing save chunk for plugin '" << pid
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden SaveService: Unknown exception writing save chunk for plugin '" << pid
                              << "'\n";
                }
            }
        }

        for (const auto& [pid, unreadChunk] : mUnreadChunks)
        {
            if (allChunks.find(pid) == allChunks.end())
            {
                allChunks[pid] = unreadChunk;
            }
        }

        append32LE(out, static_cast<std::uint32_t>(allChunks.size()));

        for (const auto& [pid, chunk] : allChunks)
        {
            // 1. Plugin ID length + string
            append32LE(out, static_cast<std::uint32_t>(pid.size()));
            out.insert(out.end(), pid.begin(), pid.end());

            // 2. Version
            append32LE(out, chunk.version);

            // 3. Payload size + bytes
            append32LE(out, static_cast<std::uint32_t>(chunk.bytes.size()));
            out.insert(out.end(), chunk.bytes.begin(), chunk.bytes.end());
        }

        return out;
    }

    bool SaveServiceImpl::deserialize(const std::vector<std::uint8_t>& data)
    {
        if (data.size() < sizeof(std::uint32_t) * 3)
            return false;

        std::size_t offset = 0;

        std::uint32_t magic = 0;
        if (!read32LE(data, offset, magic) || magic != kSaveMagic)
            return false;

        std::uint32_t formatVersion = 0;
        if (!read32LE(data, offset, formatVersion) || formatVersion != kSaveVersion)
            return false;

        std::uint32_t chunkCount = 0;
        if (!read32LE(data, offset, chunkCount))
            return false;

        for (std::uint32_t i = 0; i < chunkCount; ++i)
        {
            std::uint32_t idLen = 0;
            if (!read32LE(data, offset, idLen) || offset + idLen > data.size())
                return false;

            std::string pid(reinterpret_cast<const char*>(data.data() + offset), idLen);
            offset += idLen;

            std::uint32_t version = 0;
            if (!read32LE(data, offset, version))
                return false;

            std::uint32_t payloadSize = 0;
            if (!read32LE(data, offset, payloadSize) || offset + payloadSize > data.size())
                return false;

            const std::uint8_t* payloadPtr = data.data() + offset;
            offset += payloadSize;

            auto handlerIt = mHandlers.find(pid);
            if (handlerIt != mHandlers.end() && handlerIt->second)
            {
                try
                {
                    MemoryChunkReader reader(payloadPtr, payloadSize);
                    handlerIt->second->readSaveChunk(version, reader);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden SaveService: Exception reading chunk for plugin '" << pid
                              << "': " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden SaveService: Unknown exception reading chunk for plugin '" << pid << "'\n";
                }
            }
            else
            {
                // Preserved unread chunk byte-for-byte; notify with warning as per Design Doc 4.9
                std::cerr << "OpenEden SaveService: WARNING: Preserved unread save chunk for missing plugin '" << pid
                          << "' (version: " << version << ", size: " << payloadSize << " bytes)\n";
                mUnreadChunks[pid] = { version, std::vector<std::uint8_t>(payloadPtr, payloadPtr + payloadSize) };
            }
        }

        return true;
    }

    bool SaveServiceImpl::hasUnreadChunk(std::string_view pluginId) const
    {
        return mUnreadChunks.find(pluginId) != mUnreadChunks.end();
    }

    std::vector<std::uint8_t> SaveServiceImpl::getUnreadChunkRawBytes(std::string_view pluginId) const
    {
        auto it = mUnreadChunks.find(pluginId);
        if (it != mUnreadChunks.end())
        {
            return it->second.bytes;
        }
        return {};
    }

    std::vector<std::string> SaveServiceImpl::getUnreadChunkPluginIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(mUnreadChunks.size());
        for (const auto& [id, chunk] : mUnreadChunks)
        {
            ids.push_back(id);
        }
        return ids;
    }
} // namespace openeden::plugin
