#include "terraintile.hpp"

#include "tilekey.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace Lod
{
    namespace
    {
        // Byte offsets of the header fields (the 88-byte table of the draft layout).
        constexpr std::size_t offMagic = 0;
        constexpr std::size_t offVersion = 4;
        constexpr std::size_t offFlags = 6;
        constexpr std::size_t offGrid = 8;
        constexpr std::size_t offLevel = 12;
        constexpr std::size_t offSide = 16;
        constexpr std::size_t offOriginX = 20;
        constexpr std::size_t offOriginY = 24;
        constexpr std::size_t offMin = 28;
        constexpr std::size_t offMax = 32;
        constexpr std::size_t offError = 36;
        constexpr std::size_t offAlbedoWidth = 40;
        constexpr std::size_t offAlbedoHeight = 42;
        constexpr std::size_t offAlbedoFormat = 44;
        constexpr std::size_t offAlbedoMips = 48;
        constexpr std::size_t offHeights = 52;
        constexpr std::size_t offNormals = 56;
        constexpr std::size_t offAlbedo = 60;
        constexpr std::size_t offReserved = 64;
        constexpr char magic[4] = { 'E', 'T', 'T', '1' };

        LodError fail(const char* field, std::uint64_t offset, const char* message)
        {
            LodError error;
            error.mField = field;
            error.mOffset = offset;
            error.mMessage = message;
            return error;
        }

        std::uint64_t alignUp8(std::uint64_t value)
        {
            return (value + 7) & ~std::uint64_t{ 7 };
        }

        bool isPowerOfTwo(std::uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        std::uint32_t log2Floor(std::uint32_t value)
        {
            std::uint32_t result = 0;
            while (value >>= 1)
                ++result;
            return result;
        }

        void putU16(std::uint8_t* at, std::uint16_t value)
        {
            at[0] = static_cast<std::uint8_t>(value);
            at[1] = static_cast<std::uint8_t>(value >> 8);
        }

        void putU32(std::uint8_t* at, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                at[i] = static_cast<std::uint8_t>(value >> (8 * i));
        }

        void putF32(std::uint8_t* at, float value)
        {
            putU32(at, std::bit_cast<std::uint32_t>(value));
        }

        std::uint16_t getU16(const std::uint8_t* at)
        {
            return static_cast<std::uint16_t>(at[0] | (at[1] << 8));
        }

        std::uint32_t getU32(const std::uint8_t* at)
        {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
                value |= static_cast<std::uint32_t>(at[i]) << (8 * i);
            return value;
        }

        float getF32(const std::uint8_t* at)
        {
            return std::bit_cast<float>(getU32(at));
        }

        // The checks of the fields themselves, shared by the writer and the reader. Offsets are those of the header
        // fields, so both report the same location.
        LodError validateFields(const TerrainTile& tile)
        {
            if (tile.mGridSize < minGridSize || tile.mGridSize > maxGridSize)
                return fail("gridSize", offGrid, "the grid size must be between 3 and 129");
            if (tile.mLevel > maxLevel)
                return fail("level", offLevel, "the level must be between 0 and 15");
            if (!std::isfinite(tile.mSide) || tile.mSide < 0)
                return fail("side", offSide, "the tile side must be finite and not negative");
            if (!std::isfinite(tile.mOriginX))
                return fail("originX", offOriginX, "the origin must be finite");
            if (!std::isfinite(tile.mOriginY))
                return fail("originY", offOriginY, "the origin must be finite");
            if (!std::isfinite(tile.mMinHeight))
                return fail("minHeight", offMin, "the minimum height must be finite");
            if (!std::isfinite(tile.mMaxHeight))
                return fail("maxHeight", offMax, "the maximum height must be finite");
            if (tile.mMinHeight > tile.mMaxHeight)
                return fail("minHeight", offMin, "the minimum height is above the maximum");
            if (!std::isfinite(tile.mGeometricError) || tile.mGeometricError < 0)
                return fail("geometricError", offError, "the error must be finite and not negative");
            if (tile.mHasAlbedo)
            {
                if (!isPowerOfTwo(tile.mAlbedoWidth) || tile.mAlbedoWidth > maxAlbedoSide)
                    return fail("albedoWidth", offAlbedoWidth, "the albedo width must be a power of two up to 2048");
                if (!isPowerOfTwo(tile.mAlbedoHeight) || tile.mAlbedoHeight > maxAlbedoSide)
                    return fail("albedoHeight", offAlbedoHeight, "the albedo height must be a power of two up to 2048");
                if (static_cast<std::uint32_t>(tile.mAlbedoFormat) > static_cast<std::uint32_t>(AlbedoFormat::Bc1))
                    return fail("albedoFormat", offAlbedoFormat, "unknown albedo format");
                if (albedoByteSize(tile.mAlbedoWidth, tile.mAlbedoHeight, tile.mAlbedoFormat, tile.mAlbedoMipCount)
                    == 0)
                    return fail("albedoMipCount", offAlbedoMips, "the mip count does not fit the picture size");
            }
            else
            {
                if (tile.mAlbedoWidth != 0)
                    return fail("albedoWidth", offAlbedoWidth, "no albedo, so the width must be 0");
                if (tile.mAlbedoHeight != 0)
                    return fail("albedoHeight", offAlbedoHeight, "no albedo, so the height must be 0");
                if (tile.mAlbedoFormat != AlbedoFormat::Rgba8)
                    return fail("albedoFormat", offAlbedoFormat, "no albedo, so the format must be 0");
                if (tile.mAlbedoMipCount != 0)
                    return fail("albedoMipCount", offAlbedoMips, "no albedo, so the mip count must be 0");
            }
            return {};
        }

        TerrainTileLayout layoutOf(const TerrainTile& tile)
        {
            TerrainTileLayout layout;
            const std::uint64_t n = tile.mGridSize;
            std::uint64_t cursor = terrainTileHeaderSize;
            layout.mHeightsOffset = cursor;
            layout.mHeightsSize = n * n * 2;
            cursor = alignUp8(cursor + layout.mHeightsSize);
            if (tile.mHasNormals)
            {
                layout.mNormalsOffset = cursor;
                layout.mNormalsSize = n * n * 2;
                cursor = alignUp8(cursor + layout.mNormalsSize);
            }
            if (tile.mHasAlbedo)
            {
                layout.mAlbedoOffset = cursor;
                layout.mAlbedoSize
                    = albedoByteSize(tile.mAlbedoWidth, tile.mAlbedoHeight, tile.mAlbedoFormat, tile.mAlbedoMipCount);
                cursor = alignUp8(cursor + layout.mAlbedoSize);
            }
            layout.mTotalSize = cursor;
            return layout;
        }
    }

    std::uint64_t albedoByteSize(
        std::uint32_t width, std::uint32_t height, AlbedoFormat format, std::uint32_t mipCount) noexcept
    {
        if (!isPowerOfTwo(width) || !isPowerOfTwo(height) || width > maxAlbedoSide || height > maxAlbedoSide)
            return 0;
        if (format != AlbedoFormat::Rgba8 && format != AlbedoFormat::Bc1)
            return 0;
        const std::uint32_t longest = width > height ? width : height;
        if (mipCount == 0 || mipCount > log2Floor(longest) + 1)
            return 0;
        std::uint64_t total = 0;
        for (std::uint32_t m = 0; m < mipCount; ++m)
        {
            const std::uint64_t mw = (width >> m) > 0 ? (width >> m) : 1;
            const std::uint64_t mh = (height >> m) > 0 ? (height >> m) : 1;
            if (format == AlbedoFormat::Rgba8)
                total += mw * mh * 4;
            else
                total += ((mw + 3) / 4) * ((mh + 3) / 4) * 8;
        }
        return total;
    }

    LodError computeTerrainTileLayout(const TerrainTile& tile, TerrainTileLayout& layout)
    {
        layout = {};
        if (LodError error = validateFields(tile))
            return error;
        layout = layoutOf(tile);
        return {};
    }

    LodError writeTerrainTile(const TerrainTile& tile, std::vector<std::uint8_t>& out)
    {
        out.clear();
        if (LodError error = validateFields(tile))
            return error;
        const std::uint64_t n = tile.mGridSize;
        if (tile.mHeights.size() != n * n)
            return fail("heights", terrainTileHeaderSize, "the height array must hold gridSize * gridSize values");
        if (tile.mHasNormals ? tile.mNormals.size() != n * n * 2 : !tile.mNormals.empty())
            return fail("normals", terrainTileHeaderSize, "the normal array does not match the flags and grid size");
        const TerrainTileLayout layout = layoutOf(tile);
        if (tile.mHasAlbedo ? tile.mAlbedo.size() != layout.mAlbedoSize : !tile.mAlbedo.empty())
            return fail("albedo", terrainTileHeaderSize, "the albedo bytes do not match the flags and size");

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(layout.mTotalSize), 0);
        std::uint8_t* base = bytes.data();
        std::memcpy(base + offMagic, magic, 4);
        putU16(base + offVersion, terrainTilePayloadVersion);
        const std::uint16_t flags = static_cast<std::uint16_t>(
            (tile.mHasNormals ? flagHasNormals : 0) | (tile.mHasAlbedo ? flagHasAlbedo : 0));
        putU16(base + offFlags, flags);
        putU32(base + offGrid, tile.mGridSize);
        putU32(base + offLevel, tile.mLevel);
        putF32(base + offSide, tile.mSide);
        putF32(base + offOriginX, tile.mOriginX);
        putF32(base + offOriginY, tile.mOriginY);
        putF32(base + offMin, tile.mMinHeight);
        putF32(base + offMax, tile.mMaxHeight);
        putF32(base + offError, tile.mGeometricError);
        putU16(base + offAlbedoWidth, static_cast<std::uint16_t>(tile.mAlbedoWidth));
        putU16(base + offAlbedoHeight, static_cast<std::uint16_t>(tile.mAlbedoHeight));
        putU32(base + offAlbedoFormat, static_cast<std::uint32_t>(tile.mAlbedoFormat));
        putU32(base + offAlbedoMips, tile.mAlbedoMipCount);
        putU32(base + offHeights, static_cast<std::uint32_t>(layout.mHeightsOffset));
        putU32(base + offNormals, static_cast<std::uint32_t>(layout.mNormalsOffset));
        putU32(base + offAlbedo, static_cast<std::uint32_t>(layout.mAlbedoOffset));

        std::uint8_t* heights = base + layout.mHeightsOffset;
        for (std::size_t i = 0; i < tile.mHeights.size(); ++i)
            putU16(heights + 2 * i, tile.mHeights[i]);
        if (tile.mHasNormals)
            std::memcpy(base + layout.mNormalsOffset, tile.mNormals.data(), tile.mNormals.size());
        if (tile.mHasAlbedo)
            std::memcpy(base + layout.mAlbedoOffset, tile.mAlbedo.data(), tile.mAlbedo.size());
        out = std::move(bytes);
        return {};
    }

    LodError parseTerrainTileHeader(
        std::span<const std::uint8_t> header, std::uint64_t entrySize, TerrainTile& tile, TerrainTileLayout& layout)
    {
        tile = {};
        layout = {};
        if (header.size() < terrainTileHeaderSize || entrySize < terrainTileHeaderSize)
            return fail("header", 0, "the payload is shorter than the 88-byte header");
        const std::uint8_t* h = header.data();
        if (std::memcmp(h + offMagic, magic, 4) != 0)
            return fail("magic", offMagic, "the magic is not ETT1");
        if (getU16(h + offVersion) != terrainTilePayloadVersion)
            return fail("version", offVersion, "unsupported payload version (only the draft version 0 is read)");
        const std::uint16_t flags = getU16(h + offFlags);
        if ((flags & ~(flagHasNormals | flagHasAlbedo)) != 0)
            return fail("flags", offFlags, "unknown or unsupported flag bits");

        TerrainTile parsed;
        parsed.mHasNormals = (flags & flagHasNormals) != 0;
        parsed.mHasAlbedo = (flags & flagHasAlbedo) != 0;
        parsed.mGridSize = getU32(h + offGrid);
        parsed.mLevel = getU32(h + offLevel);
        parsed.mSide = getF32(h + offSide);
        parsed.mOriginX = getF32(h + offOriginX);
        parsed.mOriginY = getF32(h + offOriginY);
        parsed.mMinHeight = getF32(h + offMin);
        parsed.mMaxHeight = getF32(h + offMax);
        parsed.mGeometricError = getF32(h + offError);
        parsed.mAlbedoWidth = getU16(h + offAlbedoWidth);
        parsed.mAlbedoHeight = getU16(h + offAlbedoHeight);
        const std::uint32_t format = getU32(h + offAlbedoFormat);
        parsed.mAlbedoMipCount = getU32(h + offAlbedoMips);
        // An unknown format value is kept out of the enum: validateFields sees 2 and refuses it.
        parsed.mAlbedoFormat = format <= 1 ? static_cast<AlbedoFormat>(format) : static_cast<AlbedoFormat>(2);
        if (!parsed.mHasAlbedo && format != 0)
            return fail("albedoFormat", offAlbedoFormat, "no albedo, so the format must be 0");
        if (LodError error = validateFields(parsed))
            return error;

        for (std::size_t i = offReserved; i < terrainTileHeaderSize; ++i)
        {
            if (h[i] != 0)
                return fail("reserved", i, "reserved bytes must be zero");
        }

        const TerrainTileLayout expected = layoutOf(parsed);
        if (getU32(h + offHeights) != expected.mHeightsOffset)
            return fail("heightsOffset", offHeights, "the heights section is not where the layout puts it");
        if (getU32(h + offNormals) != expected.mNormalsOffset)
            return fail("normalsOffset", offNormals, "the normals section is not where the layout puts it");
        if (getU32(h + offAlbedo) != expected.mAlbedoOffset)
            return fail("albedoOffset", offAlbedo, "the albedo section is not where the layout puts it");
        if (expected.mTotalSize != entrySize)
            return fail("size", 0, "the section sizes do not add up to the size of the entry");

        tile = parsed;
        layout = expected;
        return {};
    }

    LodError parseTerrainTile(std::span<const std::uint8_t> bytes, TerrainTile& tile)
    {
        TerrainTile parsed;
        TerrainTileLayout layout;
        if (LodError error = parseTerrainTileHeader(bytes, bytes.size(), parsed, layout))
        {
            tile = {};
            return error;
        }
        // Padding between and after the sections must be zero.
        const std::uint64_t starts[3] = { layout.mHeightsOffset, layout.mNormalsOffset, layout.mAlbedoOffset };
        const std::uint64_t sizes[3] = { layout.mHeightsSize, layout.mNormalsSize, layout.mAlbedoSize };
        std::uint64_t cursor = terrainTileHeaderSize;
        for (int s = 0; s < 3; ++s)
        {
            if (sizes[s] == 0)
                continue;
            for (std::uint64_t i = cursor; i < starts[s]; ++i)
            {
                if (bytes[i] != 0)
                    return fail("padding", i, "padding bytes must be zero");
            }
            cursor = starts[s] + sizes[s];
        }
        for (std::uint64_t i = cursor; i < layout.mTotalSize; ++i)
        {
            if (bytes[i] != 0)
                return fail("padding", i, "padding bytes must be zero");
        }

        const std::uint64_t n = parsed.mGridSize;
        parsed.mHeights.resize(static_cast<std::size_t>(n * n));
        for (std::size_t i = 0; i < parsed.mHeights.size(); ++i)
            parsed.mHeights[i] = getU16(bytes.data() + layout.mHeightsOffset + 2 * i);
        if (parsed.mHasNormals)
        {
            parsed.mNormals.resize(static_cast<std::size_t>(layout.mNormalsSize));
            std::memcpy(parsed.mNormals.data(), bytes.data() + layout.mNormalsOffset, parsed.mNormals.size());
        }
        if (parsed.mHasAlbedo)
        {
            parsed.mAlbedo.resize(static_cast<std::size_t>(layout.mAlbedoSize));
            std::memcpy(parsed.mAlbedo.data(), bytes.data() + layout.mAlbedoOffset, parsed.mAlbedo.size());
        }
        tile = std::move(parsed);
        return {};
    }
}
