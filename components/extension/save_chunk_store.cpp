#include "save_chunk_store.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <optional>
#include <utility>

namespace openeden::extension
{
    namespace
    {
        constexpr std::size_t kMaxPackIdBytes = 128;

        class VectorWriter final : public openeden::sdk::ChunkWriter
        {
        public:
            explicit VectorWriter(std::vector<std::byte>& out)
                : mOut(out)
            {
            }

            void write(const void* data, std::size_t size) override
            {
                const auto* bytes = static_cast<const std::byte*>(data);
                mOut.insert(mOut.end(), bytes, bytes + size);
            }

        private:
            std::vector<std::byte>& mOut;
        };

        class SpanReader final : public openeden::sdk::ChunkReader
        {
        public:
            explicit SpanReader(std::span<const std::byte> bytes)
                : mBytes(bytes)
            {
            }

            bool read(void* data, std::size_t size) override
            {
                if (size > remaining())
                    return false;
                if (size != 0)
                    std::memcpy(data, mBytes.data() + mOffset, size);
                mOffset += size;
                return true;
            }

            std::size_t remaining() const override { return mBytes.size() - mOffset; }

        private:
            std::span<const std::byte> mBytes;
            std::size_t mOffset = 0;
        };

        std::string describe(const std::string& packId, const std::string& name)
        {
            return "chunk '" + name + "' of game pack '" + packId + "'";
        }

        void putU32(std::vector<std::byte>& out, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
        }

        void putString(std::vector<std::byte>& out, const std::string& text)
        {
            putU32(out, static_cast<std::uint32_t>(text.size()));
            const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
            out.insert(out.end(), bytes, bytes + text.size());
        }

        // Reads from a span with bounds checks; every failure leaves a message.
        class Cursor
        {
        public:
            explicit Cursor(std::span<const std::byte> bytes)
                : mBytes(bytes)
            {
            }

            std::size_t remaining() const { return mBytes.size() - mOffset; }

            bool getU32(std::uint32_t& value)
            {
                if (remaining() < 4)
                    return false;
                value = 0;
                for (int i = 0; i < 4; ++i)
                    value |= static_cast<std::uint32_t>(std::to_integer<unsigned>(mBytes[mOffset + i])) << (8 * i);
                mOffset += 4;
                return true;
            }

            bool getBytes(std::size_t size, const std::byte*& begin)
            {
                if (remaining() < size)
                    return false;
                begin = mBytes.data() + mOffset;
                mOffset += size;
                return true;
            }

        private:
            std::span<const std::byte> mBytes;
            std::size_t mOffset = 0;
        };

