#ifndef OPENEDEN_LAUNCHER_MODSPAGE_H
#define OPENEDEN_LAUNCHER_MODSPAGE_H

#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

#include <components/modorganizer/conflicts.hpp>
#include <components/modorganizer/masters.hpp>
#include <components/modorganizer/profilestore.hpp>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QThread;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    /// The mod organizer page (task 0057): named profiles of mods, enable and load order, plugin order with master
    /// hints, a conflict view, a repack check, and applying the active profile to the user openmw.cfg. The page is
    /// a view on ModOrganizer::Store (components/modorganizer/profilestore): every rule lives there and is tested
    /// without Qt. Nothing here writes inside a mod folder or the game data; files are scanned off the GUI thread.
    class ModsPage : public QWidget
    {
        Q_OBJECT

    public:
        explicit ModsPage(const Files::ConfigurationManager& cfg, QWidget* parent = nullptr);
        ~ModsPage() override;

        /// Reads the library and profiles; on the first run makes a profile from the user openmw.cfg.
        void loadSettings();
        /// Writes the library and profiles (not openmw.cfg: that is the Apply button).
        void saveSettings();

        /// How the page learns that the game is running (it refuses to change openmw.cfg then). The default looks
        /// for a process named openmw; tests replace it.
        void setGameRunningCheck(std::function<bool()> check) { mGameRunning = std::move(check); }

    signals:
        /// openmw.cfg was rewritten from the active profile: the other pages should read it again.
        void profileApplied();

    private slots:
        void onProfileChosen(int index);
        void newProfile();
        void copyProfile();
        void renameProfile();
        void deleteProfile();
        void addFolder();
        void addPack();
        void removeSelectedMod();
        void moveModUp();
        void moveModDown();
        void movePluginUp();
        void movePluginDown();
        void addNewPlugins();
        void removeMissingPlugins();
        void sortByMasters();
        void scanConflicts();
        void applyConflictFilter();
        void checkRepack();
        void applyProfile();
        void onModItemChanged();
        void onPluginItemChanged();
        void onModSelectionChanged();

    private:
        void buildUi();
        QWidget* buildModsTab();
        QWidget* buildPluginsTab();
        QWidget* buildConflictsTab();
        QWidget* buildRepackTab();

        void refreshAll();
        void refreshProfiles();
        void refreshMods();
        void refreshPlugins();
        void refreshApplyState();
        void refreshRepackChoices();
        void startPluginReport();
        void changed(); // saves the store and refreshes what depends on it
        bool saveNow();
        void setBusy(bool busy, const QString& what = QString());
        void showResult(const QString& text, bool problem);
        void updateNotice();
        void moveMod(int delta);
        void movePlugin(int delta);
        int selectedModIndex() const; // index into the profile's mods, -1 for none or a base row
        int selectedPluginIndex() const;
        QString modName(const QString& id) const;
        bool askName(const QString& title, const QString& label, QString& name, const QString& initial = QString());
        bool doApply(bool acceptHandEdits);

        const Files::ConfigurationManager& mCfgMgr;
        ModOrganizer::StorePaths mPaths;
        ModOrganizer::Store mStore;

        QComboBox* mProfileBox = nullptr;
        QPushButton* mCopyProfile = nullptr;
        QPushButton* mRenameProfile = nullptr;
        QPushButton* mDeleteProfile = nullptr;
        QLabel* mNotice = nullptr;
        QString mResult; // the last message; kept across a reload of the page
        QTabWidget* mTabs = nullptr;

        QTableWidget* mModsTable = nullptr;
        QPushButton* mRemoveMod = nullptr;
        QPushButton* mModUp = nullptr;
        QPushButton* mModDown = nullptr;

        QTableWidget* mPluginsTable = nullptr;
        QPushButton* mPluginUp = nullptr;
        QPushButton* mPluginDown = nullptr;
        QPushButton* mSortMasters = nullptr;

        QPushButton* mScanConflicts = nullptr;
        QLabel* mConflictStatus = nullptr;
        QTableWidget* mConflictCounts = nullptr;
        QLineEdit* mConflictFilter = nullptr;
        QTableWidget* mConflictFiles = nullptr;
        ModOrganizer::ConflictReport mConflicts;

        QComboBox* mRepackChoice = nullptr;
        QPushButton* mRepackCheck = nullptr;
        QPlainTextEdit* mRepackOutput = nullptr;

        QLabel* mApplyState = nullptr;
        QPushButton* mApply = nullptr;

        ModOrganizer::PluginReport mReport;
        std::vector<ModOrganizer::Hint> mHints;
        unsigned mReportGeneration = 0;
        bool mRefreshing = false;
        bool mResultIsBusy = false; // mResult says what a running job does
        bool mLoaded = false;
        bool mSaveFailed = false; // the last save failed: saveSettings tries again
        int mBusy = 0;
        QList<QThread*> mThreads;
        std::function<bool()> mGameRunning;
    };
}

#endif
