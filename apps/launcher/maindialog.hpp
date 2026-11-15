#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#ifndef Q_MOC_RUN
#include <components/modorganizer/games.hpp>

#include <QComboBox>

#include <string>
#include <vector>

#include <components/process/processinvoker.hpp>

#include <components/config/gamesettings.hpp>
#include <components/config/launchersettings.hpp>

#endif
#include "ui_mainwindow.h"

class QListWidgetItem;
class QStackedWidget;
class QStringListModel;
class QString;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    class GraphicsPage;
    class DataFilesPage;
    class UnshieldThread;
    class ImportPage;
    class ExtensionsPage;
    class ModsPage;
    class WorldLodPage;
    class SettingsPage;

    enum FirstRunDialogResult
    {
        FirstRunDialogResultFailure,
        FirstRunDialogResultContinue,
        FirstRunDialogResultWizard
    };

#ifndef WIN32
    bool expansions(Launcher::UnshieldThread& cd);
#endif

    class MainDialog : public QMainWindow, private Ui::MainWindow
    {
        Q_OBJECT

    public:
        explicit MainDialog(const Files::ConfigurationManager& configurationManager, QWidget* parent = nullptr);

        /// Task 0173: the launcher's own command line; --config, --replace, --user-data and --resources are handed
        /// to the programs it starts (ModOrganizer::forwardedConfigArguments).
        void setCommandLine(std::vector<std::string> arguments) { mCommandLine = std::move(arguments); }
        ~MainDialog() override;

        FirstRunDialogResult showFirstRunDialog();

        bool reloadSettings();
        /// After the Mods page rewrote openmw.cfg: reads the game settings again and refreshes the Data Files and
        /// Extensions pages. Graphics and other pending changes are left alone (reloadSettings would drop them).
        bool reloadGameSettings();
        bool writeSettings();

    public slots:
        void enableDataPage();
        void enableGraphicsPage();
        void enableSettingsPage();
        void enableImportPage();
        void enableExtensionsPage();
        void enableModsPage();
        void enableWorldLodPage();
        void play();
        void help();
        void startEditor();
        void onGameSelected(int index);

    protected:
        bool event(QEvent* event) override;

    private slots:
        void wizardStarted();
        void wizardFinished(int exitCode, QProcess::ExitStatus exitStatus);

    private:
        bool setup();

        void createIcons();
        void createPages();

        bool setupLauncherSettings();
        bool setupGameSettings();
        bool setupGraphicsSettings();
        bool setupGameData();

        void setVersionLabel();

        void loadSettings();
        void saveSettings();

        inline bool startProgram(const QString& name, bool detached = false)
        {
            return startProgram(name, QStringList(), detached);
        }
        bool startProgram(const QString& name, const QStringList& arguments, bool detached = false);

        void closeEvent(QCloseEvent* event) override;

        GraphicsPage* mGraphicsPage;
        DataFilesPage* mDataFilesPage;
        ImportPage* mImportPage;
        SettingsPage* mSettingsPage;
        ExtensionsPage* mExtensionsPage;
        ModsPage* mModsPage;
        WorldLodPage* mWorldLodPage;

        Process::ProcessInvoker* mGameInvoker;
        Process::ProcessInvoker* mEditorInvoker;
        /// Task 0173: the game the launcher's programs run (it writes game= with the game's profile)
        QComboBox* mGameBox = nullptr;
        std::vector<ModOrganizer::GameEntry> mGames;
        void refreshGames();
        void selectGameInBox(const QString& gameId);
        bool startForwarded(Process::ProcessInvoker* invoker, const QString& name);
        Process::ProcessInvoker* mWizardInvoker;

        const Files::ConfigurationManager& mCfgMgr;

        Config::GameSettings mGameSettings;
        Config::LauncherSettings mLauncherSettings;
        std::vector<std::string> mCommandLine;
    };
}
#endif
