#include "cell.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include <vsg/nodes/Group.h>

#include <components/esm/defs.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/pipeline/builder.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/terrain/paging.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/compileop.hpp>

#include "../../model/doc/document.hpp"
#include "../../model/world/idtable.hpp"

#include "cellarrow.hpp"
#include "cellborder.hpp"
#include "cellmarker.hpp"
#include "cellwater.hpp"
#include "terrainstorage.hpp"
#include "instancedragmodes.hpp"
#include "mask.hpp"
#include "object.hpp"
#include "objectmarker.hpp"
#include "scenehost.hpp"
#include "worldspacewidget.hpp"

#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/ref.hpp>
#include <apps/opencs/model/world/refcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

bool CSVRender::Cell::removeObject(const std::string& id)
{
    std::map<std::string, Object*>::iterator iter = mObjects.find(Misc::StringUtils::lowerCase(id));

    if (iter == mObjects.end())
        return false;

    removeObject(iter);
    return true;
}

std::map<std::string, CSVRender::Object*>::iterator CSVRender::Cell::removeObject(
    std::map<std::string, Object*>::iterator iter)
{
    delete iter->second;
    mObjects.erase(iter++);
    return iter;
}

bool CSVRender::Cell::addObjects(int start, int end)
{
    const CSMWorld::RefCollection& collection = mData.getReferences();

    // One group for those added together, compiled at once and left in the cell: compiling each object alone costs a
    // submission each.
    auto batch = vsg::Group::create();
    std::vector<std::pair<std::string, std::unique_ptr<Object>>> added;

    for (int i = start; i <= end; ++i)
    {
        const auto& cellId = ESM::RefId::stringRefId(collection.getRecord(i).get().mCell.toString());

        CSMWorld::RecordBase::State state = collection.getRecord(i).mState;

        if (cellId == mId && state != CSMWorld::RecordBase::State_Deleted)
        {
            const std::string& id = collection.getRecord(i).get().mId.getRefIdString();

            added.emplace_back(id, std::make_unique<Object>(mData, mHost, *batch, id, false));
        }
    }

    if (added.empty())
        return false;

    mHost.compileContext()->compile(batch);
    mCellNode->addChild(batch);
    for (auto& [id, object] : added)
        mObjects.insert(std::make_pair(id, object.release()));
    return true;
}

void CSVRender::Cell::updateLand()
{
    if (!mUpdateLand || mLandDeleted)
        return;

    mUpdateLand = false;

    // Cell is deleted
    if (mDeleted)
    {
        unloadLand();
        return;
    }

    const CSMWorld::IdCollection<CSMWorld::Land>& land = mData.getLand();

    if (land.getRecord(mId).isDeleted())
        return;

    const ESM::Land& esmLand = land.getRecord(mId).get();

    if (!mTerrain)
    {
        Resource::ResourceSystem& resources = mHost.resources();
        mTerrain = std::make_unique<Terrain::Paging>(mHost.compileContext(), resources.textureOptions,
            resources.shaderOptions, resources.builder->createSampler());
        mTerrain->setStorage(mTerrainStorage.get());
        mTerrainView = mTerrain->createView();
        mTerrainNode = SceneHost::maskNode(Mask_Terrain, mTerrainView->node());
    }

    // One cell's bounds in one batch: the alteration hooks of TerrainStorage do not know which cell they are asked about.
    if (auto compile = mTerrain->updateViewForCell(
            *mTerrainView, vsg::ivec2(esmLand.mX, esmLand.mY), mTerrainStorage->cellWorldSize))
        compile->operate();
    auto& children = mCellNode->children;
    if (std::find(children.begin(), children.end(), mTerrainNode) == children.end())
        mCellNode->addChild(mTerrainNode);

    if (!mCellBorder)
        mCellBorder = std::make_unique<CellBorder>(mHost, *mCellNode, mCoordinates);

    mCellBorder->buildShape(esmLand);
    mHost.requestRedraw();
}

void CSVRender::Cell::unloadLand()
{
    if (mTerrain)
    {
        mTerrain->clearView(*mTerrainView);
        auto& children = mCellNode->children;
        children.erase(std::remove(children.begin(), children.end(), mTerrainNode), children.end());
    }

    if (mCellBorder)
        mCellBorder.reset();
    mHost.requestRedraw();
}

