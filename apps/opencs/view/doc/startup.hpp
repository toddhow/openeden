#ifndef CSV_DOC_STARTUP_H
#define CSV_DOC_STARTUP_H

#include <QWidget>

class QGridLayout;
class QString;
class QPushButton;
class QWidget;
class QIcon;

namespace CSVDoc
{
    class StartupDialogue : public QWidget
    {
        Q_OBJECT

    private:
        int mWidth;
        int mColumn;
        QGridLayout* mLayout;

        QPushButton* addButton(const QString& label, const QString& icon);

        QWidget* createButtons();

        QWidget* createTools();

        QPushButton* mLoadDocument = nullptr;
        QPushButton* mCreateAddon = nullptr;
        QPushButton* mCreateGame = nullptr;
        QPushButton* mBrowseWorld = nullptr;

    public:
        StartupDialogue();

        /// The session's game pack is not Morrowind (task 0173): the three content file buttons are disabled, since
        /// the editor edits only Morrowind records, and "Browse the world" opens a cell of the pack's world when it
        /// registered one. Never called for Morrowind, so the dialogue is as before.
        void setGamePack(const QString& name, bool worldAvailable);

    signals:

        void browseWorld();

        void createGame();

        void createAddon();

        void loadDocument();

        void editConfig();
    };
}

#endif
