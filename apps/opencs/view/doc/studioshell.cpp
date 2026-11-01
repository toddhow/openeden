#include "studioshell.hpp"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMainWindow>
#include <QPainter>
#include <QSlider>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace CSVDoc
{
    /// What the viewport area shows before any cell is open: an empty gradient with a hint.
    class ViewportBackdrop : public QWidget
    {
    public:
        explicit ViewportBackdrop(QWidget* parent)
            : QWidget(parent)
        {
            setObjectName("viewportBackdrop");
            setMinimumSize(200, 120);
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter painter(this);
            // Colours are fixed, not taken from the theme: the area stands for the (dark) 3D view.
            QLinearGradient gradient(0, 0, 0, height());
            gradient.setColorAt(0.0, QColor(0x2b, 0x36, 0x4a));
            gradient.setColorAt(1.0, QColor(0x12, 0x16, 0x1f));
            painter.fillRect(rect(), gradient);
            painter.setPen(QColor(0xb8, 0xc2, 0xd6));
            painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                tr("Viewport\n\nOpen a cell (double-click it in the Asset Browser, or use View > World > Cells) to "
                   "see it here."));
        }
    };

    namespace
    {
        const char* const transportTip
            = "Play, Pause and Step drive the physics world of the editor. That world is "
              "not built yet (task 0024, stage E), so they stay disabled.";

        QWidget* makeTransportRow(QWidget* parent)
        {
            QWidget* row = new QWidget(parent);
            QHBoxLayout* layout = new QHBoxLayout(row);
            layout->setContentsMargins(4, 2, 4, 2);

            for (const char* text : { "Play", "Pause", "Step" })
            {
                QToolButton* button = new QToolButton(row);
                button->setText(QObject::tr(text));
                button->setObjectName(QString("transport") + text);
                button->setEnabled(false);
                button->setToolTip(QObject::tr(transportTip));
                layout->addWidget(button);
            }

            QSlider* timeline = new QSlider(Qt::Horizontal, row);
            timeline->setObjectName("timeline");
            timeline->setEnabled(false);
            timeline->setToolTip(QObject::tr("Timeline: scrubbing is planned, nothing records snapshots yet."));
            layout->addWidget(timeline, 1);
            return row;
        }

        QWidget* makeWorkspaceTabs(QWidget* parent)
        {
            QTabBar* tabs = new QTabBar(parent);
            tabs->setObjectName("workspaceTabs");
            tabs->setExpanding(false);
            tabs->setDrawBase(true);
            // The only built-in workspace. Extensions' editor modules add tabs here (plan section 3.2).
            tabs->addTab(QObject::tr("World && Scene"));
            return tabs;
        }

        QWidget* makePlaceholder(const QString& text, QWidget* parent)
        {
            QLabel* label = new QLabel(text, parent);
            label->setAlignment(Qt::AlignCenter);
            label->setWordWrap(true);
            return label;
        }
    }
}

CSVDoc::StudioShell::StudioShell(QMainWindow& window, QMainWindow& subViewWindow, CSMPrefs::LayoutStore store)
    : QObject(&window)
    , mWindow(window)
    , mSubViewWindow(subViewWindow)
    , mStore(std::move(store))
    , mLayout(mStore.layout())
    , mBackdrop(new ViewportBackdrop(&subViewWindow))
{
    mAssetBrowser = makeDock(tr("Asset Browser"), "assetBrowser", makePlaceholder(tr("Assets"), &window));
    mInspector
        = makeDock(tr("Context Inspector"), "contextInspector", makePlaceholder(tr("Nothing selected"), &window));
    mWorkspaces = makeDock(tr("Workspaces"), "workspaces", makeWorkspaceTabs(&window));
    mTransport = makeDock(tr("Timeline and Transport"), "transport", makeTransportRow(&window));
    mOutputLog = makeDock(tr("Output Log"), "outputLog", makePlaceholder(tr("Output"), &window));

    // The workspace strip is a bar, not a panel: no title, no moving, no closing.
    mWorkspaces->setFeatures(QDockWidget::NoDockWidgetFeatures);
    mWorkspaces->setTitleBarWidget(new QWidget(mWorkspaces));
}

QDockWidget* CSVDoc::StudioShell::makeDock(const QString& title, const QString& objectName, QWidget* content)
{
    QDockWidget* dock = new QDockWidget(title, &mWindow);
    // QMainWindow::saveState needs a name for every dock to write and restore it.
    dock->setObjectName(objectName);
    dock->setWidget(content);
    return dock;
}

