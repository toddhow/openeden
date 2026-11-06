#include "contextinspector.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include "../module/moduleui.hpp"
#include <apps/opencs/model/module/moduleregistry.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/view/render/mask.hpp>
#include <apps/opencs/view/render/object.hpp>
#include <apps/opencs/view/render/worldspacewidget.hpp>

namespace
{
    namespace Columns = CSMWorld::Columns;

    constexpr int pollIntervalMs = 250;
    constexpr int fieldDecimals = 3; // positions, rotations and scale alike: the table shows about this many
    constexpr double fieldLimit = 1.0e9; // far beyond any Morrowind coordinate; only keeps the spin boxes finite
    // A change smaller than this is the spin box rounding what the table holds, not an edit by the user.
    constexpr double editThreshold = 1.0e-4;

    const Columns::ColumnId fieldColumns[] = {
        Columns::ColumnId_PositionXPos,
        Columns::ColumnId_PositionYPos,
        Columns::ColumnId_PositionZPos,
        Columns::ColumnId_PositionXRot,
        Columns::ColumnId_PositionYRot,
        Columns::ColumnId_PositionZRot,
        Columns::ColumnId_Scale,
    };

    const char* const fieldNames[]
        = { "Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale" };

    CSMWorld::IdTable* referencesTable(CSMDoc::Document& document)
    {
        try
        {
            return dynamic_cast<CSMWorld::IdTable*>(
                document.getData().getTableModel(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_References)));
        }
        catch (const std::exception&)
        {
            return nullptr;
        }
    }
}

CSVDoc::ContextInspector::ContextInspector(CSMDoc::Document& document, ViewportProvider provider, QWidget* parent)
    : QWidget(parent)
    , mDocument(document)
    , mProvider(std::move(provider))
    , mTimer(new QTimer(this))
    , mTitle(new QLabel(this))
    , mRecordType(new QLabel(this))
    , mObjectId(new QLabel(this))
    , mCell(new QLabel(this))
    , mOpen(new QPushButton(tr("Open Record Editor"), this))
{
    setObjectName("contextInspector");

    QFormLayout* form = new QFormLayout;
    form->addRow(tr("Object"), mObjectId);
    form->addRow(tr("Record type"), mRecordType);
    form->addRow(tr("Cell"), mCell);
    for (int field = 0; field < FieldCount; ++field)
    {
        QDoubleSpinBox* box = new QDoubleSpinBox(this);
        box->setRange(-fieldLimit, fieldLimit);
        box->setDecimals(fieldDecimals);
        box->setKeyboardTracking(false); // one change when Enter is pressed or focus leaves, not one per digit
        box->setObjectName(QString("inspectorField%1").arg(field));
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, field] { fieldEdited(field); });
        mFields[field] = box;
        form->addRow(tr(fieldNames[field]), box);
    }

    mTitle->setWordWrap(true);
    mTitle->setObjectName("inspectorTitle");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(mTitle);
    layout->addLayout(form);
    layout->addWidget(mOpen);
    mModuleSections = new QVBoxLayout;
    layout->addLayout(mModuleSections);
    layout->addStretch(1);
    if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get())
    {
        connect(modules, &CSVModule::ModuleUi::changed, this, &ContextInspector::rebuildModuleSections);
        rebuildModuleSections();
    }

    connect(mOpen, &QPushButton::clicked, this, [this] {
        if (mIds.size() == 1)
            emit openRecordRequested(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Reference, mIds.front()));
    });

    if (CSMWorld::IdTable* table = referencesTable(mDocument))
        connect(table, &QAbstractItemModel::dataChanged, this, &ContextInspector::tableDataChanged);

    mTimer->setInterval(pollIntervalMs);
    connect(mTimer, &QTimer::timeout, this, &ContextInspector::poll);

    showSelection();
}

std::vector<std::string> CSVDoc::ContextInspector::readSelection() const
{
    std::vector<std::string> ids;
    CSVRender::WorldspaceWidget* viewport = mProvider ? mProvider() : nullptr;
    if (viewport == nullptr)
        return ids;

    for (const auto& tag : viewport->getSelection(CSVRender::Mask_Reference))
    {
        auto* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get());
        // The object is null while its marker is detached (apps/opencs/CLAUDE.md, "An object marker's tag").
        if (objectTag == nullptr || objectTag->mObject == nullptr)
            continue;
        const std::string id = objectTag->mObject->getReferenceId();
        if (!id.empty())
            ids.push_back(id);
    }
    return ids;
}

void CSVDoc::ContextInspector::poll()
{
    std::vector<std::string> ids = readSelection();
    if (ids == mIds)
        return;
    mIds = std::move(ids);
    showSelection();
}

void CSVDoc::ContextInspector::setFieldsEnabled(bool enabled)
{
    for (QDoubleSpinBox* box : mFields)
        box->setEnabled(enabled && !mLocked);
    mOpen->setEnabled(enabled);
}

void CSVDoc::ContextInspector::setEditLock(bool locked)
{
    mLocked = locked;
    setFieldsEnabled(mIds.size() == 1);
}

void CSVDoc::ContextInspector::showSelection()
{
    mObjectId->clear();
    mRecordType->clear();
    mCell->clear();

    if (mIds.empty())
    {
        mTitle->setText(tr("Nothing selected"));
        mUpdating = true;
        for (QDoubleSpinBox* box : mFields)
            box->setValue(0);
        mUpdating = false;
        setFieldsEnabled(false);
        return;
    }

    if (mIds.size() > 1)
    {
        mTitle->setText(tr("%1 objects selected").arg(mIds.size()));
        setFieldsEnabled(false);
        return;
    }

    mTitle->setText(QString::fromStdString(mIds.front()));
    setFieldsEnabled(true);
    refreshValues();
}

