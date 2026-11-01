#ifndef CSV_DOC_EXTENSIONMANAGER_H
#define CSV_DOC_EXTENSIONMANAGER_H

#include <QDialog>

class QTreeWidget;

namespace CSMExtension
{
    class EditorExtensionHost;
}

namespace CSVDoc
{
    /// The Extension Manager (plan section 2.1, item 8; task 0024, stage D): a window listing every extension the
    /// editor's host found, whether it is enabled, whether it is active, and why it failed if it did.
    ///
    /// Read-only in this stage. Which extensions load is decided by `extension=` lines in openmw.cfg (and
    /// --extension), the same list the game uses, and changes take effect when the editor starts; the window says
    /// so. A window, not a dock, so that it is the same in both layouts.
    ///
    /// Ownership: the host is owned by the editor and must outlive the dialog (it does: the dialog is a child of a
    /// document window, which closes first).
    class ExtensionManagerDialog : public QDialog
    {
        Q_OBJECT

    public:
        ExtensionManagerDialog(const CSMExtension::EditorExtensionHost* host, QWidget* parent = nullptr);

        /// Reads the host again (it does not change while the editor runs, so this is for the first show).
        void refresh();

    private:
        const CSMExtension::EditorExtensionHost* mHost;
        QTreeWidget* mList;
    };
}

#endif