QWidget* CSVDoc::StudioShell::backdrop() const
{
    return mBackdrop;
}

std::vector<QDockWidget*> CSVDoc::StudioShell::panels() const
{
    return { mAssetBrowser, mInspector, mTransport, mOutputLog };
}

void CSVDoc::StudioShell::arrangeDefault()
{
    // Remove first, so that this also works for a window whose docks were moved around by the user.
    for (QDockWidget* dock : { mAssetBrowser, mInspector, mWorkspaces, mTransport, mOutputLog, mOperations })
        if (dock != nullptr)
            mWindow.removeDockWidget(dock);

    mWindow.addDockWidget(Qt::LeftDockWidgetArea, mAssetBrowser);
    mWindow.addDockWidget(Qt::RightDockWidgetArea, mInspector);

    // Bottom row, top to bottom: workspace tabs, then the transport row beside the output log, then the progress
    // bars of running operations.
    mWindow.addDockWidget(Qt::BottomDockWidgetArea, mWorkspaces);
    mWindow.splitDockWidget(mWorkspaces, mTransport, Qt::Vertical);
    mWindow.splitDockWidget(mTransport, mOutputLog, Qt::Horizontal);
    if (mOperations != nullptr)
        mWindow.splitDockWidget(mTransport, mOperations, Qt::Vertical);

    for (QDockWidget* dock : { mAssetBrowser, mInspector, mWorkspaces, mTransport, mOutputLog })
        dock->show();
    if (mOperations != nullptr)
        mOperations->show();

    mWindow.resizeDocks({ mAssetBrowser, mInspector }, { 280, 300 }, Qt::Horizontal);
    mWindow.resizeDocks({ mWorkspaces, mTransport }, { 30, 170 }, Qt::Vertical);
    mWindow.resizeDocks({ mTransport, mOutputLog }, { 380, 820 }, Qt::Horizontal);
}

void CSVDoc::StudioShell::applyLayout(CSMPrefs::EditorLayout layout)
{
    mLayout = layout;
    const QByteArray saved = mStore.dockState(layout);

    if (layout == CSMPrefs::EditorLayout::EdenStudio)
    {
        if (mSubViewWindow.centralWidget() != mBackdrop)
            mSubViewWindow.setCentralWidget(mBackdrop);
        mBackdrop->setVisible(!mSceneOpen);

        arrangeDefault();
        if (!saved.isEmpty())
            mWindow.restoreState(saved);
    }
    else
    {
        // The old arrangement: no docks of ours, and nothing in the middle of the sub view area. Operations is a
        // dock the window always had, so it goes back to the bottom area alone.
        for (QDockWidget* dock : { mAssetBrowser, mInspector, mWorkspaces, mTransport, mOutputLog })
        {
            mWindow.removeDockWidget(dock);
            dock->hide();
        }
        if (mOperations != nullptr)
        {
            mWindow.removeDockWidget(mOperations);
            mWindow.addDockWidget(Qt::BottomDockWidgetArea, mOperations);
            mOperations->show();
        }
        if (mSubViewWindow.centralWidget() == mBackdrop)
            mSubViewWindow.takeCentralWidget();
        // takeCentralWidget leaves the widget without a parent; give it back so that it is deleted with the window.
        mBackdrop->setParent(&mSubViewWindow);
        mBackdrop->hide();

        if (!saved.isEmpty())
            mWindow.restoreState(saved);
    }
}

void CSVDoc::StudioShell::start()
{
    applyLayout(mLayout);
}

void CSVDoc::StudioShell::setLayout(CSMPrefs::EditorLayout layout)
{
    if (layout == mLayout)
        return;

    saveDockState();
    applyLayout(layout);
    mStore.setLayout(layout);
    emit layoutChanged(layout);
}

void CSVDoc::StudioShell::resetLayout()
{
    mStore.clearDockState(mLayout);
    applyLayout(mLayout);
}

void CSVDoc::StudioShell::saveDockState()
{
    mStore.setDockState(mLayout, mWindow.saveState());
}

void CSVDoc::StudioShell::setSceneOpen(bool open)
{
    mSceneOpen = open;
    if (isStudio())
        mBackdrop->setVisible(!open);
}
