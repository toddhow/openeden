#ifndef OPENEDEN_GAME_SKYRIMSE_LAND_H
#define OPENEDEN_GAME_SKYRIMSE_LAND_H

// Skyrim SE exterior terrain records as plain data (task 0117, stage E0 of docs/research/0053): LAND (heights, normals,
// colours, base texture and alpha layers), LTEX (a ground texture) and TXST (a texture set). The pack's own types: no
// SDK type, no engine type, no Jolt or Qt type (the SDK's terrain types are stage E1).
//
// Layout status of every field is marked in land.cpp: VERIFIED against the installed Skyrim SE masters by
// tools/skyrimse_land_check (aggregates only), or RECALLED-VERIFY. The inherited ESM4 reader (components/esm4/loadland.hpp
// and loadland.cpp) was read as a reference and agrees on every layout used here.
//
// Hostile input: every size read from the file is checked against the bytes that remain, and against a cap, before
// anything is allocated or reserved; nothing throws; a bad record is a located error and the walk goes on.

#include "game_skyrimse/records.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace LandConstants
    {
        /// A cell's terrain is 33 x 33 vertices, 32 intervals of 128 units, 4096 units across (VERIFIED: VHGT is 1,096
        /// bytes in every record that has one).
        inline constexpr std::uint32_t kVertsPerSide = 33;
        inline constexpr std::uint32_t kVertCount = kVertsPerSide * kVertsPerSide;
        inline constexpr float kCellSizeUnits = 4096.0f;
        inline constexpr float kVertexSpacingUnits = 128.0f;
        /// A VHGT gradient step is this many units (VERIFIED by the shared edges of neighbouring cells matching).
        inline constexpr float kHeightScale = 8.0f;
        /// A cell is four quadrants of 17 x 17 vertices (the quadrants share their middle row and column).
        inline constexpr std::uint32_t kQuadrants = 4;
        inline constexpr std::uint32_t kQuadrantVertsPerSide = 17;
        inline constexpr std::uint32_t kQuadrantVertCount = kQuadrantVertsPerSide * kQuadrantVertsPerSide;
    }

    /// The caps on what a file may make the decoders allocate.
    struct ExteriorLimits
    {
        /// One record's inflated bytes (a record over this is refused with an error).
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxStringBytes = 64 * 1024; ///< one zstring or texture path
        /// Alpha layers of one quadrant (the installed masters have at most 6 or 7) and of one cell.
        std::uint32_t mMaxLayersPerQuadrant = 32;
        std::uint32_t mMaxLayersPerLand = 128;
        std::uint32_t mMaxGrassPerGroundTexture = 256; ///< GNAM entries of one LTEX
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    /// What the decoders counted and did not keep. Subrecords whose size or content did not fit the layout are
    /// counted by "TYPE.SUB" and never guessed at (the record is still kept).
    struct DecodeNotes
    {
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy;
        std::uint64_t mUnresolvedFormIds = 0;

        void odd(const char* record, const char* sub);
    };

    /// What a decoder needs besides the record's bytes.
    struct DecodeContext
    {
        const FormIdResolver& mResolver;
        const ExteriorLimits& mLimits;
        DecodeNotes& mNotes;
        bool mLocalized = false;
    };

    /// One entry of a VTXT: where in the quadrant's 17 x 17 grid, and how opaque the layer is there.
    struct LandOpacity
    {
        std::uint16_t mPosition = 0; ///< 0 to 288; VERIFIED sorted ascending and never above 288
        float mOpacity = 0.0f; ///< 0 to 1
    };

    /// One alpha layer of a quadrant (ATXT, with the VTXT that follows it).
    struct LandLayer
    {
        FormId mTexture = kNullFormId; ///< an LTEX; kNullFormId is kept as stored (see land.cpp for what the data says)
        std::uint16_t mLayerIndex = 0;
        std::vector<LandOpacity> mOpacity; ///< empty when no VTXT follows (the layer then adds nothing)
    };

    struct LandQuadrant
    {
        FormId mBase = kNullFormId; ///< BTXT, 0 when the record has none for this quadrant
        std::vector<LandLayer> mLayers; ///< in file order
    };

    /// A LAND record.
    struct LandRecord
    {
        RecordCommon mCommon;
        /// Where the walk found it (the cell's temporary children group): the world, the cell and the cell's XCLC
        /// grid. kNullFormId / nullopt when decoded outside a walk.
        FormId mWorld = kNullFormId;
        FormId mCell = kNullFormId;
        std::optional<CellGrid> mGrid;

        bool mHasDataFlags = false;
        std::uint32_t mDataFlags = 0; ///< DATA, a flag word of which data are present (bit meanings: see land.cpp)

        /// VHGT decoded: mHeights[row * 33 + col] in units (the offset and the running gradient times 8). The row and
        /// column orientation is VERIFIED in land.cpp (the shared edges of neighbouring cells).
        bool mHasHeights = false;
        float mHeightOffset = 0.0f; ///< the raw offset (in gradient steps)
        std::vector<float> mHeights;
        bool mHasNormals = false;
        std::vector<std::int8_t> mNormals; ///< VNML, 33 x 33 x (x y z), signed bytes as stored
        bool mHasColours = false;
        std::vector<std::uint8_t> mColours; ///< VCLR, 33 x 33 x (r g b)

        std::array<LandQuadrant, LandConstants::kQuadrants> mQuadrants;

        [[nodiscard]] std::size_t layerCount() const;
    };

    /// LTEX: a ground texture.
    struct GroundTextureRecord
    {
        RecordCommon mCommon;
        FormId mTextureSet = kNullFormId; ///< TNAM, a TXST
        FormId mMaterial = kNullFormId; ///< MNAM, a material type
        std::optional<std::uint8_t> mFriction; ///< HNAM, two bytes (friction, restitution)
        std::optional<std::uint8_t> mRestitution;
        std::optional<std::uint8_t> mSpecularExponent; ///< SNAM
        std::vector<FormId> mGrass; ///< GNAM, one per entry
    };

    /// TXST: a texture set. TX00 to TX07 are the file paths, "" where the set has none.
    struct TextureSetRecord
    {
        RecordCommon mCommon;
        std::array<std::string, 8> mPaths;
        std::optional<std::uint16_t> mFlags; ///< DNAM
    };

    // ---- the pure decoders: one record's inflated bytes in, a struct out ------------------------------------------

    /// The gradient walk: the offset plus the running sum along the first column, then along each row, times 8.
    /// `gradient` has 33 x 33 values. Exposed for the tests.
    [[nodiscard]] std::vector<float> decodeHeights(float offset, std::span<const std::int8_t> gradient);

    /// Each returns false with `why` set (a located message without the file name) for a record the caps or the
    /// framing refuse; a subrecord of a wrong size is counted in the notes and skipped, not an error.
    [[nodiscard]] bool decodeLand(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, LandRecord& out, std::string& why);
    [[nodiscard]] bool decodeGroundTexture(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, GroundTextureRecord& out, std::string& why);
    [[nodiscard]] bool decodeTextureSet(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, TextureSetRecord& out, std::string& why);
}

#endif
