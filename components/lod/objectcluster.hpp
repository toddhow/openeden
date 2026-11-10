#ifndef OPENMW_COMPONENTS_LOD_OBJECTCLUSTER_H
#define OPENMW_COMPONENTS_LOD_OBJECTCLUSTER_H

// OpenEden world LOD (task 0059, stage S4b). The payload of one baked object cluster: the far-visible statics of one
// tile, merged into one mesh per material group and simplified. The bytes are one Eden Archive entry of type 0x0011.
//
// Layout version 1: the draft layout (version 0) was approved and frozen by toddhow on 2026-11-03 (task 0066 stage d),
// with the version field changed from 0 to 1 and levels 1 to 4 and up to 256 cell slots allowed (a slot is one byte).
// The header's version field holds objectClusterPayloadVersion (1) and the reader refuses any other value, so a
// version 0 pack is refused, never half-read.
// Nothing outside components/lod may depend on offsets or bit positions; use these functions. The layout is written in
// docs/tasks/0059-lod-s4-object-clusters.md ("Draft cluster entry layout").
//
// Reading untrusted bytes: every size and offset is checked before it is used; a failure returns a LodError and never
// throws or reads outside the span.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "clustereffect.hpp"
#include "loderror.hpp"
#include "tilekey.hpp"

namespace Lod
{
    inline constexpr std::uint16_t entryTypeObjectCluster = 0x0011;
    /// The container's key tag of an object cluster (spec section 10.3, 11.3). The low 56 bits of the key are those of
    /// the terrain tile key of the same tile.
    inline constexpr std::uint8_t tagObjectCluster = 0x11;
    inline constexpr std::uint16_t objectClusterPayloadVersion = 1; // frozen 2026-11-03
    /// Layout version 2 (task 0096 stage b; toddhow approved the succession 2026-11-09): version 1 plus one effect
    /// record per group (clusterEffectRecordSize bytes, after the string table). The writer uses it only when a
    /// group carries an effect, so a cluster without effects is still written as version 1, byte for byte as before;
    /// the reader accepts both, and version 1 means "no effects".
    inline constexpr std::uint16_t objectClusterEffectsVersion = 2;
    inline constexpr std::size_t clusterEffectRecordSize = 16;
    inline constexpr std::size_t objectClusterHeaderSize = 104;
    inline constexpr std::size_t clusterGroupSize = 32;
    inline constexpr std::size_t clusterRangeSize = 16;
    inline constexpr std::size_t clusterCellSize = 16;
    inline constexpr std::size_t clusterRefSize = 8;
    inline constexpr std::size_t clusterVertexSize = 18;

    inline constexpr std::uint32_t maxClusterGroups = 4096;
    inline constexpr std::uint32_t maxClusterRanges = 65536;
    inline constexpr std::uint32_t maxClusterCells = 256; // a vertex's cell slot is one byte
    inline constexpr std::uint32_t maxClusterRefs = 1u << 20;
    inline constexpr std::uint32_t maxClusterVertices = 1u << 22;
    inline constexpr std::uint32_t maxClusterIndices = 1u << 24;
    inline constexpr std::uint32_t minClusterLevel = 1;
    inline constexpr std::uint32_t maxClusterLevel = 4; // a level 4 tile is 16 x 16 cells = 256 slots
    inline constexpr std::uint32_t maxClusterTexturePath = 1024;

    /// Material flags of a group (any other bit is refused).
    inline constexpr std::uint32_t clusterFlagAlphaTest = 1u << 0;
    inline constexpr std::uint32_t clusterFlagTwoSided = 1u << 1;

    /// 18 bytes in the payload: position as three 16-bit numbers on a grid (below), normal as three snorm8, the cell
    /// slot, vertex colour, and the texture coordinates as two half floats. The logical values here are what the
    /// payload decodes to, so a cluster must already lie on the grid and be representable (snapClusterToGrid).
    struct ClusterVertex
    {
        float mPosition[3] = { 0, 0, 0 };
        std::int8_t mNormal[3] = { 0, 0, 0 };
        std::uint8_t mCellSlot = 0; // index into ObjectCluster::mCells
        std::uint8_t mColor[4] = { 255, 255, 255, 255 };
        float mUv[2] = { 0, 0 };
        bool operator==(const ClusterVertex&) const = default;
    };

    /// One material: a texture and its state. The group's triangles are mIndexCount indices from mFirstIndex.
    struct ClusterGroup
    {
        std::string mTexture; // VFS path, normalised, valid UTF-8, 1 to maxClusterTexturePath bytes
        std::uint32_t mFlags = 0;
        float mAlphaCutoff = 0; // 0 unless clusterFlagAlphaTest
        std::uint32_t mFirstIndex = 0;
        std::uint32_t mIndexCount = 0;
        ClusterEffect mEffect; // layout version 2; none in version 1 (last, so older aggregate initialisers still work)
        bool operator==(const ClusterGroup&) const = default;
    };

