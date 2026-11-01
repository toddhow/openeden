#include "meshsubview.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/data.hpp>

#include "../render/meshscene.hpp"
#include "../render/meshviewwidget.hpp"

namespace CSVWorld
{
    namespace
    {
        constexpr int sNodeRole = Qt::UserRole + 1;
        constexpr int sScrubberSteps = 1000;
    }

    MeshSubView::MeshSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document)
        : SubView(id)
        , mMesh(id.getId())
    {
        mView = new CSVRender::MeshViewWidget(document.getData(), this);

        mMode = new QComboBox(this);
        for (const char* name : { "Textured", "Wireframe", "Flat", "Vertex colours", "Normals" })
            mMode->addItem(tr(name));
        mMode->setToolTip(tr("How the model is drawn. Everything but Textured is drawn from the model's geometry, "
                             "not with the game's materials."));
        mLighting = new QComboBox(this);
        for (const char* name : { "Headlight", "Sun", "Flat" })
            mLighting->addItem(tr(name));
        mCollision = new QCheckBox(tr("Collision"), this);
        mCollision->setToolTip(tr("Not built yet: the collision shape (task 0043, stage 5)."));
        mCollision->setEnabled(false);
        mBounds = new QCheckBox(tr("Bounds"), this);
        mSkeleton = new QCheckBox(tr("Skeleton"), this);
        mSkeleton->setToolTip(tr("Lines from each transform node to its parent transform."));
        mAxes = new QCheckBox(tr("Node axes"), this);
        mGrid = new QCheckBox(tr("Grid"), this);
        mGrid->setChecked(true);
        QPushButton* frame = new QPushButton(tr("Frame (F)"), this);
        QPushButton* background = new QPushButton(tr("Background..."), this);
        QPushButton* screenshot = new QPushButton(tr("Screenshot..."), this);
        mStatus = new QLabel(this);

        QHBoxLayout* controls = new QHBoxLayout;
        controls->addWidget(new QLabel(tr("View:"), this));
        controls->addWidget(mMode);
        controls->addWidget(new QLabel(tr("Light:"), this));
        controls->addWidget(mLighting);
        for (QWidget* widget : { static_cast<QWidget*>(mCollision), static_cast<QWidget*>(mBounds),
                 static_cast<QWidget*>(mSkeleton), static_cast<QWidget*>(mAxes), static_cast<QWidget*>(mGrid),
                 static_cast<QWidget*>(frame), static_cast<QWidget*>(background), static_cast<QWidget*>(screenshot) })
            controls->addWidget(widget);
        controls->addStretch(1);

        mTabs = new QTabWidget(this);
        // Nodes
        QWidget* nodes = new QWidget(mTabs);
        mFilter = new QLineEdit(nodes);
        mFilter->setPlaceholderText(tr("Filter by name or type"));
        mFilter->setClearButtonEnabled(true);
        mTree = new QTreeWidget(nodes);
        mTree->setColumnCount(2);
        mTree->setHeaderLabels({ tr("Node (tick = visible)"), tr("Type") });
        mTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        QVBoxLayout* nodesLayout = new QVBoxLayout(nodes);
        nodesLayout->setContentsMargins(0, 0, 0, 0);
        nodesLayout->addWidget(mFilter);
        nodesLayout->addWidget(mTree);
        mTabs->addTab(nodes, tr("Nodes"));
        // Properties
        mProperties = new QPlainTextEdit(mTabs);
        mProperties->setReadOnly(true);
        mProperties->setPlainText(tr("Nothing selected."));
        mTabs->addTab(mProperties, tr("Properties"));
        // Textures
        QWidget* textures = new QWidget(mTabs);
        mTextures = new QTreeWidget(textures);
        mTextures->setColumnCount(3);
        mTextures->setHeaderLabels({ tr("Texture"), tr("Size"), tr("In the VFS") });
        mTextures->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        mTextureWarnings = new QLabel(textures);
        mTextureWarnings->setWordWrap(true);
        QVBoxLayout* texturesLayout = new QVBoxLayout(textures);
        texturesLayout->setContentsMargins(0, 0, 0, 0);
        texturesLayout->addWidget(mTextures);
        texturesLayout->addWidget(mTextureWarnings);
        mTabs->addTab(textures, tr("Textures"));
        // Animation
        QWidget* animation = new QWidget(mTabs);
        mGroups = new QListWidget(animation);
        mPlay = new QPushButton(tr("Play"), animation);
        QPushButton* addKf = new QPushButton(tr("Add .kf from the VFS..."), animation);
        mScrub = new QSlider(Qt::Horizontal, animation);
        mScrub->setRange(0, sScrubberSteps);
        QVBoxLayout* animationLayout = new QVBoxLayout(animation);
        animationLayout->setContentsMargins(0, 0, 0, 0);
        animationLayout->addWidget(new QLabel(tr("Animation groups (from the text keys)"), animation));
        animationLayout->addWidget(mGroups);
        QHBoxLayout* animationButtons = new QHBoxLayout;
        animationButtons->addWidget(mPlay);
        animationButtons->addWidget(addKf);
        animationLayout->addLayout(animationButtons);
        animationLayout->addWidget(mScrub);
        mTabs->addTab(animation, tr("Animation"));

        QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(mView);
        splitter->addWidget(mTabs);
        splitter->setStretchFactor(0, 4);
        splitter->setStretchFactor(1, 1);
        mView->setMinimumSize(320, 240);

        QVBoxLayout* layout = new QVBoxLayout;
        layout->addLayout(controls);
        layout->addWidget(splitter, 1);
        layout->addWidget(mStatus);
        QWidget* widget = new QWidget;
        widget->setLayout(layout);
        setWidget(widget);

        mScrubTimer = new QTimer(this);
        mScrubTimer->setInterval(100);

        connect(mMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeshSubView::controlsChanged);
        connect(mLighting, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeshSubView::controlsChanged);
        for (QCheckBox* box : { mBounds, mSkeleton, mAxes, mGrid, mCollision })
            connect(box, &QCheckBox::toggled, this, &MeshSubView::controlsChanged);
        connect(frame, &QPushButton::clicked, mView, &CSVRender::MeshViewWidget::frameObject);
        connect(background, &QPushButton::clicked, this, &MeshSubView::pickBackground);
        connect(screenshot, &QPushButton::clicked, this, &MeshSubView::saveScreenshot);
        connect(mView, &CSVRender::MeshViewWidget::modelLoaded, this, &MeshSubView::modelLoaded);
        connect(mView, &CSVRender::MeshViewWidget::loadFailed, this, &MeshSubView::loadFailed);
        connect(mView, &CSVRender::MeshViewWidget::nodePicked, this, &MeshSubView::nodePicked);
        connect(mTree, &QTreeWidget::itemChanged, this, &MeshSubView::treeItemChanged);
        connect(mTree, &QTreeWidget::itemSelectionChanged, this, &MeshSubView::treeSelectionChanged);
        connect(mFilter, &QLineEdit::textChanged, this, &MeshSubView::filterChanged);
        connect(mGroups, &QListWidget::currentRowChanged, this, &MeshSubView::animationGroupChosen);
        connect(mPlay, &QPushButton::clicked, this, &MeshSubView::togglePlay);
        connect(addKf, &QPushButton::clicked, this, &MeshSubView::addAnimationFile);
        connect(mScrub, &QSlider::sliderMoved, this, &MeshSubView::scrubbed);
        connect(mScrubTimer, &QTimer::timeout, this, &MeshSubView::updateScrubber);

        mStatus->setText(tr("Loading %1...").arg(QString::fromStdString(mMesh)));
        mView->openMesh(mMesh);
    }

    std::string MeshSubView::getTitle() const
    {
        return "Mesh: " + mMesh;
    }

    void MeshSubView::setViewMode(const std::string& name)
    {
        if (auto mode = CSMMesh::parseViewMode(name))
            mMode->setCurrentIndex(static_cast<int>(*mode));
    }

    CSMMesh::MeshViewState MeshSubView::readControls() const
    {
        CSMMesh::MeshViewState state;
        state.mode = static_cast<CSMMesh::ViewMode>(mMode->currentIndex());
        state.lighting = static_cast<CSMMesh::LightingPreset>(mLighting->currentIndex());
        state.showCollision = mCollision->isChecked();
        state.showBounds = mBounds->isChecked();
        state.showSkeleton = mSkeleton->isChecked();
        state.showNodeAxes = mAxes->isChecked();
        state.showGrid = mGrid->isChecked();
        state.background = mBackground;
        return state;
    }

    void MeshSubView::controlsChanged()
    {
        mView->setViewState(readControls());
    }

    void MeshSubView::pickBackground()
    {
        const QColor start = QColor::fromRgbF(mBackground[0], mBackground[1], mBackground[2]);
        const QColor chosen = QColorDialog::getColor(start, this, tr("Background colour"));
        if (!chosen.isValid())
            return;
        mBackground = { static_cast<float>(chosen.redF()), static_cast<float>(chosen.greenF()),
            static_cast<float>(chosen.blueF()) };
        controlsChanged();
    }

    void MeshSubView::saveScreenshot()
    {
        const QString file = QFileDialog::getSaveFileName(this, tr("Save screenshot"), QString(), tr("PNG (*.png)"));
        if (file.isEmpty())
            return;
        if (!mView->saveScreenshot(file.toStdString()))
            mStatus->setText(tr("The screenshot could not be saved: no frame has been drawn yet."));
    }

    void MeshSubView::modelLoaded()
    {
        const CSVRender::MeshScene* scene = mView->scene();
        const auto totals = scene->tree().size() > 0 ? scene->tree().subtreeTotals(scene->tree().roots().front())
                                                       : CSMMesh::MeshTree::Totals{};
        mStatus->setText(tr("%1: %2 nodes, %3 vertices, %4 triangles")
                             .arg(QString::fromStdString(scene->path()))
                             .arg(totals.nodes)
                             .arg(totals.vertices)
                             .arg(totals.triangles));
        fillTree();
        fillTextures();
        fillAnimation();
        controlsChanged();
    }

    void MeshSubView::loadFailed(const QString& message)
    {
        mStatus->setText(tr("Could not open %1: %2").arg(QString::fromStdString(mMesh), message));
    }

    void MeshSubView::fillTree()
    {
        const CSMMesh::MeshTree& tree = mView->scene()->tree();
        mFilling = true;
        mTree->clear();
        mItems.assign(tree.size(), nullptr);
        for (CSMMesh::NodeId id : tree.depthFirst())
        {
            const CSMMesh::MeshNode& node = tree.node(id);
            QTreeWidgetItem* item = node.parent == CSMMesh::kNoNode ? new QTreeWidgetItem(mTree)
                                                                    : new QTreeWidgetItem(mItems[node.parent]);
            item->setText(0, node.name.empty() ? tr("<unnamed>") : QString::fromStdString(node.name));
            item->setText(1, QString::fromStdString(node.type));
            item->setData(0, sNodeRole, id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, node.visible ? Qt::Checked : Qt::Unchecked);
            mItems[id] = item;
        }
        mTree->expandToDepth(1);
        mFilling = false;
    }

    void MeshSubView::fillTextures()
    {
        mTextures->clear();
        const CSMMesh::TextureReport report = mView->scene()->textureReport();
        for (const CSMMesh::TextureEntry& entry : report.entries)
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(mTextures);
            item->setText(0, QString::fromStdString(entry.name));
            item->setText(1, entry.found && entry.width > 0 ? QString("%1 x %2").arg(entry.width).arg(entry.height)
                                                            : QString());
            item->setText(2, entry.found ? tr("yes") : tr("MISSING"));
            if (!entry.found)
                item->setForeground(2, QBrush(QColor(0xe0, 0x50, 0x50)));
        }
        QStringList warnings;
        for (const std::string& warning : report.warnings)
            warnings << QString::fromStdString(warning);
        mTextureWarnings->setText(warnings.join("\n"));
        mTabs->setTabText(2, report.warnings.empty() ? tr("Textures") : tr("Textures (!)"));
    }

    void MeshSubView::fillAnimation()
    {
        mGroups->clear();
        mScrubTimer->stop();
        mPlay->setText(tr("Play"));
        for (const CSMMesh::AnimationGroup& group : mView->scene()->animationGroups())
            mGroups->addItem(QString("%1  (%2 keys, %3 to %4 s)")
                                 .arg(QString::fromStdString(group.name))
                                 .arg(group.keys.size())
                                 .arg(group.start, 0, 'f', 2)
                                 .arg(group.stop, 0, 'f', 2));
    }

    void MeshSubView::filterChanged(const QString& text)
    {
        if (!mView->scene())
            return;
        const auto kept = mView->scene()->tree().filter(text.toStdString());
        std::vector<bool> keep(mItems.size(), false);
        for (CSMMesh::NodeId id : kept)
            keep[id] = true;
        for (std::size_t i = 0; i < mItems.size(); ++i)
            if (mItems[i])
                mItems[i]->setHidden(!keep[i]);
        if (!text.isEmpty())
            mTree->expandAll();
    }

    void MeshSubView::treeItemChanged(QTreeWidgetItem* item, int column)
    {
        if (mFilling || column != 0 || !mView->scene())
            return;
        mView->scene()->setNodeVisible(item->data(0, sNodeRole).toInt(), item->checkState(0) == Qt::Checked);
    }

    void MeshSubView::treeSelectionChanged()
    {
        if (mFilling || !mView->scene())
            return;
        const auto selected = mTree->selectedItems();
        const int node = selected.isEmpty() ? CSMMesh::kNoNode : selected.front()->data(0, sNodeRole).toInt();
        mView->selectNode(node);
        mProperties->setPlainText(QString::fromStdString(mView->scene()->describeNode(node)));
    }

    void MeshSubView::nodePicked(int node)
    {
        selectItem(node);
    }

    void MeshSubView::selectItem(int node)
    {
        if (node == CSMMesh::kNoNode || node < 0 || static_cast<std::size_t>(node) >= mItems.size())
        {
            mTree->clearSelection();
            return;
        }
        mTree->setCurrentItem(mItems[node]);
        mTree->scrollToItem(mItems[node]);
        mTabs->setCurrentIndex(1);
    }

    void MeshSubView::animationGroupChosen()
    {
        CSVRender::MeshScene* scene = mView->scene();
        const int row = mGroups->currentRow();
        if (!scene || row < 0)
            return;
        const auto groups = scene->animationGroups();
        if (static_cast<std::size_t>(row) >= groups.size())
            return;
        scene->playGroup(groups[row].name, 0.f);
        scene->setAnimationPaused(true);
        mPlay->setText(tr("Play"));
        mScrub->setValue(0);
    }

    void MeshSubView::togglePlay()
    {
        CSVRender::MeshScene* scene = mView->scene();
        if (!scene)
            return;
        if (scene->currentGroup().empty() && mGroups->count() > 0)
            mGroups->setCurrentRow(0);
        const bool nowPlaying = scene->animationPaused() && !scene->currentGroup().empty();
        scene->setAnimationPaused(!nowPlaying);
        mPlay->setText(nowPlaying ? tr("Pause") : tr("Play"));
        if (nowPlaying)
            mScrubTimer->start();
        else
            mScrubTimer->stop();
        mView->flagAsModified();
    }

    void MeshSubView::addAnimationFile()
    {
        CSVRender::MeshScene* scene = mView->scene();
        if (!scene)
            return;
        const QString file = QFileDialog::getOpenFileName(this, tr("Animation file in a data folder"), QString(),
            tr("Animations (*.kf *.nif *.dae)"));
        if (file.isEmpty())
            return;
        const int at = file.toLower().lastIndexOf("/meshes/");
        if (at < 0)
        {
            mStatus->setText(tr("The file is not inside a \"meshes\" folder of a data folder."));
            return;
        }
        std::string error;
        if (!scene->addAnimationFile(file.mid(at + 1).toStdString(), error))
            mStatus->setText(QString::fromStdString(error));
        fillAnimation();
    }

    void MeshSubView::scrubbed(int value)
    {
        CSVRender::MeshScene* scene = mView->scene();
        if (!scene || scene->currentGroup().empty())
            return;
        scene->playGroup(scene->currentGroup(), static_cast<float>(value) / sScrubberSteps);
        scene->setAnimationPaused(true);
        mPlay->setText(tr("Play"));
        mView->flagAsModified();
    }

    void MeshSubView::updateScrubber()
    {
        CSVRender::MeshScene* scene = mView->scene();
        if (scene && !scene->animationPaused() && !mScrub->isSliderDown())
            mScrub->setValue(static_cast<int>(scene->animationCompletion() * sScrubberSteps));
    }
}
