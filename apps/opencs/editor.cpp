#include "editor.hpp"

#include "model/prefs/state.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QSpinBox>
#include <optional>
#include <set>

#include <QListWidget>
#include <QMouseEvent>
#include <QTabBar>
#include <QTimer>
#include <QTreeView>
#include <QUndoStack>
#include <QWidget>

#include <boost/program_options.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <variant>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/documentmanager.hpp>
#include <apps/opencs/view/doc/adjusterwidget.hpp>
#include <apps/opencs/view/doc/curvestrip.hpp>
#include <apps/opencs/view/doc/filedialog.hpp>
#include <apps/opencs/view/doc/newgame.hpp>
#include <apps/opencs/view/doc/startup.hpp>
#include <apps/opencs/view/prefs/dialogue.hpp>
#include <apps/opencs/view/tools/merge.hpp>

#ifdef _WIN32
#include <components/misc/windows.hpp>
#endif

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loadtes3.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/qtconversion.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>
#include <components/toutf8/toutf8.hpp>

#include "model/extension/editorextensionhost.hpp"
#include "model/prefs/configmigration.hpp"
#include "model/world/columns.hpp"
#include "model/world/data.hpp"
#include "model/world/idtable.hpp"
#include "model/world/tablemimedata.hpp"
#include "model/world/universalid.hpp"
#include "view/doc/assetbrowser.hpp"
#include "view/doc/contextinspector.hpp"
#include "view/doc/logtap.hpp"
#include "view/doc/subview.hpp"
#include "view/doc/viewmanager.hpp"
#include "view/module/moduleui.hpp"
#include "view/prefs/dialogue.hpp"
#include "view/render/meshviewwidget.hpp"
#include "view/render/physicshub.hpp"
#include "view/render/scenehost.hpp"
#include "view/render/worldspacewidget.hpp"
#include "view/world/scenesubview.hpp"

CS::Editor::Editor(CSMPrefs::CommandLine commandLine)
    : mConfigVariables(readConfiguration(std::move(commandLine.mVariables)))
    , mSettingsState(mCfgMgr)
    , mDocumentManager(mCfgMgr)
    , mPid(std::filesystem::temp_directory_path() / "eden-studio.pid")
    , mLockFile(QFileInfo(Files::pathToQString(mPid)).absoluteFilePath() + ".lock")
    , mMerge(mDocumentManager)
    , mIpcServerName("org.openeden.EdenStudio")
    , mServer(nullptr)
    , mClientSocket(nullptr)
{
    // Copy everything logged to the Output Log panel (the log file and console get it unchanged).
    CSVDoc::LogTap::install();

    startExtensionHost();

    std::pair<Files::PathContainer, std::vector<std::string>> config = readConfig();

    startModules(config.first);

    mViewManager = new CSVDoc::ViewManager(mDocumentManager);
    mViewManager->setExtensionHost(mExtensionHost.get());
    if (!commandLine.mFileToLoad.empty())
    {
        mFileToLoad = commandLine.mFileToLoad;
        mDataDirs = config.first;
    }

    mDocumentManager.setFileData(config.first, config.second);

    startUiCheck();

    mNewGame.setLocalData(mLocal);
    mFileDialog.setLocalData(mLocal);
    mMerge.setLocalData(mLocal);

    connect(&mDocumentManager, &CSMDoc::DocumentManager::documentAdded, this, &Editor::documentAdded);
    connect(
        &mDocumentManager, &CSMDoc::DocumentManager::documentAboutToBeRemoved, this, &Editor::documentAboutToBeRemoved);
    connect(&mDocumentManager, &CSMDoc::DocumentManager::lastDocumentDeleted, this, &Editor::lastDocumentDeleted);

    connect(mViewManager, &CSVDoc::ViewManager::newGameRequest, this, &Editor::createGame);
    connect(mViewManager, &CSVDoc::ViewManager::newAddonRequest, this, &Editor::createAddon);
    connect(mViewManager, &CSVDoc::ViewManager::loadDocumentRequest, this, &Editor::loadDocument);
    connect(mViewManager, &CSVDoc::ViewManager::editSettingsRequest, this, &Editor::showSettings);
    connect(mViewManager, &CSVDoc::ViewManager::mergeDocument, this, &Editor::mergeDocument);

    connect(&mStartup, &CSVDoc::StartupDialogue::createGame, this, &Editor::createGame);
    connect(&mStartup, &CSVDoc::StartupDialogue::createAddon, this, &Editor::createAddon);
    connect(&mStartup, &CSVDoc::StartupDialogue::loadDocument, this, &Editor::loadDocument);
    connect(&mStartup, &CSVDoc::StartupDialogue::editConfig, this, &Editor::showSettings);

    connect(&mFileDialog, &CSVDoc::FileDialog::signalOpenFiles, this,
        [this](const std::filesystem::path& savePath) { this->openFiles(savePath); });
    connect(&mFileDialog, &CSVDoc::FileDialog::signalCreateNewFile, this, &Editor::createNewFile);
    connect(&mFileDialog, &CSVDoc::FileDialog::rejected, this, &Editor::cancelFileDialog);

    connect(&mNewGame, &CSVDoc::NewGameDialogue::createRequest, this, &Editor::createNewGame);
    connect(&mNewGame, &CSVDoc::NewGameDialogue::cancelCreateGame, this, &Editor::cancelCreateGame);
}

void CS::Editor::beforeStep(float dt)
{
    if (mExtensionHost)
        mExtensionHost->beforeStep(dt);
}

void CS::Editor::afterStep(float dt)
{
    if (mExtensionHost)
        mExtensionHost->afterStep(dt);
}

CSMDoc::Document* CS::Editor::activeDocument()
{
    return mViewManager != nullptr ? mViewManager->activeDocument() : nullptr;
}

void CS::Editor::startModules(const Files::PathContainer& dataDirs)
{
    // The modules' settings are kept in eden-studio.cfg, under [module.<id>] (API 1.2, task 0091).
    mModuleRegistry.settings().setStore(&mModuleSettingsStore);
    mModuleHost = std::make_unique<CSMModule::ModuleHost>(mModuleRegistry, mModuleUi);

    // Core systems' modules first (decision 0007), then the extensions' (their manifests' "editor" entries).
    std::vector<CSMModule::ModuleSource> sources = CSMModule::coreModuleSources(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "editor-modules");
    // OPENEDEN_CS_MODULES=<id>=<library path>[;<id>=<library path>...]: more core modules, for checks.
    if (const char* extra = std::getenv("OPENEDEN_CS_MODULES"))
        for (const QString& item : QString::fromUtf8(extra).split(';', Qt::SkipEmptyParts))
        {
            const int equals = item.indexOf('=');
            if (equals > 0)
                sources.push_back({ item.left(equals).toStdString(),
                    std::filesystem::path(item.mid(equals + 1).toStdString()), CSMModule::ModuleSource::Origin::Core });
        }
    if (mExtensionHost)
        for (CSMModule::ModuleSource& source : mExtensionHost->editorModuleSources())
            sources.push_back(std::move(source));
    mModuleHost->load(sources);
    // Defaults the modules just registered (and repairs of the values found) reach the file now.
    if (mModuleRegistry.settings().takeWritten())
        try
        {
            CSMPrefs::State::get().save();
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Could not save the editor modules' settings: " << e.what();
        }
    for (const CSMModule::ModuleInfo& info : mModuleHost->modules())
        if (info.mState == CSMModule::ModuleInfo::State::Failed)
            Log(Debug::Error) << "Editor module '" << info.mId << "' failed: " << info.mMessage;

    std::vector<std::filesystem::path> roots;
    for (const std::filesystem::path& dir : dataDirs)
        roots.push_back(dir);
    if (const char* folder = std::getenv("OPENEDEN_CS_ASSETDIR"))
        roots.push_back(std::filesystem::path(folder));
    mModuleUi.setAssetRoots(std::move(roots));
    mModuleUi.setUserDataPath(mCfgMgr.getUserDataPath());
    mModuleUi.setSimulationSandbox(&mSandbox);
}

CS::Editor::~Editor()
{
    mPhysicsWorld.setStepObserver(nullptr);
    delete mViewManager;
    mViewManager = nullptr;
    // What the modules handed over has code in their libraries: gone before the libraries are.
    mModuleUi.releaseModuleObjects();
    mModuleRegistry.settings().setStore(nullptr); // the store is a member that goes before the registry
    mModuleHost.reset();

    mLockFile.unlock();
    mPidFile.close();

    if (mServer && std::filesystem::exists(mPid))
        std::filesystem::remove(mPid);
}

boost::program_options::variables_map CS::Editor::readConfiguration(boost::program_options::variables_map variables)
{
    // The command line's options (CSMPrefs::parseCommandLine) come first, then the config files below them, as in the
    // game (apps/openmw/main.cpp, parseArgs). OpenMW-CS never parsed its command line here (upstream commit
    // 4e26a61db3, 2013), so --replace=config, --config and --user-data did nothing and the real user folder was used.
    const boost::program_options::options_description desc = CSMPrefs::makeEditorOptions();

    boost::program_options::notify(variables);

    mCfgMgr.processPaths(variables, std::filesystem::current_path());
    mCfgMgr.readConfiguration(variables, desc, false);

    // OpenMW-CS kept its settings in openmw-cs.cfg. The first run of Eden Studio copies that file to eden-studio.cfg
    // (the old file is only read) so that the user keeps their preferences; see model/prefs/configmigration.hpp.
    const bool migrated = CSMPrefs::migrateLegacyConfig(mCfgMgr.getUserConfigPath());

    Settings::Manager::load(mCfgMgr, true);
    Debug::setupLogging(mCfgMgr.getLogPath(), "Eden-Studio");
    if (migrated)
        Log(Debug::Info) << "Copied " << CSMPrefs::legacyConfigFileName << " to " << CSMPrefs::configFileName;

    return variables;
}

void CS::Editor::startExtensionHost()
{
    // As the game does (apps/openmw/vsgengine.cpp): extensions/ beside the program, then in the user data folder. The
    // editor only reads: no world, no phases (task 0024, stage D).
    CSMExtension::EditorExtensionHost::Config config;
    config.mSearchDirs = { mCfgMgr.getLocalPath() / "extensions", mCfgMgr.getUserDataPath() / "extensions" };
    for (const std::string& id : mConfigVariables["extension"].as<std::vector<std::string>>())
        if (!id.empty())
            config.mEnabled.push_back(id);
    mExtensionHost = std::make_unique<CSMExtension::EditorExtensionHost>(config);
    // Read-only queries on the editor's world, before extensions start (task 0030).
    mExtensionHost->attachPhysicsWorld(mPhysicsWorld.system(), CSMPhysics::StudioWorld::sUnitsPerMeter);
    mPhysicsWorld.setStepObserver(this);
    mExtensionHost->start();
    Log(Debug::Info) << "Extensions: " << mExtensionHost->summary();
}

std::pair<Files::PathContainer, std::vector<std::string>> CS::Editor::readConfig(bool quiet)
{
    boost::program_options::variables_map& variables = mConfigVariables;

    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);

    mEncodingName = variables["encoding"].as<std::string>();
    mDocumentManager.setEncoding(ToUTF8::calculateEncoding(mEncodingName));
    mFileDialog.setEncoding(QString::fromUtf8(mEncodingName.c_str()));

    mDocumentManager.setResourceDir(mResources = variables["resources"]
                                                     .as<Files::MaybeQuotedPath>()
                                                     .u8string()); // This call to u8string is redundant, but required
                                                                   // to build on MSVC 14.26 due to implementation bugs.

    Files::PathContainer dataDirs, dataLocal;
    if (!variables["data"].empty())
    {
        dataDirs = asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
    }

    Files::PathContainer::value_type local(variables["data-local"]
                                               .as<Files::MaybeQuotedPathContainer::value_type>()
                                               .u8string()); // This call to u8string is redundant, but required to
                                                             // build on MSVC 14.26 due to implementation bugs.
    if (!local.empty())
    {
        std::filesystem::create_directories(local);
        dataLocal.push_back(std::move(local));
    }
    mCfgMgr.filterOutNonExistingPaths(dataDirs);
    mCfgMgr.filterOutNonExistingPaths(dataLocal);

    if (!dataLocal.empty())
        mLocal = dataLocal[0];
    else
    {
        QMessageBox messageBox;
        messageBox.setWindowTitle(tr("No local data path available"));
        messageBox.setIcon(QMessageBox::Critical);
        messageBox.setStandardButtons(QMessageBox::Ok);
        messageBox.setText(tr(
            "<br><b>Eden Studio is unable to access the local data directory. This may indicate a faulty configuration "
            "or a broken install.</b>"));
        messageBox.exec();

        QApplication::exit(1);
    }

    dataDirs.insert(dataDirs.end(), dataLocal.begin(), dataLocal.end());

    dataDirs.insert(dataDirs.begin(), mResources / "vfs");

    // iterate the data directories and add them to the file dialog for loading
    mFileDialog.addFiles(dataDirs);

    return std::make_pair(dataDirs, variables["fallback-archive"].as<std::vector<std::string>>());
}