CSVRender::Cell::Cell(CSMDoc::Document& document, ObjectMarker* selectionMarker, SceneHost& host, const std::string& id,
    bool deleted, bool isExterior)
    : mHost(host)
    , mSelectionMarker(selectionMarker)
    , mData(document.getData())
    , mId(ESM::RefId::stringRefId(id))
    , mDeleted(deleted)
    , mSubMode(0)
    , mSubModeElementMask(0)
    , mUpdateLand(isExterior)
    , mLandDeleted(false)
{
    std::pair<CSMWorld::CellCoordinates, bool> result = CSMWorld::CellCoordinates::fromId(id);

    if (result.second)
        mCoordinates = result.first;

    mTerrainStorage = std::make_unique<TerrainStorage>(mData);

    mCellNode = vsg::Group::create();
    mHost.compileContext()->compile(mCellNode);
    mHost.contentRoot().addChild(mCellNode);
    mHost.addListener(*this);

    setCellMarker();

    if (!mDeleted)
    {
        CSMWorld::IdTable& references
            = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_References));

        int rows = references.rowCount();

        addObjects(0, rows - 1);

        if (mUpdateLand)
        {
            int landIndex = document.getData().getLand().searchId(mId);
            if (landIndex == -1)
            {
                CSMWorld::IdTable& landTable
                    = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Land));
                document.getUndoStack().push(new CSMWorld::CreateCommand(landTable, mId.getRefIdString()));
            }
            updateLand();
        }

        mCellWater = std::make_unique<CellWater>(mHost, *mCellNode, mId.getRefIdString(), mCoordinates);
    }
    mHost.requestRedraw();
}

CSVRender::Cell::~Cell()
{
    mHost.removeListener(*this);

    for (std::map<std::string, Object*>::iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
        delete iter->second;

    // Each of these detaches its own nodes. So does the terrain, before the storage it reads goes.
    mCellWater.reset();
    mCellBorder.reset();
    mCellMarker.reset();
    for (auto& arrow : mCellArrows)
        arrow.reset();
    if (mTerrain)
        mTerrain->clearView(*mTerrainView);

    auto& children = mHost.contentRoot().children;
    children.erase(std::remove(children.begin(), children.end(), vsg::ref_ptr<vsg::Node>(mCellNode)), children.end());
    mHost.compileContext()->detach(mCellNode);
    mHost.requestRedraw();
}

void CSVRender::Cell::frameUpdate(float dt)
{
    for (auto& [_, object] : mObjects)
        object->frameUpdate(dt);
    updateLand();
    if (mCellMarker)
        mCellMarker->update();
}

bool CSVRender::Cell::isAnimated() const
{
    return std::any_of(mObjects.begin(), mObjects.end(),
        [](const auto& entry) { return entry.second->isAnimated(); });
}

std::vector<vsg::dvec3> CSVRender::Cell::getObjectPositions() const
{
    std::vector<vsg::dvec3> positions;
    positions.reserve(mObjects.size());
    for (const auto& [_, object] : mObjects)
    {
        const ESM::Position position = object->getPosition();
        positions.emplace_back(position.pos[0], position.pos[1], position.pos[2]);
    }
    return positions;
}

bool CSVRender::Cell::referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    bool modified = false;

    for (std::map<std::string, Object*>::iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
        if (iter->second->referenceableDataChanged(topLeft, bottomRight))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (parent.isValid())
        return false;

    bool modified = false;

    for (std::map<std::string, Object*>::iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
        if (iter->second->referenceableAboutToBeRemoved(parent, start, end))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mDeleted)
        return false;

    CSMWorld::IdTable& references
        = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_References));

    int idColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    int cellColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
    int stateColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Modification);

    // list IDs in cell
    std::map<std::string, bool> ids; // id, deleted state

    for (int i = topLeft.row(); i <= bottomRight.row(); ++i)
    {
        auto cell
            = ESM::RefId::stringRefId(references.data(references.index(i, cellColumn)).toString().toUtf8().constData());

        if (cell == mId)
        {
            std::string id = Misc::StringUtils::lowerCase(
                references.data(references.index(i, idColumn)).toString().toUtf8().constData());

            int state = references.data(references.index(i, stateColumn)).toInt();

            ids.insert(std::make_pair(id, state == CSMWorld::RecordBase::State_Deleted));
        }
    }

    // perform update and remove where needed
    bool modified = false;

    std::map<std::string, Object*>::iterator iter = mObjects.begin();
    while (iter != mObjects.end())
    {
        if (iter->second->referenceDataChanged(topLeft, bottomRight))
            modified = true;

        std::map<std::string, bool>::iterator iter2 = ids.find(iter->first);

        if (iter2 != ids.end())
        {
            bool deleted = iter2->second;
            ids.erase(iter2);

            if (deleted)
            {
                iter = removeObject(iter);
                modified = true;
                continue;
            }
        }

        ++iter;
    }

    // add new objects
    for (std::map<std::string, bool>::iterator mapIter(ids.begin()); mapIter != ids.end(); ++mapIter)
    {
        if (!mapIter->second)
        {
            mObjects.insert(std::make_pair(
                mapIter->first, new Object(mData, mHost, *mCellNode, mapIter->first, false, false, true)));

            modified = true;
        }
    }

    return modified;
}

