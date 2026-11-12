#ifndef OPENEDEN_COMPONENTS_NEUTRALTERRAIN_SPLAT_H
#define OPENEDEN_COMPONENTS_NEUTRALTERRAIN_SPLAT_H

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The alpha layers of a Skyrim quadrant as a blend map the terrain fragment shader can read (task 0149).
//
// Morrowind's blend map holds, per texel, the four layer indices of the texel's CORNERS and the shader blends them
// bilinearly: one dominant texture per vertex. A Skyrim quadrant is a base texture plus alpha layers with a per-vertex
// opacity (17 x 17 vertices), several of them can be partly visible at one vertex, so the corner scheme cannot hold it.
// In "splat" mode the same blend map texture (no new binding, so no change to the descriptor layouts or the golden
// interface records) is not square and holds two parts, which the shader tells by its shape:
//   * the weights: each quadrant slot is s x s texels (s = 17: one vertex per texel), RGBA = the four layers' weights
//     (0..255, summing to 255), read with four texelFetch calls and blended by hand inside one slot (never across
//     slots, and no dependence on sampler state);
//   * under them, k rows of k texels: the four layer indices (texture array layers) of each quadrant, read with
//     texelFetch (nearest).
// A chunk of k x k quadrant slots (k >= 1) therefore has width k*s and height k*s + k. A chunk smaller than a quadrant
// is one slot of s = size * 32 + 1 texels (its own vertices only).
//
// The shader's maths is `lookupFor` / `tilesAcross` below in C++ (terrain/main.frag and terrain.frag.slang repeat it;
// a test pins both against the same numbers).
//
// Orientation: the same as the heights: x = +X (east), y = +Y (north); a quadrant is 2 * north + east inside its
// cell (0 south-west, 1 south-east, 2 north-west, 3 north-east).

namespace NeutralTerrain::Splat
{
    inline constexpr int kSide = 17; ///< vertices per quadrant side
    inline constexpr int kVertices = kSide * kSide;
    inline constexpr int kSlots = 4; ///< layers per quadrant the shader blends

    struct LayerIn
    {
        std::uint8_t mIndex = 0; ///< the texture's index in the layer array
        const std::vector<std::uint8_t>* mOpacity = nullptr; ///< kVertices values, or null/short: the layer adds nothing
    };

    /// One quadrant: the four array layers it uses (padded with the base layer) and, per vertex, their weights.
    struct Quadrant
    {
        std::array<std::uint8_t, kSlots> mIds{};
        std::vector<std::array<std::uint8_t, kSlots>> mWeights; ///< kVertices entries, each summing to 255
    };

    /// The base layer then `layers` in draw order, each drawn over the result with its opacity:
    /// w(base) = prod (1 - o_i), w(i) = o_i * prod over later j of (1 - o_j). When more than kSlots layers
    /// (base included) exist, the kSlots with the largest peak weight are kept (ties: earlier first) and the weights
    /// of the others are given to the kept ones in proportion. Weights are whole numbers summing to 255 (largest
    /// remainder).
    [[nodiscard]] Quadrant composeQuadrant(std::uint8_t baseIndex, std::span<const LayerIn> layers);

    struct Layout
    {
        int mK = 1; ///< quadrant slots per side
        int mS = kSide; ///< texels per slot side
        int mWidth = kSide;
        int mHeight = kSide + 1;
    };

    /// The layout of a chunk `sizeInCells` cells wide (cells of two quadrants a side).
    [[nodiscard]] Layout layoutFor(float sizeInCells);

    /// Whether a blend map of this size is a splat map (the shader's test: not square).
    [[nodiscard]] inline bool isSplatSize(int width, int height)
    {
        return height > width;
    }

    /// The layout a splat map of this size has (the shader derives it the same way).
    [[nodiscard]] Layout layoutOfSize(int width, int height);

    /// Where the shader reads for a texture coordinate (0..1 across the chunk).
    struct Lookup
    {
        int mSlotX = 0;
        int mSlotY = 0;
        float mVertexX = 0.f; ///< the position inside the slot in vertex units, 0 to s - 1
        float mVertexY = 0.f;
    };
    [[nodiscard]] Lookup lookupFor(const Layout& layout, float texCoordX, float texCoordY);

    /// The largest chunk (cells a side) that gets a splat map: a chunk of 8 cells is 272 x 288 texels, 313 KiB. Larger
    /// (far) chunks keep the square base-layer map, which the shader still reads (the shape tells which it is).
    inline constexpr float kMaxChunkCells = 8.f;

    /// How many times the ground texture repeats across the chunk (what the Morrowind scheme takes from the blend map's
    /// width): 16 per cell.
    [[nodiscard]] float tilesAcross(const Layout& layout);
}

#endif
