#include "assetbrowser.hpp"

#include <filesystem>

#include "../module/moduleui.hpp"
#include <apps/opencs/model/module/moduleregistry.hpp>

#include <QHeaderView>
#include <QLineEdit>
#include <QMap>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <exception>
#include <utility>
#include <vector>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/model/world/resources.hpp>
#include <apps/opencs/model/world/tablemimedata.hpp>

#include <components/misc/scalableicon.hpp>

namespace
{
    using CSMWorld::UniversalId;

    // What an item of the tree stands for. Stored in the item's data under roleKind.
    enum Kind
    {
        Kind_Group = 1, ///< A top-level folder; filled when opened.
        Kind_Placeholder, ///< The one child of an unfilled group, so that Qt draws an expand arrow.
        Kind_Record, ///< A record that opens an editor and can be dragged into the viewport (an object).
        Kind_OpenRecord, ///< A record that opens an editor (a script).
        Kind_Cell, ///< A cell: opens a 3D scene.
        Kind_Resource, ///< An asset file: opens the table of that asset type.
        Kind_Table, ///< A table: opens it.
        Kind_ModuleAsset, ///< A file an editor module handles: opens it through the module (task 0031).
    };

    enum GroupSource
    {
        Source_Records, ///< The rows of an id table (record type column and id column).
        Source_Resources, ///< The files of an asset folder.
        Source_Tables, ///< The fixed list of tables.
        Source_ModuleAssets, ///< Files with an extension an editor module registered.
    };

    constexpr int roleKind = Qt::UserRole + 1;
    constexpr int roleType = Qt::UserRole + 2; // UniversalId::Type, as int
    constexpr int roleId = Qt::UserRole + 3; // the record id or file name
    constexpr int roleSource = Qt::UserRole + 4; // GroupSource, groups only
    constexpr int roleChildKind = Qt::UserRole + 5; // Kind of the children, groups only

    struct GroupDefinition
    {
        const char* title;
        UniversalId::Type type;
        GroupSource source;
        Kind childKind;
    };

    const GroupDefinition groups[] = {
        { "Cells", UniversalId::Type_Cells, Source_Records, Kind_Cell },
        { "Objects", UniversalId::Type_Referenceables, Source_Records, Kind_Record },
        { "Scripts", UniversalId::Type_Scripts, Source_Records, Kind_OpenRecord },
        { "Meshes", UniversalId::Type_Meshes, Source_Resources, Kind_Resource },
        { "Textures", UniversalId::Type_Textures, Source_Resources, Kind_Resource },
        { "Icons", UniversalId::Type_Icons, Source_Resources, Kind_Resource },
        { "Sounds (files)", UniversalId::Type_SoundsRes, Source_Resources, Kind_Resource },
        { "Music", UniversalId::Type_Musics, Source_Resources, Kind_Resource },
        { "Videos", UniversalId::Type_Videos, Source_Resources, Kind_Resource },
        { "Tables", UniversalId::Type_None, Source_Tables, Kind_Table },
    };

    // The tables the View menus open, in the menus' order.
    const UniversalId::Type tableTypes[] = {
        UniversalId::Type_Referenceables,
        UniversalId::Type_References,
        UniversalId::Type_Cells,
        UniversalId::Type_Lands,
        UniversalId::Type_LandTextures,
        UniversalId::Type_Pathgrids,
        UniversalId::Type_Regions,
        UniversalId::Type_RegionMap,
        UniversalId::Type_Scripts,
        UniversalId::Type_StartScripts,
        UniversalId::Type_Globals,
        UniversalId::Type_Gmsts,
        UniversalId::Type_Spells,
        UniversalId::Type_Enchantments,
        UniversalId::Type_MagicEffects,
        UniversalId::Type_Sounds,
        UniversalId::Type_SoundGens,
        UniversalId::Type_Skills,
        UniversalId::Type_Classes,
        UniversalId::Type_Factions,
        UniversalId::Type_Races,
        UniversalId::Type_Birthsigns,
        UniversalId::Type_BodyParts,
        UniversalId::Type_Topics,
        UniversalId::Type_Journals,
        UniversalId::Type_TopicInfos,
        UniversalId::Type_JournalInfos,
        UniversalId::Type_Filters,
        UniversalId::Type_DebugProfiles,
    };

