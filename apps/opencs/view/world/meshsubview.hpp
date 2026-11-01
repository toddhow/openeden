#ifndef CSV_WORLD_MESHSUBVIEW_H
#define CSV_WORLD_MESHSUBVIEW_H

#include "../doc/subview.hpp"

#include <array>
#include <string>
#include <vector>

#include <apps/opencs/model/mesh/meshviewstate.hpp>
#include <apps/opencs/model/world/universalid.hpp>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace CSMDoc
{
    class Document;
}

namespace CSVRender
{
    class MeshViewWidget;
}

namespace CSVWorld
{
    /// The mesh viewer's sub view (task 0043): the 3D view with its view mode, lighting and overlay controls, and
    /// beside it the panels Nodes, Properties, Textures and Animation. Read only: nothing here changes the document.
    /// Ownership: a sub view of one document window; owns its widgets through Qt.
    class MeshSubView : public CSVDoc::SubView
    {
        Q_OBJECT

    public:
        MeshSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document);

        void setEditLock(bool locked) override {}
        std::string getTitle() const override;

        CSVRender::MeshViewWidget* viewWidget() const { return mView; }

        /// Used by unattended checks: acts as the control of that name would.
        void setViewMode(const std::string& name);

    private slots:
        void modelLoaded();
        void loadFailed(const QString& message);
        void controlsChanged();
        void treeItemChanged(QTreeWidgetItem* item, int column);
        void treeSelectionChanged();
        void nodePicked(int node);
        void filterChanged(const QString& text);
        void pickBackground();
        void saveScreenshot();
        void animationGroupChosen();
        void togglePlay();
        void addAnimationFile();
        void scrubbed(int value);
        void updateScrubber();

    private:
        void fillTree();
        void fillTextures();
        void fillAnimation();
        void selectItem(int node);
        CSMMesh::MeshViewState readControls() const;

        std::string mMesh;
        CSVRender::MeshViewWidget* mView;
        QComboBox* mMode;
        QComboBox* mLighting;
        QCheckBox* mCollision;
        QCheckBox* mBounds;
        QCheckBox* mSkeleton;
        QCheckBox* mAxes;
        QCheckBox* mGrid;
        QLabel* mStatus;
        QTabWidget* mTabs;
        QLineEdit* mFilter;
        QTreeWidget* mTree;
        QPlainTextEdit* mProperties;
        QTreeWidget* mTextures;
        QLabel* mTextureWarnings;
        QListWidget* mGroups;
        QPushButton* mPlay;
        QSlider* mScrub;
        QTimer* mScrubTimer;
        std::vector<QTreeWidgetItem*> mItems;
        std::array<float, 3> mBackground{ 0.16f, 0.18f, 0.22f };
        bool mFilling = false;
    };
}

#endif