bool CSVRender::Cell::referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (parent.isValid())
        return false;

    if (mDeleted)
        return false;

    CSMWorld::IdTable& references
        = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_References));

    int idColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Id);

    bool modified = false;

    for (int row = start; row <= end; ++row)
        if (removeObject(references.data(references.index(row, idColumn)).toString().toUtf8().constData()))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceAdded(const QModelIndex& parent, int start, int end)
{
    if (parent.isValid())
        return false;

    if (mDeleted)
        return false;

    return addObjects(start, end);
}

void CSVRender::Cell::setAlteredHeight(int inCellX, int inCellY, float height)
{
    mTerrainStorage->setAlteredHeight(inCellX, inCellY, height);
    mUpdateLand = true;
}

float CSVRender::Cell::getSumOfAlteredAndTrueHeight(int cellX, int cellY, int inCellX, int inCellY)
{
    return mTerrainStorage->getSumOfAlteredAndTrueHeight(cellX, cellY, inCellX, inCellY);
}

float* CSVRender::Cell::getAlteredHeight(int inCellX, int inCellY)
{
    return mTerrainStorage->getAlteredHeight(inCellX, inCellY);
}

void CSVRender::Cell::resetAlteredHeights()
{
    mTerrainStorage->resetHeights();
    mUpdateLand = true;
}

void CSVRender::Cell::landDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    mLandDeleted = true;
    unloadLand();
}

void CSVRender::Cell::landAdded(const QModelIndex& parent, int start, int end)
{
    mUpdateLand = true;
    mLandDeleted = false;
}

void CSVRender::Cell::landTextureChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landTextureAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landTextureAdded(const QModelIndex& parent, int start, int end)
{
    mUpdateLand = true;
}

void CSVRender::Cell::reloadAssets()
{
    for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
    {
        iter->second->reloadAssets();
    }

    mUpdateLand = true;

    if (mCellWater)
        mCellWater->reloadAssets();
}

void CSVRender::Cell::setSelection(int elementMask, Selection mode)
{
    if (elementMask & Mask_Reference)
    {
        for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
        {
            bool selected = false;

            switch (mode)
            {
                case Selection_Clear:
                    selected = false;
                    break;
                case Selection_All:
                    selected = true;
                    break;
                case Selection_Invert:
                    selected = !iter->second->getSelected();
                    break;
            }

            iter->second->setSelected(selected);
            if (selected)
                mSelectionMarker->addToSelectionHistory(iter->second->getReferenceId(), false);
        }
        mSelectionMarker->updateSelectionMarker();
    }
}