    QIcon iconFor(UniversalId::Type type)
    {
        // No cache: a static QIcon cache would be destroyed after QApplication, and ScalableIcon::load is cheap.
        const std::string name = UniversalId(type).getIcon();
        if (name.empty() || name == ":placeholder")
            return QIcon();
        return Misc::ScalableIcon::load(QString::fromStdString(name));
    }

    bool isUnfilled(const QStandardItem* group)
    {
        return group->rowCount() == 1 && group->child(0)->data(roleKind).toInt() == Kind_Placeholder;
    }
}

/// QStandardItemModel whose drags carry TableMimeData, the type the viewport's drop handling already understands.
class CSVDoc::AssetBrowser::Model : public QStandardItemModel
{
public:
    explicit Model(const CSMDoc::Document& document, QObject* parent)
        : QStandardItemModel(parent)
        , mDocument(document)
    {
    }

    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        std::vector<UniversalId> ids;
        for (const QModelIndex& index : indexes)
            if (index.data(roleKind).toInt() == Kind_Record)
                ids.emplace_back(static_cast<UniversalId::Type>(index.data(roleType).toInt()),
                    index.data(roleId).toString().toUtf8().constData());
        if (ids.empty())
            return nullptr;
        return new CSMWorld::TableMimeData(ids, mDocument);
    }

private:
    const CSMDoc::Document& mDocument;
};

CSVDoc::AssetBrowser::AssetBrowser(CSMDoc::Document& document, QWidget* parent)
    : QWidget(parent)
    , mDocument(document)
    , mModel(new Model(document, this))
    , mProxy(new QSortFilterProxyModel(this))
    , mTree(new QTreeView(this))
    , mSearch(new QLineEdit(this))
    , mRefreshTimer(new QTimer(this))
{
    mSearch->setPlaceholderText(tr("Search assets and records"));
    mSearch->setClearButtonEnabled(true);

    mProxy->setSourceModel(mModel);
    mProxy->setRecursiveFilteringEnabled(true);
    mProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    mTree->setModel(mProxy);
    mTree->setHeaderHidden(true);
    mTree->setDragEnabled(true);
    mTree->setDragDropMode(QAbstractItemView::DragOnly);
    mTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTree->setUniformRowHeights(true);
    mTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTree->setObjectName("assetTree");
    mTree->setExpandsOnDoubleClick(false); // itemActivated opens or toggles, once

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(mSearch);
    layout->addWidget(mTree, 1);

    mRefreshTimer->setSingleShot(true);
    mRefreshTimer->setInterval(300);
    connect(mRefreshTimer, &QTimer::timeout, this, &AssetBrowser::refreshDirtyGroups);

    connect(mTree, &QTreeView::activated, this, &AssetBrowser::itemActivated);
    connect(mTree, &QTreeView::expanded, this, &AssetBrowser::groupExpanded);
    connect(mSearch, &QLineEdit::textChanged, this, &AssetBrowser::filterChanged);

    addGroups();

    // The files editor modules handle (task 0031), when any module registered an asset type.
    if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
        modules != nullptr && !modules->registry().assetTypes().empty())
    {
        QStandardItem* group = new QStandardItem(tr("Module assets"));
        group->setData(Kind_Group, roleKind);
        group->setData(static_cast<int>(UniversalId::Type_None), roleType);
        group->setData(static_cast<int>(Source_ModuleAssets), roleSource);
        group->setData(static_cast<int>(Kind_ModuleAsset), roleChildKind);
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QStandardItem* placeholder = new QStandardItem;
        placeholder->setData(Kind_Placeholder, roleKind);
        group->appendRow(placeholder);
        mModel->appendRow(group);
        // The list follows the workspace tab: one module's assets, or all of them (ModuleUi::setAssetOwnerFilter).
        connect(modules, &CSVModule::ModuleUi::assetOwnerFilterChanged, this, [this, group] { markDirty(group); });
    }
}

