#include "gettilespositions.hpp"
#include "settings.hpp"
#include "settingsutils.hpp"
#include "tilebounds.hpp"
#include "tileposition.hpp"

#include <components/misc/convert.hpp>
#include <components/misc/vsgmath.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace DetourNavigator
{
    TilesPositionsRange makeTilesPositionsRange(
        const vsg::vec2& aabbMin, const vsg::vec2& aabbMax, const RecastSettings& settings)
    {
        vsg::vec2 min = toNavMeshCoordinates(settings, aabbMin);
        vsg::vec2 max = toNavMeshCoordinates(settings, aabbMax);

        const float border = getBorderSize(settings);
        min -= vsg::vec2(border, border);
        max += vsg::vec2(border, border);

        TilePosition minTile = getTilePosition(settings, min);
        TilePosition maxTile = getTilePosition(settings, max);

        if (minTile.x > maxTile.x)
            std::swap(minTile.x, maxTile.x);

        if (minTile.y > maxTile.y)
            std::swap(minTile.y, maxTile.y);

        return { minTile, maxTile + vsg::ivec2(1, 1) };
    }

    TilesPositionsRange makeTilesPositionsRange(
        const JPH::Shape& shape, const vsg::dmat4& transform, const RecastSettings& settings)
    {
        const TileBounds bounds = makeObjectTileBounds(shape, transform);
        return makeTilesPositionsRange(bounds.mMin, bounds.mMax, settings);
    }

    TilesPositionsRange makeTilesPositionsRange(
        const JPH::Shape& shape, const vsg::dmat4& transform, const TileBounds& bounds, const RecastSettings& settings)
    {
        if (const auto intersection = getIntersection(bounds, makeObjectTileBounds(shape, transform)))
            return makeTilesPositionsRange(intersection->mMin, intersection->mMax, settings);
        return {};
    }

    TilesPositionsRange makeTilesPositionsRange(
        const int cellSize, const vsg::vec3& shift, const RecastSettings& settings)
    {
        const int halfCellSize = cellSize / 2;
        const auto halfCellSizeF = static_cast<vsg::vec3::value_type>(halfCellSize);

        vsg::vec3 aabbMin = shift + vsg::vec3(-halfCellSizeF, -halfCellSizeF, 0.0f);
        vsg::vec3 aabbMax = shift + vsg::vec3(halfCellSizeF, halfCellSizeF, 0.0f);

        aabbMin.set(std::min(aabbMin.x, aabbMax.x), std::min(aabbMin.y, aabbMax.y), aabbMin.z);
        aabbMax.set(std::max(aabbMin.x, aabbMax.x), std::max(aabbMin.y, aabbMax.y), aabbMax.z);

        return makeTilesPositionsRange(Misc::Convert::toOsgXY(aabbMin), Misc::Convert::toOsgXY(aabbMax), settings);
    }

    TilesPositionsRange getIntersection(const TilesPositionsRange& a, const TilesPositionsRange& b) noexcept
    {
        const int beginX = std::max(a.mBegin.x, b.mBegin.x);
        const int endX = std::min(a.mEnd.x, b.mEnd.x);
        if (beginX > endX)
            return {};
        const int beginY = std::max(a.mBegin.y, b.mBegin.y);
        const int endY = std::min(a.mEnd.y, b.mEnd.y);
        if (beginY > endY)
            return {};
        return TilesPositionsRange{ TilePosition(beginX, beginY), TilePosition(endX, endY) };
    }

    TilesPositionsRange getUnion(const TilesPositionsRange& a, const TilesPositionsRange& b) noexcept
    {
        const int beginX = std::min(a.mBegin.x, b.mBegin.x);
        const int endX = std::max(a.mEnd.x, b.mEnd.x);
        const int beginY = std::min(a.mBegin.y, b.mBegin.y);
        const int endY = std::max(a.mEnd.y, b.mEnd.y);
        return TilesPositionsRange{ .mBegin = TilePosition(beginX, beginY), .mEnd = TilePosition(endX, endY) };
    }
}