void CS::Editor::createGame()
{
    mStartup.hide();

    if (mNewGame.isHidden())
        mNewGame.show();

    mNewGame.raise();
    mNewGame.activateWindow();
}

void CS::Editor::cancelCreateGame()
{
    if (!mDocumentManager.isEmpty())
        return;

    mNewGame.hide();

    if (mStartup.isHidden())
        mStartup.show();

    mStartup.raise();
    mStartup.activateWindow();
}

void CS::Editor::createAddon()
{
    mStartup.hide();

    mFileDialog.clearFiles();
    readConfig(/*quiet*/ true);

    mFileDialog.showDialog(CSVDoc::ContentAction_New);
}

void CS::Editor::cancelFileDialog()
{
    if (!mDocumentManager.isEmpty())
        return;

    mFileDialog.hide();

    if (mStartup.isHidden())
        mStartup.show();

    mStartup.raise();
    mStartup.activateWindow();
}

void CS::Editor::loadDocument()
{
    mStartup.hide();

    mFileDialog.clearFiles();
    readConfig(/*quiet*/ true);

    mFileDialog.showDialog(CSVDoc::ContentAction_Edit);
}

void CS::Editor::openFiles(
    const std::filesystem::path& savePath, const std::vector<std::filesystem::path>& discoveredFiles)
{
    std::vector<std::filesystem::path> files;

    if (discoveredFiles.empty())
    {
        for (const QString& path : mFileDialog.selectedFilePaths())
        {
            files.emplace_back(Files::pathFromQString(path));
        }
    }
    else
    {
        files = discoveredFiles;
    }

    mDocumentManager.addDocument(files, savePath, false);

    mFileDialog.hide();
}

void CS::Editor::createNewFile(const std::filesystem::path& savePath)
{
    std::vector<std::filesystem::path> files;

    for (const QString& path : mFileDialog.selectedFilePaths())
    {
        files.emplace_back(Files::pathFromQString(path));
    }

    files.push_back(savePath);

    mDocumentManager.addDocument(files, savePath, true);

    mFileDialog.hide();
}

void CS::Editor::createNewGame(const std::filesystem::path& file)
{
    std::vector<std::filesystem::path> files;

    files.push_back(file);

    mDocumentManager.addDocument(files, file, true);

    mNewGame.hide();
}

void CS::Editor::showStartup()
{
    if (mStartup.isHidden())
        mStartup.show();
    mStartup.raise();
    mStartup.activateWindow();
}

void CS::Editor::showSettings()
{
    if (mSettings.isHidden())
        mSettings.show();

    mSettings.move(QCursor::pos());
    mSettings.raise();
    mSettings.activateWindow();
}

bool CS::Editor::makeIPCServer()
{
    try
    {
        bool pidExists = std::filesystem::exists(mPid);

        mPidFile.open(mPid);

        if (!mLockFile.tryLock())
        {
            Log(Debug::Error) << "Error: Eden Studio is already running.";
            return false;
        }

#ifdef _WIN32
        mPidFile << GetCurrentProcessId() << std::endl;
#else
        mPidFile << getpid() << std::endl;
#endif

        mServer = new QLocalServer(this);

        if (pidExists)
        {
            // hack to get the temp directory path
            mServer->listen("dummy");
            QString fullPath = mServer->fullServerName();
            mServer->close();
            fullPath.remove(QRegularExpression("dummy$"));
            fullPath += mIpcServerName;
            const auto path = Files::pathFromQString(fullPath);
            if (exists(path))
            {
                // TODO: compare pid of the current process with that in the file
                Log(Debug::Info) << "Detected unclean shutdown.";
                // delete the stale file
                if (remove(path))
                    Log(Debug::Error) << "Error: can not remove stale connection file.";
            }
        }
    }

    catch (const std::exception& e)
    {
        Log(Debug::Error) << "Error: " << e.what();
        return false;
    }

    if (mServer->listen(mIpcServerName))
    {
        connect(mServer, &QLocalServer::newConnection, this, &Editor::showStartup);
        return true;
    }

    mServer->close();
    mServer = nullptr;
    return false;
}

void CS::Editor::connectToIPCServer()
{
    mClientSocket = new QLocalSocket(this);
    mClientSocket->connectToServer(mIpcServerName);
    mClientSocket->close();
}

int CS::Editor::run()
{
    if (mLocal.empty())
        return 1;

    Misc::Rng::init();

    QApplication::setQuitOnLastWindowClosed(true);

    if (mFileToLoad.empty())
    {
        mStartup.show();
    }
    else
    {
        ESM::ESMReader fileReader;
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding(mEncodingName));
        fileReader.setEncoder(&encoder);
        fileReader.open(mFileToLoad);

        std::vector<std::filesystem::path> discoveredFiles;

        for (const auto& item : fileReader.getGameFiles())
        {
            for (const auto& path : mDataDirs)
            {
                if (auto masterPath = path / item.name; std::filesystem::exists(masterPath))
                {
                    discoveredFiles.emplace_back(std::move(masterPath));
                    break;
                }
            }
        }
        discoveredFiles.push_back(mFileToLoad);

        const auto extension = Files::pathToQString(mFileToLoad.extension()).toLower();
        if (extension == ".esm")
        {
            mFileToLoad.replace_extension(".omwgame");
            mDocumentManager.addDocument(discoveredFiles, mFileToLoad, false);
        }
        else if (extension == ".esp")
        {
            mFileToLoad.replace_extension(".omwaddon");
            mDocumentManager.addDocument(discoveredFiles, mFileToLoad, false);
        }
        else
        {
            openFiles(mFileToLoad, discoveredFiles);
        }
    }

    return QApplication::exec();
}

namespace
{
    constexpr int sUiCheckCloseIntervalMs = 500;