void CSVDoc::AssetBrowser::addGroups()
{
    for (const GroupDefinition& definition : groups)
    {
        QStandardItem* group = new QStandardItem(tr(definition.title));
        group->setData(Kind_Group, roleKind);
        group->setData(static_cast<int>(definition.type), roleType);
        group->setData(static_cast<int>(definition.source), roleSource);
        group->setData(static_cast<int>(definition.childKind), roleChildKind);
        if (definition.type != UniversalId::Type_None)
            group->setIcon(iconFor(definition.type));
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        QStandardItem* placeholder = new QStandardItem;
        placeholder->setData(Kind_Placeholder, roleKind);
        group->appendRow(placeholder);
        mModel->appendRow(group);

        // When the records of a group change, the group is rebuilt (if it was filled), a moment later so that
        // loading a file with thousands of records does it once.
        if (definition.source == Source_Records)
        {
            try
            {
                QAbstractItemModel* table = mDocument.getData().getTableModel(UniversalId(definition.type));
                auto changed = [this, group] { markDirty(group); };
                connect(table, &QAbstractItemModel::rowsInserted, this, changed);
                connect(table, &QAbstractItemModel::rowsRemoved, this, changed);
                connect(table, &QAbstractItemModel::modelReset, this, changed);
            }
            catch (const std::exception&)
            {
                // No such table in this document: the group stays empty.
            }
        }
    }
}

void CSVDoc::AssetBrowser::markDirty(QStandardItem* group)
{
    mDirty.insert(group);
    mRefreshTimer->start();
}

void CSVDoc::AssetBrowser::refreshDirtyGroups()
{
    const QSet<QStandardItem*> dirty = std::exchange(mDirty, {});
    for (QStandardItem* group : dirty)
        if (!isUnfilled(group)) // groups that were never opened fill when they are
            populate(group);
}

void CSVDoc::AssetBrowser::populate(QStandardItem* group)
{
    group->removeRows(0, group->rowCount());

    const auto type = static_cast<UniversalId::Type>(group->data(roleType).toInt());
    const auto source = static_cast<GroupSource>(group->data(roleSource).toInt());
    const auto childKind = static_cast<Kind>(group->data(roleChildKind).toInt());
    QList<QStandardItem*> children;

    auto makeChild = [&](const QString& text, UniversalId::Type childType, const QString& id) {
        QStandardItem* item = new QStandardItem(text);
        item->setData(childKind, roleKind);
        item->setData(static_cast<int>(childType), roleType);
        item->setData(id, roleId);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (childKind == Kind_Record)
            flags |= Qt::ItemIsDragEnabled;
        item->setFlags(flags);
        children.append(item);
    };

    try
    {
        if (source == Source_Records)
        {
            auto* table = dynamic_cast<CSMWorld::IdTableBase*>(mDocument.getData().getTableModel(UniversalId(type)));
            if (table != nullptr)
            {
                const int idColumn = table->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
                const int typeColumn = table->findColumnIndex(CSMWorld::Columns::ColumnId_RecordType);
                const int rows = table->rowCount();
                children.reserve(rows);
                for (int row = 0; row < rows; ++row)
                {
                    const QString id = table->data(table->index(row, idColumn)).toString();
                    auto recordType = type;
                    if (childKind == Kind_Record || childKind == Kind_OpenRecord)
                        recordType = static_cast<UniversalId::Type>(table->data(table->index(row, typeColumn)).toInt());
                    makeChild(id, recordType, id);
                }
            }
        }
        else if (source == Source_Resources)
        {
            const CSMWorld::Resources& resources = mDocument.getData().getResources(UniversalId(type));
            for (int i = 0; i < resources.getSize(); ++i)
            {
                const QString name = QString::fromStdString(resources.getId(i));
                makeChild(name, type, name);
            }
        }
        else if (source == Source_ModuleAssets)
        {
            if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get())
                for (const std::filesystem::path& path :
                    modules->findAssets(modules->assetRoots(), modules->assetOwnerFilter()))
                {
                    makeChild(QString::fromStdString(path.filename().string()), UniversalId::Type_None,
                        QString::fromStdString(path.string()));
                    children.last()->setToolTip(QString::fromStdString(path.string()));
                }
        }
        else
        {
            for (UniversalId::Type tableType : tableTypes)
            {
                makeChild(QString::fromStdString(UniversalId(tableType).getTypeName()), tableType, QString());
                children.last()->setIcon(iconFor(tableType));
            }
        }
    }
    catch (const std::exception&)
    {
        // A document without that table or resource list shows an empty group.
    }

    if (!children.isEmpty())
        group->appendRows(children);
}

