#include "lodworld.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "tilebake.hpp"

namespace Lod
{
    namespace
    {
        constexpr std::size_t maxComponentLength = 64;

        // 64-bit FNV-1a over fields written as little-endian numbers and length-prefixed text, so that no two different
        // sets of facts run together ("ab" + "c" against "a" + "bc").
        class Fnv
        {
        public:
            void bytes(const unsigned char* data, std::size_t size)
            {
                for (std::size_t i = 0; i < size; ++i)
                {
                    mState ^= data[i];
                    mState *= 0x100000001b3ull;
                }
            }
            void u64(std::uint64_t value)
            {
                unsigned char b[8];
                for (int i = 0; i < 8; ++i)
                    b[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xff);
                bytes(b, 8);
            }
            void f32(float value)
            {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                u64(bits);
            }
            void text(const std::string& value)
            {
                u64(value.size());
                bytes(reinterpret_cast<const unsigned char*>(value.data()), value.size());
            }
            [[nodiscard]] std::uint64_t value() const { return mState; }

        private:
            std::uint64_t mState = 0xcbf29ce484222325ull;
        };

        std::filesystem::path packPath(const std::filesystem::path& lodRoot, std::string_view gameId,
            std::string_view worldName, const char* fileName)
        {
            if (gameId.empty())
                return lodRoot / fileName;
            return lodRoot / safePathComponent(gameId) / safePathComponent(worldName) / fileName;
        }
    }

    std::string safePathComponent(std::string_view text)
    {
        std::string result;
        for (const char c : text)
        {
            if (result.size() >= maxComponentLength)
                break;
            if (c >= 'A' && c <= 'Z')
                result += static_cast<char>(c - 'A' + 'a');
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                result += c;
            else
                result += '_';
        }
        if (result.empty())
            result = "_";
        return result;
    }

    std::filesystem::path terrainPackPath(
        const std::filesystem::path& lodRoot, std::string_view gameId, std::string_view worldName)
    {
        return packPath(lodRoot, gameId, worldName, "terrain.edenpak");
    }

    std::filesystem::path objectPackPath(
        const std::filesystem::path& lodRoot, std::string_view gameId, std::string_view worldName)
    {
        return packPath(lodRoot, gameId, worldName, "objects.edenpak");
    }

    std::string neutralFingerprint(const NeutralWorldFacts& facts)
    {
        Fnv hash;
        hash.text(facts.mGameId);
        hash.text(facts.mWorldEditorId);
        hash.f32(facts.mCellSizeUnits);
        hash.u64(facts.mVertsPerSide);
        hash.f32(facts.mDefaultLandHeight);
        hash.u64(facts.mCellCount);
        static const char digits[] = "0123456789abcdef";
        std::string text(16, '0');
        std::uint64_t value = hash.value();
        for (int i = 15; i >= 0; --i)
        {
            text[static_cast<std::size_t>(i)] = digits[value & 15];
            value >>= 4;
        }
        return text;
    }

    std::string describeNeutralPackMismatch(const TileManifest& pack, const std::string& worldspaceName,
        std::uint32_t cellSize, std::uint32_t gridSize, std::uint32_t albedoSize)
    {
        if (pack.mLayoutVersion != tileKeyLayoutVersion)
            return "layout version " + std::to_string(pack.mLayoutVersion) + " against "
                + std::to_string(tileKeyLayoutVersion);
        if (pack.mBakerVersion != bakerVersion)
            return "baker version " + std::to_string(pack.mBakerVersion) + " against " + std::to_string(bakerVersion);
        if (pack.mWorldspaces.size() != 1)
            return "the pack holds " + std::to_string(pack.mWorldspaces.size()) + " worldspaces, a neutral pack one";
        if (pack.mWorldspaces[0].mName != worldspaceName)
            return "worldspace \"" + pack.mWorldspaces[0].mName + "\" against \"" + worldspaceName + "\"";
        if (pack.mWorldspaces[0].mCellSize != cellSize)
            return "cell size " + std::to_string(pack.mWorldspaces[0].mCellSize) + " against "
                + std::to_string(cellSize);
        if (pack.mGridSize != gridSize)
            return "grid size " + std::to_string(pack.mGridSize) + " against " + std::to_string(gridSize);
        if (pack.mAlbedoSize != albedoSize)
            return "albedo size " + std::to_string(pack.mAlbedoSize) + " against " + std::to_string(albedoSize);
        return {};
    }

    std::uint32_t albedoSizeForCell(float cellSizeUnits)
    {
        constexpr std::uint32_t defaultSize = 256;
        if (!std::isfinite(cellSizeUnits) || cellSizeUnits <= 0.f || cellSizeUnits > 1.0e8f)
            return defaultSize;
        // 256 texels over Morrowind's 8192 units: 32 units a texel.
        const double texels = static_cast<double>(cellSizeUnits) * defaultSize / 8192.0;
        const std::uint32_t whole = texels >= 1.0 ? static_cast<std::uint32_t>(texels) : 1u;
        return std::clamp(std::bit_floor(whole), 32u, defaultSize);
    }

    LandExtent extentOfTiles(const std::vector<TileId>& tiles)
    {
        LandExtent extent;
        bool any = false;
        for (const TileId& id : tiles)
        {
            if (!packTileKey(id))
                continue;
            std::int32_t cellX = 0;
            std::int32_t cellY = 0;
            tileOriginCell(id, cellX, cellY);
            const std::int64_t span = std::int64_t{ 1 } << id.mLevel;
            const std::int32_t maxX = static_cast<std::int32_t>(std::min<std::int64_t>(cellX + span - 1, INT32_MAX));
            const std::int32_t maxY = static_cast<std::int32_t>(std::min<std::int64_t>(cellY + span - 1, INT32_MAX));
            if (!any)
            {
                extent = { cellX, cellY, maxX, maxY };
                any = true;
                continue;
            }
            extent.mMinX = std::min(extent.mMinX, cellX);
            extent.mMinY = std::min(extent.mMinY, cellY);
            extent.mMaxX = std::max(extent.mMaxX, maxX);
            extent.mMaxY = std::max(extent.mMaxY, maxY);
        }
        return extent;
    }

    std::string neutralWorldspaceName(const NeutralWorldFacts& facts)
    {
        return facts.mGameId + ":" + facts.mWorldEditorId + ":" + neutralFingerprint(facts);
    }

    ClusterRef clusterRefFromPlacement(std::uint64_t placementId) noexcept
    {
        return { static_cast<std::uint32_t>(placementId & 0xFFFFFFFFull), static_cast<std::uint32_t>(placementId >> 32) };
    }

    std::uint64_t placementFromClusterRef(const ClusterRef& ref) noexcept
    {
        return (static_cast<std::uint64_t>(ref.mContentFile) << 32) | ref.mIndex;
    }

    std::vector<std::uint32_t> objectClusterLevels(bool neutralWorld)
    {
        if (neutralWorld)
            return { 2, 3, 4 };
        return { 1, 2, 3, 4 };
    }

    bool terrainPackStored(bool neutralWorld)
    {
        return !neutralWorld;
    }
}
