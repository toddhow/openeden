#include "editor.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>
#include <QTreeView>
#include <QUndoStack>
#include <QWidget>

#include <boost/program_options.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/documentmanager.hpp>
#include <apps/opencs/view/doc/adjusterwidget.hpp>
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
#include "view/render/worldspacewidget.hpp"
#include "view/world/scenesubview.hpp"

CS::Editor::Editor(int argc, char** argv)
    : mConfigVariables(readConfiguration())
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

    mViewManager = new CSVDoc::ViewManager(mDocumentManager);
    mViewManager->setExtensionHost(mExtensionHost.get());
    if (argc > 1)
    {
        mFileToLoad = argv[1];
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

CS::Editor::~Editor()
{
    delete mViewManager;

    mLockFile.unlock();
    mPidFile.close();

    if (mServer && std::filesystem::exists(mPid))
        std::filesystem::remove(mPid);
}

boost::program_options::variables_map CS::Editor::readConfiguration()
{
    boost::program_options::variables_map variables;
    boost::program_options::options_description desc("Syntax: eden-studio <options>\nAllowed options");

    auto addOption = desc.add_options();
    addOption("data",
        boost::program_options::value<Files::MaybeQuotedPathContainer>()
            ->default_value(Files::MaybeQuotedPathContainer(), "data")
            ->multitoken()
            ->composing());
    addOption("data-local",
        boost::program_options::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
            Files::MaybeQuotedPathContainer::value_type(), ""));
    addOption("encoding", boost::program_options::value<std::string>()->default_value("win1252"));
    // The same names as the game's (apps/openmw/options.cpp): extension= lines in openmw.cfg are shared.
    addOption("extension",
        boost::program_options::value<std::vector<std::string>>()
            ->default_value(std::vector<std::string>(), "")
            ->multitoken()
            ->composing(),
        "OpenEden extension ID(s) to enable (extension= lines in openmw.cfg)");
    addOption("fallback-archive",
        boost::program_options::value<std::vector<std::string>>()
            ->default_value(std::vector<std::string>(), "fallback-archive")
            ->multitoken());
    addOption("fallback",
        boost::program_options::value<Fallback::FallbackMap>()
            ->default_value(Fallback::FallbackMap(), "")
            ->multitoken()
            ->composing(),
        "fallback values");
    Files::ConfigurationManager::addCommonOptions(desc);

    boost::program_options::notify(variables);

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