        // "1.2.3" as numbers; nullopt for anything else (empty parts, letters, more than 4 parts, huge numbers).
        std::optional<std::vector<std::uint64_t>> parseVersion(std::string_view text)
        {
            std::vector<std::uint64_t> parts;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t dot = text.find('.', start);
                const std::string_view part = text.substr(start, dot == std::string_view::npos ? dot : dot - start);
                if (part.empty() || part.size() > 9 || parts.size() >= 4)
                    return std::nullopt;
                std::uint64_t number = 0;
                for (const char c : part)
                {
                    if (c < '0' || c > '9')
                        return std::nullopt;
                    number = number * 10 + static_cast<std::uint64_t>(c - '0');
                }
                parts.push_back(number);
                if (dot == std::string_view::npos)
                    break;
                start = dot + 1;
            }
            return parts;
        }
    }

    bool SaveChunkServiceImpl::registerHandler(std::string_view packId, openeden::sdk::SaveChunkHandler& handler)
    {
        const std::string_view name = handler.chunkName();
        if (packId.empty() || packId.size() > kMaxPackIdBytes || name.empty() || name.size() > kMaxChunkNameBytes)
            return false;
        const bool duplicate = std::any_of(mHandlers.begin(), mHandlers.end(),
            [&](const Entry& entry) { return entry.packId == packId && entry.name == name; });
        if (duplicate)
            return false;
        mHandlers.push_back(Entry{ std::string(packId), std::string(name), &handler });
        return true;
    }

    void SaveChunkServiceImpl::unregisterPack(std::string_view packId) noexcept
    {
        mHandlers.erase(std::remove_if(mHandlers.begin(), mHandlers.end(),
                            [&](const Entry& entry) { return entry.packId == packId; }),
            mHandlers.end());
    }

    void SaveChunkServiceImpl::clear() noexcept
    {
        mHandlers.clear();
        mPreserved.clear();
    }

    void SaveChunkServiceImpl::refuseSaving(std::string reason)
    {
        mRefused = true;
        mRefusedReason = std::move(reason);
    }

    ChunkCollectResult SaveChunkServiceImpl::collect()
    {
        ChunkCollectResult result;
        if (mRefused)
        {
            result.ok = false;
            result.error = mRefusedReason.empty() ? "saving is refused" : mRefusedReason;
            return result;
        }

        for (const Entry& entry : mHandlers)
        {
            SavedChunk chunk;
            chunk.packId = entry.packId;
            chunk.name = entry.name;
            chunk.version = entry.handler->chunkVersion();
            try
            {
                VectorWriter writer(chunk.bytes);
                entry.handler->write(writer);
            }
            catch (const std::exception& e)
            {
                return { false, {}, "Writing " + describe(entry.packId, entry.name) + " failed: " + e.what() };
            }
            catch (...)
            {
                return { false, {}, "Writing " + describe(entry.packId, entry.name) + " failed: unknown exception" };
            }
            result.chunks.push_back(std::move(chunk));
        }

        result.chunks.insert(result.chunks.end(), mPreserved.begin(), mPreserved.end());
        return result;
    }

    ChunkRestoreResult SaveChunkServiceImpl::restore(const std::vector<SavedChunk>& chunks)
    {
        const auto handlerOf = [this](const SavedChunk& chunk) -> openeden::sdk::SaveChunkHandler* {
            for (const Entry& entry : mHandlers)
            {
                if (entry.packId == chunk.packId && entry.name == chunk.name)
                    return entry.handler;
            }
            return nullptr;
        };

        // Versions first, so that a newer chunk never leaves earlier ones applied
        for (const SavedChunk& chunk : chunks)
        {
            const openeden::sdk::SaveChunkHandler* handler = handlerOf(chunk);
            if (handler != nullptr && chunk.version > handler->chunkVersion())
            {
                return { false,
                    "The save's " + describe(chunk.packId, chunk.name) + " is version " + std::to_string(chunk.version)
                        + ", newer than version " + std::to_string(handler->chunkVersion())
                        + " this build understands" };
            }
        }

        std::vector<SavedChunk> unhandled;
        for (const SavedChunk& chunk : chunks)
        {
            openeden::sdk::SaveChunkHandler* handler = handlerOf(chunk);
            if (handler == nullptr)
            {
                unhandled.push_back(chunk);
                continue;
            }

            bool accepted = false;
            try
            {
                SpanReader reader(chunk.bytes);
                accepted = handler->read(reader, chunk.version);
            }
            catch (const std::exception& e)
            {
                return { false, "Reading " + describe(chunk.packId, chunk.name) + " failed: " + e.what() };
            }
            catch (...)
            {
                return { false, "Reading " + describe(chunk.packId, chunk.name) + " failed: unknown exception" };
            }
            if (!accepted)
                return { false, "The game pack refused the save's " + describe(chunk.packId, chunk.name) };
        }

        mPreserved = std::move(unhandled);
        return {};
    }

    std::vector<std::byte> encodeChunks(const std::vector<SavedChunk>& chunks)
    {
        std::vector<std::byte> out;
        putU32(out, static_cast<std::uint32_t>(chunks.size()));
        for (const SavedChunk& chunk : chunks)
        {
            putString(out, chunk.packId);
            putString(out, chunk.name);
            putU32(out, chunk.version);
            putU32(out, static_cast<std::uint32_t>(chunk.bytes.size()));
            out.insert(out.end(), chunk.bytes.begin(), chunk.bytes.end());
        }
        return out;
    }

    bool decodeChunks(std::span<const std::byte> bytes, std::vector<SavedChunk>& out, std::string& error)
    {
        out.clear();
        Cursor cursor(bytes);

        const auto fail = [&](const std::string& message) {
            out.clear();
            error = message;
            return false;
        };

        std::uint32_t count = 0;
        if (!cursor.getU32(count))
            return fail("save chunks: truncated before the chunk count");
        if (count > kMaxChunkCount)
            return fail("save chunks: " + std::to_string(count) + " chunks is more than the limit of "
                + std::to_string(kMaxChunkCount));

        const auto readName = [&](const char* what, std::string& text) {
            std::uint32_t size = 0;
            if (!cursor.getU32(size))
                return fail(std::string("save chunks: truncated in a ") + what);
            if (size == 0 || size > kMaxChunkNameBytes)
                return fail(std::string("save chunks: a ") + what + " of " + std::to_string(size)
                    + " bytes is empty or too long");
            const std::byte* begin = nullptr;
            if (!cursor.getBytes(size, begin))
                return fail(std::string("save chunks: truncated in a ") + what);
            text.assign(reinterpret_cast<const char*>(begin), size);
            return true;
        };

        for (std::uint32_t i = 0; i < count; ++i)
        {
            SavedChunk chunk;
            if (!readName("pack id", chunk.packId) || !readName("chunk name", chunk.name))
                return false;

            std::uint32_t payload = 0;
            if (!cursor.getU32(chunk.version) || !cursor.getU32(payload))
                return fail("save chunks: truncated in the header of chunk '" + chunk.name + "'");
            if (payload > kMaxChunkBytes || payload > cursor.remaining())
                return fail("save chunks: chunk '" + chunk.name + "' of game pack '" + chunk.packId + "' declares "
                    + std::to_string(payload) + " bytes, more than the file holds");
            const std::byte* begin = nullptr;
            if (!cursor.getBytes(payload, begin))
                return fail("save chunks: truncated in the data of chunk '" + chunk.name + "'");
            chunk.bytes.assign(begin, begin + payload);
            out.push_back(std::move(chunk));
        }

        if (cursor.remaining() != 0)
            return fail(
                "save chunks: " + std::to_string(cursor.remaining()) + " unexpected bytes after the last chunk");

        error.clear();
        return true;
    }

    std::string checkSavedGamePack(std::string_view savedPackId, std::string_view savedPackVersion,
        std::string_view activePackId, std::string_view activePackVersion)
    {
        if (savedPackId.empty())
            return {};

        if (activePackId.empty())
        {
            return "This save was made with game pack '" + std::string(savedPackId) + "' (version "
                + std::string(savedPackVersion) + "), which is not loaded in this session.";
        }

        if (savedPackId != activePackId)
        {
            return "This save was made with game pack '" + std::string(savedPackId)
                + "', but this session runs game pack '" + std::string(activePackId)
                + "': a save of another game cannot be loaded.";
        }

        auto saved = parseVersion(savedPackVersion);
        auto active = parseVersion(activePackVersion);
        bool newer = savedPackVersion != activePackVersion;
        if (saved && active)
        {
            // 1.2 and 1.2.0 are the same version
            const std::size_t length = std::max(saved->size(), active->size());
            saved->resize(length, 0);
            active->resize(length, 0);
            newer = *active < *saved;
        }
        if (newer)
        {
            return "This save was made with version " + std::string(savedPackVersion) + " of game pack '"
                + std::string(savedPackId) + "'; this build has version " + std::string(activePackVersion)
                + " and cannot load a save from a newer one.";
        }
        return {};
    }
} // namespace openeden::extension
