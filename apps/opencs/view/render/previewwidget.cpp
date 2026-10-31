#include "previewwidget.hpp"

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/object.hpp>
#include <apps/opencs/view/render/scenewidget.hpp>

#include "../../model/world/data.hpp"
#include "../../model/world/idtable.hpp"

class QWidget;

CSVRender::PreviewWidget::PreviewWidget(
    CSMWorld::Data& worldData, const std::string& id, bool referenceable, QWidget* parent)
    : SceneWidget(worldData, parent)
    , mData(worldData)
    , mId(id)
    , mReferenceable(referenceable)
{
    selectNavigationMode("orbit");

    QAbstractItemModel* referenceables = mData.getTableModel(CSMWorld::UniversalId::Type_Referenceables);

    connect(referenceables, &QAbstractItemModel::dataChanged, this, &PreviewWidget::referenceableDataChanged);
    connect(
        referenceables, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PreviewWidget::referenceableAboutToBeRemoved);

    connect(&mData, &CSMWorld::Data::assetTablesChanged, this, &PreviewWidget::assetTablesChanged);

    setExterior(false);

    if (!referenceable)
    {
        QAbstractItemModel* references = mData.getTableModel(CSMWorld::UniversalId::Type_References);

        connect(references, &QAbstractItemModel::dataChanged, this, &PreviewWidget::referenceDataChanged);
        connect(references, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PreviewWidget::referenceAboutToBeRemoved);
    }
}

void CSVRender::PreviewWidget::referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mObject && mObject->referenceableDataChanged(topLeft, bottomRight))
        flagAsModified();

    if (mObject && mObject->getReferenceId().empty())
    {
        CSMWorld::IdTable& referenceables
            = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Referenceables));

        QModelIndex index = referenceables.getModelIndex(
            mObject->getReferenceableId(), referenceables.findColumnIndex(CSMWorld::Columns::ColumnId_Modification));

        if (referenceables.data(index).toInt() == CSMWorld::RecordBase::State_Deleted)
            emit closeRequest();
    }
}

void CSVRender::PreviewWidget::referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (mObject && mObject->referenceableAboutToBeRemoved(parent, start, end))
        flagAsModified();

    if (mObject && mObject->getReferenceableId().empty())
        return;

    CSMWorld::IdTable& referenceables
        = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Referenceables));

    QModelIndex index = referenceables.getModelIndex(mObject->getReferenceableId(), 0);

    if (index.row() >= start && index.row() <= end)
    {
        if (mObject && mObject->getReferenceId().empty())
        {
            // this is a preview for a referenceble
            emit closeRequest();
        }
    }
}

void CSVRender::PreviewWidget::referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mObject && mObject->referenceDataChanged(topLeft, bottomRight))
        flagAsModified();

    if (mObject && mObject->getReferenceId().empty())
        return;

    CSMWorld::IdTable& references
        = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_References));

    // check for deleted state
    {
        QModelIndex index = references.getModelIndex(
            mObject->getReferenceId(), references.findColumnIndex(CSMWorld::Columns::ColumnId_Modification));

        if (references.data(index).toInt() == CSMWorld::RecordBase::State_Deleted)
        {
            emit closeRequest();
            return;
        }
    }

    int columnIndex = references.findColumnIndex(CSMWorld::Columns::ColumnId_ReferenceableId);

    QModelIndex index = references.getModelIndex(mObject->getReferenceId(), columnIndex);

    if (index.row() >= topLeft.row() && index.row() <= bottomRight.row())
        if (index.column() >= topLeft.column() && index.column() <= bottomRight.row())
            emit referenceableIdChanged(mObject->getReferenceableId());
}

void CSVRender::PreviewWidget::referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (mObject && mObject->getReferenceId().empty())
        return;

    CSMWorld::IdTable& references
        = dynamic_cast<CSMWorld::IdTable&>(*mData.getTableModel(CSMWorld::UniversalId::Type_References));

    QModelIndex index = references.getModelIndex(mObject->getReferenceId(), 0);

    if (index.row() >= start && index.row() <= end)
        emit closeRequest();
}

void CSVRender::PreviewWidget::assetTablesChanged()
{
    if (mObject)
        mObject->reloadAssets();
}

void CSVRender::PreviewWidget::hostReady()
{
    SceneWidget::hostReady();
    SceneHost* host = getHost();
    mObject = std::make_unique<Object>(mData, *host, host->contentRoot(), mId, mReferenceable, false, true);
    host->frame({ vsg::dvec3(mObject->getPosition().pos[0], mObject->getPosition().pos[1], mObject->getPosition().pos[2]) });
    host->setAnimated(true);
}
