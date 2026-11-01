#ifndef CSV_DOC_STUDIOSHELL_H
#define CSV_DOC_STUDIOSHELL_H

#include <vector>

#include <QObject>
#include <QPointer>

#include <apps/opencs/model/prefs/layoutstore.hpp>

class QDockWidget;
class QMainWindow;
class QTabBar;
class QToolButton;
class QWidget;

namespace CSVDoc
{
    class ViewportBackdrop;

    /// The Eden Studio arrangement of one document window (task 0024, stage B; plan section 2).
    ///
    /// A document window (CSVDoc::View) is a QMainWindow. In the Eden Studio layout the shell adds docks around
    /// the sub view area: the Asset Browser on the left, the Context Inspector on the right, and a bottom row
    /// (workspace tabs, Timeline and Transport, Output Log). The sub view area itself is a second QMainWindow
    /// (View::mSubViewWindow). The shell gives it a backdrop as its centre widget, so that a viewport is always
    /// there, and View puts 3D scenes above and the record tables below.
    ///
    /// Ownership: the docks are children of the window and are deleted with it. The shell is a child of the
    /// window as well. It does not own the sub view window.
    ///
    /// Classic keeps the old arrangement: the shell hides its docks and takes the backdrop away.
    class StudioShell : public QObject
    {
        Q_OBJECT

    public:
        /// \param window The document window. \param subViewWindow Its sub view area.
        StudioShell(QMainWindow& window, QMainWindow& subViewWindow, CSMPrefs::LayoutStore store);

        CSMPrefs::EditorLayout layout() const { return mLayout; }
        bool isStudio() const { return mLayout == CSMPrefs::EditorLayout::EdenStudio; }

        /// Arranges the window for the remembered layout. Call it once the window has its operations dock.
        void start();

        /// Applies \a layout, saving the dock state of the layout being left, and remembers the choice.
        void setLayout(CSMPrefs::EditorLayout layout);

        /// Forgets the saved docks of the current layout and arranges them as on a first run.
        void resetLayout();

        /// Writes the docks' state to the layout file. Called when the window closes and on a switch.
        void saveDockState();

        /// Shows the backdrop while no 3D scene is open, hides it (so the scene takes its place) otherwise.
        void setSceneOpen(bool open);

        /// For the Window menu's panel toggles; empty in no layout, the docks exist always.
        QDockWidget* assetBrowser() const { return mAssetBrowser; }
        QDockWidget* contextInspector() const { return mInspector; }
        QDockWidget* outputLog() const { return mOutputLog; }
        QDockWidget* transport() const { return mTransport; }
        QDockWidget* workspaces() const { return mWorkspaces; }

        /// The dock that already existed before the shell (the progress bars). Optional.
        void setOperationsDock(QDockWidget* dock) { mOperations = dock; }

        /// The sub view area's centre widget when it is not a 3D scene (see setSceneOpen).
        QWidget* backdrop() const;

        std::vector<QDockWidget*> panels() const;

    signals:
        void layoutChanged(CSMPrefs::EditorLayout layout);

    private:
        QDockWidget* makeDock(const QString& title, const QString& objectName, QWidget* content);
        void arrangeDefault();
        void applyLayout(CSMPrefs::EditorLayout layout);

        QMainWindow& mWindow;
        QMainWindow& mSubViewWindow;
        CSMPrefs::LayoutStore mStore;
        CSMPrefs::EditorLayout mLayout;
        bool mSceneOpen = false;

        ViewportBackdrop* mBackdrop; // owned by the sub view window while it is its centre widget, else by us
        QDockWidget* mAssetBrowser;
        QDockWidget* mInspector;
        QDockWidget* mWorkspaces;
        QDockWidget* mTransport;
        QDockWidget* mOutputLog;
        QDockWidget* mOperations = nullptr;
    };
}

#endif