void CSVRender::Cell::selectAllWithSameParentId(int elementMask)
{
    std::set<std::string> ids;

    for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
    {
        if (iter->second->getSelected())
            ids.insert(iter->second->getReferenceableId());
    }

    for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
    {
        if (!iter->second->getSelected() && ids.find(iter->second->getReferenceableId()) != ids.end())
        {
            iter->second->setSelected(true);
            mSelectionMarker->addToSelectionHistory(iter->second->getReferenceId(), false);
        }
    }
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::handleSelectDrag(Object* object, DragMode dragMode)
{
    if (dragMode == DragMode_Select_Only || dragMode == DragMode_Select_Add)
        object->setSelected(true);

    else if (dragMode == DragMode_Select_Remove)
        object->setSelected(false);

    else if (dragMode == DragMode_Select_Invert)
        object->setSelected(!object->getSelected());

    if (object->getSelected())
        mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
}

void CSVRender::Cell::selectInsideCube(const vsg::dvec3& pointA, const vsg::dvec3& pointB, DragMode dragMode)
{
    for (auto& object : mObjects)
    {
        if (dragMode == DragMode_Select_Only)
            object.second->setSelected(false);

        if ((object.second->getPosition().pos[0] > pointA[0] && object.second->getPosition().pos[0] < pointB[0])
            || (object.second->getPosition().pos[0] > pointB[0] && object.second->getPosition().pos[0] < pointA[0]))
        {
            if ((object.second->getPosition().pos[1] > pointA[1] && object.second->getPosition().pos[1] < pointB[1])
                || (object.second->getPosition().pos[1] > pointB[1] && object.second->getPosition().pos[1] < pointA[1]))
            {
                if ((object.second->getPosition().pos[2] > pointA[2] && object.second->getPosition().pos[2] < pointB[2])
                    || (object.second->getPosition().pos[2] > pointB[2]
                        && object.second->getPosition().pos[2] < pointA[2]))
                    handleSelectDrag(object.second, dragMode);
            }
        }
    }

    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::selectWithinDistance(const vsg::dvec3& point, float distance, DragMode dragMode)
{
    for (auto& object : mObjects)
    {
        if (dragMode == DragMode_Select_Only)
            object.second->setSelected(false);

        const ESM::Position position = object.second->getPosition();
        const float distanceFromObject = static_cast<float>(
            vsg::length(point - vsg::dvec3(position.pos[0], position.pos[1], position.pos[2])));
        if (distanceFromObject < distance)
            handleSelectDrag(object.second, dragMode);
    }

    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::setCellArrows(int mask)
{
    for (int i = 0; i < 4; ++i)
    {
        CellArrow::Direction direction = static_cast<CellArrow::Direction>(1 << i);

        bool enable = mask & direction;

        if (enable != (mCellArrows[i].get() != nullptr))
        {
            if (enable)
                mCellArrows[i] = std::make_unique<CellArrow>(mHost, *mCellNode, direction, mCoordinates);
            else
                mCellArrows[i].reset(nullptr);
        }
    }
}

void CSVRender::Cell::setCellMarker()
{
    bool cellExists = false;
    bool isInteriorCell = false;

    int cellIndex = mData.getCells().searchId(mId);
    if (cellIndex > -1)
    {
        const CSMWorld::Record<CSMWorld::Cell>& cellRecord = mData.getCells().getRecord(cellIndex);
        cellExists = !cellRecord.isDeleted();
        isInteriorCell = cellRecord.get().mData.mFlags & ESM::Cell::Interior;
    }

    if (!isInteriorCell)
    {
        mCellMarker = std::make_unique<CellMarker>(mHost, *mCellNode, mCoordinates, cellExists);
    }
}

CSMWorld::CellCoordinates CSVRender::Cell::getCoordinates() const
{
    return mCoordinates;
}

bool CSVRender::Cell::isDeleted() const
{
    return mDeleted;
}

vsg::ref_ptr<CSVRender::TagBase> CSVRender::Cell::getSnapTarget(unsigned int elementMask) const
{
    vsg::ref_ptr<TagBase> result;

    if (elementMask & Mask_Reference)
        for (auto& obj : mObjects)
            if (obj.second->getSnapTarget())
                return obj.second->getTag();

    return result;
}

void CSVRender::Cell::selectFromGroup(const std::vector<std::string>& group)
{
    for (const auto& [_, object] : mObjects)
    {
        for (const auto& objectName : group)
        {
            if (objectName == object->getReferenceId())
            {
                object->setSelected(true, vsg::vec4(1, 0, 1, 1));
                mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
            }
        }
    }
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::unhideAll()
{
    for (const auto& [_, object] : mObjects)
        object->setHidden(false);
}

std::vector<vsg::ref_ptr<CSVRender::TagBase>> CSVRender::Cell::getSelection(unsigned int elementMask) const
{
    std::vector<vsg::ref_ptr<TagBase>> result;

    if (elementMask & Mask_Reference)
        for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
            if (iter->second->getSelected())
                result.push_back(iter->second->getTag());

    return result;
}

std::vector<vsg::ref_ptr<CSVRender::TagBase>> CSVRender::Cell::getEdited(unsigned int elementMask) const
{
    std::vector<vsg::ref_ptr<TagBase>> result;

    if (elementMask & Mask_Reference)
        for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
            if (iter->second->isEdited())
                result.push_back(iter->second->getTag());

    return result;
}

void CSVRender::Cell::setSubMode(int subMode, unsigned int elementMask)
{
    mSubMode = subMode;
    mSubModeElementMask = elementMask;

    if (elementMask & Mask_Reference)
        mSelectionMarker->setSubMode(subMode);
}

void CSVRender::Cell::reset(unsigned int elementMask)
{
    if (elementMask & Mask_Reference)
        for (std::map<std::string, Object*>::const_iterator iter(mObjects.begin()); iter != mObjects.end(); ++iter)
            iter->second->reset();
}

CSVRender::Object* CSVRender::Cell::getObjectByReferenceId(const std::string& referenceId)
{
    if (auto iter = mObjects.find(Misc::StringUtils::lowerCase(referenceId)); iter != mObjects.end())
        return iter->second;
    else
        return nullptr;
}