void CSVDoc::ContextInspector::tableDataChanged()
{
    if (mIds.size() == 1)
        refreshValues();
}

void CSVDoc::ContextInspector::refreshValues()
{
    CSMWorld::IdTable* table = referencesTable(mDocument);
    if (table == nullptr || mIds.size() != 1)
        return;

    const int idColumn = table->findColumnIndex(Columns::ColumnId_Id);
    const QModelIndex row = table->getModelIndex(mIds.front(), idColumn);
    if (!row.isValid())
        return; // deleted since the selection was read; the next poll clears it

    const auto text = [&](Columns::ColumnId column) {
        const int index = table->searchColumnIndex(column);
        return index < 0 ? QString() : table->data(table->index(row.row(), index)).toString();
    };

    const QString objectId = text(Columns::ColumnId_ReferenceableId);
    mObjectId->setText(objectId);
    mCell->setText(text(Columns::ColumnId_Cell));

    // The record type is that of the object this is an instance of, from the Objects table.
    mRecordType->clear();
    try
    {
        auto* objects = dynamic_cast<CSMWorld::IdTable*>(
            mDocument.getData().getTableModel(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Referenceables)));
        if (objects != nullptr)
        {
            const QModelIndex objectRow
                = objects->getModelIndex(objectId.toUtf8().constData(), objects->findColumnIndex(Columns::ColumnId_Id));
            if (objectRow.isValid())
            {
                const auto type = static_cast<CSMWorld::UniversalId::Type>(
                    objects
                        ->data(objects->index(objectRow.row(), objects->findColumnIndex(Columns::ColumnId_RecordType)))
                        .toInt());
                mRecordType->setText(QString::fromStdString(CSMWorld::UniversalId(type).getTypeName()));
            }
        }
    }
    catch (const std::exception&)
    {
    }

    mUpdating = true;
    for (int field = 0; field < FieldCount; ++field)
    {
        const int column = table->searchColumnIndex(fieldColumns[field]);
        const bool present = column >= 0;
        mFields[field]->setEnabled(present && !mLocked);
        if (present)
            mFields[field]->setValue(table->data(table->index(row.row(), column)).toDouble());
    }
    mUpdating = false;
}

void CSVDoc::ContextInspector::fieldEdited(int field)
{
    if (mUpdating || mLocked || mIds.size() != 1)
        return;

    CSMWorld::IdTable* table = referencesTable(mDocument);
    if (table == nullptr)
        return;

    const QModelIndex row = table->getModelIndex(mIds.front(), table->findColumnIndex(Columns::ColumnId_Id));
    const int column = table->searchColumnIndex(fieldColumns[field]);
    if (!row.isValid() || column < 0)
        return;

    const QModelIndex cell = table->index(row.row(), column);
    const double value = mFields[field]->value();
    if (std::abs(table->data(cell).toDouble() - value) < editThreshold)
        return;

    // One command: Undo puts the old value back, and the table's change signal moves the object in the viewport.
    mDocument.getUndoStack().push(new CSMWorld::ModifyCommand(*table, cell, value));
}

void CSVDoc::ContextInspector::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    poll();
    mTimer->start();
}

void CSVDoc::ContextInspector::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    mTimer->stop();
}

namespace
{
    // A section with many fields (a rig has dozens) scrolls inside the inspector at this height.
    constexpr int kModuleSectionMaxHeight = 320;
}

void CSVDoc::ContextInspector::rebuildModuleSections()
{
    CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
    if (modules == nullptr || mModuleSections == nullptr)
        return;
    // Sections are rebuilt only when the set that applies changes, not at every edit (an edit refreshes the forms).
    std::vector<std::string> wanted;
    if (modules->activeAsset() != nullptr)
        for (const auto& section : modules->registry().sections())
            if (section.mDesc.selectionKind == modules->activeFormat())
                wanted.push_back(section.mDesc.id);
    if (wanted == mModuleSectionIds && (!wanted.empty() || mModuleSections->count() == 0)
        && mModuleSectionGeneration == modules->layoutRevision())
        return;
    mModuleSectionIds = wanted;
    mModuleSectionGeneration = modules->layoutRevision();
    while (QLayoutItem* item = mModuleSections->takeAt(0))
    {
        if (item->widget() != nullptr)
            item->widget()->deleteLater();
        delete item;
    }
    if (modules->activeAsset() == nullptr)
        return;
    for (const std::string& id : wanted)
    {
        const auto& sections = modules->registry().sections();
        const auto found
            = std::find_if(sections.begin(), sections.end(), [&](const auto& s) { return s.mDesc.id == id; });
        if (found == sections.end())
            continue;
        openeden::editor::PropertySource* source = modules->registry().sectionSource(id, *modules->activeAsset());
        if (source == nullptr)
            continue;
        auto* box = new QGroupBox(QString::fromStdString(found->mDesc.title), this);
        box->setObjectName(QString::fromStdString("moduleSection_" + id));
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->addWidget(modules->makeScrollingForm(*source, box, kModuleSectionMaxHeight));
        mModuleSections->addWidget(box);
    }
}
