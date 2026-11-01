#include "brushdraw.hpp"

#include <cmath>

#include <components/esm3/loadland.hpp>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>

#include "../../model/world/cellcoordinates.hpp"
#include "../widget/brushshapes.hpp"
#include "cell.hpp"
#include "overlaylines.hpp"
#include "terraintoolgeometry.hpp"
#include "worldspacewidget.hpp"

namespace
{
    const vsg::vec4 sLineColour(1.0f, 1.0f, 1.0f, 0.6f);
}

CSVRender::BrushDraw::BrushDraw(WorldspaceWidget& worldspaceWidget, bool textureMode)
    : mWorldspaceWidget(worldspaceWidget)
    , mTextureMode(textureMode)
    , mLandSizeFactor(TerrainToolGeometry::landSizeFactor(textureMode))
{
}

CSVRender::BrushDraw::~BrushDraw() = default;

float CSVRender::BrushDraw::vertexHeight(int x, int y) const
{
    const int cellX = static_cast<int>(std::floor(static_cast<float>(x) / (ESM::Land::LAND_SIZE - 1)));
    const int cellY = static_cast<int>(std::floor(static_cast<float>(y) / (ESM::Land::LAND_SIZE - 1)));
    const int localX = x - cellX * (ESM::Land::LAND_SIZE - 1);
    const int localY = y - cellY * (ESM::Land::LAND_SIZE - 1);

    if (Cell* cell = mWorldspaceWidget.getCell(CSMWorld::CellCoordinates(cellX, cellY)))
        return cell->getSumOfAlteredAndTrueHeight(cellX, cellY, localX, localY);
    return 0.f;
}

float CSVRender::BrushDraw::getTerrainHeight(float worldX, float worldY) const
{
    // From the land itself: the OSG editor's ray per point of the outline, up to a thousand for each mouse move, would
    // rebuild the terrain's vertices for each here. The drawn triangles differ from this by little, with the lines not
    // depth tested.
    const float vertexSize = TerrainToolGeometry::landSizeFactor(false);
    const float vertexX = worldX / vertexSize;
    const float vertexY = worldY / vertexSize;
    const int x = static_cast<int>(std::floor(vertexX));
    const int y = static_cast<int>(std::floor(vertexY));
    return TerrainToolGeometry::bilinear(vertexX - static_cast<float>(x), vertexY - static_cast<float>(y),
        vertexHeight(x, y), vertexHeight(x + 1, y), vertexHeight(x, y + 1), vertexHeight(x + 1, y + 1));
}

void CSVRender::BrushDraw::update(const vsg::dvec3& point, int brushSize, CSVWidget::BrushShape toolShape)
{
    SceneHost* host = mWorldspaceWidget.getHost();
    if (!host)
        return;
    if (!mLines)
        mLines = std::make_unique<OverlayLines>(*host);

    const float radius = (mLandSizeFactor * static_cast<float>(brushSize)) / 2;
    const vsg::dvec3 snapped = TerrainToolGeometry::snapPoint(point, mTextureMode);
    const auto height = [this](float x, float y) { return getTerrainHeight(x, y); };

    switch (toolShape)
    {
        case CSVWidget::BrushShape_Point:
            mLines->set(TerrainToolGeometry::pointLines(snapped, height), sLineColour);
            break;
        case CSVWidget::BrushShape_Square:
            mLines->set(TerrainToolGeometry::squareLines(radius, snapped, mLandSizeFactor, height), sLineColour);
            break;
        case CSVWidget::BrushShape_Circle:
            mLines->set(TerrainToolGeometry::circleLines(radius, snapped, height), sLineColour);
            break;
        case CSVWidget::BrushShape_Custom:
            // As the OSG editor: a custom shape shows a small square.
            mLines->set(TerrainToolGeometry::squareLines(1.f, snapped, mLandSizeFactor, height), sLineColour);
            break;
    }
}

void CSVRender::BrushDraw::hide()
{
    if (mLines)
        mLines->clear();
}
