#include "cellcache.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace NeutralTerrain
{
    namespace sdk = openeden::sdk::game;

    namespace
    {
        std::string where(const sdk::TerrainCell& cell)
        {
            return "cell (" + std::to_string(cell.gridX) + ", " + std::to_string(cell.gridY) + ")";
        }
    }

    bool validateTerrainCell(const sdk::TerrainCell& cell, std::uint32_t expectedVerts, const CacheLimits& limits,
        std::string& error)
    {
        const auto fail = [&](const std::string& why) {
            error = where(cell) + ": " + why;
            return false;
        };
        if (cell.vertsPerSide != expectedVerts)
            return fail("it has " + std::to_string(cell.vertsPerSide) + " vertices per side, the world says "
                + std::to_string(expectedVerts));
        const std::size_t count = static_cast<std::size_t>(expectedVerts) * expectedVerts;
        if (cell.hasHeights)
        {
            if (cell.heights.size() != count)
                return fail("heights has " + std::to_string(cell.heights.size()) + " values, expected "
                    + std::to_string(count));
            for (float h : cell.heights)
                if (!std::isfinite(h) || std::fabs(h) > 1.0e7f)
                    return fail("a height is not finite or is beyond 1e7 units");
        }
        if (cell.hasNormals && cell.normals.size() != count * 3)
            return fail("normals has " + std::to_string(cell.normals.size()) + " values, expected "
                + std::to_string(count * 3));
        if (cell.hasColours && cell.colours.size() != count * 3)
            return fail("colours has " + std::to_string(cell.colours.size()) + " values, expected "
                + std::to_string(count * 3));
        for (std::size_t q = 0; q < cell.layers.size(); ++q)
        {
            if (cell.layers[q].size() > limits.mMaxLayersPerQuadrant)
                return fail("quadrant " + std::to_string(q) + " has " + std::to_string(cell.layers[q].size())
                    + " alpha layers, over the limit of " + std::to_string(limits.mMaxLayersPerQuadrant));
            for (const sdk::TerrainLayer& layer : cell.layers[q])
                if (layer.opacity.size() != static_cast<std::size_t>(sdk::kTerrainLayerSide) * sdk::kTerrainLayerSide)
                    return fail("a layer of quadrant " + std::to_string(q) + " has " + std::to_string(layer.opacity.size())
                        + " opacity values, expected 289");
        }
        return true;
    }

    CellCache::CellCache(sdk::WorldCellSource& source, CacheLimits limits)
        : mSource(source)
        , mLimits(limits)
    {
    }

    bool CellCache::setWorld(sdk::WorldId world, std::string& error)
    {
        mHasWorld = false;
        mInfos.clear();
        mTextures.clear();
        mTextureIndex.clear();
        // The chain, stopping at a cycle or the depth limit.
        std::set<sdk::WorldId> seen;
        sdk::WorldId id = world;
        for (std::uint32_t depth = 0; id != sdk::kNoWorldId && depth <= mLimits.mMaxParentDepth; ++depth)
        {
            if (!seen.insert(id).second)
                break;
            sdk::WorldSpaceInfo info;
            if (!mSource.worldInfo(id, info))
            {
                if (depth == 0)
                {
                    error = "the source has no world " + std::to_string(world) + " (or no terrain)";
                    return false;
                }
                break;
            }
            mInfos[id] = info;
            id = (info.parentUsage & sdk::TerrainParentUsage::Land) != 0 ? info.parent : sdk::kNoWorldId;
        }
        mWorld = mInfos.at(world);
        if (mWorld.vertsPerSide < 2 || mWorld.vertsPerSide > mLimits.mMaxVertsPerSide
            || !(mWorld.cellSizeUnits > 0.f) || !std::isfinite(mWorld.cellSizeUnits))
        {
            error = "world " + std::to_string(world) + " describes a cell of " + std::to_string(mWorld.vertsPerSide)
                + " vertices and " + std::to_string(mWorld.cellSizeUnits) + " units";
            return false;
        }
        std::vector<sdk::GroundTexture> listed;
        mSource.listGroundTextures(listed);
        if (listed.size() > mLimits.mMaxTextures)
            listed.resize(mLimits.mMaxTextures);
        for (const sdk::GroundTexture& t : listed)
            if (t.id == 0)
            {
                mTextures.push_back(t);
                break;
            }
        if (mTextures.empty())
        {
            error = "the source lists no default ground texture (id 0)";
            return false;
        }
        for (const sdk::GroundTexture& t : listed)
            if (t.id != 0 && mTextureIndex.find(t.id) == mTextureIndex.end())
            {
                mTextureIndex[t.id] = mTextures.size();
                mTextures.push_back(t);
            }
        mHasWorld = true;
        return true;
    }

    std::size_t CellCache::textureIndex(sdk::WorldId id) const
    {
        const auto found = mTextureIndex.find(id);
        return found == mTextureIndex.end() ? 0 : found->second;
    }

    void CellCache::noteError(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mErrors.size() < mLimits.mMaxErrors)
            mErrors.push_back(message);
    }

    void CellCache::publish(const Key& key, std::shared_ptr<const sdk::TerrainCell> cell)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mEntries.find(key) == mEntries.end())
            mOrder.push_back(key);
        mEntries[key] = Entry{ std::move(cell) };
        while (mEntries.size() > mLimits.mMaxCells && !mOrder.empty())
        {
            const Key oldest = mOrder.front();
            mOrder.pop_front();
            if (oldest != key)
                mEntries.erase(oldest);
        }
    }

    bool CellCache::ensureCell(std::int32_t x, std::int32_t y)
    {
        if (!mHasWorld)
            return false;
        const Key key{ x, y };
        if (isKnown(x, y))
            return true;

        std::set<sdk::WorldId> seen;
        sdk::WorldId id = mWorld.id;
        std::shared_ptr<const sdk::TerrainCell> found;
        for (std::uint32_t depth = 0; id != sdk::kNoWorldId && depth <= mLimits.mMaxParentDepth; ++depth)
        {
            if (!seen.insert(id).second)
                break;
            const auto info = mInfos.find(id);
            if (info == mInfos.end())
                break;
            sdk::TerrainCell cell;
            std::string error;
            sdk::TerrainResult result = sdk::TerrainResult::NoLand;
            try
            {
                result = mSource.buildTerrain(id, x, y, cell, error);
            }
            catch (const std::exception& e)
            {
                result = sdk::TerrainResult::Failed;
                error = e.what();
            }
            if (result == sdk::TerrainResult::Built)
            {
                std::string why;
                if (validateTerrainCell(cell, mWorld.vertsPerSide, mLimits, why))
                {
                    if (!mLimits.mKeepLayers)
                        for (auto& quadrant : cell.layers)
                            quadrant.clear();
                    found = std::make_shared<const sdk::TerrainCell>(std::move(cell));
                    ++mBuilt;
                }
                else
                {
                    ++mRefused;
                    noteError(why);
                }
                break;
            }
            if (result == sdk::TerrainResult::Failed)
            {
                ++mRefused;
                noteError("cell (" + std::to_string(x) + ", " + std::to_string(y) + "): " + error);
                break;
            }
            if (result == sdk::TerrainResult::NotSupported)
                break;
            // NoLand: the world may lend its parent's
            id = (info->second.parentUsage & sdk::TerrainParentUsage::Land) != 0 ? info->second.parent
                                                                               : sdk::kNoWorldId;
        }
        publish(key, std::move(found));
        return true;
    }

    std::size_t CellCache::ensure(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1)
    {
        if (!mHasWorld || x1 < x0 || y1 < y0)
            return 0;
        const std::int64_t area
            = (static_cast<std::int64_t>(x1) - x0 + 1) * (static_cast<std::int64_t>(y1) - y0 + 1);
        if (area > mLimits.mMaxEnsureCells)
            return 0;
        std::size_t asked = 0;
        for (std::int32_t y = y0; y <= y1; ++y)
            for (std::int32_t x = x0; x <= x1; ++x)
            {
                ++asked;
                ensureCell(x, y);
            }
        return asked;
    }

    std::shared_ptr<const sdk::TerrainCell> CellCache::get(std::int32_t x, std::int32_t y) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto found = mEntries.find(Key{ x, y });
        if (found == mEntries.end())
        {
            ++mUnloadedReads;
            return nullptr;
        }
        return found->second.mCell;
    }

    bool CellCache::isKnown(std::int32_t x, std::int32_t y) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEntries.find(Key{ x, y }) != mEntries.end();
    }

    std::size_t CellCache::resident() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEntries.size();
    }

    std::vector<std::string> CellCache::errors() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mErrors;
    }
}
