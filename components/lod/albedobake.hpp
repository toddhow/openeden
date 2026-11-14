#ifndef OPENMW_COMPONENTS_LOD_ALBEDOBAKE_H
#define OPENMW_COMPONENTS_LOD_ALBEDOBAKE_H

// OpenEden world LOD (task 0055, albedo bake). The final ground colour of a square of cells as one picture: the
// blend of the land texture layers through the blend map, times the vertex colours, with no lighting. It is a CPU
// copy of what files/shaders/terrain/main.frag does with `diffuseMap`, `blendMap` and `colorMap` (non-maps path), so
// that a far tile can be drawn with this one picture. Pure code.
//
// The shader's rule, for a point at (u, v) in [0, 1] x [0, 1] over the square (u east, v north):
//   blendTexSize = 16 * sizeInCells (blend texels a side; one blend texel is one land texture cell, 512 units)
//   blendCoord   = (u, v) * blendTexSize;  blendTexel = floor(blendCoord);  fraction = blendCoord - blendTexel
//   p = clamp((fraction - 0.5) * 2 + 0.5, 0, 1)                        (per axis; sharpens the blend)
//   weights: w0 = (1-px)(1-py), w1 = px (1-py), w2 = (1-px) py, w3 = px py
//   the four bytes of the blend texel are the layer indices of the weights w0..w3 (corner i is offset (i & 1, i >> 1))
//   texture coordinate of every layer = (u, v) * blendTexSize, repeating, bilinear and mipmapped
//   colour = sum over i of w_i * layer[index_i](texture coordinate)      (blended on the stored 8-bit values)
// then the vertex colours (bilinear over the (64 * sizeInCells + 1) square grid of vertices) multiply it. The result is
// RGBA8 with alpha 255. The baker samples each picture texel at its centre, ((i + 0.5) / size, (j + 0.5) / size), and
// averages the layers over the texel's footprint with the mip level the footprint calls for, as the GPU would, so the
// 16 or more repeats of a texture inside one picture texel do not alias.
//
// Known difference: in a view that converts to linear light (`hdr`), the shader multiplies the vertex colour after the
// conversion; this multiplies before it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <edenarchive/hash.hpp>

#include "loderror.hpp"
#include "texturedecode.hpp"

namespace Lod
{
    inline constexpr std::uint32_t blendTexelsPerCell = 16;
    /// Version of the blending algorithm; it goes into the inputs digest, so a change makes old packs stale.
    inline constexpr std::uint32_t albedoAlgorithmVersion = 1;

    /// Where the blend data, vertex colours and layer textures come from (the game's terrain storage, or test data).
    class AlbedoSource
    {
    public:
        virtual ~AlbedoSource() = default;

        virtual std::size_t layerCount() const = 0;
        /// The layer's picture, or nullptr when it could not be read (the layer then counts as mid grey).
        virtual const Rgba8Image* layerImage(std::size_t index) const = 0;
        /// A digest of the layer's file (so that changing a texture changes it); used in the inputs digest.
        virtual EdenArchive::Hash layerDigest(std::size_t index) const = 0;

        /// The blend map of the square of `sizeInCells` x `sizeInCells` cells whose south-west cell is (cellX, cellY):
        /// n = 16 * sizeInCells texels a side, x fastest, south row first, 4 bytes a texel (the layer indices of the
        /// corners, see above). `indices` gets n * n * 4 bytes.
        virtual void blendMap(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& indices) const = 0;

        /// The vertex colours of the same square: n = (verticesPerCell - 1) * sizeInCells + 1 vertices a side (cells
        /// share their edge vertices; 65 vertices a cell for Morrowind, 33 for Skyrim SE: task 0133), x fastest, south
        /// row first, 3 bytes (R, G, B) a vertex. `rgb` gets n * n * 3 bytes.
        virtual void vertexColours(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& rgb) const = 0;
    };

    /// Bakes the picture of the square: `albedoSize` x `albedoSize` RGBA8 texels, row 0 the SOUTH row (the order of the
    /// tile's height grid; the draw code flips nothing), x fastest. Fails (and leaves `rgba` empty) when sizeInCells
    /// is 0, albedoSize is not a power of two from 4 to 2048, or the source returns arrays of the wrong size. A blend
    /// index at or above layerCount() counts as a missing layer.
    LodError bakeAlbedoImage(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY,
        std::uint32_t sizeInCells, std::uint32_t albedoSize, std::vector<std::uint8_t>& rgba,
        std::uint32_t verticesPerCell = 65);

    /// A digest of everything bakeAlbedoImage would read for this square, without blending anything: the algorithm
    /// version, the sizes, the blend map bytes, the vertex colour bytes, and the layerDigest of every layer index the
    /// blend map uses (each once, in index order). It changes when a used texture, a blend or a vertex colour changes,
    /// and not when an unused layer does. Fails on the same input errors as bakeAlbedoImage.
    /// `usedLayers`, when given, gets the layer indices that went into the digest.
    LodError albedoInputsDigest(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY,
        std::uint32_t sizeInCells, std::uint32_t albedoSize, EdenArchive::Hash& digest,
        std::vector<std::uint32_t>* usedLayers = nullptr, std::uint32_t verticesPerCell = 65);
}

#endif
