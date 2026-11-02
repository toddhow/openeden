#ifndef VSGOPENMW_TERRAIN_COVERAGE_H
#define VSGOPENMW_TERRAIN_COVERAGE_H

#include <cstdint>
#include <vector>

#include "bounds.hpp"

namespace Terrain
{
    // OpenEden (task 0055, stage S1d): squares of cells (aligned powers of two, like every Bounds of the quadtree) that
    // something else draws, so Paging::updateView leaves its chunks out. Built on the main thread; Paging reads it only
    // during the updateView call it is passed to (never from the preload thread, which passes none).
    struct CoveredSquare
    {
        Bounds bounds;
        // The vertex LOD (relative to the native resolution, as getVertexLod) of the covering draw, so that live
        // chunks next to it stitch their edges to it.
        uint32_t vertexLod = 0;
    };

    struct Coverage
    {
        std::vector<CoveredSquare> squares;
        // Changes whenever `squares` changes, so a view that was built for another version is rebuilt.
        uint64_t version = 0;

        // True when `inner` lies entirely inside one covered square.
        bool contains(const Bounds& inner) const
        {
            for (const CoveredSquare& square : squares)
            {
                const vsg::vec2 outerMax = square.bounds.max();
                const vsg::vec2 innerMax = inner.max();
                if (inner.min.x >= square.bounds.min.x && inner.min.y >= square.bounds.min.y && innerMax.x <= outerMax.x
                    && innerMax.y <= outerMax.y)
                    return true;
            }
            return false;
        }

        // True when `other` shares any area with a covered square.
        bool intersects(const Bounds& other) const
        {
            for (const CoveredSquare& square : squares)
            {
                const vsg::vec2 squareMax = square.bounds.max();
                const vsg::vec2 otherMax = other.max();
                if (other.min.x < squareMax.x && otherMax.x > square.bounds.min.x && other.min.y < squareMax.y
                    && otherMax.y > square.bounds.min.y)
                    return true;
            }
            return false;
        }

        // The smallest vertex LOD among the covered squares that `other` touches (shares area or an edge or a corner
        // with), or -1 when it touches none. A live chunk next to a covered square must not have fewer vertices than
        // the square's draw: the draw cannot stitch itself to a coarser neighbour, so the chunk is split instead.
        int touchedVertexLod(const Bounds& other) const
        {
            int result = -1;
            for (const CoveredSquare& square : squares)
            {
                const vsg::vec2 squareMax = square.bounds.max();
                const vsg::vec2 otherMax = other.max();
                if (other.min.x <= squareMax.x && otherMax.x >= square.bounds.min.x && other.min.y <= squareMax.y
                    && otherMax.y >= square.bounds.min.y)
                {
                    const int lod = static_cast<int>(square.vertexLod);
                    if (result < 0 || lod < result)
                        result = lod;
                }
            }
            return result;
        }

        // The covered square that holds the point, or nullptr.
        const CoveredSquare* find(const vsg::vec2& point) const
        {
            for (const CoveredSquare& square : squares)
            {
                if (square.bounds.contains(point))
                    return &square;
            }
            return nullptr;
        }
    };
}

#endif
