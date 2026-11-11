#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLESTATICS_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLESTATICS_H

// OpenEden puddles (task 0095, stage W-c). Static surfaces: roads, paving, decks and floors of exterior statics catch
// rain too, so the bake needs the TOP-MOST static surface of every column, not only the terrain. Research note 0049
// section 4.3 has the reasoning; the decisions are recorded in the task spec's W-c stage log.
//
// This file is pure (no game types, no Vulkan, no clock). It holds
//  * `StaticSurfaceSource`, where the baker gets the captured surfaces and the digest of the references they came from
//    (the game implements it in apps/openmw/puddlestatics.*; the tests implement it with generated scenes);
//  * the capture file of one cell (`openeden.puddlestatic`, canonical, strict reader) that the engine writes to the
//    bake's capture folder after it has cast the rays;
//  * `applyStatics`, the filter that turns raw captured tops into the surfaces the fill sees.
//
// What a "capture" is: for every texel of a cell, the world Z where a ray cast straight down from above first meets a
// static collision shape, or NaN where it meets none. The ray stands at the texel's corner, the same place the terrain
// is sampled (`terrainHeightAt`). It is a RAW capture: roofs, tops of walls, rocks and clutter are all in it, and
// `applyStatics` decides what counts.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "puddlemap.hpp"

namespace Puddle
{
    /// Version of the filter's behaviour. Goes into the parameter digest (only when statics are on, so a statics-off
    /// pack stays byte-identical to stage W-b's). Bump when `applyStatics` can give another answer for the same input.
    inline constexpr std::uint32_t staticFilterVersion = 1;

    // Tuning of the filter (by eye and by the shape of Morrowind's roads; no source). Named, not magic.

    /// A raw top counts only where it is at least this far above the terrain (world units): anything lower is the
    /// ground itself or something buried in it.
    inline constexpr float staticMinRise = 0.5f;
    /// The steepest a surface may be, as height per horizontal unit between neighbouring texels (0.5 is about 27
    /// degrees). A texel next to a steeper one is not a surface: sloped roofs, rocks and stairs' risers drop out.
    inline constexpr float staticSlopeLimit = 0.5f;
    /// A surface narrower than this many texels is not one (fences, rails, posts): the side of the square that must
    /// fit, in texels.
    inline constexpr std::uint32_t staticMinWidthTexels = 2;
    /// A connected surface smaller than this many texels is clutter (a barrel, a sign, a stall).
    inline constexpr std::uint32_t staticMinAreaTexels = 16;
    /// The captured heights are untrusted input: beyond this they are refused.
    inline constexpr float staticMaxAbsHeight = 1.0e7f;

    /// Where the baker gets static surfaces. One call from one thread at a time.
    class StaticSurfaceSource
    {
    public:
        virtual ~StaticSurfaceSource() = default;
        /// A digest of the references (and the models' contents) that make the cell's static surfaces; false when it
        /// cannot be made. The baker's input digest covers it, so a moved or changed static makes the cell stale.
        virtual bool refsDigest(std::int32_t cellX, std::int32_t cellY, Digest& digest) = 0;
        /// The raw captured tops of the cell's own texels: side * side values, x fastest, south row first, NaN where a
        /// ray found nothing. False when there is no capture for the cell as it is now.
        virtual bool cellTops(std::int32_t cellX, std::int32_t cellY, std::vector<float>& tops) = 0;
    };

    /// What a cell's capture holds in a file.
    struct StaticCapture
    {
        std::int32_t mCellX = 0;
        std::int32_t mCellY = 0;
        std::uint32_t mSide = 0;
        float mSpacing = 0.f;
        /// The references' digest at the time of the capture; the capture is current while it still equals the digest
        /// of now.
        Digest mRefsDigest{};
        std::vector<float> mTops;

        bool operator==(const StaticCapture& other) const;
    };

    inline constexpr std::uint32_t captureLayoutVersion = 1;
    inline constexpr std::uint32_t captureHeaderSize = 64;

    /// Payload (little-endian): magic "EDENPST\0" (8), layout version (4), cell x (4), cell y (4), side (4), spacing
    /// (float, 4), reserved 0 (4), references digest (32); then side * side floats: finite, at most staticMaxAbsHeight
    /// in absolute value, or the one canonical NaN 0x7FC00000 for "nothing". Exactly that size.
    std::uint64_t capturePayloadSize(std::uint32_t side) noexcept;
    Error writeCapture(const StaticCapture& capture, std::vector<std::uint8_t>& out);
    /// Strict, canonical: a read followed by a write gives the same bytes. Never throws, checks sizes before it
    /// allocates.
    Error parseCapture(const std::uint8_t* data, std::size_t size, StaticCapture& capture);

    /// The captures of a bake, one file a cell in a folder (`<puddles>/statics`). Unlike the work folder of the cells
    /// it is kept after a bake: a capture is expensive (the game has to load the cell) and still valid while the
    /// references are unchanged.
    class CaptureStore
    {
    public:
        explicit CaptureStore(std::filesystem::path folder);
        /// The tops of the stored capture if it parses, is the asked cell, has this side and spacing and carries
        /// exactly this references digest.
        std::optional<std::vector<float>> load(
            std::int32_t cellX, std::int32_t cellY, std::uint32_t side, float spacing, const Digest& refsDigest) const;
        Error store(const StaticCapture& capture) const;
        std::filesystem::path pathOf(std::int32_t cellX, std::int32_t cellY) const;
        const std::filesystem::path& folder() const { return mFolder; }

    private:
        std::filesystem::path mFolder;
    };

    struct StaticFilterResult
    {
        /// The heights the fill sees: the terrain, raised to the surface where one was accepted.
        std::vector<float> mHeights;
        /// 1 where a static surface was accepted.
        std::vector<std::uint8_t> mIsStatic;
        std::uint32_t mCandidates = 0; // raw tops above the terrain
        std::uint32_t mAccepted = 0;
    };

    /// Decides which raw tops are surfaces. `terrain` and `raw` are `side * side` values (the baker's window, row-major
    /// south row first); `spacing` is the world distance between texels. A texel becomes a static surface when:
    ///  1. its raw top is finite and at least `staticMinRise` above the terrain there;
    ///  2. it is not next to a candidate whose top differs by more than `staticSlopeLimit * spacing`;
    ///  3. it lies in a `staticMinWidthTexels` square of texels that passed 1 and 2;
    ///  4. its 8-connected group has at least `staticMinAreaTexels` texels.
    /// No randomness, fixed scan order: the answer is a function of the arrays alone.
    StaticFilterResult applyStatics(
        const std::vector<float>& terrain, const std::vector<float>& raw, std::uint32_t side, float spacing);
}

#endif
