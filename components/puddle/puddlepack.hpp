#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEPACK_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEPACK_H

// OpenEden puddles (task 0095, stage W-b). The derived Eden Archive pack of baked puddle cells
// (<user data>/openeden/puddles/puddles.edenpak): one keyed entry per cell (type 0x0014, key tag 0x14) and the manifest
// entry `puddles/manifest.json`. Same container rules as the LOD packs (components/lod/tilepack.*): DERIVED flag,
// provenance on every cell entry (tool id, the cell's input digest, the parameter digest), a generation, and a source
// digest of the whole set so that "is it up to date" is a comparison of two digests.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <edenarchive/archive.hpp>

#include "puddlebake.hpp"
#include "puddlemap.hpp"

namespace Puddle
{
    inline constexpr std::string_view manifestPath = "puddles/manifest.json";
    inline constexpr std::string_view bakerToolId = "openeden.puddles";

    /// What the pack was made with. Canonical JSON (fixed key order, no spaces, one line, no trailing newline) that
    /// round trips byte for byte; every value is an integer.
    struct Manifest
    {
        std::uint32_t mLayoutVersion = layoutVersion;
        std::uint32_t mBakerVersion = bakerVersion;
        BakeSettings mSettings;
        bool operator==(const Manifest&) const = default;
    };

    std::string writeManifestJson(const Manifest& manifest);
    /// Strict: text that is not exactly what writeManifestJson would write for the parsed value is refused, with the
    /// offset of the first problem.
    Error parseManifestJson(std::string_view text, Manifest& manifest);

    /// One baked cell with the digest of its input, as the pack stores it.
    struct PackedCell
    {
        std::int32_t mCellX = 0;
        std::int32_t mCellY = 0;
        CellMap mMap;
    };

    /// The digest of a whole pack's content: BLAKE3 over the manifest text and, in key order, every cell's key and
    /// input digest. A pack is up to date when its source digest equals this for the current land.
    Digest contentDigest(const Manifest& manifest, const std::vector<std::pair<std::uint64_t, Digest>>& cellInputs);

    /// A cell whose payload is in a file (the payload `writeCell` makes), for packs too big to hold in memory.
    struct CellFile
    {
        std::int32_t mCellX = 0;
        std::int32_t mCellY = 0;
        std::filesystem::path mPath;
        Digest mInputDigest{};
        Digest mParameterDigest{};
        std::uint32_t mSide = 0;
        std::uint64_t mSize = 0;
    };

    struct PackOptions
    {
        std::uint64_t mGeneration = 0;
        Digest mSourceDigest{};
        unsigned mThreads = 1;
        bool mStoreOnly = false;
    };

    /// Builds the pack. Cells may come in any order; the output depends only on the set. Fails on a duplicate cell.
    Error writePuddlePack(const Manifest& manifest, const std::vector<PackedCell>& cells, const PackOptions& options,
        const std::filesystem::path& path);
    /// The same, with the cells read from their files while the pack is written (the writer holds a bounded amount of
    /// them at a time). The files are not checked here: the reader checks every cell when the pack is read back.
    Error writePuddlePackFromFiles(const Manifest& manifest, const std::vector<CellFile>& cells,
        const PackOptions& options, const std::filesystem::path& path);
    Error buildPuddlePack(const Manifest& manifest, const std::vector<PackedCell>& cells, const PackOptions& options,
        std::vector<std::uint8_t>& out);

    /// An open pack. Immutable; the const functions may be called from several threads.
    class PuddlePack
    {
    public:
        ~PuddlePack();
        PuddlePack(const PuddlePack&) = delete;
        PuddlePack& operator=(const PuddlePack&) = delete;

        /// Opens the container, checks the DERIVED flag and parses the manifest (a located error otherwise; a pack
        /// of another layout version is refused). Returns nullptr on failure.
        static std::unique_ptr<PuddlePack> open(const std::filesystem::path& path, Error& error);
        static std::unique_ptr<PuddlePack> open(std::shared_ptr<const EdenArchive::Source> source, Error& error);

        const Manifest& manifest() const noexcept;
        std::uint64_t generation() const noexcept;
        const Digest& sourceDigest() const noexcept;
        /// The cells' coordinates in key order.
        const std::vector<std::pair<std::int32_t, std::int32_t>>& cells() const noexcept;
        bool contains(std::int32_t cellX, std::int32_t cellY) const noexcept;
        /// The input digest in the cell's provenance record, without reading the cell.
        std::optional<Digest> inputDigest(std::int32_t cellX, std::int32_t cellY) const;
        /// Reads and parses one cell, and checks that its coordinates and spacing are the entry's.
        Error readCell(std::int32_t cellX, std::int32_t cellY, CellMap& cell) const;

    private:
        struct Impl;
        explicit PuddlePack(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
