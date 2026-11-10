#ifndef CSM_MESH_TEXTURECOLOUR_H
#define CSM_MESH_TEXTURECOLOUR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "scenemodel.hpp"

namespace CSMMesh
{
    /// Task 0086, textures in the cell viewer: the mean colour of a shape's diffuse texture, used as the shape's flat
    /// colour in the viewer's solid path (the viewer has no UV-mapped textured picture for a Nif::Scene). Plain data,
    /// no Qt, no GPU. The file bytes are hostile input: every size is checked against the bytes that exist before it
    /// is used, and nothing throws out of these functions.

    /// The mean colour (0..1 per channel, as stored, no gamma conversion) of a .dds file's picture. Reads one mip
    /// level of at most about 1,024 blocks (or, for a file with no small level, a spread of that many blocks of the
    /// top level), weighting texels by alpha where the format has it. Supports BC1 to BC5 and BC7 (legacy fourCCs and
    /// DX10 headers) and 24 and 32 bit uncompressed images with 8 bit channels. Nothing for any other file, for
    /// cube maps, volumes and arrays, and for damaged or truncated data.
    std::optional<std::array<float, 3>> averageColourOfDds(std::span<const unsigned char> file);

    /// The file's bytes by normalised path ("textures/architecture/x_d.dds"); nothing when the virtual file system has
    /// no such file or cannot read it. It may stop reading after `maxBytes + 1` bytes: a result longer than `maxBytes`
    /// is counted as over budget and not used.
    using TextureBytes
        = std::function<std::optional<std::vector<unsigned char>>(const std::string& path, std::uint64_t maxBytes)>;

    struct TextureColourLimits
    {
        /// Distinct textures looked at in one cache; the rest are counted as over budget.
        std::size_t maxTextures = 1024;
        /// One file's size; a longer file is counted as over budget.
        std::uint64_t maxFileBytes = 32ull * 1024 * 1024;
        /// All bytes read by one cache; past it, no further file is read.
        std::uint64_t maxTotalBytes = 768ull * 1024 * 1024;
    };

    struct TextureColourStats
    {
        std::size_t requested = 0; ///< distinct textures asked for
        std::size_t loaded = 0; ///< a colour was computed
        std::size_t missing = 0; ///< not in the virtual file system
        std::size_t unsupported = 0; ///< found, but not a .dds this module can read, or damaged
        std::size_t overBudget = 0; ///< not read: a count, file size or total byte limit
        std::uint64_t bytesRead = 0;
        std::size_t shapesTextured = 0; ///< shapes given a texture colour
        std::size_t shapesFallback = 0; ///< shapes with a diffuse path that kept their flat colour
    };

    /// Reads each distinct texture at most once (names compared without case and with / for \\) and remembers a
    /// failure as well as a colour.
    class TextureColourCache
    {
    public:
        TextureColourCache(TextureBytes bytes, TextureColourLimits limits = TextureColourLimits());

        /// The texture's mean colour, nothing if it has none (see the stats for why).
        std::optional<std::array<float, 3>> colourOf(const std::string& path);

        const TextureColourStats& stats() const { return mStats; }
        TextureColourStats& mutableStats() { return mStats; }

    private:
        TextureBytes mBytes;
        TextureColourLimits mLimits;
        TextureColourStats mStats;
        std::map<std::string, std::optional<std::array<float, 3>>> mColours;
    };

    /// The key two spellings of one path share: lower case, / for \\, and a "textures/" prefix when the path has none.
    std::string normaliseTexturePath(const std::string& path);

    /// Gives each untinted shape with a diffuse path whose texture has a colour that colour and marks it textured;
    /// the others keep their flat colour (and count as fallbacks when they had a path).
    void applyTextureColours(SceneModel& model, TextureColourCache& cache);
}

#endif