    /// Keeps every window the editor shows from taking the focus during an unattended check.
    class NoActivation : public QObject
    {
    public:
        using QObject::QObject;

        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::Polish)
                if (auto* widget = qobject_cast<QWidget*>(watched); widget && widget->isWindow())
                {
                    widget->setAttribute(Qt::WA_ShowWithoutActivating);
                    widget->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
                }
            return false;
        }
    };

    /// The Asset Browser check, see startUiCheck: searches, makes the drag data of an object, and opens an object
    /// and a table the way a double click does.
    void runAssetCheck(const std::string& search, int milliseconds)
    {
        const int step = std::max(500, milliseconds / 8);
        auto browser = []() -> CSVDoc::AssetBrowser* {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto* found = widget->findChild<CSVDoc::AssetBrowser*>())
                    return found;
            return nullptr;
        };
        QTimer::singleShot(step * 3, qApp, [=] {
            CSVDoc::AssetBrowser* panel = browser();
            if (panel == nullptr)
                return;
            Log(Debug::Info) << "UI check: assets before search " << panel->visibleItemCount();
            panel->findChild<QLineEdit*>()->setText(QString::fromStdString(search));
            Log(Debug::Info) << "UI check: assets after search '" << search << "' " << panel->visibleItemCount();

            QTreeView* tree = panel->findChild<QTreeView*>("assetTree");
            QAbstractItemModel* model = tree->model();
            for (int group = 0; group < model->rowCount(); ++group)
            {
                const QModelIndex groupIndex = model->index(group, 0);
                if (model->rowCount(groupIndex) == 0)
                    continue;
                const QModelIndex first = model->index(0, 0, groupIndex);
                Log(Debug::Info) << "UI check: assets group " << groupIndex.data().toString().toStdString()
                                 << " first match " << first.data().toString().toStdString() << " of "
                                 << model->rowCount(groupIndex);
                if (groupIndex.data().toString() == "Objects")
                {
                    std::unique_ptr<QMimeData> mime(model->mimeData({ first }));
                    auto* table = dynamic_cast<CSMWorld::TableMimeData*>(mime.get());
                    Log(Debug::Info) << "UI check: assets drag data "
                                     << (table != nullptr && !table->getData().empty()
                                                ? table->getData().front().toString()
                                                : std::string("none"));
                    emit tree->activated(first); // opens the record editor
                }
                if (groupIndex.data().toString() == "Tables")
                    emit tree->activated(first); // opens the first table
            }
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            for (QWidget* widget : QApplication::topLevelWidgets())
                for (CSVDoc::SubView* subView : widget->findChildren<CSVDoc::SubView*>())
                    Log(Debug::Info) << "UI check: assets open sub view " << subView->getTitle();
        });
    }

    /// Finds the first 3D world-space view of any window.
    CSVRender::WorldspaceWidget* findWorldspaceWidget()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* found = widget->findChild<CSVRender::WorldspaceWidget*>())
                return found;
        return nullptr;
    }

    /// The physics check, see startUiCheck (task 0030): drops the test ball, captures the 3D view before and after
    /// steps, plays and pauses. Everything it sees is logged as "UI check: physics ...".
    void runPhysicsCheck(const std::string& prefix, int milliseconds)
    {
        const int step = std::max(500, milliseconds / 8);
        auto report = [](const char* what) {
            CSVRender::PhysicsHub* hub = CSVRender::PhysicsHub::get();
            const JPH::RVec3 p = hub->world().position(hub->lastBall());
            Log(Debug::Info) << "UI check: physics " << what << ": step " << hub->world().stepIndex() << ", bodies "
                             << hub->world().bodyCount() << ", ball z " << static_cast<double>(p.GetZ()) << " units, "
                             << (hub->isPlaying() ? "playing" : "paused");
        };
        auto capture = [prefix](const char* name) {
            if (CSVRender::WorldspaceWidget* widget = findWorldspaceWidget())
                if (CSVRender::SceneHost* host = widget->getHost())
                {
                    const std::filesystem::path file = prefix + name + ".png";
                    Log(Debug::Info) << "UI check: physics frame " << file.string() << ": "
                                     << (host->capture(file) ? "written" : "no frame");
                }
        };
        QTimer::singleShot(step * 2, qApp, [=] {
            CSVRender::PhysicsHub::get()->dropTestBall();
            report("dropped");
        });
        QTimer::singleShot(step * 3, qApp, [=] {
            report("before steps");
            capture("_0_dropped");
        });
        QTimer::singleShot(step * 4, qApp, [=] {
            for (int i = 0; i < 30; ++i)
                CSVRender::PhysicsHub::get()->step();
            report("after 30 Step presses");
            capture("_1_stepped");
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            CSVRender::PhysicsHub::get()->play();
            report("play pressed");
        });
        QTimer::singleShot(step * 6, qApp, [=] {
            report("while playing");
            CSVRender::PhysicsHub::get()->pause();
        });
        QTimer::singleShot(step * 7, qApp, [=] {
            report("after Pause");
            capture("_2_played");
        });
    }

    /// The drop check (task 0030, task 0024 box 28): a constructed drop of the Asset Browser's first object on the
    /// 3D view, counting the References table's rows before and after.
    void runDropCheck(CSMDoc::Document* document, int milliseconds)
    {
        QTimer::singleShot(milliseconds / 2, qApp, [=] {
            CSVRender::WorldspaceWidget* widget = findWorldspaceWidget();
            CSVDoc::AssetBrowser* panel = nullptr;
            for (QWidget* top : QApplication::topLevelWidgets())
                if (panel == nullptr)
                    panel = top->findChild<CSVDoc::AssetBrowser*>();
            if (widget == nullptr || panel == nullptr)
            {
                Log(Debug::Error) << "UI check: drop: no 3D view or Asset Browser";
                return;
            }
            panel->findChild<QLineEdit*>()->setText("menhir");
            QAbstractItemModel* model = panel->findChild<QTreeView*>("assetTree")->model();
            std::unique_ptr<QMimeData> mime;
            for (int group = 0; group < model->rowCount() && !mime; ++group)
            {
                const QModelIndex groupIndex = model->index(group, 0);
                if (groupIndex.data().toString() == "Objects" && model->rowCount(groupIndex) > 0)
                    mime.reset(model->mimeData({ model->index(0, 0, groupIndex) }));
            }
            if (!mime)
            {
                Log(Debug::Error) << "UI check: drop: the Asset Browser has no object";
                return;
            }
            CSMWorld::IdTable* references = dynamic_cast<CSMWorld::IdTable*>(
                document->getData().getTableModel(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_References)));
            const int before = references->rowCount();
            const QPointF centre(widget->width() / 2.0, widget->height() / 2.0);
            QDropEvent drop(centre, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
            const CSVRender::WorldspaceHitResult hit
                = widget->mousePick(centre.toPoint(), widget->getInteractionMask());
            Log(Debug::Info) << "UI check: drop: pick hit " << hit.hit << " at " << hit.worldPos.x << ", "
                             << hit.worldPos.y << ", " << hit.worldPos.z << " cell '"
                             << widget->getCellId(vsg::vec3(hit.worldPos)) << "' size " << widget->width() << "x"
                             << widget->height() << " enabled " << widget->isEnabled() << " drops "
                             << widget->acceptDrops() << " mime ids "
                             << static_cast<CSMWorld::TableMimeData*>(mime.get())->getData().size();
            widget->deliverDropForCheck(&drop);
            const bool handled = true;
            Log(Debug::Info) << "UI check: drop: handled " << handled << " accepted " << drop.isAccepted()
                             << ", references " << before << " -> " << references->rowCount() << ", undo stack count "
                             << document->getUndoStack().count();
        });
    }

    /// The editor module check (task 0031, stage B), see startUiCheck. Run with OPENEDEN_CS_MODULES naming the sample
    /// module and OPENEDEN_CS_ASSETDIR a folder with a .note.json file. Everything it sees is logged as "UI check:
    /// module ...".
    void runModuleCheck(CSMDoc::Document* document, const std::string& prefix, int milliseconds)
    {
        const int step = std::max(500, milliseconds / 10);
        QTimer::singleShot(step * 2, qApp, [=] {
            // The Extensions menu: the module's submenu and items, then the sample command.
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto* window = qobject_cast<QMainWindow*>(widget); window && window->isVisible())
                {
                    for (QAction* action : window->findChildren<QAction*>())
                        if (action->objectName().startsWith("moduleCommand_"))
                        {
                            Log(Debug::Info) << "UI check: module menu item " << action->text().toStdString() << " ("
                                             << action->objectName().toStdString() << ")";
                            action->trigger();
                        }
                    for (QMenu* menu : window->findChildren<QMenu*>())
                        if (menu->objectName().startsWith("moduleMenu_"))
                            Log(Debug::Info) << "UI check: module menu " << menu->title().toStdString();
                    break;
                }
        });
        QTimer::singleShot(step * 3, qApp, [=] {
            // The Asset Browser's "Module assets" group.
            CSVDoc::AssetBrowser* panel = nullptr;
            for (QWidget* top : QApplication::topLevelWidgets())
                if (panel == nullptr)
                    panel = top->findChild<CSVDoc::AssetBrowser*>();
            if (panel == nullptr)
            {
                Log(Debug::Error) << "UI check: module: no Asset Browser";
                return;
            }
            QTreeView* tree = panel->findChild<QTreeView*>("assetTree");
            QAbstractItemModel* model = tree->model();
            for (int group = 0; group < model->rowCount(); ++group)
            {
                const QModelIndex groupIndex = model->index(group, 0);
                if (groupIndex.data().toString() != "Module assets")
                    continue;
                tree->expand(groupIndex);
                Log(Debug::Info) << "UI check: module assets listed: " << model->rowCount(groupIndex);
                if (model->rowCount(groupIndex) > 0)
                {
                    Log(Debug::Info) << "UI check: module asset "
                                     << model->index(0, 0, groupIndex).data().toString().toStdString();
                    emit tree->activated(model->index(0, 0, groupIndex));
                }
            }
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            Log(Debug::Info) << "UI check: module active asset " << (ui->activeAsset() ? "open" : "none") << " format "
                             << ui->activeFormat() << " " << ui->openError();
        });
        QTimer::singleShot(step * 4, qApp, [=] {
            // The workspace tab shows the panel; its form edits the weight through the undo stack.
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "Sample")
                        {
                            Log(Debug::Info) << "UI check: module workspace tab Sample found";
                            tabs->setCurrentIndex(i);
                        }
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            QDoubleSpinBox* weight = nullptr;
            QLineEdit* title = nullptr;
            for (QWidget* top : QApplication::topLevelWidgets())
                if (weight == nullptr)
                {
                    for (QDoubleSpinBox* spin : top->findChildren<QDoubleSpinBox*>("property_weight"))
                        if (spin->isVisibleTo(top))
                            weight = spin;
                    for (QLineEdit* line : top->findChildren<QLineEdit*>("property_title"))
                        if (line->isVisibleTo(top))
                            title = line;
                }
            if (weight == nullptr)
            {
                Log(Debug::Error) << "UI check: module: no visible weight field in the panel";
                return;
            }
            Log(Debug::Info) << "UI check: module panel shows title " << (title ? title->text().toStdString() : "?")
                             << " weight " << weight->value() << " unit " << weight->suffix().toStdString();
            // Module steps go on the document's module stack, not on the content stack (CSMDoc::UndoRouting): the
            // document must stay unmodified, and Edit > Undo acts on the module stack while it was touched last.
            QUndoStack& moduleStack = document->getUndoRouting().moduleStack();
            auto editMenuAction = [](const QString& itemPrefix) -> QAction* {
                for (QWidget* top : QApplication::topLevelWidgets())
                    if (auto* window = qobject_cast<QMainWindow*>(top); window && window->menuBar())
                        for (QAction* menuAction : window->menuBar()->actions())
                            if (menuAction->menu() && menuAction->text().remove('&') == "Edit")
                                for (QAction* inner : menuAction->menu()->actions())
                                    if (inner->text().startsWith(itemPrefix))
                                        return inner;
                return nullptr;
            };
            const int before = moduleStack.count();
            const int contentBefore = document->getUndoStack().count();
            weight->setValue(6.0);
            weight->setValue(7.5);
            Log(Debug::Info) << "UI check: module edit: undo steps " << before << " -> " << moduleStack.count()
                             << " (two edits merge into one), content undo steps " << contentBefore << " -> "
                             << document->getUndoStack().count() << ", document modified "
                             << ((document->getState() & CSMDoc::State_Modified) != 0) << ", weight "
                             << weight->value();
            QAction* editUndo = editMenuAction("Undo");
            Log(Debug::Info) << "UI check: module edit menu: Undo '"
                             << (editUndo ? editUndo->text().toStdString() : std::string("none")) << "' enabled "
                             << (editUndo && editUndo->isEnabled());
            if (editUndo != nullptr)
                editUndo->trigger();
            Log(Debug::Info) << "UI check: module undo (Edit > Undo): weight " << weight->value()
                             << ", module stack index " << moduleStack.index() << ", document modified "
                             << ((document->getState() & CSMDoc::State_Modified) != 0);
            if (QAction* editRedo = editMenuAction("Redo"))
                editRedo->trigger();
            Log(Debug::Info) << "UI check: module redo (Edit > Redo): weight " << weight->value();
            // The inspector section and the timeline strip.
            for (QWidget* top : QApplication::topLevelWidgets())
            {
                if (auto* box = top->findChild<QGroupBox*>("moduleSection_sample.section"))
                    Log(Debug::Info) << "UI check: module inspector section " << box->title().toStdString();
                if (auto* strip = top->findChild<QWidget*>("moduleTimeline"))
                    Log(Debug::Info) << "UI check: module timeline strip " << strip->width() << "x" << strip->height();
            }
            Log(Debug::Info) << "UI check: module timeline keys " << CSVModule::ModuleUi::get()->timelineKeys().size();
        });
        QTimer::singleShot(step * 7, qApp, [=] {
            if (CSVRender::WorldspaceWidget* widget = findWorldspaceWidget())
                if (CSVRender::SceneHost* host = widget->getHost())
                {
                    const std::filesystem::path file = prefix + "_overlay.png";
                    Log(Debug::Info) << "UI check: module frame " << file.string() << ": "
                                     << (host->capture(file) ? "written" : "no frame");
                }
            int index = 0;
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && qobject_cast<QMainWindow*>(widget))
                    widget->grab().save(QString::fromStdString(prefix) + "_window" + QString::number(index++) + ".png");
        });
        // The preferences window's "Modules" page (API 1.2, task 0091 stage e): lists the sample's two settings, an
        // edit of the axis length goes to the store and is written to eden-studio.cfg by the window's own saving.
        QTimer::singleShot(step * 8, qApp, [=] {
            auto* dialogue = new CSVPrefs::Dialogue();
            dialogue->show();
            QListWidget* categories = dialogue->findChild<QListWidget*>();
            bool found = false;
            if (categories != nullptr)
                for (int row = 0; row < categories->count(); ++row)
                    if (categories->item(row)->text() == "Modules")
                    {
                        categories->setCurrentRow(row);
                        found = true;
                    }
            Log(Debug::Info) << "UI check: prefs Modules category " << (found ? "listed" : "missing");
            QStringList names;
            for (QWidget* field : dialogue->findChildren<QWidget*>())
                if (field->objectName().startsWith("moduleSetting_"))
                    names << field->objectName();
            names.sort();
            Log(Debug::Info) << "UI check: prefs module settings shown: " << names.join(",").toStdString();
            if (auto* spin = dialogue->findChild<QDoubleSpinBox*>("moduleSetting_sample.axisHalfLength"))
            {
                const double before = spin->value();
                spin->setValue(4.5);
                const auto stored = CSVModule::ModuleUi::get()->setting("sample.axisHalfLength");
                Log(Debug::Info) << "UI check: prefs axis length " << before << " -> " << spin->value() << ", setting "
                                 << (stored ? std::get<double>(*stored) : -1.0) << ", range " << spin->minimum()
                                 << " to " << spin->maximum();
                spin->setValue(99.0); // beyond the range: the box clamps it
                Log(Debug::Info) << "UI check: prefs axis length clamped to " << spin->value();
                spin->setValue(4.5);
            }
            dialogue->grab().save(QString::fromStdString(prefix) + "_prefs.png");
            dialogue->close(); // saves eden-studio.cfg
            dialogue->deleteLater();
        });
    }

    /// The CreativeMotion rig check (task 0032): OPENEDEN_CS_RIGCHECK=<prefix>;<rig file>;<profile file>. Opens the rig
    /// and the profile as module assets, edits a twist limit and a motor gain through the panel's fields, undoes,
    /// saves, and captures the 3D view. Logged as "UI check: rig ...".
    void runRigCheck(CSMDoc::Document* document, const std::string& spec, int milliseconds)
    {
        const std::size_t first = spec.find(';');
        const std::size_t second = spec.find(';', first + 1);
        const std::string prefix = spec.substr(0, first);
        const std::filesystem::path rigPath = spec.substr(first + 1, second - first - 1);
        const std::filesystem::path profilePath = spec.substr(second + 1);
        const int step = std::max(500, milliseconds / 10);

        auto findField = [](const QString& name) -> QDoubleSpinBox* {
            for (QWidget* top : QApplication::topLevelWidgets())
                for (QDoubleSpinBox* spin : top->findChildren<QDoubleSpinBox*>(name))
                    if (spin->isVisibleTo(top))
                        return spin;
            return nullptr;
        };
        auto fieldNames = [](const QString& prefixText) {
            QStringList names;
            for (QWidget* top : QApplication::topLevelWidgets())
                for (QDoubleSpinBox* spin : top->findChildren<QDoubleSpinBox*>())
                    if (spin->isVisibleTo(top) && spin->objectName().startsWith(prefixText))
                        names << spin->objectName();
            names.removeDuplicates();
            return names;
        };

        QTimer::singleShot(step * 2, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            ui->openAsset(rigPath);
            Log(Debug::Info) << "UI check: rig opened " << (ui->activeAsset() ? "yes" : "no") << " format "
                             << ui->activeFormat() << " " << ui->openError();
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "CreativeMotion")
                            tabs->setCurrentIndex(i);
        });
        QTimer::singleShot(step * 4, qApp, [=] {
            const QStringList names = fieldNames("property_joint.");
            Log(Debug::Info) << "UI check: rig joint fields shown: " << names.size() << ", e.g. "
                             << (names.isEmpty() ? "none" : names.front().toStdString());
            QDoubleSpinBox* twist = nullptr;
            for (const QString& name : names)
                if (name.endsWith(".twistMaxRad") && findField(name)->value() > 0.5)
                    twist = findField(name);
            if (twist == nullptr)
            {
                Log(Debug::Error) << "UI check: rig: no twistMaxRad field with a value above 0.5";
                return;
            }
            const double before = twist->value();
            const int steps = document->getUndoRouting().moduleStack().count();
            twist->setValue(before - 0.1);
            twist->setValue(before - 0.2);
            Log(Debug::Info) << "UI check: rig twist " << twist->objectName().toStdString() << " unit "
                             << twist->suffix().toStdString() << " " << before << " -> " << twist->value()
                             << ", undo steps " << steps << " -> " << document->getUndoRouting().moduleStack().count();
            document->getUndoRouting().moduleStack().undo();
            Log(Debug::Info) << "UI check: rig undo: " << twist->value();
            document->getUndoRouting().moduleStack().redo();
            std::string error;
            const bool saved = CSVModule::ModuleUi::get()->activeAsset()->save(error);
            Log(Debug::Info) << "UI check: rig saved " << saved << " " << error << ", dirty "
                             << CSVModule::ModuleUi::get()->activeAsset()->isDirty();
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            // The rig stands where the user looked when it opened: look at it from close by (a person is 1.8 m).
            if (CSVRender::WorldspaceWidget* widget = findWorldspaceWidget())
                if (CSVRender::SceneHost* host = widget->getHost())
                    if (const auto hit = host->pickNearest(0.5, 0.5, ~0u))
                    {
                        constexpr double units = CSMPhysics::StudioWorld::sUnitsPerMeter;
                        host->setCamera(*hit + vsg::dvec3(0.0, -4.0 * units, 1.5 * units),
                            *hit + vsg::dvec3(0.0, 0.0, 1.0 * units), vsg::dvec3(0.0, 0.0, 1.0));
                    }
        });
        QTimer::singleShot(step * 6, qApp, [=] {
            if (CSVRender::WorldspaceWidget* widget = findWorldspaceWidget())
                if (CSVRender::SceneHost* host = widget->getHost())
                {
                    const std::filesystem::path file = prefix + "_rig.png";
                    Log(Debug::Info) << "UI check: rig frame " << file.string() << ": "
                                     << (host->capture(file) ? "written" : "no frame");
                }
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            ui->openAsset(profilePath);
            Log(Debug::Info) << "UI check: rig profile opened " << (ui->activeAsset() ? "yes" : "no") << " "
                             << ui->openError();
        });
        QTimer::singleShot(step * 8, qApp, [=] {
            const QStringList names = fieldNames("property_default.");
            for (const QString& name : names)
                if (QDoubleSpinBox* spin = findField(name))
                    Log(Debug::Info) << "UI check: rig motor " << name.toStdString() << " = " << spin->value() << " "
                                     << spin->suffix().toStdString();
            if (QDoubleSpinBox* stiffness = findField("property_default.stiffnessNmPerRad"))
            {
                const double before = stiffness->value();
                const int steps = document->getUndoRouting().moduleStack().count();
                stiffness->setValue(before + 100);
                Log(Debug::Info) << "UI check: rig stiffness edit " << before << " -> " << stiffness->value()
                                 << ", undo steps " << steps << " -> "
                                 << document->getUndoRouting().moduleStack().count();
                document->getUndoRouting().moduleStack().undo();
                Log(Debug::Info) << "UI check: rig stiffness undo " << stiffness->value();
            }
            int index = 0;
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && qobject_cast<QMainWindow*>(widget) && index == 0)
                    widget->grab().save(QString::fromStdString(prefix) + "_window" + QString::number(index++) + ".png");
        });
    }

    /// The gizmo handle check (task 0091 stages b to d): OPENEDEN_CS_HANDLECHECK=<prefix>;<rig file>. Opens the rig as
    /// a module asset in a model-less Mesh view, selects the first joint through the module's own command (the panel
    /// narrows, the joint's name is drawn as a label), then with synthetic mouse events on the view: hovers the cone
    /// half angle handle (Studio highlights it), presses it and drags it across several moves, releases (one undo
    /// step), undoes. Frames <prefix>_labels.png, _hover.png, _drag.png, _after.png. Logged as "UI check: handle ...".
    void runHandleCheck(CSMDoc::Document* document, const std::string& spec, int milliseconds)
    {
        const std::size_t semicolon = spec.find(';');
        if (semicolon == std::string::npos)
        {
            Log(Debug::Error) << "UI check: handle: OPENEDEN_CS_HANDLECHECK needs <prefix>;<rig file>";
            return;
        }
        const std::string prefix = spec.substr(0, semicolon);
        const std::filesystem::path rigPath = spec.substr(semicolon + 1);
        const int step = std::max(600, milliseconds / 12);

        auto meshView = []() -> CSVRender::MeshViewWidget* {
            for (QWidget* widget : QApplication::topLevelWidgets())
                for (auto* view : widget->findChildren<CSVRender::MeshViewWidget*>())
                    if (view->showsModuleAsset())
                        return view;
            return nullptr;
        };
        // The pixel of a world position in the view (the mapping Picking::viewRay inverts).
        auto pixelOf = [](CSVRender::MeshViewWidget* view, const vsg::dvec3& world) {
            const vsg::dmat4 viewProjection = view->getHost()->getProjectionMatrix() * view->getHost()->getViewMatrix();
            const vsg::dvec4 clip = viewProjection * vsg::dvec4(world.x, world.y, world.z, 1.0);
            return QPointF(
                (clip.x / clip.w * 0.5 + 0.5) * view->width(), (clip.y / clip.w * 0.5 + 0.5) * view->height());
        };
        auto send = [](CSVRender::MeshViewWidget* view, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, at, view->mapToGlobal(at.toPoint()), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(view, &event);
        };
        auto handlePosition = [](const std::string& id) -> std::optional<openeden::editor::Vec3> {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            if (ui == nullptr || ui->activeAsset() == nullptr)
                return std::nullopt;
            for (openeden::editor::Gizmo* gizmo : ui->activeAsset()->gizmos())
                for (const openeden::editor::GizmoHandle& handle : gizmo->handles())
                    if (handle.id == id)
                        return handle.position;
            return std::nullopt;
        };
        auto frame = [=](const std::string& name) {
            if (CSVRender::MeshViewWidget* view = meshView())
            {
                view->flagAsModified();
                QTimer::singleShot(step / 3, qApp, [=] {
                    if (CSVRender::MeshViewWidget* again = meshView())
                    {
                        const std::filesystem::path file = prefix + "_" + name + ".png";
                        Log(Debug::Info) << "UI check: handle frame " << file.string() << ": "
                                         << (again->saveScreenshot(file) ? "written" : "no frame");
                    }
                });
            }
        };
        auto halfAngle = []() {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const auto value = ui->activeAsset()->properties().get("joint.Head.planeSwingMaxRad");
            return std::get_if<double>(&value) != nullptr ? std::get<double>(value) : -1.0;
        };

        QTimer::singleShot(step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            ui->openAsset(rigPath);
            Log(Debug::Info) << "UI check: handle opened " << (ui->activeAsset() ? "yes" : "no") << " "
                             << ui->openError();
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "CreativeMotion")
                            tabs->setCurrentIndex(i);
            if (meshView() == nullptr)
                for (QWidget* top : QApplication::topLevelWidgets())
                    if (auto* window = qobject_cast<QMainWindow*>(top); window && window->isVisible())
                        for (QAction* action : window->findChildren<QAction*>("showModuleAssetInMeshViewer"))
                        {
                            action->trigger();
                            break;
                        }
        });
        QTimer::singleShot(3 * step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            // Spine, Chest, Head: the first joint that is a cone.
            for (int i = 0; i < 3; ++i)
                ui->registry().runCommand("creativemotion.nextJoint");
            Log(Debug::Info) << "UI check: handle selection " << ui->selection().kind << " " << ui->selection().id;
            if (CSVRender::MeshViewWidget* view = meshView())
            {
                for (QWidget* up = view->parentWidget(); up != nullptr; up = up->parentWidget())
                    if (auto* dock = qobject_cast<QDockWidget*>(up))
                    {
                        if (!dock->isFloating())
                        {
                            dock->setFloating(true);
                            dock->resize(960, 720);
                        }
                        break;
                    }
                view->frameObject();
            }
            else
                Log(Debug::Error) << "UI check: handle: no model-less Mesh view";
        });
        QTimer::singleShot(5 * step, qApp, [=] {
            // The module's panel dock: how many joint rows, and whose.
            int rows = 0;
            std::set<std::string> joints;
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* dock = top->findChild<QDockWidget*>("modulePanel_creativemotion.panel"))
                    for (QWidget* field : dock->findChildren<QWidget*>())
                        if (field->objectName().startsWith("property_joint."))
                        {
                            ++rows;
                            joints.insert(field->objectName().mid(15).split('.').front().toStdString());
                        }
            Log(Debug::Info) << "UI check: handle panel joint rows " << rows << " of " << joints.size() << " joints";
            frame("labels");
        });
        QTimer::singleShot(6 * step, qApp, [=] {
            CSVRender::MeshViewWidget* view = meshView();
            const auto position = handlePosition("creativemotion.cone.plane");
            if (view == nullptr || !position)
            {
                Log(Debug::Error) << "UI check: handle: no view or no cone handle";
                return;
            }
            constexpr double units = CSMPhysics::StudioWorld::sUnitsPerMeter;
            const QPointF at = pixelOf(view, vsg::dvec3(position->x * units, position->y * units, position->z * units));
            Log(Debug::Info) << "UI check: handle cone.plane at pixel " << at.x() << " " << at.y() << " of "
                             << view->width() << "x" << view->height();
            send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
            Log(Debug::Info) << "UI check: handle hover highlights '" << CSVModule::ModuleUi::get()->highlightedHandle()
                             << "'";
        });
        QTimer::singleShot(7 * step, qApp, [=] { frame("hover"); });
        QTimer::singleShot(8 * step, qApp, [=] {
            CSVRender::MeshViewWidget* view = meshView();
            const auto position = handlePosition("creativemotion.cone.plane");
            if (view == nullptr || !position)
                return;
            constexpr double units = CSMPhysics::StudioWorld::sUnitsPerMeter;
            const QPointF start
                = pixelOf(view, vsg::dvec3(position->x * units, position->y * units, position->z * units));
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            Log(Debug::Info) << "UI check: handle half angle before " << halfAngle() << ", undo steps "
                             << document->getUndoRouting().moduleStack().count();
            send(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
                send(view, QEvent::MouseMove, start + QPointF(14.0 * i, -6.0 * i), Qt::NoButton, Qt::LeftButton);
            Log(Debug::Info) << "UI check: handle during drag: half angle " << halfAngle() << ", dragging "
                             << (ui->isDraggingHandle() ? "yes" : "no") << ", undo steps "
                             << document->getUndoRouting().moduleStack().count();
        });
        QTimer::singleShot(9 * step, qApp, [=] { frame("drag"); });
        QTimer::singleShot(10 * step, qApp, [=] {
            CSVRender::MeshViewWidget* view = meshView();
            if (view == nullptr)
                return;
            // The mouse is where the drag left it.
            const auto position = handlePosition("creativemotion.cone.plane");
            constexpr double units = CSMPhysics::StudioWorld::sUnitsPerMeter;
            send(view, QEvent::MouseButtonRelease,
                position ? pixelOf(view, vsg::dvec3(position->x * units, position->y * units, position->z * units))
                         : QPointF(0, 0),
                Qt::LeftButton, Qt::NoButton);
            Log(Debug::Info) << "UI check: handle released: half angle " << halfAngle() << ", undo steps "
                             << document->getUndoRouting().moduleStack().count();
            document->getUndoRouting().moduleStack().undo();
            Log(Debug::Info) << "UI check: handle undone: half angle " << halfAngle() << ", undo steps "
                             << document->getUndoRouting().moduleStack().count();
            document->getUndoRouting().moduleStack().redo();
            Log(Debug::Info) << "UI check: handle redone: half angle " << halfAngle();
        });
        QTimer::singleShot(11 * step, qApp, [=] { frame("after"); });
    }

    /// The push sandbox check (task 0065 stage C): OPENEDEN_CS_PUSHCHECK=<prefix>;<rig file>;<profile file>[;<bone>].
    /// Opens the profile and the rig as module assets in a model-less Mesh view, builds the sandbox with the module's
    /// Reset command, sets a push through the panel's fields and runs the module's Push command, then steps the
    /// editor's physics world the way the Step command does (PhysicsHub::step): the push is pending until the first
    /// step, the ragdoll moves, settles on the ground, and Reset puts it back. Frames <prefix>_before.png,
    /// _pending.png, _flight.png, _settled.png, _reset.png. Logged as "UI check: push ...", poses with 9 digits so that
    /// two runs can be compared.
    void runPushCheck(const std::string& spec, int milliseconds)
    {
        std::vector<std::string> parts;
        for (std::size_t from = 0;;)
        {
            const std::size_t semicolon = spec.find(';', from);
            parts.push_back(spec.substr(from, semicolon == std::string::npos ? std::string::npos : semicolon - from));
            if (semicolon == std::string::npos)
                break;
            from = semicolon + 1;
        }
        if (parts.size() < 3)
        {
            Log(Debug::Error) << "UI check: push: OPENEDEN_CS_PUSHCHECK needs <prefix>;<rig file>;<profile file>";
            return;
        }
        const std::string prefix = parts[0];
        const std::filesystem::path rigPath = parts[1];
        const std::filesystem::path profilePath = parts[2];
        const std::string requestedBone = parts.size() > 3 ? parts[3] : std::string();
        const int step = std::max(700, milliseconds / 14);

        auto meshView = []() -> CSVRender::MeshViewWidget* {
            for (QWidget* widget : QApplication::topLevelWidgets())
                for (auto* view : widget->findChildren<CSVRender::MeshViewWidget*>())
                    if (view->showsModuleAsset())
                        return view;
            return nullptr;
        };
        auto frame = [=](const std::string& name) {
            if (CSVRender::MeshViewWidget* view = meshView())
            {
                view->flagAsModified();
                QTimer::singleShot(step / 3, qApp, [=] {
                    if (CSVRender::MeshViewWidget* again = meshView())
                    {
                        const std::filesystem::path file = prefix + "_" + name + ".png";
                        Log(Debug::Info) << "UI check: push frame " << file.string() << ": "
                                         << (again->saveScreenshot(file) ? "written" : "no frame");
                    }
                });
            }
        };
        auto report = [](const char* what) {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            openeden::editor::SimulationSandbox* sandbox = ui->simulationSandbox();
            std::ostringstream text;
            text.precision(9);
            text << "UI check: push " << what << ": step " << CSVRender::PhysicsHub::get()->world().stepIndex()
                 << ", built " << (sandbox->built() ? "yes" : "no") << ", pending " << sandbox->pendingPushes();
            for (const std::string& name : sandbox->bodyNames())
            {
                openeden::editor::SandboxBodyPose pose;
                if (sandbox->bodyPose(name, pose))
                    text << ", " << name << " (" << pose.positionM.x << " " << pose.positionM.y << " "
                         << pose.positionM.z << ")";
            }
            Log(Debug::Info) << text.str();
        };
        auto setField = [](const char* id, const openeden::editor::PropertyValue& value) {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            openeden::editor::PropertySource* panel = ui->registry().panelSource("creativemotion.sandbox");
            if (panel == nullptr)
            {
                Log(Debug::Error) << "UI check: push: the sandbox panel has no source";
                return;
            }
            if (std::unique_ptr<openeden::editor::Command> command = panel->makeSetCommand(id, value))
                ui->pushCommand(std::move(command));
            else
                Log(Debug::Error) << "UI check: push: the panel refused " << id;
        };

        QTimer::singleShot(step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            ui->openAsset(profilePath);
            ui->openAsset(rigPath);
            Log(Debug::Info) << "UI check: push opened " << (ui->activeAsset() ? "yes" : "no") << " " << ui->openError()
                             << ", sandbox " << (ui->simulationSandbox() != nullptr ? "yes" : "no");
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "CreativeMotion")
                            tabs->setCurrentIndex(i);
            if (meshView() == nullptr)
                for (QWidget* top : QApplication::topLevelWidgets())
                    if (auto* window = qobject_cast<QMainWindow*>(top); window && window->isVisible())
                        for (QAction* action : window->findChildren<QAction*>("showModuleAssetInMeshViewer"))
                        {
                            action->trigger();
                            break;
                        }
        });
        QTimer::singleShot(step * 3, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            Log(Debug::Info) << "UI check: push commands enabled: push "
                             << ui->registry().isCommandEnabled("creativemotion.sandbox.push") << ", reset "
                             << ui->registry().isCommandEnabled("creativemotion.sandbox.reset");
            const bool ran = ui->registry().runCommand("creativemotion.sandbox.reset");
            Log(Debug::Info) << "UI check: push reset command ran " << ran;
            report("built");
            if (openeden::editor::PropertySource* panel = ui->registry().panelSource("creativemotion.sandbox"))
                Log(Debug::Info) << "UI check: push panel status: "
                                 << std::get<std::string>(panel->get("sandbox.status"));
        });
        QTimer::singleShot(step * 4, qApp, [=] {
            report("before");
            frame("before");
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const openeden::editor::PropertySource* panel = ui->registry().panelSource("creativemotion.sandbox");
            std::string bone = requestedBone;
            if (bone.empty() && panel != nullptr)
                for (const openeden::editor::PropertyDesc& desc : panel->describe())
                    if (desc.id == "sandbox.bone" && !desc.choices.empty())
                        bone = desc.choices.front();
            setField("sandbox.bone", bone);
            setField("sandbox.direction", openeden::editor::Vec3{ 1.0, 0.4, 0.1 });
            setField("sandbox.magnitudeNs", 300.0);
            const bool ran = ui->registry().runCommand("creativemotion.sandbox.push");
            Log(Debug::Info) << "UI check: push command ran " << ran << " on bone " << bone;
            report("pushed, before any step");
            frame("pending");
        });
        QTimer::singleShot(step * 6, qApp, [=] {
            CSVRender::PhysicsHub::get()->step();
            report("after one step");
            for (int i = 0; i < 11; ++i)
                CSVRender::PhysicsHub::get()->step();
            report("after 12 steps");
            frame("flight");
        });
        QTimer::singleShot(step * 8, qApp, [=] {
            for (int i = 0; i < 400; ++i)
                CSVRender::PhysicsHub::get()->step();
            report("after 412 steps");
            frame("settled");
        });
        QTimer::singleShot(step * 10, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const bool ran = ui->registry().runCommand("creativemotion.sandbox.reset");
            Log(Debug::Info) << "UI check: push reset again ran " << ran;
            report("after reset");
            frame("reset");
        });
    }

    /// OPENEDEN_CS_FACECHECK=<file prefix>;<voice audio file>: the face timeline in the real editor (task 0147). Makes
    /// a curve file for the audio through the face editor module's New Curves File command (its dialog answered by the
    /// check), fills three channels with keys from the audio's loudness, grabs the timeline canvas (waveform, curves, playhead), drags a key
    /// with synthetic mouse events on the canvas (ONE undo step), adds a key with a double click, undoes both through
    /// Edit > Undo, moves the playhead with a click on the ruler, plays the audio, shows the module's dialog, saves and
    /// reads the file back. Everything is logged as "UI check: face ...".
    void runFaceCheck(CSMDoc::Document* document, const std::string& spec, int milliseconds)
    {
        const std::size_t semicolon = spec.find(';');
        if (semicolon == std::string::npos)
        {
            Log(Debug::Error) << "UI check: face: OPENEDEN_CS_FACECHECK needs <prefix>;<voice audio file>";
            return;
        }
        const std::string prefix = spec.substr(0, semicolon);
        const std::filesystem::path audioPath = spec.substr(semicolon + 1);
        std::filesystem::path curvesPath = audioPath;
        curvesPath.replace_extension();
        curvesPath += ".facecurves.json";
        const int step = std::max(500, milliseconds / 16);

        auto strip = []() -> CSVDoc::CurveStrip* {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* found = top->findChild<CSVDoc::CurveStrip*>("moduleCurveStrip"))
                    if (found->isVisibleTo(top))
                        return found;
            return nullptr;
        };
        auto grab = [=](const std::string& name, QWidget* widget) {
            if (widget == nullptr)
            {
                Log(Debug::Info) << "UI check: face frame " << name << ": no widget";
                return;
            }
            const std::string file = prefix + "_" + name + ".png";
            Log(Debug::Info) << "UI check: face frame " << file << ": "
                             << (widget->grab().save(QString::fromStdString(file)) ? "written" : "not written");
        };
        auto send = [](QWidget* target, QEvent::Type type, const QPoint& at, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(at), QPointF(target->mapToGlobal(at)), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        };
        auto keysText = [](const std::vector<openeden::editor::CurveKey>& keys) {
            std::ostringstream text;
            for (const auto& k : keys)
                text << "(" << k.timeSeconds << "," << k.value << ")";
            return text.str();
        };
        auto jawKeys = []() {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const auto tracks = ui->timelineTracks();
            return tracks.mCurves.empty() ? std::vector<openeden::editor::CurveKey>{}
                                          : tracks.mCurves.front()->channelKeys("jawopen");
        };
        auto editMenuAction = [](const QString& itemPrefix) -> QAction* {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* window = qobject_cast<QMainWindow*>(top); window && window->menuBar())
                    for (QAction* menuAction : window->menuBar()->actions())
                        if (menuAction->menu() && menuAction->text().remove('&') == "Edit")
                            for (QAction* inner : menuAction->menu()->actions())
                                if (inner->text().startsWith(itemPrefix))
                                    return inner;
            return nullptr;
        };

        QTimer::singleShot(step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "Face")
                            tabs->setCurrentIndex(i);
            // The New Curves File command, its dialog answered with the audio file and 30 frames per second.
            ui->setDialogRunner([=](const openeden::editor::DialogDesc& desc,
                                    const std::vector<openeden::editor::PropertyValue>&)
                -> std::optional<std::vector<openeden::editor::PropertyValue>> {
                Log(Debug::Info) << "UI check: face dialog asked '" << desc.title << "' with " << desc.fields.size()
                                 << " fields";
                return std::vector<openeden::editor::PropertyValue>{ audioPath.generic_string(), std::int64_t{ 30 } };
            });
            const bool ran = ui->registry().runCommand("face.newCurves");
            ui->resetDialogRunner();
            Log(Debug::Info) << "UI check: face new curves command " << (ran ? "ran" : "failed") << ", file "
                             << (std::filesystem::exists(curvesPath) ? "written" : "missing");
            Log(Debug::Info) << "UI check: face opened " << (ui->activeAsset() ? "yes" : "no") << " " << ui->openError();
            // Keys from the audio's loudness: jawopen follows the envelope, two blinks, funnel on the loud parts.
            const auto filled = ui->timelineTracks();
            if (!filled.mCurves.empty() && !filled.mWaves.empty())
            {
                const auto wave = filled.mWaves.front()->waveform();
                CSMModule::CurveEditor editor(*ui, *filled.mCurves.front());
                const double length = std::min(filled.mCurves.front()->durationSeconds(), 3.0);
                int added = 0;
                for (double t = 0.0; t <= length && wave.sampleRate != 0; t += 0.1)
                {
                    const std::size_t first = static_cast<std::size_t>(t * wave.sampleRate);
                    const std::size_t last = std::min(wave.sampleCount, first + wave.sampleRate / 10);
                    double sum = 0.0;
                    for (std::size_t i = first; i < last; ++i)
                        sum += static_cast<double>(wave.samples[i]) * wave.samples[i];
                    const double rms = last > first ? std::sqrt(sum / (last - first)) : 0.0;
                    const double jaw = std::clamp(rms * 4.0, 0.0, 1.0);
                    added += editor.addKey("jawopen", { t, jaw });
                    added += editor.addKey("mouthfunnel", { t, std::clamp(jaw * 0.5 * (std::fmod(t, 0.4) < 0.2 ? 1.0 : 0.2), 0.0, 1.0) });
                }
                for (double blink : { 0.8, 2.2 })
                    if (blink < length)
                    {
                        added += editor.addKey("eyeblinkleft", { blink - 0.06, 0.0 });
                        added += editor.addKey("eyeblinkleft", { blink, 1.0 });
                        added += editor.addKey("eyeblinkleft", { blink + 0.08, 0.0 });
                    }
                Log(Debug::Info) << "UI check: face filled " << added << " keys from the loudness";
            }
            const auto tracks = ui->timelineTracks();
            Log(Debug::Info) << "UI check: face tracks: curves " << tracks.mCurves.size() << ", waveforms "
                             << tracks.mWaves.size() << ", duration " << tracks.duration();
            if (!tracks.mCurves.empty())
                Log(Debug::Info) << "UI check: face channels " << tracks.mCurves.front()->channels().size();
            if (!tracks.mWaves.empty())
            {
                const auto wave = tracks.mWaves.front()->waveform();
                Log(Debug::Info) << "UI check: face waveform " << wave.sampleCount << " samples at " << wave.sampleRate;
            }
        });
        QTimer::singleShot(2 * step, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            Log(Debug::Info) << "UI check: face strip " << (s ? "visible" : "missing") << " with "
                             << (s ? s->channelCount() : 0) << " channels";
            if (s != nullptr)
            {
                // A floating, sized dock, so that a grab shows the whole canvas.
                for (QWidget* up = s->parentWidget(); up != nullptr; up = up->parentWidget())
                    if (auto* dock = qobject_cast<QDockWidget*>(up))
                    {
                        dock->setFloating(true);
                        dock->resize(900, 360);
                        break;
                    }
                s->selectChannel("jawopen");
            }
        });
        QTimer::singleShot(3 * step, qApp, [=] { grab("waveform_and_curves", strip()); });
        QTimer::singleShot(4 * step, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            if (s == nullptr)
                return;
            CSVDoc::CurveCanvas* canvas = s->canvas();
            const auto keys = jawKeys();
            Log(Debug::Info) << "UI check: face jawopen keys before " << keys.size() << " " << keysText(keys);
            const int index = static_cast<int>(keys.size() / 2);
            const QPoint start = canvas->keyPixel(index);
            QUndoStack& moduleStack = document->getUndoRouting().moduleStack();
            Log(Debug::Info) << "UI check: face drag key " << index << " from pixel " << start.x() << "," << start.y()
                             << ", undo steps " << moduleStack.count();
            send(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
                send(canvas, QEvent::MouseMove, start + QPoint(2 * i, -10 * i), Qt::NoButton, Qt::LeftButton);
            Log(Debug::Info) << "UI check: face during drag: keys " << keysText(jawKeys()) << ", undo steps "
                             << moduleStack.count();
            send(canvas, QEvent::MouseButtonRelease, start + QPoint(16, -80), Qt::LeftButton, Qt::NoButton);
            const auto after = jawKeys();
            Log(Debug::Info) << "UI check: face drag released: undo steps " << moduleStack.count() << ", key "
                             << index << " now (" << (after.size() > static_cast<std::size_t>(index) ? after[index].timeSeconds : -1)
                             << "," << (after.size() > static_cast<std::size_t>(index) ? after[index].value : -1)
                             << "), document modified " << ((document->getState() & CSMDoc::State_Modified) != 0)
                             << ", asset dirty "
                             << (CSVModule::ModuleUi::get()->activeAsset()->isDirty() ? "yes" : "no");
        });
        QTimer::singleShot(5 * step, qApp, [=] { grab("after_drag", strip()); });
        QTimer::singleShot(6 * step, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            if (s == nullptr)
                return;
            CSVDoc::CurveCanvas* canvas = s->canvas();
            QUndoStack& moduleStack = document->getUndoRouting().moduleStack();
            const std::size_t before = jawKeys().size();
            const QPoint at(canvas->width() / 5, canvas->height() - 30);
            send(canvas, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton);
            Log(Debug::Info) << "UI check: face double click add: keys " << before << " -> " << jawKeys().size()
                             << ", undo steps " << moduleStack.count();
        });
        QTimer::singleShot(7 * step, qApp, [=] {
            QUndoStack& moduleStack = document->getUndoRouting().moduleStack();
            QAction* undo = editMenuAction("Undo");
            const std::size_t withBoth = jawKeys().size();
            if (undo != nullptr)
                undo->trigger();
            const std::size_t afterOne = jawKeys().size();
            if (undo != nullptr)
                undo->trigger();
            Log(Debug::Info) << "UI check: face Edit > Undo twice: keys " << withBoth << " -> " << afterOne << " -> "
                             << jawKeys().size() << ", undo steps " << moduleStack.count() << ", index "
                             << moduleStack.index() << ", keys now " << keysText(jawKeys());
        });
        QTimer::singleShot(8 * step, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            if (s == nullptr)
                return;
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            CSVDoc::CurveCanvas* canvas = s->canvas();
            const QPoint at = canvas->rulerPixel(0.6);
            send(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
            send(canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
            Log(Debug::Info) << "UI check: face playhead after a click on the ruler at 0.6 s: "
                             << ui->playheadSeconds();
        });
        QTimer::singleShot(9 * step, qApp, [=] { grab("playhead", strip()); });
        QTimer::singleShot(10 * step, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            if (s == nullptr)
                return;
            ui->setPlayhead(0.0);
            const bool started = s->togglePlayback();
            Log(Debug::Info) << "UI check: face play started " << (started ? "yes" : "no") << ", playing "
                             << (s->playing() ? "yes" : "no");
        });
        QTimer::singleShot(10 * step + 700, qApp, [=] {
            CSVDoc::CurveStrip* s = strip();
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            if (s == nullptr)
                return;
            Log(Debug::Info) << "UI check: face playhead while playing: " << ui->playheadSeconds() << ", playing "
                             << (s->playing() ? "yes" : "no");
            grab("playing", s);
            s->togglePlayback();
            Log(Debug::Info) << "UI check: face playing after stop " << (s->playing() ? "yes" : "no");
        });
        QTimer::singleShot(12 * step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            // The module's Generate dialog: grabbed while it is up, then accepted by the timer.
            QTimer::singleShot(step / 2, qApp, [=] {
                for (QWidget* widget : QApplication::topLevelWidgets())
                    if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->objectName() == "moduleDialog")
                    {
                        const std::string file = prefix + "_dialog.png";
                        Log(Debug::Info) << "UI check: face dialog '" << dialog->windowTitle().toStdString() << "': "
                                         << (dialog->grab().save(QString::fromStdString(file)) ? "written" : "not written");
                        dialog->accept();
                    }
            });
            const bool ran = ui->registry().runCommand("face.generate");
            Log(Debug::Info) << "UI check: face generate command ran " << (ran ? "yes" : "no");
        });
        QTimer::singleShot(13 * step, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            // A real edit, then Save: the file is read back by the check script.
            CSVDoc::CurveStrip* s = strip();
            if (s != nullptr)
            {
                CSVDoc::CurveCanvas* canvas = s->canvas();
                const QPoint at(canvas->width() / 2, canvas->height() - 40);
                send(canvas, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton);
            }
            std::string error;
            const bool dirty = ui->activeAsset()->isDirty();
            const bool saved = ui->activeAsset()->save(error);
            Log(Debug::Info) << "UI check: face saved " << (saved ? "yes" : "no") << " " << error << ", dirty before "
                             << (dirty ? "yes" : "no") << ", dirty after "
                             << (ui->activeAsset()->isDirty() ? "yes" : "no");
            if (auto* models = ui->registry().panelSource("face.models"))
                for (const auto& row : models->describe())
                    Log(Debug::Info) << "UI check: face models row " << row.id << " = "
                                     << std::get<std::string>(models->get(row.id));
        });
    }

    /// The curve generator check (task 0147 slice 2): OPENEDEN_CS_FACEGEN=<prefix>;<voice audio>;<variant>;<directml 0|1>.
    /// Makes a curve file for the audio (the New Curves File command), runs Face > Generate Curves with the given model
    /// (its dialog answered here), waits for the background task's undo step, and logs the time it took and what the
    /// channels hold ("UI check: facegen ..."); frames before and after, then Undo and Redo of the ONE step, then Save.
    /// The models must be in <user data>/models/audio2face.
    void runFaceGenCheck(CSMDoc::Document* document, const std::string& spec, int milliseconds)
    {
        const QStringList parts = QString::fromStdString(spec).split(';');
        if (parts.size() < 4)
        {
            Log(Debug::Error) << "UI check: facegen: OPENEDEN_CS_FACEGEN needs <prefix>;<audio>;<variant>;<directml 0|1>";
            return;
        }
        const std::string prefix = parts[0].toStdString();
        const std::filesystem::path audioPath = parts[1].toStdString();
        const std::string variant = parts[2].toStdString();
        const bool directMl = parts[3] != "0";
        const int deadline = std::max(30000, milliseconds - 15000);
        auto grab = [=](const std::string& name) {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* found = top->findChild<CSVDoc::CurveStrip*>("moduleCurveStrip"))
                    if (found->isVisibleTo(top))
                    {
                        const std::string file = prefix + "_" + name + ".png";
                        Log(Debug::Info) << "UI check: facegen frame " << file << ": "
                                         << (found->grab().save(QString::fromStdString(file)) ? "written" : "not written");
                        return;
                    }
            Log(Debug::Info) << "UI check: facegen frame " << name << ": no strip";
        };
        auto report = [document](const char* label) {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const auto tracks = ui->timelineTracks();
            if (tracks.mCurves.empty())
                return;
            std::ostringstream text;
            for (const char* channel : { "jawopen", "mouthclose", "mouthfunnel", "mouthpucker", "eyeblinkleft", "browinnerup" })
            {
                const auto keys = tracks.mCurves.front()->channelKeys(channel);
                double peak = 0;
                for (const auto& k : keys)
                    peak = std::max(peak, k.value);
                text << " " << channel << " keys " << keys.size() << " peak " << peak << ";";
            }
            Log(Debug::Info) << "UI check: facegen " << label << ":" << text.str() << " dirty "
                             << (ui->activeAsset() && ui->activeAsset()->isDirty() ? "yes" : "no") << ", module undo steps "
                             << document->getUndoRouting().moduleStack().count();
        };

        QTimer::singleShot(1000, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == "Face")
                            tabs->setCurrentIndex(i);
            ui->setDialogRunner([=](const openeden::editor::DialogDesc&, const std::vector<openeden::editor::PropertyValue>&)
                -> std::optional<std::vector<openeden::editor::PropertyValue>> {
                return std::vector<openeden::editor::PropertyValue>{ audioPath.generic_string(), std::int64_t{ 30 } };
            });
            const bool ran = ui->registry().runCommand("face.newCurves");
            ui->resetDialogRunner();
            Log(Debug::Info) << "UI check: facegen new curves " << (ran ? "ran" : "failed") << ", opened "
                             << (ui->activeAsset() ? "yes" : "no") << " " << ui->openError();
            const auto tracks = ui->timelineTracks();
            if (!tracks.mWaves.empty())
                Log(Debug::Info) << "UI check: facegen audio " << tracks.mWaves.front()->waveform().sampleCount
                                 << " samples at " << tracks.mWaves.front()->waveform().sampleRate << " Hz";
        });
        QTimer::singleShot(2000, qApp, [=] {
            for (QWidget* up : QApplication::topLevelWidgets())
                if (auto* found = up->findChild<CSVDoc::CurveStrip*>("moduleCurveStrip"))
                    for (QWidget* w = found->parentWidget(); w != nullptr; w = w->parentWidget())
                        if (auto* dock = qobject_cast<QDockWidget*>(w))
                        {
                            dock->setFloating(true);
                            dock->resize(1000, 420);
                            found->selectChannel("jawopen");
                            break;
                        }
        });
        QTimer::singleShot(3000, qApp, [=] { grab("before"); report("before"); });
        auto started = std::make_shared<std::chrono::steady_clock::time_point>();
        QTimer::singleShot(4000, qApp, [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            ui->setDialogRunner([=](const openeden::editor::DialogDesc& desc, const std::vector<openeden::editor::PropertyValue>&)
                -> std::optional<std::vector<openeden::editor::PropertyValue>> {
                Log(Debug::Info) << "UI check: facegen dialog '" << desc.title << "' fields " << desc.fields.size()
                                 << " choices " << (desc.fields.empty() ? 0 : desc.fields[0].choices.size());
                return std::vector<openeden::editor::PropertyValue>{ variant, false, directMl };
            });
            *started = std::chrono::steady_clock::now();
            const bool ran = ui->registry().runCommand("face.generate");
            ui->resetDialogRunner();
            Log(Debug::Info) << "UI check: facegen generate command " << (ran ? "ran" : "failed") << " (variant " << variant
                             << ", directml " << (directMl ? "yes" : "no") << ")";
        });
        // Poll for the finished task: its command makes the asset dirty.
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [=] {
            CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - *started).count();
            if (ui->activeAsset() != nullptr && ui->activeAsset()->isDirty())
            {
                Log(Debug::Info) << "UI check: facegen finished after " << seconds << " s (wall, from the command)";
                QTimer::singleShot(800, qApp, [=] {
                    grab("generated");
                    report("generated");
                    CSVModule::ModuleUi* u = CSVModule::ModuleUi::get();
                    for (QWidget* top : QApplication::topLevelWidgets())
                        if (auto* window = qobject_cast<QMainWindow*>(top); window && window->menuBar())
                            for (QAction* menuAction : window->menuBar()->actions())
                                if (menuAction->menu() && menuAction->text().remove('&') == "Edit")
                                    for (QAction* inner : menuAction->menu()->actions())
                                        if (inner->text().startsWith("Undo"))
                                        {
                                            inner->trigger();
                                            report("after undo");
                                            for (QAction* again : menuAction->menu()->actions())
                                                if (again->text().startsWith("Redo"))
                                                    again->trigger();
                                            report("after redo");
                                            std::string error;
                                            const bool saved = u->activeAsset()->save(error);
                                            Log(Debug::Info) << "UI check: facegen saved " << (saved ? "yes" : "no") << " " << error;
                                            return;
                                        }
                    Log(Debug::Info) << "UI check: facegen: no Undo action found";
                });
                return;
            }
            if (seconds > deadline / 1000.0)
            {
                Log(Debug::Info) << "UI check: facegen did not finish within " << seconds << " s";
                return;
            }
            QTimer::singleShot(250, qApp, *poll);
        };
        QTimer::singleShot(4500, qApp, *poll);
    }

    /// The body package panel check (task 0069 stage f): OPENEDEN_CS_BODYCHECK=<prefix>;<source>[;<source>...], each
    /// source a module asset path (a body.json, a .edenpak or a merged cache's current), optionally followed by
    /// "|<race> <sex> <view>" to choose that body of a merged cache afterwards. For each source: opens it as a module
    /// asset, shows the AdvancedBodies workspace, logs what the panel's rows show, and writes the model-less Mesh
    /// view's frame (<prefix>_<i>_mesh.png, the region and weld gizmos) and a grab of the window
    /// (<prefix>_<i>_window.png); with a chosen body, the same again after choosing it (<prefix>_<i>b_...). Logged as
    /// "UI check: body ...".
    void runBodyCheck(CSMDoc::Document* document, const std::string& spec, int milliseconds)
    {
        const QStringList parts = QString::fromStdString(spec).split(';', Qt::SkipEmptyParts);
        if (parts.empty())
        {
            Log(Debug::Error) << "UI check: body: OPENEDEN_CS_BODYCHECK needs <prefix>[;<source>]";
            return;
        }
        const std::string prefix = parts.front().toStdString();
        // With no source the check opens the merged body cache of Studio's user data folder, as the module's own
        // "Open Merged Body Cache" command does (Editor Module API 1.1, EditorContext::userDataPath).
        const int sources = std::max(1, static_cast<int>(parts.size()) - 1);
        const int step = std::max(700, milliseconds / (6 * sources + 4));

        auto visibleWindow = []() -> QMainWindow* {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto* window = qobject_cast<QMainWindow*>(widget); window && window->isVisible())
                    return window;
            return nullptr;
        };
        auto meshView = []() -> CSVRender::MeshViewWidget* {
            for (QWidget* widget : QApplication::topLevelWidgets())
                for (auto* view : widget->findChildren<CSVRender::MeshViewWidget*>())
                    if (view->showsModuleAsset())
                        return view;
            return nullptr;
        };
        auto logRows = [](const std::string& label) {
            int findings = 0;
            int regions = 0;
            // Main windows only: a floating dock is a top-level widget too, and also a child of its window.
            for (QWidget* top : QApplication::topLevelWidgets())
                for (QWidget* field :
                    qobject_cast<QMainWindow*>(top) ? top->findChildren<QWidget*>() : QList<QWidget*>())
                {
                    const QString name = field->objectName();
                    if (!name.startsWith("property_") || !field->isVisibleTo(top))
                        continue;
                    QString text;
                    if (auto* line = qobject_cast<QLineEdit*>(field))
                        text = line->text();
                    else if (auto* spin = qobject_cast<QSpinBox*>(field))
                        text = QString::number(spin->value());
                    else if (auto* combo = qobject_cast<QComboBox*>(field))
                        text = combo->currentText();
                    else
                        continue;
                    findings += name.startsWith("property_finding.") ? 1 : 0;
                    regions += name.startsWith("property_region.") ? 1 : 0;
                    Log(Debug::Info) << "UI check: body " << label << " row " << name.mid(9).toStdString() << " = "
                                     << text.toStdString();
                }
            Log(Debug::Info) << "UI check: body " << label << " panel shows " << findings << " finding rows and "
                             << regions << " region rows";
            // Another module's panel must show nothing for a body document (per-module filtering), and the Asset
            // Browser lists the chosen workspace's module only.
            int foreignRows = 0;
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* dock = top->findChild<QDockWidget*>("modulePanel_creativemotion.panel"))
                    for (QWidget* field : dock->findChildren<QWidget*>())
                        foreignRows += field->objectName().startsWith("property_") ? 1 : 0;
            const CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
            Log(Debug::Info) << "UI check: body " << label << " creativemotion panel rows " << foreignRows
                             << ", active owner '" << modules->activeOwner() << "', asset owner filter '"
                             << modules->assetOwnerFilter() << "'";
        };
        auto capture = [=](const std::string& name) {
            // The panel dock shares its side with the inspector, which keeps its height: float one window's panel
            // at a size that shows its rows, for its own grab.
            QDockWidget* panel = nullptr;
            if (QMainWindow* window = visibleWindow())
                panel = window->findChild<QDockWidget*>("modulePanel_advancedbodies.panel");
            if (panel != nullptr && !panel->isFloating())
            {
                panel->setFloating(true);
                panel->resize(720, 900);
            }
            if (CSVRender::MeshViewWidget* view = meshView())
            {
                // The Mesh view's own window, large enough to see the body.
                for (QWidget* up = view->parentWidget(); up != nullptr; up = up->parentWidget())
                    if (auto* dock = qobject_cast<QDockWidget*>(up))
                    {
                        if (!dock->isFloating())
                        {
                            dock->setFloating(true);
                            dock->resize(960, 720);
                        }
                        break;
                    }
                view->frameObject();
                Log(Debug::Info) << "UI check: body mesh view draws " << view->moduleLineCount() << " lines";
            }
            else
                Log(Debug::Error) << "UI check: body: no model-less Mesh view";
            // A frame again once the floated view has its size. The frame frameObject asks for rebuilds the swapchain,
            // and the capture draws one frame of its own (Render::Engine::drawFrameAndCapture) at the swapchain's
            // size: on the desktop an expose event used to ask for it, but a window off the desktop
            // (OPENEDEN_QUIET_WINDOW) is never exposed (QWindowsWindow::handleWmPaint finds no update rectangle).
            QTimer::singleShot(step / 4, qApp, [=] {
                if (CSVRender::MeshViewWidget* view = meshView())
                    view->flagAsModified();
            });
            QTimer::singleShot(step / 2, qApp, [=] {
                if (CSVRender::MeshViewWidget* view = meshView())
                {
                    const std::filesystem::path file = prefix + "_" + name + "_mesh.png";
                    Log(Debug::Info) << "UI check: body frame " << file.string() << ": "
                                     << (view->saveScreenshot(file) ? "written" : "no frame");
                }
                if (QMainWindow* window = visibleWindow())
                    window->grab().save(QString::fromStdString(prefix + "_" + name + "_window.png"));
                if (panel != nullptr)
                    panel->grab().save(QString::fromStdString(prefix + "_" + name + "_panel.png"));
            });
        };

        for (int i = 0; i < sources; ++i)
        {
            const QStringList source = i + 1 < parts.size() ? parts[i + 1].split('|') : QStringList{ QString() };
            const std::filesystem::path path = source.front().toStdString();
            const std::string entry = source.size() > 1 ? source[1].toStdString() : std::string();
            const int base = (2 + 6 * i) * step;
            const std::string label = std::to_string(i);
            QTimer::singleShot(base, qApp, [=] {
                CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
                const auto start = std::chrono::steady_clock::now();
                if (path.empty())
                    ui->registry().runCommand("advancedbodies.openMerged");
                else
                    ui->openAsset(path);
                Log(Debug::Info)
                    << "UI check: body " << label << " opened "
                    << (path.empty() ? std::string("the default merged cache") : path.string()) << ": "
                    << (ui->activeAsset() ? "yes" : "no") << " format " << ui->activeFormat() << " " << ui->openError()
                    << " in "
                    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                    << " ms";
                for (QWidget* top : QApplication::topLevelWidgets())
                    if (auto* tabs = top->findChild<QTabBar*>("workspaceTabs"))
                        for (int t = 0; t < tabs->count(); ++t)
                            if (tabs->tabText(t) == "AdvancedBodies")
                                tabs->setCurrentIndex(t);
                if (meshView() == nullptr)
                    if (QMainWindow* window = visibleWindow())
                        for (QAction* action : window->findChildren<QAction*>("showModuleAssetInMeshViewer"))
                        {
                            action->trigger();
                            break;
                        }
            });
            QTimer::singleShot(base + 2 * step, qApp, [=] {
                logRows(label);
                capture(label);
            });
            if (entry.empty())
                continue;
            QTimer::singleShot(base + 3 * step, qApp, [=] {
                // Through the module's own command, as Extensions > advancedbodies_editor > Bodies > Next Body does.
                CSVModule::ModuleUi* ui = CSVModule::ModuleUi::get();
                const int undoBefore = document->getUndoStack().count();
                constexpr int maxSteps = 4096;
                int steps = 0;
                for (; steps < maxSteps && ui->activeAsset() != nullptr; ++steps)
                {
                    const openeden::editor::PropertyValue shown = ui->activeAsset()->properties().get("source.entry");
                    const std::string* title = std::get_if<std::string>(&shown);
                    if (title == nullptr || *title == entry)
                        break;
                    const auto start = std::chrono::steady_clock::now();
                    if (!ui->registry().runCommand("advancedbodies.nextBody"))
                        break;
                    Log(Debug::Info)
                        << "UI check: body " << label << " Next Body took "
                        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                        << " ms";
                }
                const openeden::editor::PropertyValue shown = ui->activeAsset() != nullptr
                    ? ui->activeAsset()->properties().get("source.entry")
                    : openeden::editor::PropertyValue(std::string());
                const std::string* title = std::get_if<std::string>(&shown);
                Log(Debug::Info) << "UI check: body " << label << " chose " << (title ? *title : std::string("?"))
                                 << " after " << steps << " Next Body commands (wanted " << entry << "), undo steps "
                                 << undoBefore << " -> " << document->getUndoStack().count() << ", document modified "
                                 << ((document->getState() & CSMDoc::State_Modified) != 0);
                // After the replaced forms are gone (they are deleted when the event loop runs again).
                QTimer::singleShot(step, qApp, [=] {
                    logRows(label + "b");
                    capture(label + "b");
                });
            });
        }
    }

    /// The Context Inspector check, see startUiCheck. Free of the editor class: it only needs the document.
    void runInspectorCheck(CSMDoc::Document* document, const std::string& cell, int milliseconds)
    {
        CSMWorld::IdTable* references = dynamic_cast<CSMWorld::IdTable*>(
            document->getData().getTableModel(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_References)));
        const int idColumn = references->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
        const int cellColumn = references->findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
        const int posXColumn = references->findColumnIndex(CSMWorld::Columns::ColumnId_PositionXPos);
        std::string id;
        for (int row = 0; row < references->rowCount() && id.empty(); ++row)
            if (references->data(references->index(row, cellColumn)).toString().toStdString() == cell)
                id = references->data(references->index(row, idColumn)).toString().toStdString();
        Log(Debug::Info) << "UI check: inspector target " << id;

        auto posX = [=] {
            const QModelIndex row = references->getModelIndex(id, idColumn);
            return references->data(references->index(row.row(), posXColumn)).toDouble();
        };
        // The window that has the scene: the inspector of the window whose scene has the object selected.
        auto sceneInspector = [] {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (widget->findChild<CSVWorld::SceneSubView*>() != nullptr)
                    return widget->findChild<CSVDoc::ContextInspector*>();
            return static_cast<CSVDoc::ContextInspector*>(nullptr);
        };

        const int step = std::max(500, milliseconds / 10);
        QTimer::singleShot(step * 4, qApp, [=] {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto* scene = widget->findChild<CSVWorld::SceneSubView*>())
                    if (auto* viewport = scene->getWorldspaceWidget())
                        viewport->selectGroup({ id });
            Log(Debug::Info) << "UI check: inspector selected " << id;
        });
        QTimer::singleShot(step * 5, qApp, [=] {
            CSVDoc::ContextInspector* panel = sceneInspector();
            if (panel == nullptr)
                return;
            panel->poll();
            Log(Debug::Info) << "UI check: inspector shows " << panel->selectedIds().size() << " object(s), posX "
                             << posX();
            const double before = posX();
            auto* field = panel->findChild<QDoubleSpinBox*>("inspectorField0");
            const int undoBefore = document->getUndoStack().count();
            field->setValue(before + 100.0);
            Log(Debug::Info) << "UI check: inspector edit: posX " << before << " -> " << posX() << ", undo stack +"
                             << (document->getUndoStack().count() - undoBefore);
        });
        QTimer::singleShot(step * 6, qApp, [=] {
            document->getUndoStack().undo();
            Log(Debug::Info) << "UI check: inspector undo: posX " << posX();
        });
    }
}

