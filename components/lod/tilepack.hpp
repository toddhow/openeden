#ifndef OPENMW_COMPONENTS_LOD_TILEPACK_H
#define OPENMW_COMPONENTS_LOD_TILEPACK_H

// OpenEden world LOD (task 0055, stage S1b). A derived Eden Archive pack of baked terrain tiles: one keyed entry per
// tile (type 0x0010, key tag 0x10) plus the manifest entry `lod/manifest.json`. Layout version 1.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <edenarchive/archive.hpp>
#include <edenarchive/hash.hpp>

#include "loderror.hpp"
#include "terraintile.hpp"
#include "tilekey.hpp"

namespace Lod
{
    inline constexpr std::string_view manifestPath = "lod/manifest.json";
    /// Provenance tool id of every tile entry.
    inline constexpr std::string_view bakerToolId = "openeden.lod.terrain";

    struct WorldspaceInfo
    {
        std::uint32_t mIndex = 0;
        std::string mName;
        std::uint32_t mCellSize = 0; // world units, 8192 for Morrowind
        bool operator==(const WorldspaceInfo&) const = default;
    };

    /// The manifest entry: what the pack holds. Canonical JSON (fixed key order, no spaces, one line, no trailing
    /// newline) that round trips byte for byte.
    struct TileManifest
    {
        std::uint32_t mLayoutVersion = tileKeyLayoutVersion;
        std::uint32_t mBakerVersion = 0;
        std::vector<WorldspaceInfo> mWorldspaces;
        std::vector<std::uint32_t> mLevels; // levels baked
        std::uint32_t mGridSize = 0;
        std::uint32_t mAlbedoSize = 0; // texels per side, 0 when no albedo
        bool operator==(const TileManifest&) const = default;
    };

    std::string writeManifestJson(const TileManifest& manifest);
    /// Strict: text that is not exactly what writeManifestJson would write for the parsed value is refused (so the
    /// byte-for-byte round trip holds), with the offset of the first problem.
    LodError parseManifestJson(std::string_view text, TileManifest& manifest);

    struct BakedTile
    {
        TileId mId;
        TerrainTile mData;
        /// Digest of the land records and texture list this tile came from (the entry's provenance).
        EdenArchive::Hash mSourceDigest{};
        /// The baker settings that made it (provenance parameter bytes).
        std::vector<std::uint8_t> mParameters;
        /// What mSourceDigest is made of, for the message that names the input of a stale pack (set by
        /// bakeTerrainTile, not stored in a pack): the digests of the full-detail heights and normals, of the albedo
        /// inputs (levels with an albedo only) and the layer indices the albedo inputs used.
        EdenArchive::Hash mHeightsDigest{};
        EdenArchive::Hash mNormalsDigest{};
        EdenArchive::Hash mAlbedoInputsDigest{};
        std::vector<std::uint32_t> mUsedLayers;
    };

    struct TilePackOptions
    {
        std::uint64_t mGeneration = 0;
        EdenArchive::Hash mSourceDigest{}; // of the whole content set
        unsigned mThreads = 1;
        bool mStoreOnly = false; // no compression: byte-identical on any machine
        int mZstdLevel = 3; // when compressing (task 0059: the cluster pack uses a higher level)
    };

    /// Builds the pack in memory. The order of `tiles` does not matter; the output depends only on the set (the
    /// writer sorts by key). Fails on a duplicate tile id, an invalid tile or an id that does not pack. The pack has
    /// the DERIVED flag.
    LodError buildTilePack(const TileManifest& manifest, const std::vector<BakedTile>& tiles,
        const TilePackOptions& options, std::vector<std::uint8_t>& out);
    /// The same, to a file (written under a temporary name and renamed, by the archive writer).
    LodError writeTilePack(const TileManifest& manifest, const std::vector<BakedTile>& tiles,
        const TilePackOptions& options, const std::filesystem::path& path);

    /// An open pack of tiles. Immutable; the const functions may be called from several threads.
    class TilePack
    {
    public:
        ~TilePack();
        TilePack(const TilePack&) = delete;
        TilePack& operator=(const TilePack&) = delete;

        /// Opens the container, checks the DERIVED flag and parses the manifest (a located error otherwise; a pack
        /// whose manifest layout version is not tileKeyLayoutVersion is refused). Returns nullptr on failure.
        static std::unique_ptr<TilePack> open(const std::filesystem::path& path, LodError& error);
        static std::unique_ptr<TilePack> open(std::shared_ptr<const EdenArchive::Source> source, LodError& error);

        const TileManifest& manifest() const noexcept;
        std::uint64_t generation() const noexcept;
        const EdenArchive::Hash& sourceDigest() const noexcept;
        /// The tile entries' ids in key order.
        const std::vector<TileId>& tileIds() const noexcept;
        bool contains(const TileId& id) const noexcept;
        /// The digest of the land and textures the tile was baked from (its provenance record), or nothing when the
        /// pack has no such tile or the entry has no provenance. Used to check a pack tile by tile.
        std::optional<EdenArchive::Hash> tileSourceDigest(const TileId& id) const;

        /// Reads just the 88-byte header (one range read) and checks it against the entry's size.
        LodError readHeader(const TileId& id, TerrainTile& tile, TerrainTileLayout& layout) const;
        /// Reads and parses the whole tile.
        LodError readTile(const TileId& id, TerrainTile& tile, EdenArchive::DecodeBudget* budget = nullptr) const;
        /// Reads only the albedo section (header range read, then a range read of the albedo bytes).
        LodError readAlbedo(
            const TileId& id, std::vector<std::uint8_t>& out, EdenArchive::DecodeBudget* budget = nullptr) const;

        const EdenArchive::Archive& archive() const noexcept;

    private:
        struct Impl;
        explicit TilePack(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
