#include "navmeshmanager.hpp"

#include "debug.hpp"
#include "gettilespositions.hpp"
#include "makenavmesh.hpp"
#include "navmeshcacheitem.hpp"
#include "settings.hpp"
#include "settingsutils.hpp"
#include "waitconditiontype.hpp"

#include <vsg/io/stream.h>

#include <components/debug/debuglog.hpp>
#include <components/esmbridge/util.hpp>
#include <components/misc/vsgmath.hpp>

#include <DetourNavMesh.h>

namespace
{
    /// Safely reset shared_ptr with definite underlying object destrutor call.
    /// Assuming there is another thread holding copy of this shared_ptr or weak_ptr to this shared_ptr.
    template <class T>
    bool resetIfUnique(std::shared_ptr<T>& ptr)
    {
        const std::weak_ptr<T> weak(ptr);
        ptr.reset();
        if (auto shared = weak.lock())
        {
            ptr = std::move(shared);
            return false;
        }
        return true;
    }
}

namespace DetourNavigator
{
    namespace
    {
        int getMaxRadius(int maxTiles)
        {
            return static_cast<int>(std::ceil(std::sqrt(static_cast<float>(maxTiles) / Misc::VsgMath::PIf) + 1));
        }

        TilesPositionsRange makeRange(const TilePosition& center, int radius)
        {
            return TilesPositionsRange{
                .mBegin = center - TilePosition(radius, radius),
                .mEnd = center + TilePosition(radius + 1, radius + 1),
            };
        }

        vsg::vec2 getMinCellGridPosition(const vsg::ivec2& center, int offset, float cellSize)
        {
            const vsg::ivec2 cell = center + vsg::ivec2(offset, offset);
            return vsg::vec2(static_cast<float>(cell.x) * cellSize, static_cast<float>(cell.y) * cellSize);
        }

        TilesPositionsRange makeCellGridRange(
            const RecastSettings& settings, ESM::RefId worldspace, const CellGridBounds& bounds)
        {
            const float floatCellSize = static_cast<float>(ESM::getCellSize(worldspace));
            const vsg::vec2 min = getMinCellGridPosition(bounds.mCenter, -bounds.mHalfSize, floatCellSize);
            const vsg::vec2 max = getMinCellGridPosition(bounds.mCenter, bounds.mHalfSize + 1, floatCellSize);
            return TilesPositionsRange{
                .mBegin = getTilePosition(settings, toNavMeshCoordinates(settings, min)),
                .mEnd = getTilePosition(settings, toNavMeshCoordinates(settings, max)),
            };
        }

        TilesPositionsRange makeRange(const Settings& settings, ESM::RefId worldspace,
            const std::optional<CellGridBounds>& bounds, int radius, const TilePosition& center)
        {
            TilesPositionsRange result = makeRange(center, radius);
            if (bounds.has_value())
                result = getIntersection(result, makeCellGridRange(settings.mRecast, worldspace, *bounds));
            return result;
        }

        TilePosition toNavMeshTilePosition(const RecastSettings& settings, const vsg::vec3& position)
        {
            return getTilePosition(settings, toNavMeshCoordinates(settings, position));
        }
    }

    NavMeshManager::NavMeshManager(const Settings& settings, std::unique_ptr<NavMeshDb>&& db)
        : mSettings(settings)
        , mMaxRadius(getMaxRadius(settings.mMaxTilesNumber))
        , mRecastMeshManager(settings.mRecast)
        , mOffMeshConnectionsManager(settings.mRecast)
        , mAsyncNavMeshUpdater(settings, mRecastMeshManager, mOffMeshConnectionsManager, std::move(db))
    {
    }