void CS::Editor::startUiCheck()
{
    // OPENEDEN_CS_UI_CHECK=<scene hint>;<milliseconds>, with a content file as the first argument. Once the file has
    // loaded this opens the references table and a scene subview as the menus do, then closes every window after the
    // time given. For checks with no person at the keyboard (task 0014, C3).
    const char* value = std::getenv("OPENEDEN_CS_UI_CHECK");
    if (!value)
        return;
    const std::string check(value);
    const std::size_t separator = check.rfind(';');
    if (separator == std::string::npos)
        return;
    const std::string hint = check.substr(0, separator);
    const int milliseconds = std::stoi(check.substr(separator + 1));

    QApplication::instance()->installEventFilter(new NoActivation(this));
    connect(&mDocumentManager, &CSMDoc::DocumentManager::loadingStopped, this,
        [this, hint, milliseconds](CSMDoc::Document* document, bool completed, const std::string& error) {
            if (!completed)
            {
                Log(Debug::Error) << "UI check: loading failed: " << error;
                QApplication::exit(1);
                return;
            }
            mViewManager->addView(document, CSMWorld::UniversalId(CSMWorld::UniversalId::Type_References), "");
            mViewManager->addView(
                document, CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Scene, "sys::default"), hint);
            Log(Debug::Info) << "UI check: views opened";
            // OPENEDEN_CS_MESH=<vfs path>: also opens the mesh viewer on that model, as the Asset Browser does
            // (task 0043). The grab (OPENEDEN_CS_GRAB) then shows its panels.
            if (const char* mesh = std::getenv("OPENEDEN_CS_MESH"))
                mViewManager->addView(
                    document, CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Mesh, std::string(mesh)), "");
            // OPENEDEN_CS_GRAB=<folder>: render every document window with QWidget::grab shortly before the close,
            // as <folder>/window<N>.png. This shows the Qt panels (not the 3D window inside them, which Qt cannot
            // grab); the engine capture (--auto-screenshot) shows that. For checks with nobody at the screen.
            // OPENEDEN_CS_INSPECT=<cell>: for the Context Inspector check (task 0024, stage C). Selects the first
            // instance in that cell through the viewport, then moves it by editing the inspector's Position X field
            // and undoes the edit, logging what each step shows ("UI check: inspector ...").
            if (const char* cell = std::getenv("OPENEDEN_CS_INSPECT"))
                runInspectorCheck(document, cell, milliseconds);
            // OPENEDEN_CS_EXTENSIONS=<folder>: logs the Extensions menu's lines, opens the Extension Manager and
            // grabs it as <folder>/extensions.png (task 0024, stage D).
            if (const char* extensionsFolder = std::getenv("OPENEDEN_CS_EXTENSIONS"))
            {
                const QString folder = QString::fromUtf8(extensionsFolder);
                QTimer::singleShot(milliseconds / 3, this, [folder] {
                    for (QWidget* widget : QApplication::topLevelWidgets())
                    {
                        auto* window = qobject_cast<QMainWindow*>(widget);
                        if (window == nullptr || !window->isVisible())
                            continue;
                        for (QAction* menuAction : window->menuBar()->actions())
                            if (menuAction->menu() != nullptr && menuAction->text() == "Extensions")
                            {
                                for (QAction* line : menuAction->menu()->actions())
                                    Log(Debug::Info) << "UI check: Extensions menu: " << line->text().toStdString();
                                for (QAction* line : menuAction->menu()->actions())
                                    if (line->text() == "Extension Manager...")
                                        line->trigger();
                                return;
                            }
                    }
                });
                QTimer::singleShot(milliseconds / 3 + 800, this, [folder] {
                    for (QWidget* widget : QApplication::topLevelWidgets())
                        if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible())
                            dialog->grab().save(folder + "/extensions.png");
                });
            }
            // OPENEDEN_CS_PHYSICS=<file prefix>: the physics check (task 0030), see runPhysicsCheck.
            if (const char* prefix = std::getenv("OPENEDEN_CS_PHYSICS"))
                runPhysicsCheck(prefix, milliseconds);
            // OPENEDEN_CS_MODULECHECK=<file prefix>: the editor module check, see runModuleCheck.
            if (const char* prefix = std::getenv("OPENEDEN_CS_MODULECHECK"))
                runModuleCheck(document, prefix, milliseconds);
            // OPENEDEN_CS_RIGCHECK=<prefix>;<rig>;<profile>: the CreativeMotion rig check, see runRigCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_RIGCHECK"))
                runRigCheck(document, spec, milliseconds);
            // OPENEDEN_CS_HANDLECHECK=<prefix>;<rig>: selection, labels and handle dragging, see runHandleCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_HANDLECHECK"))
                runHandleCheck(document, spec, milliseconds);
            // OPENEDEN_CS_FACECHECK=<prefix>;<curves file>: the face timeline check, see runFaceCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_FACECHECK"))
                runFaceCheck(document, spec, milliseconds);
            // OPENEDEN_CS_FACEGEN=<prefix>;<audio>;<variant>;<directml 0|1>: the curve generator check, see runFaceGenCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_FACEGEN"))
                runFaceGenCheck(document, spec, milliseconds);
            // OPENEDEN_CS_PUSHCHECK=<prefix>;<rig>;<profile>[;<bone>]: the push sandbox check, see runPushCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_PUSHCHECK"))
                runPushCheck(spec, milliseconds);
            // OPENEDEN_CS_BODYCHECK=<prefix>;<source>[|<body>]...: the body package panel check, see runBodyCheck.
            if (const char* spec = std::getenv("OPENEDEN_CS_BODYCHECK"))
                runBodyCheck(document, spec, milliseconds);
            // OPENEDEN_CS_DROP=1: the constructed drop check, see runDropCheck.
            if (std::getenv("OPENEDEN_CS_DROP") != nullptr)
                runDropCheck(document, milliseconds);
            // OPENEDEN_CS_ASSET=<search text>: for the Asset Browser check (task 0024, stage C).
            if (const char* search = std::getenv("OPENEDEN_CS_ASSET"))
                runAssetCheck(search, milliseconds);
            // OPENEDEN_CS_TRIGGER=<text>: a third of the way in, triggers the menu action with that text (for
            // example "Classic" or "Eden Studio" in Window > Layout) in every document window, so that a switch
            // of layout with sub views open can be grabbed.
            if (const char* trigger = std::getenv("OPENEDEN_CS_TRIGGER"))
            {
                const QString text = QString::fromUtf8(trigger);
                QTimer::singleShot(milliseconds / 3, this, [text] {
                    for (QWidget* widget : QApplication::topLevelWidgets())
                        if (qobject_cast<QMainWindow*>(widget))
                            for (QAction* action : widget->findChildren<QAction*>())
                                if (action->text() == text && action->isCheckable())
                                    action->trigger();
                });
            }
            if (const char* grabFolder = std::getenv("OPENEDEN_CS_GRAB"))
            {
                const QString folder = QString::fromUtf8(grabFolder);
                QTimer::singleShot(milliseconds > 1500 ? milliseconds - 1500 : 0, this, [folder] {
                    int index = 0;
                    for (QWidget* widget : QApplication::topLevelWidgets())
                        if (widget->isVisible() && qobject_cast<QMainWindow*>(widget))
                            widget->grab().save(folder + "/window" + QString::number(index++) + ".png");
                });
            }
            // Repeatedly: a window that is not the document's last refuses its close event, deleting itself later, and
            // closeAllWindows stops at the first refusal.
            QTimer::singleShot(milliseconds, this, [this] {
                Log(Debug::Info) << "UI check: closing";
                auto* closer = new QTimer(this);
                connect(closer, &QTimer::timeout, [] { QApplication::closeAllWindows(); });
                closer->start(sUiCheckCloseIntervalMs);
            });
        });
}

void CS::Editor::documentAdded(CSMDoc::Document* document)
{
    mViewManager->addView(document);
}

void CS::Editor::documentAboutToBeRemoved(CSMDoc::Document* document)
{
    if (mMerge.getDocument() == document)
        mMerge.cancel();
}

void CS::Editor::lastDocumentDeleted()
{
    QApplication::quit();
}

void CS::Editor::mergeDocument(CSMDoc::Document* document)
{
    mMerge.configure(document);
    mMerge.show();
    mMerge.raise();
    mMerge.activateWindow();
}
