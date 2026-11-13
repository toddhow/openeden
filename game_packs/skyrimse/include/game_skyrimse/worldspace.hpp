#ifndef OPENEDEN_GAME_SKYRIMSE_WORLDSPACE_H
#define OPENEDEN_GAME_SKYRIMSE_WORLDSPACE_H

// Skyrim SE worldspaces, exterior cell water and WATR as plain data (task 0117, stage E0 of docs/research/0053), plus
// the walker that collects them with LAND, LTEX and TXST from one plugin. Pack-own types only (SDK terrain types are
// stage E1). See land.hpp for the hostile-input rules, which apply here too.

#include "game_skyrimse/grass.hpp"
#include "game_skyrimse/land.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace WorldConstants
    {
        /// Skyrim SE has no per-world cell size field: an exterior cell is 4096 units (VERIFIED against the XCLC grid and the
        /// object positions, task 0086).
        inline constexpr float kCellSizeUnits = LandConstants::kCellSizeUnits;
    }

    /// PNAM: which data a child worldspace takes from its parent (WNAM). Bit meanings: see worldspace.cpp for what the
    /// installed data settles.
    namespace ParentUsage
    {
        inline constexpr std::uint16_t Land = 0x01;
        inline constexpr std::uint16_t Lod = 0x02;
        inline constexpr std::uint16_t Map = 0x04;
        inline constexpr std::uint16_t Water = 0x08;
        inline constexpr std::uint16_t Climate = 0x10;
        inline constexpr std::uint16_t ImageSpace = 0x20;
        inline constexpr std::uint16_t SkyCell = 0x40;
    }

    struct Vec2
    {
        float x = 0;
        float y = 0;
    };

    /// MNAM: the map's usable size and the cells at its north-west and south-east corners.
    struct WorldMapData
    {
        std::int32_t mUsableWidth = 0;
        std::int32_t mUsableHeight = 0;
        std::int16_t mNorthWestX = 0;
        std::int16_t mNorthWestY = 0;
        std::int16_t mSouthEastX = 0;
        std::int16_t mSouthEastY = 0;
        std::uint32_t mSize = 0; ///< the subrecord's size as stored
        bool mHasCamera = false; ///< the 28 byte form: camera heights and pitch follow
        float mCameraMinHeight = 0;
        float mCameraMaxHeight = 0;
        float mCameraInitialPitch = 0;
    };

    struct WorldSpaceRecord
    {
        RecordCommon mCommon;
        LString mName;
        FormId mParent = kNullFormId; ///< WNAM
        std::optional<std::uint16_t> mParentUsage; ///< PNAM
        FormId mClimate = kNullFormId; ///< CNAM
        FormId mWater = kNullFormId; ///< NAM2, a WATR
        FormId mLodWater = kNullFormId; ///< NAM3, a WATR
        std::optional<float> mLodWaterHeight; ///< NAM4
        std::optional<float> mDefaultLandHeight; ///< DNAM, first float
        std::optional<float> mDefaultWaterHeight; ///< DNAM, second float
        std::optional<WorldMapData> mMap; ///< MNAM
        std::optional<std::uint8_t> mFlags; ///< DATA
        std::optional<Vec2> mMinBounds; ///< NAM0, in units
        std::optional<Vec2> mMaxBounds; ///< NAM9, in units
        std::optional<std::array<float, 4>> mMapOffset; ///< ONAM: scale and the x, y, z offsets
        std::optional<std::array<std::int16_t, 2>> mCenterCell; ///< WCTR
    };

    /// What an exterior cell's XCLW means. The sentinel values are settled by the installed data (worldspace.cpp).
    enum class CellWaterKind
    {
        Absent, ///< no XCLW
        UseWorldDefault, ///< the maximum float: the world's default water height applies
        NoWater, ///< an integer-looking sentinel: the cell has no water
        Height, ///< a real height, in units
        Invalid, ///< not finite
    };

    struct CellWater
    {
        CellWaterKind mKind = CellWaterKind::Absent;
        float mRaw = 0.0f;
        float mHeight = 0.0f; ///< valid for Height
    };

    [[nodiscard]] CellWater classifyCellWater(std::optional<float> raw);

    /// An exterior cell's facts that terrain and water need (the cell record itself is in ParsedPlugin).
    struct ExteriorCellInfo
    {
        RecordCommon mCommon;
        FormId mWorld = kNullFormId;
        std::optional<CellGrid> mGrid;
        std::uint16_t mFlags = 0; ///< DATA
        CellWater mWater;
        bool mHasLand = false; ///< a LAND record was in its children
    };

    /// WATR, the basic fields. DNAM (the shading parameters) is not decoded: only its size is kept.
    struct WaterRecord
    {
        RecordCommon mCommon;
        LString mName;
        std::string mNoiseMap; ///< NNAM
        std::optional<std::uint8_t> mOpacity; ///< ANAM
        std::optional<std::uint8_t> mFlags; ///< FNAM
        FormId mSound = kNullFormId; ///< SNAM
        std::uint32_t mDnamSize = 0;
    };

    [[nodiscard]] bool decodeWorldSpace(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, WorldSpaceRecord& out, std::string& why);
    [[nodiscard]] bool decodeWater(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, WaterRecord& out, std::string& why);

    /// Everything the walk collects from one plugin.
    struct ExteriorData
    {
        std::string mName;
        std::vector<WorldSpaceRecord> mWorlds;
        std::vector<ExteriorCellInfo> mCells;
        std::vector<LandRecord> mLands;
        std::vector<GroundTextureRecord> mGroundTextures;
        std::vector<TextureSetRecord> mTextureSets;
        std::vector<GrassRecord> mGrass; ///< task 0153
        std::vector<WaterRecord> mWaters;
        DecodeNotes mNotes;
        std::map<std::uint32_t, TypeCount> mCounts; ///< by record type: seen, parsed, failed, deleted
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    /// Walks an opened plugin and decodes WRLD, exterior CELL water, LAND, LTEX, TXST and WATR. `order` must hold the
    /// plugin and its masters. False only when the walk itself failed (mWalkError); records before it are kept.
    [[nodiscard]] bool parseExterior(
        const Content::Plugin& plugin, const LoadOrder& order, const ExteriorLimits& limits, ExteriorData& out);
    [[nodiscard]] bool parseExteriorBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ExteriorLimits& limits, ExteriorData& out, Content::Error& error);
    [[nodiscard]] bool parseExteriorFile(const std::filesystem::path& path, const LoadOrder& order,
        const ExteriorLimits& limits, ExteriorData& out, Content::Error& error);
}

#endif