    void NavMeshManager::updateBounds(ESM::RefId worldspace, const std::optional<CellGridBounds>& cellGridBounds,
        const vsg::vec3& playerPosition, const UpdateGuard* guard)
    {
        if (worldspace != mWorldspace)
        {
            mRecastMeshManager.setWorldspace(worldspace, guard);
            for (auto& [agent, cache] : mCache)
                cache = std::make_shared<GuardedNavMeshCacheItem>(++mGenerationCounter, mSettings);
            mWorldspace = worldspace;
        }

        const TilePosition playerTile = toNavMeshTilePosition(mSettings.mRecast, playerPosition);

        mRecastMeshManager.setRange(makeRange(mSettings, worldspace, cellGridBounds, mMaxRadius, playerTile), guard);
        mCellGridBounds = cellGridBounds;
    }

    bool NavMeshManager::addObject(const ObjectId id, const CollisionShape& shape, const vsg::dmat4& transform,
        const AreaType areaType, const UpdateGuard* guard)
    {
        return mRecastMeshManager.addObject(id, shape, transform, areaType, guard);
    }

    bool NavMeshManager::updateObject(
        const ObjectId id, const vsg::dmat4& transform, const AreaType areaType, const UpdateGuard* guard)
    {
        return mRecastMeshManager.updateObject(id, transform, areaType, guard);
    }

    void NavMeshManager::removeObject(const ObjectId id, const UpdateGuard* guard)
    {
        mRecastMeshManager.removeObject(id, guard);
    }

    void NavMeshManager::addWater(const vsg::ivec2& cellPosition, int cellSize, float level, const UpdateGuard* guard)
    {
        mRecastMeshManager.addWater(cellPosition, cellSize, level, guard);
    }

    void NavMeshManager::removeWater(const vsg::ivec2& cellPosition, const UpdateGuard* guard)
    {
        mRecastMeshManager.removeWater(cellPosition, guard);
    }

    void NavMeshManager::addHeightfield(
        const vsg::ivec2& cellPosition, int cellSize, const HeightfieldShape& shape, const UpdateGuard* guard)
    {
        mRecastMeshManager.addHeightfield(cellPosition, cellSize, shape, guard);
    }

    void NavMeshManager::removeHeightfield(const vsg::ivec2& cellPosition, const UpdateGuard* guard)
    {
        mRecastMeshManager.removeHeightfield(cellPosition, guard);
    }

    void NavMeshManager::addAgent(const AgentBounds& agentBounds)
    {
        auto cached = mCache.find(agentBounds);
        if (cached != mCache.end())
            return;
        mCache.emplace(agentBounds, std::make_shared<GuardedNavMeshCacheItem>(++mGenerationCounter, mSettings));
        mPlayerTile.reset();
        Log(Debug::Debug) << "cache add for agent=" << agentBounds;
    }

    bool NavMeshManager::reset(const AgentBounds& agentBounds)
    {
        const auto it = mCache.find(agentBounds);
        if (it == mCache.end())
            return true;
        if (!resetIfUnique(it->second))
            return false;
        mCache.erase(agentBounds);
        mPlayerTile.reset();
        return true;
    }

    void NavMeshManager::addOffMeshConnection(
        const ObjectId id, const vsg::vec3& start, const vsg::vec3& end, const AreaType areaType)
    {
        mOffMeshConnectionsManager.add(id, OffMeshConnection{ start, end, areaType });

        const auto startTilePosition = getTilePosition(mSettings.mRecast, start);
        const auto endTilePosition = getTilePosition(mSettings.mRecast, end);

        mRecastMeshManager.addChangedTile(startTilePosition, ChangeType::add);

        if (startTilePosition != endTilePosition)
            mRecastMeshManager.addChangedTile(endTilePosition, ChangeType::add);
    }

    void NavMeshManager::removeOffMeshConnections(const ObjectId id)
    {
        const auto changedTiles = mOffMeshConnectionsManager.remove(id);
        for (const auto& tile : changedTiles)
            mRecastMeshManager.addChangedTile(tile, ChangeType::update);
    }

