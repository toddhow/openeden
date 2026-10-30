#include "navmesh.hpp"

#include "debuggeometry.hpp"

#include <components/detournavigator/guardednavmeshcacheitem.hpp>
#include <components/detournavigator/navmeshcacheitem.hpp>
#include <components/detournavigator/settings.hpp>
#include <components/sceneutil/navmesh.hpp>

#include <DetourNavMesh.h>

#include <algorithm>
#include <limits>

namespace MWRender
{
    void NavMesh::Primitives::addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color)
    {
        mLineVertices.push_back(from);
        mLineVertices.push_back(to);
        mLineColors.push_back(color);
    }

    void NavMesh::Primitives::addTriangle(
        const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color)
    {
        mTriangleVertices.push_back(a);
        mTriangleVertices.push_back(b);
        mTriangleVertices.push_back(c);
        mTriangleColors.push_back(color);
    }

    void NavMesh::Primitives::submit(SceneUtil::DebugPrimitiveSink& sink) const
    {
        for (std::size_t i = 0; i < mLineColors.size(); ++i)
            sink.addLine(mLineVertices[2 * i], mLineVertices[2 * i + 1], mLineColors[i]);
        for (std::size_t i = 0; i < mTriangleColors.size(); ++i)
            sink.addTriangle(mTriangleVertices[3 * i], mTriangleVertices[3 * i + 1], mTriangleVertices[3 * i + 2],
                mTriangleColors[i]);
    }

    NavMesh::NavMesh(DebugGeometry& geometry, bool enabled, Settings::NavMeshRenderMode mode)
        : mGeometry(geometry)
        , mEnabled(false)
        , mMode(mode)
        , mId(std::numeric_limits<std::size_t>::max())
    {
        if (enabled)
            enable();
    }

    NavMesh::~NavMesh()
    {
        if (mEnabled)
            disable();
    }

    bool NavMesh::toggle()
    {
        if (mEnabled)
            disable();
        else
            enable();

        return mEnabled;
    }

    void NavMesh::update(const std::shared_ptr<DetourNavigator::GuardedNavMeshCacheItem>& navMesh, std::size_t id,
        const DetourNavigator::Settings& settings)
    {
        using DetourNavigator::TilePosition;
        using DetourNavigator::Version;

        if (!mEnabled)
            return;

        const auto lockedNavMesh = navMesh->lockConst();
        const Version version = lockedNavMesh->getVersion();

        if (!mTiles.empty() && mId == id && mVersion == version)
            return;

        if (mId != id)
        {
            reset();
            mId = id;
        }

        mVersion = version;

        std::vector<std::pair<TilePosition, Version>> existingTiles;
        unsigned minSalt = std::numeric_limits<unsigned>::max();
        unsigned maxSalt = 0;

        lockedNavMesh->forEachUsedTile(
            [&](const TilePosition& position, const Version& tileVersion, const dtMeshTile& meshTile) {
                existingTiles.emplace_back(position, tileVersion);
                minSalt = std::min(minSalt, meshTile.salt);
                maxSalt = std::max(maxSalt, meshTile.salt);
            });

        std::sort(existingTiles.begin(), existingTiles.end());

        for (auto it = mTiles.begin(); it != mTiles.end();)
        {
            const auto existing = std::lower_bound(existingTiles.begin(), existingTiles.end(), it->first,
                [](const std::pair<TilePosition, Version>& lhs, const TilePosition& rhs) { return lhs.first < rhs; });
            if (existing == existingTiles.end() || existing->first != it->first)
                it = mTiles.erase(it);
            else
                ++it;
        }

        const unsigned char flags = SceneUtil::NavMeshTileDrawFlagsOffMeshConnections
            | SceneUtil::NavMeshTileDrawFlagsClosedList
            | (mMode == Settings::NavMeshRenderMode::UpdateFrequency ? SceneUtil::NavMeshTileDrawFlagsHeat : 0);

        for (const auto& [position, tileVersion] : existingTiles)
        {
            const auto it = mTiles.find(position);
            if (it != mTiles.end() && it->second.mVersion == tileVersion
                && mMode != Settings::NavMeshRenderMode::UpdateFrequency)
                continue;

            const dtMeshTile* meshTile = DetourNavigator::getTile(lockedNavMesh->getImpl(), position);
            Tile tile{ tileVersion, Primitives{} };
            if (meshTile == nullptr
                || !SceneUtil::drawNavMeshTile(
                    tile.mPrimitives, lockedNavMesh->getImpl(), *meshTile, settings, flags, minSalt, maxSalt))
            {
                if (it != mTiles.end())
                    mTiles.erase(it);
                continue;
            }
            if (it == mTiles.end())
                mTiles.emplace(position, std::move(tile));
            else
                it->second = std::move(tile);
        }

        submit();
    }

    void NavMesh::submit()
    {
        mGeometry.begin();
        for (const auto& [position, tile] : mTiles)
            tile.mPrimitives.submit(mGeometry);
        mGeometry.end();
    }

    void NavMesh::reset()
    {
        mTiles.clear();
        mId = std::numeric_limits<std::size_t>::max();
        if (mEnabled)
            submit();
    }

    void NavMesh::enable()
    {
        mEnabled = true;
        mGeometry.setEnabled(true);
    }

    void NavMesh::disable()
    {
        reset();
        mEnabled = false;
        mGeometry.setEnabled(false);
    }

    void NavMesh::setMode(Settings::NavMeshRenderMode value)
    {
        if (mMode == value)
            return;
        reset();
        mMode = value;
    }
}
