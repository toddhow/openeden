#ifndef CSV_DOC_ASSETBROWSER_H
#define CSV_DOC_ASSETBROWSER_H

#include <QSet>
#include <QWidget>

#include <string>

#include <apps/opencs/model/world/universalid.hpp>

class QLineEdit;
class QModelIndex;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;
class QTimer;
class QTreeView;

namespace CSMDoc
{
    class Document;
}

namespace CSVDoc
{
    /// The left panel of Eden Studio (plan section 2.1, item 3): the content as the editor sees it, as one tree with
    /// a search box.
    ///
    /// Groups: Cells, Objects (referenceables), Scripts (the record collections), the asset folders of the virtual
    /// file system (meshes, textures, icons, sounds, music, videos), and Tables (one entry for every table the
    /// menus open). A group is filled the first time it is opened or searched. Double click opens the existing
    /// table or editor through openRequested (a cell opens in a 3D scene). Dragging an object into the viewport
    /// places it: the drag carries the same CSMWorld::TableMimeData as a drag out of the Objects table.
    ///
    /// The browser knows no feature formats (plan section 2.1): extensions' editor modules will add groups later.
    ///
    /// Ownership: a child of the dock that shows it. It reads the document's tables; it never changes them.
    class AssetBrowser : public QWidget
    {
        Q_OBJECT

    public:
        explicit AssetBrowser(CSMDoc::Document& document, QWidget* parent = nullptr);

        /// The tree's visible (after filtering) item count, for checks that need no window.
        int visibleItemCount() const;

    signals:
        /// \param hint What a sub view takes as its hint (a scene is told which cell to show).
        void openRequested(const CSMWorld::UniversalId& id, const std::string& hint);

    private slots:
        void itemActivated(const QModelIndex& index);
        void groupExpanded(const QModelIndex& index);
        void filterChanged(const QString& text);
        void refreshDirtyGroups();

    private:
        class Model;

        void addGroups();
        void populate(QStandardItem* group);
        void markDirty(QStandardItem* group);
        void populateAll();

        CSMDoc::Document& mDocument;
        Model* mModel;
        QSortFilterProxyModel* mProxy;
        QTreeView* mTree;
        QLineEdit* mSearch;
        QTimer* mRefreshTimer;
        QSet<QStandardItem*> mDirty;
    };
}

#endif
