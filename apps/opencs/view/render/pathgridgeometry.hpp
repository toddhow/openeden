#ifndef CSV_RENDER_PATHGRIDGEOMETRY_H
#define CSV_RENDER_PATHGRIDGEOMETRY_H

#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/esm3/loadpgrd.hpp>

/// What a pathgrid looks like, as plain data with no window or GPU: the OSG build's
/// components/sceneutil/pathgridutil.cpp, with its numbers. Each point has a diamond of six vertices and four more for
/// edges to start from, in its own colour.
namespace CSVRender::PathgridGeometry
{
    inline constexpr float sDiamondHalfHeight = 40.f;
    inline constexpr float sDiamondHalfWidth = 16.f;

    inline constexpr unsigned sDiamondVertexCount = 6;
    inline constexpr unsigned sConnectorVertexCount = 4;
    inline constexpr unsigned sTotalVertexCount = sDiamondVertexCount + sConnectorVertexCount;
    inline constexpr float sWireframeScalar = 1.1f;

    struct Grid
    {
        std::vector<vsg::vec3> mVertices;
        std::vector<vsg::vec4> mColours;
        std::vector<unsigned> mTriangles;
        std::vector<unsigned> mLines;
    };

    namespace Detail
    {
        inline vsg::vec3 diamondPoint(unsigned i)
        {
            const float w = sDiamondHalfWidth;
            const float h = sDiamondHalfHeight;
            const vsg::vec3 points[sDiamondVertexCount]
                = { { 0, 0, h * 2 }, { -w, -w, h }, { -w, w, h }, { w, -w, h }, { w, w, h }, { 0, 0, 0 } };
            return points[i];
        }

        inline vsg::vec3 position(const ESM::Pathgrid::Point& point)
        {
            return vsg::vec3(static_cast<float>(point.mX), static_cast<float>(point.mY), static_cast<float>(point.mZ));
        }
    }

    inline Grid createGrid(const ESM::Pathgrid& pathgrid)
    {
        static constexpr unsigned indices[]
            = { 0, 2, 1, 0, 1, 3, 0, 3, 4, 0, 4, 2, 5, 1, 2, 5, 3, 1, 5, 4, 3, 5, 2, 4 };
        static constexpr unsigned connectors[sConnectorVertexCount] = { 1, 2, 3, 4 };
        // As they are in the old file, 9.f included.
        const vsg::vec4 colours[sDiamondVertexCount] = { { 0.f, 0.f, 1.f, 1.f }, { 0.f, .05f, .95f, 1.f },
            { 0.f, .1f, .95f, 1.f }, { 0.f, .15f, .95f, 1.f }, { 0.f, .2f, .95f, 1.f }, { 0.f, .25f, 9.f, 1.f } };
        const vsg::vec4 edgeColour(0.5f, 1.f, 1.f, 1.f);

        Grid grid;
        grid.mVertices.reserve(pathgrid.mPoints.size() * sTotalVertexCount);
        grid.mColours.reserve(pathgrid.mPoints.size() * sTotalVertexCount);
        grid.mTriangles.reserve(pathgrid.mPoints.size() * std::size(indices));

        for (std::size_t pointIndex = 0; pointIndex < pathgrid.mPoints.size(); ++pointIndex)
        {
            const vsg::vec3 position = Detail::position(pathgrid.mPoints[pointIndex]);
            const unsigned offset = static_cast<unsigned>(pointIndex) * sTotalVertexCount;
            for (unsigned i = 0; i < sDiamondVertexCount; ++i)
            {
                grid.mVertices.push_back(position + Detail::diamondPoint(i));
                grid.mColours.push_back(colours[i]);
            }
            for (unsigned index : indices)
                grid.mTriangles.push_back(offset + index);
            for (unsigned connector : connectors)
            {
                grid.mVertices.push_back(position + Detail::diamondPoint(connector));
                grid.mColours.push_back(edgeColour);
            }
        }

        grid.mLines.reserve(pathgrid.mEdges.size() * 2);
        for (const ESM::Pathgrid::Edge& edge : pathgrid.mEdges)
        {
            if (edge.mV0 == edge.mV1 || edge.mV0 >= pathgrid.mPoints.size() || edge.mV1 >= pathgrid.mPoints.size())
                continue;

            const vsg::vec3 direction
                = Detail::position(pathgrid.mPoints[edge.mV1]) - Detail::position(pathgrid.mPoints[edge.mV0]);
            // The direction turned by a quarter clockwise picks the connector, on the side edges keep to.
            const float x = direction.y;
            const float y = -direction.x;

            unsigned connector = 0;
            if (y >= 0 && x > 0)
                connector = 3;
            else if (x <= 0 && y > 0)
                connector = 1;
            else if (y <= 0 && x < 0)
                connector = 0;
            else if (x >= 0 && y < 0)
                connector = 2;

            grid.mLines.push_back(
                static_cast<unsigned>(edge.mV0) * sTotalVertexCount + sDiamondVertexCount + connector);
            grid.mLines.push_back(
                static_cast<unsigned>(edge.mV1) * sTotalVertexCount + sDiamondVertexCount + connector);
        }
        return grid;
    }

    /// A wireframe of the selected points. The last one has its own colour.
    inline Grid createSelectedWireframe(const ESM::Pathgrid& pathgrid, const std::vector<unsigned short>& selected)
    {
        static constexpr unsigned indices[]
            = { 0, 1, 0, 2, 0, 3, 0, 4, 1, 2, 2, 4, 4, 3, 3, 1, 5, 1, 5, 2, 5, 3, 5, 4 };
        const vsg::vec4 wireColour(0.72f, 0.f, 0.96f, 1.f);
        const vsg::vec4 focusColour(0.91f, 0.66f, 1.f, 1.f);
        const vsg::vec3 wireOffset(0, 0, (1 - sWireframeScalar) * sDiamondHalfHeight);

        Grid grid;
        for (std::size_t it = 0; it < selected.size(); ++it)
        {
            const vsg::vec3 position = Detail::position(pathgrid.mPoints[selected[it]]) + wireOffset;
            const unsigned offset = static_cast<unsigned>(it) * sDiamondVertexCount;
            for (unsigned i = 0; i < sDiamondVertexCount; ++i)
            {
                grid.mVertices.push_back(position + Detail::diamondPoint(i) * sWireframeScalar);
                grid.mColours.push_back(it == selected.size() - 1 ? focusColour : wireColour);
            }
            for (unsigned index : indices)
                grid.mLines.push_back(offset + index);
        }
        return grid;
    }

    inline unsigned short nodeOfVertex(unsigned vertexIndex)
    {
        return static_cast<unsigned short>(vertexIndex / sTotalVertexCount);
    }
}

#endif
