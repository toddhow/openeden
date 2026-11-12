#include "tilebake.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Lod
{
    namespace
    {
        LodError fail(const char* field, const char* message)
        {
            LodError error;
            error.mField = field;
            error.mMessage = message;
            return error;
        }

        float signOf(float value)
        {
            return value < 0.f ? -1.f : 1.f;
        }

        void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        void hashU32(EdenArchive::Hasher& hasher, std::uint32_t value)
        {
            std::uint8_t bytes[4];
            for (int i = 0; i < 4; ++i)
                bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
            hasher.update(bytes, 4);
        }

        // Hashes floats as their bit patterns, so the digest does not depend on how a float prints.
        void hashFloats(EdenArchive::Hasher& hasher, const std::vector<float>& values)
        {
            for (const float value : values)
            {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                hashU32(hasher, bits);
            }
        }

        // Hashes the full-detail normals (x, y, z a sample, `n` x `n` samples) without those at cell corners. The
        // engine sets a corner's normal to the normalised sum of its four neighbours (Terrain::Storage::averageNormal),
        // a float computation whose last bit depends on whether the compiler fuses multiply-adds: the Windows and the
        // Linux bake of Morrowind's land differ there in the first differing tile and in about a third of the level 1
        // tiles, with equal heights and equal other normals (the stored normal of the corner itself is ignored). The
        // corner is not an input: its neighbours are hashed (with the tile that holds them), and everything else is
        // exact.
        void hashNormalsWithoutCorners(EdenArchive::Hasher& hasher, const std::vector<float>& normals, std::uint32_t n,
            std::uint32_t verticesPerCell)
        {
            const std::uint32_t cornerSpacing = verticesPerCell - 1;
            for (std::uint32_t j = 0; j < n; ++j)
            {
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    if (i % cornerSpacing == 0 && j % cornerSpacing == 0)
                        continue;
                    const std::size_t base = 3 * (std::size_t{ j } * n + i);
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        std::uint32_t bits = 0;
                        std::memcpy(&bits, &normals[base + k], sizeof(bits));
                        hashU32(hasher, bits);
                    }
                }
            }
        }

        bool allFinite(const std::vector<float>& values)
        {
            return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
        }

        std::uint32_t log2Exact(std::uint32_t value)
        {
            std::uint32_t result = 0;
            while (value >>= 1)
                ++result;
            return result;
        }
    }

    void encodeNormal(float x, float y, float z, std::int8_t& u, std::int8_t& v) noexcept
    {
        const float length = std::fabs(x) + std::fabs(y) + std::fabs(z);
        if (!(length > 0.f) || !std::isfinite(length))
        {
            u = 0;
            v = 0;
            return;
        }
        float px = x / length;
        float py = y / length;
        if (z < 0.f)
        {
            const float fx = (1.f - std::fabs(py)) * signOf(px);
            const float fy = (1.f - std::fabs(px)) * signOf(py);
            px = fx;
            py = fy;
        }
        u = static_cast<std::int8_t>(std::lround(std::clamp(px, -1.f, 1.f) * 127.f));
        v = static_cast<std::int8_t>(std::lround(std::clamp(py, -1.f, 1.f) * 127.f));
    }

    void decodeNormal(std::int8_t u, std::int8_t v, float& x, float& y, float& z) noexcept
    {
        float px = std::clamp(static_cast<float>(u) / 127.f, -1.f, 1.f);
        float py = std::clamp(static_cast<float>(v) / 127.f, -1.f, 1.f);
        z = 1.f - std::fabs(px) - std::fabs(py);
        if (z < 0.f)
        {
            const float fx = (1.f - std::fabs(py)) * signOf(px);
            const float fy = (1.f - std::fabs(px)) * signOf(py);
            px = fx;
            py = fy;
        }
        x = px;
        y = py;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length > 0.f)
        {
            x /= length;
            y /= length;
            z /= length;
        }
        else
        {
            x = 0.f;
            y = 0.f;
            z = 1.f;
        }
    }

    bool vertexStepLod(const TileBakeSettings& settings, std::uint32_t& lod) noexcept
    {
        if (settings.mLevel > maxLevel || settings.mGridSize < minGridSize || settings.mGridSize > maxGridSize)
            return false;
        // A cell has a power-of-two number of intervals, at least 2 (task 0130: 64 for Morrowind, 32 for Skyrim SE).
        if (settings.mVerticesPerCell < 3 || settings.mVerticesPerCell > 1025
            || ((settings.mVerticesPerCell - 1) & (settings.mVerticesPerCell - 2)) != 0)
            return false;
        const std::uint64_t intervals = std::uint64_t{ settings.mVerticesPerCell - 1 } << settings.mLevel;
        const std::uint64_t wanted = settings.mGridSize - 1;
        if (intervals % wanted != 0)
            return false;
        const std::uint64_t step = intervals / wanted;
        if ((step & (step - 1)) != 0)
            return false;
        lod = log2Exact(static_cast<std::uint32_t>(step));
        return true;
    }

    LodError bakeTerrainTile(
        const LandSource& source, const TileId& tileId, const TileBakeSettings& settings, BakedTile& tile)
    {
        // A copy: the caller may pass tile.mId itself, which the reset below would clear.
        const TileId id = tileId;
        tile = {};
        std::uint32_t lod = 0;
        if (!vertexStepLod(settings, lod))
            return fail("settings", "the grid size does not divide the tile into a power-of-two vertex step");
        if (settings.mCellSize == 0)
            return fail("settings", "the cell size must be above 0");
        if (id.mLevel != settings.mLevel || !packTileKey(id))
            return fail("tileId", "the tile id is invalid or not at the level being baked");

        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
        tileOriginCell(id, cellX, cellY);
        const std::uint32_t cells = 1u << settings.mLevel;
        const std::uint32_t n = settings.mGridSize;
        const std::uint32_t fineN = (settings.mVerticesPerCell - 1) * cells + 1;
        const std::uint32_t step = 1u << lod;

        std::vector<float> fineHeights;
        std::vector<float> fineNormals;
        source.sample(cellX, cellY, cells, 0, fineHeights, fineNormals);
        std::vector<float> coarseHeights;
        std::vector<float> coarseNormals;
        source.sample(cellX, cellY, cells, lod, coarseHeights, coarseNormals);
        if (fineHeights.size() != std::size_t{ fineN } * fineN || fineNormals.size() != fineHeights.size() * 3
            || coarseHeights.size() != std::size_t{ n } * n || coarseNormals.size() != coarseHeights.size() * 3)
            return fail("source", "the land source returned arrays of the wrong size");
        if (!allFinite(fineHeights) || !allFinite(fineNormals) || !allFinite(coarseHeights)
            || !allFinite(coarseNormals))
            return fail("source", "the land source returned a number that is not finite");

        TerrainTile data;
        data.mGridSize = n;
        data.mLevel = settings.mLevel;
        data.mSide = static_cast<float>(cells) * static_cast<float>(settings.mCellSize);
        data.mOriginX = static_cast<float>(cellX) * static_cast<float>(settings.mCellSize);
        data.mOriginY = static_cast<float>(cellY) * static_cast<float>(settings.mCellSize);
        const auto [lowest, highest] = std::minmax_element(coarseHeights.begin(), coarseHeights.end());
        data.mMinHeight = *lowest;
        data.mMaxHeight = *highest;

        // Quantise between the tile's minimum and maximum, and read the values back the way a user of the tile will.
        const double range = static_cast<double>(data.mMaxHeight) - static_cast<double>(data.mMinHeight);
        data.mHeights.resize(coarseHeights.size());
        std::vector<double> decoded(coarseHeights.size());
        for (std::size_t i = 0; i < coarseHeights.size(); ++i)
        {
            std::uint16_t quantised = 0;
            if (range > 0.0)
            {
                const double scaled = (static_cast<double>(coarseHeights[i]) - data.mMinHeight) / range * 65535.0;
                quantised = static_cast<std::uint16_t>(std::clamp(std::llround(scaled), 0ll, 65535ll));
            }
            data.mHeights[i] = quantised;
            decoded[i] = static_cast<double>(data.mMinHeight) + static_cast<double>(quantised) / 65535.0 * range;
        }

        // The error: the biggest gap between the full-detail height and the bilinear surface of the coarse grid.
        double error = 0.0;
        for (std::uint32_t fy = 0; fy < fineN; ++fy)
        {
            const std::uint32_t cy = std::min(fy / step, n - 2);
            const double ty = static_cast<double>(fy) / step - cy;
            for (std::uint32_t fx = 0; fx < fineN; ++fx)
            {
                const std::uint32_t cx = std::min(fx / step, n - 2);
                const double tx = static_cast<double>(fx) / step - cx;
                const double h00 = decoded[std::size_t{ cy } * n + cx];
                const double h10 = decoded[std::size_t{ cy } * n + cx + 1];
                const double h01 = decoded[std::size_t{ cy + 1 } * n + cx];
                const double h11 = decoded[std::size_t{ cy + 1 } * n + cx + 1];
                const double interpolated = (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty;
                error = std::max(error, std::fabs(interpolated - fineHeights[std::size_t{ fy } * fineN + fx]));
            }
        }
        data.mGeometricError = static_cast<float>(error);

        data.mHasNormals = true;
        data.mNormals.resize(coarseHeights.size() * 2);
        for (std::size_t i = 0; i < coarseHeights.size(); ++i)
        {
            std::int8_t u = 0;
            std::int8_t v = 0;
            encodeNormal(coarseNormals[3 * i], coarseNormals[3 * i + 1], coarseNormals[3 * i + 2], u, v);
            data.mNormals[2 * i] = u;
            data.mNormals[2 * i + 1] = v;
        }
        data.mHasAlbedo = false;

        EdenArchive::Hasher hasher;
        hashU32(hasher, settings.mWorldspace);
        hashU32(hasher, id.mLevel);
        hashU32(hasher, id.mX);
        hashU32(hasher, id.mY);
        hashU32(hasher, settings.mGridSize);
        hashU32(hasher, settings.mCellSize);
        // Only a cell that is not Morrowind's adds to the digest, so Morrowind's packs keep theirs (task 0130).
        if (settings.mVerticesPerCell != landVerticesPerCell)
            hashU32(hasher, settings.mVerticesPerCell);
        hashU32(hasher, bakerVersion);
        hashFloats(hasher, fineHeights);
        hashNormalsWithoutCorners(hasher, fineNormals, fineN, settings.mVerticesPerCell);

        // The parts, for the message of a stale pack (the same bytes, hashed on their own).
        EdenArchive::Hasher heightsHasher;
        hashFloats(heightsHasher, fineHeights);
        EdenArchive::Hasher normalsHasher;
        hashNormalsWithoutCorners(normalsHasher, fineNormals, fineN, settings.mVerticesPerCell);

        tile.mId = id;
        tile.mData = std::move(data);
        tile.mHeightsDigest = heightsHasher.finalize();
        tile.mNormalsDigest = normalsHasher.finalize();
        tile.mSourceDigest = hasher.finalize();
        appendU32(tile.mParameters, bakerVersion);
        appendU32(tile.mParameters, settings.mLevel);
        appendU32(tile.mParameters, settings.mGridSize);
        appendU32(tile.mParameters, settings.mCellSize);
        appendU32(tile.mParameters, settings.mWorldspace);
        if (settings.mVerticesPerCell != landVerticesPerCell)
            appendU32(tile.mParameters, settings.mVerticesPerCell);
        return {};
    }

    LodError bakeTerrainTile(const LandSource& source, const AlbedoSource* albedo, const TileId& tileId,
        const TileBakeSettings& settings, BakedTile& tile)
    {
        const TileId id = tileId;
        if (LodError error = bakeTerrainTile(source, id, settings, tile))
            return error;
        if (albedo == nullptr || settings.mAlbedoSize == 0)
            return {};

        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
        tileOriginCell(id, cellX, cellY);
        const std::uint32_t cells = 1u << settings.mLevel;
        EdenArchive::Hash inputs;
        if (LodError error
            = albedoInputsDigest(*albedo, cellX, cellY, cells, settings.mAlbedoSize, inputs, &tile.mUsedLayers,
                settings.mVerticesPerCell))
        {
            tile = {};
            return error;
        }
        tile.mAlbedoInputsDigest = inputs;
        // The tile's digest now also answers for the ground colour.
        EdenArchive::Hasher hasher;
        hasher.update(tile.mSourceDigest.data(), tile.mSourceDigest.size());
        hasher.update(inputs.data(), inputs.size());
        tile.mSourceDigest = hasher.finalize();
        appendU32(tile.mParameters, settings.mAlbedoSize);
        appendU32(tile.mParameters, albedoAlgorithmVersion);

        if (settings.mDigestOnly)
            return {};
        std::vector<std::uint8_t> rgba;
        if (LodError error = bakeAlbedoImage(*albedo, cellX, cellY, cells, settings.mAlbedoSize, rgba, settings.mVerticesPerCell))
        {
            tile = {};
            return error;
        }
        tile.mData.mHasAlbedo = true;
        tile.mData.mAlbedoWidth = settings.mAlbedoSize;
        tile.mData.mAlbedoHeight = settings.mAlbedoSize;
        tile.mData.mAlbedoFormat = AlbedoFormat::Rgba8;
        tile.mData.mAlbedoMipCount = 1;
        tile.mData.mAlbedo = std::move(rgba);
        return {};
    }

    LodError bakeTerrainLevels(const LandSource& source, const AlbedoSource* albedo,
        const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const TileBakeSettings& base,
        const TerrainBakePlan& plan, const std::string& worldspaceName, TileManifest& manifest,
        std::vector<BakedTile>& tiles)
    {
        manifest = {};
        tiles.clear();
        if (base.mCellSize == 0)
            return fail("settings", "the cell size must be above 0");
        for (const std::uint32_t level : plan.mLevels)
        {
            TileBakeSettings settings = base;
            settings.mLevel = level;
            std::uint32_t lod = 0;
            if (!vertexStepLod(settings, lod))
                return fail("plan", "a level of the plan does not fit the grid size");
        }
        for (const std::uint32_t level : plan.mLevels)
        {
            TileBakeSettings settings = base;
            settings.mLevel = level;
            settings.mAlbedoSize = (albedo != nullptr && level >= plan.mAlbedoFromLevel) ? plan.mAlbedoSize : 0;
            for (const TileId& id : tilesCoveringCells(landCells, settings))
            {
                BakedTile tile;
                if (LodError error = bakeTerrainTile(source, albedo, id, settings, tile))
                {
                    tiles.clear();
                    return error;
                }
                tiles.push_back(std::move(tile));
            }
        }
        std::sort(tiles.begin(), tiles.end(), [](const BakedTile& lhs, const BakedTile& rhs) {
            return packTileKey(lhs.mId).value_or(0) < packTileKey(rhs.mId).value_or(0);
        });
        manifest = makeTerrainManifest(base, plan, albedo != nullptr, worldspaceName);
        return {};
    }

    TileManifest makeTerrainManifest(
        const TileBakeSettings& base, const TerrainBakePlan& plan, bool withAlbedo, const std::string& worldspaceName)
    {
        TileManifest manifest;
        manifest.mLayoutVersion = tileKeyLayoutVersion;
        manifest.mBakerVersion = bakerVersion;
        manifest.mWorldspaces.push_back(WorldspaceInfo{ base.mWorldspace, worldspaceName, base.mCellSize });
        manifest.mLevels = plan.mLevels;
        manifest.mGridSize = base.mGridSize;
        manifest.mAlbedoSize = withAlbedo ? plan.mAlbedoSize : 0;
        return manifest;
    }

    std::vector<TileId> tilesCoveringCells(
        const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const TileBakeSettings& settings)
    {
        std::vector<std::pair<std::uint64_t, TileId>> keyed;
        for (const auto& [cellX, cellY] : landCells)
        {
            const std::optional<TileId> id = tileContainingCell(settings.mWorldspace, settings.mLevel, cellX, cellY);
            if (!id)
                continue;
            const std::optional<std::uint64_t> key = packTileKey(*id);
            if (key)
                keyed.emplace_back(*key, *id);
        }
        std::sort(keyed.begin(), keyed.end(),
            [](const std::pair<std::uint64_t, TileId>& lhs, const std::pair<std::uint64_t, TileId>& rhs) {
                return lhs.first < rhs.first;
            });
        keyed.erase(std::unique(keyed.begin(), keyed.end(),
                        [](const std::pair<std::uint64_t, TileId>& lhs, const std::pair<std::uint64_t, TileId>& rhs) {
                            return lhs.first == rhs.first;
                        }),
            keyed.end());
        std::vector<TileId> ids;
        ids.reserve(keyed.size());
        for (const auto& entry : keyed)
            ids.push_back(entry.second);
        return ids;
    }

    LodError bakeTerrain(const LandSource& source, const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells,
        const TileBakeSettings& settings, const std::string& worldspaceName, TileManifest& manifest,
        std::vector<BakedTile>& tiles)
    {
        manifest = {};
        tiles.clear();
        std::uint32_t lod = 0;
        if (!vertexStepLod(settings, lod) || settings.mCellSize == 0)
            return fail("settings", "the bake settings are not valid");
        for (const TileId& id : tilesCoveringCells(landCells, settings))
        {
            BakedTile tile;
            if (LodError error = bakeTerrainTile(source, id, settings, tile))
            {
                tiles.clear();
                return error;
            }
            tiles.push_back(std::move(tile));
        }
        manifest.mLayoutVersion = tileKeyLayoutVersion;
        manifest.mBakerVersion = bakerVersion;
        manifest.mWorldspaces.push_back(WorldspaceInfo{ settings.mWorldspace, worldspaceName, settings.mCellSize });
        manifest.mLevels.push_back(settings.mLevel);
        manifest.mGridSize = settings.mGridSize;
        manifest.mAlbedoSize = 0;
        return {};
    }

    EdenArchive::Hash contentDigest(const TileManifest& manifest, const std::vector<BakedTile>& tiles)
    {
        std::vector<std::pair<std::uint64_t, const BakedTile*>> ordered;
        ordered.reserve(tiles.size());
        for (const BakedTile& tile : tiles)
            ordered.emplace_back(packTileKey(tile.mId).value_or(0), &tile);
        std::sort(ordered.begin(), ordered.end(),
            [](const std::pair<std::uint64_t, const BakedTile*>& lhs,
                const std::pair<std::uint64_t, const BakedTile*>& rhs) { return lhs.first < rhs.first; });
        EdenArchive::Hasher hasher;
        const std::string json = writeManifestJson(manifest);
        hasher.update(json.data(), json.size());
        for (const auto& entry : ordered)
            hasher.update(entry.second->mSourceDigest.data(), entry.second->mSourceDigest.size());
        return hasher.finalize();
    }
}