void CSVDoc::AssetBrowser::groupExpanded(const QModelIndex& index)
{
    QStandardItem* item = mModel->itemFromIndex(mProxy->mapToSource(index));
    if (item != nullptr && item->data(roleKind).toInt() == Kind_Group && isUnfilled(item))
        populate(item);
}

void CSVDoc::AssetBrowser::populateAll()
{
    for (int row = 0; row < mModel->rowCount(); ++row)
        if (isUnfilled(mModel->item(row)))
            populate(mModel->item(row));
}

void CSVDoc::AssetBrowser::filterChanged(const QString& text)
{
    if (!text.isEmpty())
        populateAll(); // a search must look in every group
    mProxy->setFilterFixedString(text);
    if (text.isEmpty())
        mTree->collapseAll();
    else
        mTree->expandToDepth(0);
}

void CSVDoc::AssetBrowser::itemActivated(const QModelIndex& proxyIndex)
{
    const QModelIndex index = mProxy->mapToSource(proxyIndex);
    const auto kind = static_cast<Kind>(index.data(roleKind).toInt());
    const auto type = static_cast<UniversalId::Type>(index.data(roleType).toInt());
    const std::string id = index.data(roleId).toString().toUtf8().constData();

    switch (kind)
    {
        case Kind_Group:
            mTree->setExpanded(proxyIndex, !mTree->isExpanded(proxyIndex));
            break;
        case Kind_ModuleAsset:
            if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get())
                modules->openAsset(std::filesystem::path(index.data(roleId).toString().toStdString()));
            break;
        case Kind_Resource:
        {
            // A model opens in the mesh viewer (task 0043); the group's own entry and other files open the table.
            const QString lower = QString::fromStdString(id).toLower();
            if (type == UniversalId::Type_Meshes && (lower.endsWith(".nif") || lower.endsWith(".dae")))
            {
                emit openRequested(UniversalId(UniversalId::Type_Mesh, id), "");
                break;
            }
            emit openRequested(UniversalId(type), "");
            break;
        }
        case Kind_Table:
            emit openRequested(UniversalId(type), "");
            break;
        case Kind_Record:
        case Kind_OpenRecord:
            emit openRequested(UniversalId(type, id), "");
            break;
        case Kind_Cell:
        {
            // The same way the Cells table's "View" does it: the table says which scene and what to show.
            auto* table = dynamic_cast<CSMWorld::IdTable*>(mDocument.getData().getTableModel(UniversalId(type)));
            if (table == nullptr)
                break;
            const QModelIndex row = table->getModelIndex(id, table->findColumnIndex(CSMWorld::Columns::ColumnId_Id));
            if (!row.isValid())
                break;
            const auto [sceneId, hint] = table->view(row.row());
            if (sceneId.getType() != UniversalId::Type_None)
                emit openRequested(sceneId, hint);
            break;
        }
        default:
            break;
    }
}

int CSVDoc::AssetBrowser::visibleItemCount() const
{
    int count = 0;
    std::vector<QModelIndex> pending{ QModelIndex() };
    while (!pending.empty())
    {
        const QModelIndex parent = pending.back();
        pending.pop_back();
        for (int row = 0; row < mProxy->rowCount(parent); ++row)
        {
            ++count;
            pending.push_back(mProxy->index(row, 0, parent));
        }
    }
    return count;
}
