#include "terrainselection.hpp"

#include <algorithm>
#include <cmath>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>

#include <components/esm3/loadland.hpp>

#include "cell.hpp"
#include "overlaylines.hpp"
#include "terraintoolgeometry.hpp"
#include "worldspacewidget.hpp"

namespace CSMWorld
{
    struct Cell;
}

CSVRender::TerrainSelection::TerrainSelection(WorldspaceWidget* worldspaceWidget, TerrainSelectionType type)
    : mWorldspaceWidget(worldspaceWidget)
    , mSelectionType(type)
{
}

CSVRender::TerrainSelection::~TerrainSelection() = default;

std::vector<std::pair<int, int>> CSVRender::TerrainSelection::getTerrainSelection() const
{
    return mSelection;
}

void CSVRender::TerrainSelection::onlySelect(const std::vector<std::pair<int, int>>& localPositions)
{
    mSelection = localPositions;

    update();
}

void CSVRender::TerrainSelection::addSelect(const std::vector<std::pair<int, int>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::AddSelect);
}

void CSVRender::TerrainSelection::removeSelect(const std::vector<std::pair<int, int>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::RemoveSelect);
}

void CSVRender::TerrainSelection::toggleSelect(const std::vector<std::pair<int, int>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::ToggleSelect);
}

void CSVRender::TerrainSelection::clearTemporarySelection()
{
    mTemporarySelection.clear();
}

void CSVRender::TerrainSelection::activate() {}

void CSVRender::TerrainSelection::deactivate()
{
    mLines.reset();
}

void CSVRender::TerrainSelection::update()
{
    SceneHost* host = mWorldspaceWidget->getHost();
    if (!host)
        return;
    if (!mLines)
        mLines = std::make_unique<OverlayLines>(*host);

    const auto height = [this](int x, int y) { return calculateLandHeight(x, y); };
    const std::vector<vsg::vec3> vertices = mSelectionType == TerrainSelectionType::Texture
        ? TerrainToolGeometry::textureSelectionLines(mSelection, height)
        : TerrainToolGeometry::shapeSelectionLines(mSelection, height);
    mLines->set(vertices, vsg::vec4(1, 1, 1, 1));
}

void CSVRender::TerrainSelection::handleSelection(
    const std::vector<std::pair<int, int>>& localPositions, SelectionMethod selectionMethod)
{
    for (auto const& localPos : localPositions)
    {
        const auto iter = std::find(mSelection.begin(), mSelection.end(), localPos);

        switch (selectionMethod)
        {
            case SelectionMethod::OnlySelect:
                break;

            case SelectionMethod::AddSelect:
                if (iter == mSelection.end())
                {
                    mSelection.emplace_back(localPos);
                }
                break;

            case SelectionMethod::RemoveSelect:
                if (iter != mSelection.end())
                {
                    mSelection.erase(iter);
                }
                break;

            case SelectionMethod::ToggleSelect:
            {
                const auto iterTemp = std::find(mTemporarySelection.begin(), mTemporarySelection.end(), localPos);
                if (iterTemp == mTemporarySelection.end())
                {
                    if (iter == mSelection.end())
                    {
                        mSelection.emplace_back(localPos);
                    }
                    else
                    {
                        mSelection.erase(iter);
                    }
                }
                mTemporarySelection.emplace_back(localPos);
                break;
            }

            default:
                break;
        }
    }

    update();
}

bool CSVRender::TerrainSelection::noCell(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Cell>& cellCollection = document.getData().getCells();
    return cellCollection.searchId(ESM::RefId::stringRefId(cellId)) == -1;
}

bool CSVRender::TerrainSelection::noLand(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Land>& landCollection = document.getData().getLand();
    return landCollection.searchId(ESM::RefId::stringRefId(cellId)) == -1;
}

bool CSVRender::TerrainSelection::noLandLoaded(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Land>& landCollection = document.getData().getLand();
    return !landCollection.getRecord(ESM::RefId::stringRefId(cellId)).get().isDataLoaded(ESM::Land::DATA_VNML);
}

bool CSVRender::TerrainSelection::isLandLoaded(const std::string& cellId)
{
    if (!noCell(cellId) && !noLand(cellId) && !noLandLoaded(cellId))
        return true;
    return false;
}

int CSVRender::TerrainSelection::calculateLandHeight(int x, int y) // global vertex coordinates
{
    int cellX = std::floor(static_cast<float>(x) / (ESM::Land::LAND_SIZE - 1));
    int cellY = std::floor(static_cast<float>(y) / (ESM::Land::LAND_SIZE - 1));
    int localX = x - cellX * (ESM::Land::LAND_SIZE - 1);
    int localY = y - cellY * (ESM::Land::LAND_SIZE - 1);

    CSMWorld::CellCoordinates coords(cellX, cellY);

    float landHeight = 0.f;
    if (CSVRender::Cell* cell = dynamic_cast<CSVRender::Cell*>(mWorldspaceWidget->getCell(coords)))
    {
        landHeight = cell->getSumOfAlteredAndTrueHeight(cellX, cellY, localX, localY);
    }
    else if (isLandLoaded(CSMWorld::CellCoordinates::generateId(cellX, cellY)))
    {
        CSMDoc::Document& document = mWorldspaceWidget->getDocument();
        std::string cellId = CSMWorld::CellCoordinates::generateId(cellX, cellY);
        const ESM::Land::LandData* landData = document.getData()
                                                  .getLand()
                                                  .getRecord(ESM::RefId::stringRefId(cellId))
                                                  .get()
                                                  .getLandData(ESM::Land::DATA_VHGT);
        return landData->mHeights[localY * ESM::Land::LAND_SIZE + localX];
    }

    return landHeight;
}
