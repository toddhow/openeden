#ifndef OPENEDEN_MWRENDER_DISTANTOBJECTCELLS_H
#define OPENEDEN_MWRENDER_DISTANTOBJECTCELLS_H

#include <utility>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>

namespace MWRender
{
    // The renderer-free rules of distant objects (task 0012, V4n), from OpenMW's MWRender::ObjectPaging at 442eec6e2f.

    /// The exterior cells the scene loads on its own: a square of half size halfGrid around centre.
    struct ActiveCellGrid
    {
        std::pair<int, int> centre;
        int halfGrid = 0;

        bool contains(const std::pair<int, int>& cell) const;
    };

    /// The cells to draw distant objects for: those whose square, cellSize units across and with cell (x, y) spanning
    /// [x, x + 1) * cellSize, comes within viewDistance of the camera's XY position, leaving out the active grid.
    /// Sorted, without duplicates.
    std::vector<std::pair<int, int>> distantObjectCells(
        const vsg::vec3& cameraPosition, float cellSize, float viewDistance, const ActiveCellGrid& active);

    /// Whether a record type (ESM::RecNameInts) is drawn from afar: statics, activators and doors. OpenMW's
    /// typeFilter with far set, without its ESM4 types.
    bool isDistantObjectType(int type);

    /// The distance from the eye beyond which an object is culled, OpenMW's radius * scale < distance * minSize. Zero
    /// or negative minSize never culls, returning the largest float.
    float distantObjectMaxDistance(float radius, float scale, float minSize);

    /// An object's matrix from its reference's position, rotation (radians) and scale: scaled, rotated about Z, then Y,
    /// then X, each by the negative angle as ObjectPaging::createChunk does, and translated.
    vsg::dmat4 distantObjectMatrix(const vsg::vec3& position, const vsg::vec3& rotation, float scale);
}

#endif
