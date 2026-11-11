#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEMAP_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEMAP_H

// OpenEden puddles (task 0095, stage W-b). The baked puddle map of one exterior cell: for every texel the capacity
// (how deep a puddle can get there), the capacity of the deepest point of its pit, the pit's catchment ratio, flow
// direction and flags, the ground height and the pit's id. Format `openeden.puddlemap` layout version 1; design in
// docs/research/0049-puddles-and-wetness.md section 10, decisions in docs/tasks/0095-puddles-and-wetness-v2.md.
//
// Derived data: baked on the user's machine from their own game data, never committed. The payload is read as
// untrusted input: every size is bounded and checked against the bytes left before anything is allocated, a located
// error comes back for anything else (never an exception), and the reader accepts only what the writer would write for
// the parsed value (so a read followed by a write is the same bytes), and the writer only writes a cell whose parts
// agree: the basin table matches the texels (maximum, catchment code, area at least the pit's texels in the cell), the
// catchment and the pit maximum are the same on every texel of a pit, a sink has no flow direction and a static texel
// needs a cell that captured statics. What the reader does NOT check is whether the numbers are right for the land.
//
// Payload (little-endian), `n` = side * side texels:
//   offset  size  field
//        0     8  magic "EDENPUD\0"
//        8     4  layout version (1)
//       12     4  cell x (signed)
//       16     4  cell y (signed)
//       20     4  side (texels a side of the cell, 8 to 1024)
//       24     4  spacing (float, world units a texel)
//       28     4  height base (float: `top` is the height above it)
//       32     4  flags (bit 0: static surfaces were captured; bit 1: no pit in this cell, the pit channels are absent)
//       36     4  basin count (at most 65534)
//       40     4  reserved, 0
//       44    32  parameter digest (BLAKE3 of the baker settings)
//       76    32  input digest (BLAKE3 of the land the cell was made from, halo included)
//      108  header end; then, unless bit 1 of the flags is set:
//             n bytes capacity, n bytes basin maximum, n bytes catchment, then always
//             n bytes texel flags (bits 0-2 flow direction, bit 3 sink, bit 4 static surface, the rest 0),
//             2n bytes top (u16), then unless bit 1:
//             2n bytes basin id (u16, 0xFFFF for none), then the basin table: per basin u32 area (the pond's texels
//             in the baker's window), u8 maximum capacity code, u8 catchment code, u16 reserved 0.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Puddle
{
    inline constexpr std::uint32_t layoutVersion = 1;
    /// The Eden Archive entry type and key tag of a cell (proposed in research note 0049 section 10; the key tag is
    /// the next free one after task 0061's 0x13).
    inline constexpr std::uint16_t entryTypePuddleCell = 0x0014;
    inline constexpr std::uint8_t tagPuddleCell = 0x14;
    inline constexpr std::uint32_t minSide = 8;
    inline constexpr std::uint32_t maxSide = 1024;
    inline constexpr std::uint32_t maxBasins = 65534;
    inline constexpr std::uint16_t noBasin = 0xFFFF;
    inline constexpr std::uint32_t headerSize = 108;

    inline constexpr std::uint32_t flagStatics = 1u;
    inline constexpr std::uint32_t flagNoPits = 2u;
    inline constexpr std::uint32_t knownFlags = flagStatics | flagNoPits;

    /// Texel flag bits.
    inline constexpr std::uint8_t texelDirectionMask = 0x07;
    inline constexpr std::uint8_t texelSink = 0x08;
    inline constexpr std::uint8_t texelStatic = 0x10;
    inline constexpr std::uint8_t texelKnownMask = 0x1F;

    using Digest = std::array<std::uint8_t, 32>;

    /// A located error. Success is an empty mField. Tests compare the field name and offset, never the text.
    struct Error
    {
        std::string mField;
        std::uint64_t mOffset = 0;
        std::string mMessage;

        bool ok() const noexcept { return mField.empty(); }
        explicit operator bool() const noexcept { return !mField.empty(); }
        std::string describe() const { return mField + " at offset " + std::to_string(mOffset) + ": " + mMessage; }
    };

    struct BasinInfo
    {
        std::uint32_t mArea = 0; // texels of the pond in the baker's window
        std::uint8_t mMaxCapacityCode = 0;
        std::uint8_t mCatchmentCode = 0;
        bool operator==(const BasinInfo&) const = default;
    };

    struct CellMap
    {
        std::int32_t mCellX = 0;
        std::int32_t mCellY = 0;
        std::uint32_t mSide = 0;
        float mSpacing = 0.f;
        float mHeightBase = 0.f;
        std::uint32_t mFlags = 0;
        Digest mParameterDigest{};
        Digest mInputDigest{};
        // Row-major, south row first, mSide * mSide each. The pit channels are empty when flagNoPits is set.
        std::vector<std::uint8_t> mCapacity;
        std::vector<std::uint8_t> mBasinMax;
        std::vector<std::uint8_t> mCatchment;
        std::vector<std::uint8_t> mTexelFlags;
        std::vector<std::uint16_t> mTop;
        std::vector<std::uint16_t> mBasinId;
        std::vector<BasinInfo> mBasins;

        bool noPits() const { return (mFlags & flagNoPits) != 0; }
        bool operator==(const CellMap&) const = default;
    };

    /// The exact size of the payload of a cell with these properties.
    std::uint64_t payloadSize(std::uint32_t side, std::uint32_t flags, std::uint32_t basinCount) noexcept;

    /// Writes the canonical payload. Fails (with the field) when the map is not consistent: wrong channel sizes,
    /// basin ids not numbered in order of first appearance, a basin with no texel, unknown flag bits, a texel with a
    /// capacity and no basin or a basin and no capacity, a basin maximum below the capacity, non-finite floats.
    Error writeCell(const CellMap& cell, std::vector<std::uint8_t>& out);
    /// Strict: parses exactly what writeCell writes.
    Error parseCell(const std::uint8_t* data, std::size_t size, CellMap& cell);
    inline Error parseCell(const std::vector<std::uint8_t>& bytes, CellMap& cell)
    {
        return parseCell(bytes.data(), bytes.size(), cell);
    }

    /// The Eden Archive key of a cell: tag 0x14, worldspace 0, then the biased cell coordinates (22 bits each).
    /// Empty when a coordinate is outside [-2^21, 2^21 - 1].
    bool packCellKey(std::int32_t cellX, std::int32_t cellY, std::uint64_t& key) noexcept;
    bool unpackCellKey(std::uint64_t key, std::int32_t& cellX, std::int32_t& cellY) noexcept;
}

#endif
