#ifndef OPENMW_MWSTATE_GAMEPACKCHUNKS_H
#define OPENMW_MWSTATE_GAMEPACKCHUNKS_H

#include <cstddef>
#include <span>
#include <vector>

#include <components/extension/save_chunk_store.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MWState
{
    /// A game pack's save chunks as ONE record of the .omwsave stream (task 0084 leftovers, stage I4). The bytes are
    /// the canonical encoding of openeden::extension::encodeChunks. The record exists only when there is something to
    /// write: a session with no game pack, or whose pack writes no chunk, produces exactly the bytes it always did, and
    /// the record count of the save header does not change. A reader that does not know the record skips it (the
    /// default branch of MWState::LoadGame).

    /// 0 for no bytes, else 1: what the save's record count must add.
    [[nodiscard]] std::size_t countGamePackChunkRecords(std::span<const std::byte> bytes) noexcept;

    /// Writes the record, or nothing for empty bytes.
    void writeGamePackChunkRecord(ESM::ESMWriter& writer, std::span<const std::byte> bytes);

    /// Reads the record whose header has just been read. \throws std::runtime_error for a payload over `maxBytes` (a
    /// hostile save must not allocate without bound) or a malformed record.
    [[nodiscard]] std::vector<std::byte> readGamePackChunkRecord(
        ESM::ESMReader& reader, std::size_t maxBytes = openeden::extension::kMaxChunkBytes);
}

#endif
