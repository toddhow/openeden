#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace openeden::sdk
{
    /**
     * @brief Where a save chunk handler writes its bytes. Game packs only (decision 0013; decision 0005 still holds for
     * every other kind: an ordinary extension cannot reach this).
     *
     * Threading: the host calls a handler's write() and read() on the main thread, from the thread that serialises
     * the save, never from a Jolt worker thread.
     */
    class ChunkWriter
    {
    public:
        virtual ~ChunkWriter() = default;

        virtual void write(const void* data, std::size_t size) = 0;

        template <typename T>
        void writeValue(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            write(&value, sizeof(T));
        }

        /// A 32-bit length, then the bytes.
        void writeString(std::string_view text)
        {
            writeValue(static_cast<std::uint32_t>(text.size()));
            write(text.data(), text.size());
        }
    };

    /**
     * @brief Where a handler reads one chunk back. A read past the end fails and consumes nothing.
     */
    class ChunkReader
    {
    public:
        virtual ~ChunkReader() = default;

        [[nodiscard]] virtual bool read(void* data, std::size_t size) = 0;
        [[nodiscard]] virtual std::size_t remaining() const = 0;

        template <typename T>
        [[nodiscard]] bool readValue(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return read(&value, sizeof(T));
        }

        /// Reads what writeString wrote; false if the length does not fit in what is left.
        [[nodiscard]] bool readString(std::string& text)
        {
            std::uint32_t size = 0;
            if (!readValue(size) || size > remaining())
                return false;
            text.resize(size);
            return size == 0 || read(text.data(), size);
        }
    };

    /**
     * @brief One named, versioned chunk of a game pack's save data. The host keeps the chunk under
     * {pack id, chunkName(), chunkVersion()}.
     */
    class SaveChunkHandler
    {
    public:
        virtual ~SaveChunkHandler() = default;

        [[nodiscard]] virtual std::string_view chunkName() const = 0;

        /// The version write() produces.
        [[nodiscard]] virtual std::uint32_t chunkVersion() const = 0;

        virtual void write(ChunkWriter& writer) = 0;

        /// version is the one the chunk was written with, never newer than chunkVersion(). False refuses the chunk
        /// (the load is then refused cleanly, never half applied by the host). No exception may leave this call.
        [[nodiscard]] virtual bool read(ChunkReader& reader, std::uint32_t version) = 0;
    };

    /**
     * @brief Handed to a game pack by Host::getSaveChunkService(); every other kind gets nullptr.
     */
    class SaveChunkService
    {
    public:
        virtual ~SaveChunkService() = default;

        /// The handler must outlive the pack's stop(); the host forgets it before the pack is destroyed. False for a
        /// chunk name the pack already registered. Chunks are written in registration order.
        [[nodiscard]] virtual bool registerHandler(SaveChunkHandler& handler) = 0;
    };
} // namespace openeden::sdk