    /// The triangles of one cell inside one group. Sorted by (group, cell slot); the ranges of a group tile its span.
    struct ClusterRange
    {
        std::uint32_t mGroup = 0;
        std::uint32_t mCellSlot = 0;
        std::uint32_t mFirstIndex = 0;
        std::uint32_t mIndexCount = 0;
        bool operator==(const ClusterRange&) const = default;
    };

    /// One source cell and its references (a slice of ObjectCluster::mRefs). Sorted by (y, x).
    struct ClusterCell
    {
        std::int32_t mCellX = 0;
        std::int32_t mCellY = 0;
        std::uint32_t mFirstRef = 0;
        std::uint32_t mRefCount = 0;
        bool operator==(const ClusterCell&) const = default;
    };

    /// An ESM::RefNum: FormId::mIndex and FormId::mContentFile (stored as an unsigned bit pattern).
    struct ClusterRef
    {
        std::uint32_t mIndex = 0;
        std::uint32_t mContentFile = 0;
        bool operator==(const ClusterRef&) const = default;
        bool operator<(const ClusterRef& other) const
        {
            return mIndex != other.mIndex ? mIndex < other.mIndex : mContentFile < other.mContentFile;
        }
    };

    /// The logical content of one cluster. World units, Z up.
    struct ObjectCluster
    {
        std::uint32_t mLevel = 0;
        float mSide = 0; // tile side in world units
        float mOriginX = 0, mOriginY = 0; // south-west corner
        float mMinZ = 0, mMaxZ = 0; // of all vertices
        float mGeometricError = 0; // world units
        float mOverhang = 0; // largest distance any vertex lies outside the tile square in x or y
        std::uint32_t mSourceTriangles = 0; // before simplification (a statistic)
        std::vector<ClusterGroup> mGroups; // sorted by (texture, flags, cutoff, effect kind, velocity)
        std::vector<ClusterRange> mRanges;
        std::vector<ClusterCell> mCells;
        std::vector<ClusterRef> mRefs;
        std::vector<ClusterVertex> mVertices;
        std::vector<std::uint32_t> mIndices; // triangle lists
        bool operator==(const ObjectCluster&) const = default;
    };

    /// The key of the cluster of a tile: the terrain tile key's low 56 bits with the tag tagObjectCluster. Empty when
    /// the id does not pack (same rules as packTileKey).
    std::optional<std::uint64_t> packClusterKey(const TileId& id) noexcept;
    /// Empty when the tag is not tagObjectCluster or the fields are invalid.
    std::optional<TileId> unpackClusterKey(std::uint64_t key) noexcept;

    /// Whether a texture path may be stored: 1 to maxClusterTexturePath bytes, valid UTF-8, no control characters, no
    /// backslash, no upper-case ASCII letter, no leading slash (the VFS's normalised form).
    bool isValidClusterTexturePath(std::string_view path) noexcept;

    /// Moves every vertex to the nearest point the payload can hold, then fixes the cluster's z range and overhang to
    /// match. Positions go to a grid of 2^k units (k the smallest that fits the tile and the z range in 16 bits),
    /// texture coordinates to half floats. Call it on a built cluster before writing it. Fails when the tile does not
    /// fit any grid or a number is not finite.
    LodError snapClusterToGrid(ObjectCluster& cluster);

    /// What the selection needs of a cluster, from the first objectClusterHeaderSize bytes alone.
    struct ClusterHeaderInfo
    {
        std::uint32_t mLevel = 0;
        float mSide = 0;
        float mOriginX = 0, mOriginY = 0;
        float mMinZ = 0, mMaxZ = 0;
        float mGeometricError = 0;
        float mOverhang = 0;
    };

    /// Parses and checks the header's own numbers (magic, version, level, finite and ordered floats). Does not look at
    /// the tables.
    LodError readObjectClusterHeader(std::span<const std::uint8_t> bytes, ClusterHeaderInfo& info);

    /// Checks everything the reader checks, on the logical content (so a writer cannot produce what the reader
    /// refuses): level, finite numbers, caps, group spans and ranges tiling, indices below the vertex count, cell
    /// slots, vertices inside the bounds, references sorted per cell and covering the table, sorted cells and groups,
    /// paths.
    LodError validateObjectCluster(const ObjectCluster& cluster);

    /// The canonical bytes (header, groups, ranges, cells, references, vertices, indices, strings, each table
    /// 8-aligned, padding zero). Fails with the same located error as validateObjectCluster. A cluster with no groups
    /// is refused (the baker writes none).
    LodError writeObjectCluster(const ObjectCluster& cluster, std::vector<std::uint8_t>& out);

    /// Parses and validates untrusted bytes. `expected`, when given, is the tile whose key the entry was stored under:
    /// the payload's level must match its level. The bytes must be exactly canonical (write(read(x)) == x).
    LodError readObjectCluster(
        std::span<const std::uint8_t> bytes, ObjectCluster& cluster, const TileId* expected = nullptr);
}

#endif