    void NavMeshManager::update(const vsg::vec3& playerPosition, const UpdateGuard* guard)
    {
        const TilePosition playerTile = toNavMeshTilePosition(mSettings.mRecast, playerPosition);
        if (mLastRecastMeshManagerRevision == mRecastMeshManager.getRevision() && mPlayerTile.has_value()
            && *mPlayerTile == playerTile)
            return;
        mLastRecastMeshManagerRevision = mRecastMeshManager.getRevision();
        mPlayerTile = playerTile;
        mRecastMeshManager.setRange(makeRange(mSettings, mWorldspace, mCellGridBounds, mMaxRadius, playerTile), guard);
        const auto changedTiles = mRecastMeshManager.takeChangedTiles(guard);
        const TilesPositionsRange range = mRecastMeshManager.getLimitedObjectsRange();
        for (const auto& [agentBounds, cached] : mCache)
            update(agentBounds, playerTile, range, cached, changedTiles);
    }

    void NavMeshManager::update(const AgentBounds& agentBounds, const TilePosition& playerTile,
        const TilesPositionsRange& range, const SharedNavMeshCacheItem& cached,
        const std::map<vsg::ivec2, ChangeType>& changedTiles)
    {
        std::map<vsg::ivec2, ChangeType> tilesToPost = changedTiles;
        {
            const int maxTiles = mSettings.mMaxTilesNumber;
            const auto locked = cached->lockConst();
            const auto& navMesh = locked->getImpl();
            getTilesPositions(range, [&](const TilePosition& tile) {
                if (changedTiles.find(tile) != changedTiles.end())
                    return;
                const bool shouldAdd = shouldAddTile(tile, playerTile, maxTiles);
                const bool presentInNavMesh = navMesh.getTileAt(tile.x, tile.y, 0) != nullptr;
                if (shouldAdd && !presentInNavMesh)
                    tilesToPost.emplace(tile, locked->isEmptyTile(tile) ? ChangeType::update : ChangeType::add);
                else if (!shouldAdd && presentInNavMesh)
                    tilesToPost.emplace(tile, ChangeType::remove);
            });
            locked->forEachTilePosition([&](const TilePosition& tile) {
                if (!shouldAddTile(tile, playerTile, maxTiles))
                    tilesToPost.emplace(tile, ChangeType::remove);
            });
        }
        mAsyncNavMeshUpdater.post(agentBounds, cached, playerTile, mWorldspace, tilesToPost);
        Log(Debug::Debug) << "Cache update posted for agent=" << agentBounds << " playerTile=" << playerTile
                          << " recastMeshManagerRevision=" << mLastRecastMeshManagerRevision;
    }

    void NavMeshManager::wait(WaitConditionType waitConditionType, Loading::Listener* listener)
    {
        mAsyncNavMeshUpdater.wait(waitConditionType, listener);
    }

    SharedNavMeshCacheItem NavMeshManager::getNavMesh(const AgentBounds& agentBounds) const
    {
        return getCached(agentBounds);
    }

    std::map<AgentBounds, SharedNavMeshCacheItem> NavMeshManager::getNavMeshes() const
    {
        return mCache;
    }

    Stats NavMeshManager::getStats() const
    {
        return Stats{
            .mUpdater = mAsyncNavMeshUpdater.getStats(),
            .mRecast = mRecastMeshManager.getStats(),
        };
    }

    RecastMeshTiles NavMeshManager::getRecastMeshTiles() const
    {
        RecastMeshTiles result;
        getTilesPositions(mRecastMeshManager.getLimitedObjectsRange(), [&](const TilePosition& v) {
            if (auto mesh = mRecastMeshManager.getCachedMesh(mWorldspace, v))
                result.emplace(v, std::move(mesh));
        });
        return result;
    }

    SharedNavMeshCacheItem NavMeshManager::getCached(const AgentBounds& agentBounds) const
    {
        const auto cached = mCache.find(agentBounds);
        if (cached != mCache.end())
            return cached->second;
        return SharedNavMeshCacheItem();
    }
}
