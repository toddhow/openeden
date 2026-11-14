#ifndef OPENEDEN_GAME_SKYRIMSE_GRASS_H
#define OPENEDEN_GAME_SKYRIMSE_GRASS_H

// Skyrim SE grass (task 0153, milestone M-S2): the GRAS record as plain data and the pure placement of grass tufts on
// one cell's land. The ground textures name their grass (LTEX GNAM, land.hpp); a GRAS says which model, how dense, on
// which slopes and heights relative to water. Pack types only; the SDK's plain values (grass_source.hpp,
// world_terrain.hpp) are the interface to the core.
//
// Field layouts are marked VERIFIED (against the installed Skyrim SE masters by the gated real-data test
// testskyrimsegrassreal.cpp, aggregates only) or RECALLED-VERIFY (UESP/xEdit field definitions as remembered; decision
// 0014's amendment allows those sources). The inherited ESM4 reader (components/esm4/loadgras.cpp, remote
// openmw-upstream) reads DATA as the same 32 bytes.
//
// Hostile input: sizes are checked against the bytes that remain before anything is allocated; nothing throws; the
// placement is capped per square and per cell and is a pure function of its inputs (no clock, no unseeded random).

#include "game_skyrimse/land.hpp"

#include <openeden/game/grass_source.hpp>
#include <openeden/game/world_terrain.hpp>
#include <openeden/game/world_water.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace GrassFlags
    {
        inline constexpr std::uint8_t VertexLighting = 0x01; ///< RECALLED-VERIFY (UESP)
        inline constexpr std::uint8_t UniformScaling = 0x02;
        inline constexpr std::uint8_t FitToSlope = 0x04;
    }

    /// A GRAS record.
    struct GrassRecord
    {
        RecordCommon mCommon;
        std::string mModel; ///< MODL, as stored
        bool mHasData = false;
        std::uint8_t mDensity = 0; ///< DATA byte 0 (0 to 100)
        std::uint8_t mMinSlope = 0; ///< degrees, byte 1
        std::uint8_t mMaxSlope = 90; ///< degrees, byte 2
        std::uint16_t mUnitsFromWater = 0; ///< bytes 4..5
        std::uint32_t mUnitsFromWaterType = 0; ///< bytes 8..11
        float mPositionRange = 0.0f; ///< bytes 12..15
        float mHeightRange = 0.0f; ///< bytes 16..19
        float mColorRange = 0.0f; ///< bytes 20..23
        float mWavePeriod = 0.0f; ///< bytes 24..27
        std::uint8_t mFlags = 0; ///< byte 28
    };

    [[nodiscard]] bool decodeGrass(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, GrassRecord& out, std::string& why);

    /// The tables the placement reads: each ground texture's grass (LTEX GNAM, a LTEX form id to GRAS form ids) and the
    /// GRAS records themselves. Ids are the load-order ids of the terrain cell's `texture` fields.
    struct GrassTables
    {
        std::map<FormId, std::vector<FormId>> mGrassOfTexture;
        std::map<FormId, GrassRecord> mKinds;
    };

    /// The tuning of the placement. Estimates, not measured against the game: named so they are findable.
    struct GrassPlacementOptions
    {
        /// Tufts per 128 x 128 unit square for each point of GRAS density. 0.3 puts 27 tufts in a square at density 90
        /// (about one tuft per 22 units), an eyeballed estimate.
        float mTuftsPerDensityPoint = 0.3f;
        /// Tufts in one cell over which the density of every kind is scaled down together (a cell is 1,024 squares).
        std::uint32_t mMaxInstancesPerCell = 30000;
        /// A grass whose DATA is missing, or whose model path is empty, is not placed.
        float mMinScale = 0.25f;
        float mMaxScale = 4.0f;
    };

    /// The wave period at which the sway runs at the engine's own speed (task 0153 follow-up 2). The 58 GRAS records of
    /// the five masters have wave periods from 0.0001 to 600 with a median of 240 (aggregates, Python walker); the unit
    /// is not known (RECALLED-VERIFY: seconds), so the reference is the data's median and the speed only orders the
    /// kinds: slower sway for longer periods, clamped to a factor of two either way.
    inline constexpr float kWaveReferencePeriod = 240.0f;
    inline constexpr float kWaveSpeedMin = 0.5f;
    inline constexpr float kWaveSpeedMax = 2.0f;
    /// The sway speed, relative to the engine's, of a GRAS wave period: kWaveReferencePeriod / period clamped to
    /// [kWaveSpeedMin, kWaveSpeedMax]; 1 for a period that is not finite or not positive.
    [[nodiscard]] float waveSpeedOf(float wavePeriod);

    /// The density the pack ships (task 0153 follow-up): 0.35 of GrassPlacementOptions' first estimate. At that
    /// estimate frames of Whiterun's plains showed a continuous carpet (tufts are 40 to 110 units wide and 35 to 70
    /// high) where Skyrim shows ground between its tufts; at 0.35, 0.5 and 0.2 of it the ground shows at 0.35 and the
    /// field is still full (compared by eye, not measured against the game: RECALLED-VERIFY). The struct's own default
    /// is kept (the first tests pin it); the pack builds its options through shippedGrassOptions().
    inline constexpr float kShippedTuftsPerDensityPoint = 0.105f;
    [[nodiscard]] GrassPlacementOptions shippedGrassOptions();

    /// The lowest opacity-weighted layer is the base texture; each layer is drawn over what lies under it. The weight
    /// of the winning texture at a point of a quadrant (bilinear in the 17 x 17 opacity grid). Exposed for the tests.
    /// `quadrant` is 0 SW, 1 SE, 2 NW, 3 NE; (u, v) in [0, 1] across the quadrant, u east and v north.
    [[nodiscard]] openeden::sdk::game::WorldId dominantTexture(
        const openeden::sdk::game::TerrainCell& cell, std::uint32_t quadrant, float u, float v);

    /// Places the grass of one cell (a 4096 unit square whose south-west corner is at grid * cellSize). `water` is the
    /// cell's water surface when it has one. Deterministic.
    void placeGrass(const openeden::sdk::game::TerrainCell& cell, float cellSize, const GrassTables& tables,
        const openeden::sdk::game::CellWater& water, const GrassPlacementOptions& options,
        openeden::sdk::game::GrassCell& out);
}

#endif
