#pragma once

#include <openeden/game/save_chunks.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief One chunk of a game pack's save data, keyed {packId, name, version}.
     */
    struct SavedChunk
    {
        std::string packId;
        std::string name;
        std::uint32_t version = 0;
        std::vector<std::byte> bytes;
    };

    struct ChunkCollectResult
    {
        bool ok = true;
        std::vector<SavedChunk> chunks;
        std::string error; ///< Set when ok is false; names the pack and chunk
    };

    struct ChunkRestoreResult
    {
        bool ok = true;
        std::string error; ///< Set when ok is false; names the pack and chunk
    };

    /**
     * @brief The core's side of the save chunk handler (decision 0013; the interface decision 0005 removed for
     * extensions, back for game packs only). Holds the handlers game packs registered, writes their chunks, reads
     * them back, and keeps the chunks of a pack that is not loaded so that saving again does not lose them.
     *
     * Main thread only: the engine calls collect() and restore() from the thread that serialises the save. A handler's
     * exception is caught here and becomes a refusal naming the pack and chunk. Owned by HostImpl; the handlers
     * belong to the packs, and LifecycleManager removes them before a pack is destroyed.
     */
    class SaveChunkServiceImpl
    {
    public:
        /// False for an empty pack id, an empty or over-long chunk name, or a name this pack already registered.
        [[nodiscard]] bool registerHandler(std::string_view packId, openeden::sdk::SaveChunkHandler& handler);

        /// Forgets the pack's handlers (not its preserved chunks).
        void unregisterPack(std::string_view packId) noexcept;

        /// Forgets every handler and every preserved chunk.
        void clear() noexcept;

        /// After this, collect() fails with the reason: a faulted game pack never corrupts a save.
        void refuseSaving(std::string reason);
        [[nodiscard]] bool isSavingRefused() const noexcept { return mRefused; }

        /// Handlers' chunks in registration order, then the preserved chunks of packs that are not loaded.
        [[nodiscard]] ChunkCollectResult collect();

        /// Hands each chunk to its handler. A chunk with a newer version than its handler writes, or that its handler
        /// refuses, fails the call; versions are checked for every chunk before any handler reads, so a newer chunk
        /// never leaves earlier ones applied. A chunk with no handler is kept for the next collect().
        [[nodiscard]] ChunkRestoreResult restore(const std::vector<SavedChunk>& chunks);

        [[nodiscard]] std::size_t handlerCount() const noexcept { return mHandlers.size(); }
        [[nodiscard]] std::size_t preservedCount() const noexcept { return mPreserved.size(); }

    private:
        struct Entry
        {
            std::string packId;
            std::string name;
            openeden::sdk::SaveChunkHandler* handler = nullptr; // non-owning: the pack owns it
        };

        std::vector<Entry> mHandlers;
        std::vector<SavedChunk> mPreserved;
        bool mRefused = false;
        std::string mRefusedReason;
    };

    /// The most chunk bytes, names and chunks decodeChunks accepts (a hostile save must not allocate without bound).
    inline constexpr std::size_t kMaxChunkBytes = 256u * 1024u * 1024u;
    inline constexpr std::size_t kMaxChunkCount = 4096;
    inline constexpr std::size_t kMaxChunkNameBytes = 128;

    /// Canonical bytes for a list of chunks, for the engine to embed in a save record.
    [[nodiscard]] std::vector<std::byte> encodeChunks(const std::vector<SavedChunk>& chunks);

    /// False with a message for any malformed, truncated or oversized input; out is cleared first.
    [[nodiscard]] bool decodeChunks(std::span<const std::byte> bytes, std::vector<SavedChunk>& out, std::string& error);

    /**
     * @brief Decides whether a save may be loaded by the active game pack. The save header records the pack's id and
     * version ("major.minor.patch"; text that is not numbers is compared for equality only).
     * @return Empty if the save may load, else the message to show. A save with no recorded pack (made before game
     * packs) is accepted; a recorded pack that is not the active one, or is newer than it, is refused.
     */
    [[nodiscard]] std::string checkSavedGamePack(std::string_view savedPackId, std::string_view savedPackVersion,
        std::string_view activePackId, std::string_view activePackVersion);
} // namespace openeden::extension
