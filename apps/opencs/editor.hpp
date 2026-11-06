#ifndef CS_EDITOR_H
#define CS_EDITOR_H

#include <boost/program_options/variables_map.hpp>

#include <QLockFile>
#include <QObject>
#include <QString>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef Q_MOC_RUN
#include <components/files/configurationmanager.hpp>
#endif

#include <components/files/multidircollection.hpp>

#include "model/doc/documentmanager.hpp"
#include "model/extension/editorextensionhost.hpp"
#include "model/module/managersettingsstore.hpp"
#include "model/module/modulehost.hpp"
#include "model/module/moduleregistry.hpp"
#include "model/physics/studioworld.hpp"
#include "model/prefs/commandline.hpp"
#include "model/prefs/state.hpp"

#include "view/doc/filedialog.hpp"
#include "view/doc/newgame.hpp"
#include "view/doc/startup.hpp"
#include "view/module/moduleui.hpp"
#include "view/prefs/dialogue.hpp"
#include "view/render/physicshub.hpp"
#include "view/tools/merge.hpp"

class QLocalServer;
class QLocalSocket;

namespace CSMDoc
{
    class Document;
}

namespace CSVDoc
{
    class ViewManager;
}

namespace CS
{
    class Editor : public QObject, private CSMPhysics::StudioWorld::StepObserver
    {
        Q_OBJECT

        Files::ConfigurationManager mCfgMgr;
        boost::program_options::variables_map mConfigVariables;
        CSMPrefs::State mSettingsState;
        CSMDoc::DocumentManager mDocumentManager;
        CSVDoc::StartupDialogue mStartup;
        CSVDoc::NewGameDialogue mNewGame;
        CSVPrefs::Dialogue mSettings;
        CSVDoc::FileDialog mFileDialog;
        std::filesystem::path mLocal;
        std::filesystem::path mResources;
        std::filesystem::path mPid;
        QLockFile mLockFile;
        std::ofstream mPidFile;
        CSVTools::Merge mMerge;
        CSVDoc::ViewManager* mViewManager;
        // Eden Studio's one physics world (task 0030) and what draws and steps it. After the views, which point at
        // the hub; before the extension host, which reads the world and so must go first.
        CSMPhysics::StudioWorld mPhysicsWorld;
        CSVRender::PhysicsHub mPhysicsHub{ mPhysicsWorld };
        std::unique_ptr<CSMExtension::EditorExtensionHost> mExtensionHost; // after the views: they point at it
        // The editor modules (task 0031): the registry and the UI they talk to come first, the host that owns the
        // modules last, so that the modules go before the extensions whose manifests named some of them.
        CSMModule::ModuleRegistry mModuleRegistry;
        CSMModule::ManagerSettingsStore
            mModuleSettingsStore; ///< The modules' settings: [module.<id>] in eden-studio.cfg
        CSVModule::ModuleUi mModuleUi{ mModuleRegistry, [this] { return activeDocument(); } };
        CSMDoc::Document* activeDocument();
        std::unique_ptr<CSMModule::ModuleHost> mModuleHost;

        // CSMPhysics::StudioWorld::StepObserver: the extension host's phases run around each step.
        void beforeStep(float dt) override;
        void afterStep(float dt) override;
        std::filesystem::path mFileToLoad;
        Files::PathContainer mDataDirs;
        std::string mEncodingName;

        boost::program_options::variables_map readConfiguration(boost::program_options::variables_map variables);
        ///< Adds the config files below the command line's \a variables (mCfgMgr.readConfiguration); should be used
        ///< before initialization of mSettingsState as it depends on the configuration.
        std::pair<Files::PathContainer, std::vector<std::string>> readConfig(bool quiet = false);
        ///< \return data paths

        void startExtensionHost();
        ///< Needs mConfigVariables.

        void startModules(const Files::PathContainer& dataDirs);
        ///< After the extension host and the configuration, before the first view.

        void startUiCheck();

        // not implemented
        Editor(const Editor&);
        Editor& operator=(const Editor&);

    public:
        /// \a commandLine must have no errors (CSMPrefs::parseCommandLine, main.cpp).
        explicit Editor(CSMPrefs::CommandLine commandLine);
        ~Editor();

        bool makeIPCServer();
        void connectToIPCServer();

        int run();
        ///< \return error status

    private slots:

        void createGame();
        void createAddon();
        void cancelCreateGame();
        void cancelFileDialog();

        void loadDocument();
        void openFiles(
            const std::filesystem::path& path, const std::vector<std::filesystem::path>& discoveredFiles = {});
        void createNewFile(const std::filesystem::path& path);
        void createNewGame(const std::filesystem::path& file);

        void showStartup();

        void showSettings();

        void documentAdded(CSMDoc::Document* document);

        void documentAboutToBeRemoved(CSMDoc::Document* document);

        void lastDocumentDeleted();

        void mergeDocument(CSMDoc::Document* document);

    private:
        QString mIpcServerName;
        QLocalServer* mServer;
        QLocalSocket* mClientSocket;
    };
}

#endif
