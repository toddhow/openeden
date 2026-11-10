#include "view.hpp"

#include "../module/moduleui.hpp"
#include "../render/physicshub.hpp"
#include <apps/opencs/model/module/moduleregistry.hpp>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "../../model/doc/document.hpp"
#include "../../model/doc/state.hpp"

#include "../../model/extension/editorextensionhost.hpp"
#include "../../model/prefs/layoutstore.hpp"
#include "../../model/prefs/shortcut.hpp"
#include "../../model/prefs/state.hpp"

#include "../../model/world/idtable.hpp"

#include "../lod/tileviewer.hpp"
#include "../render/worldspacewidget.hpp"
#include "../world/dialoguesubview.hpp"
#include "../world/meshsubview.hpp"
#include "../world/scenesubview.hpp"
#include "../world/scriptsubview.hpp"
#include "../world/subviews.hpp"
#include "../world/tablesubview.hpp"

#include "../tools/subviews.hpp"

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/prefs/shortcutmanager.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/view/doc/subviewfactory.hpp>

#include <components/files/conversion.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/misc/scalableicon.hpp>
#include <components/misc/strings/format.hpp>
#include <components/misc/timeconvert.hpp>
#include <components/version/version.hpp>

#include "assetbrowser.hpp"
#include "contextinspector.hpp"
#include "extensionmanager.hpp"
#include "globaldebugprofilemenu.hpp"
#include "operations.hpp"
#include "outputlogpanel.hpp"
#include "runlogsubview.hpp"
#include "studioshell.hpp"
#include "subview.hpp"
#include "subviewfactoryimp.hpp"
#include "viewmanager.hpp"

QRect desktopRect()
{
    QRegion virtualGeometry;
    for (auto screen : QGuiApplication::screens())
    {
        virtualGeometry += screen->geometry();
    }
    return virtualGeometry.boundingRect();
}

void CSVDoc::View::closeEvent(QCloseEvent* event)
{
    if (!mViewManager.closeRequest(this))
        event->ignore();
    else
    {
        mShell->saveDockState();
        // closeRequest() returns true if last document
        mViewManager.removeDocAndView(mDocument);
    }
}

void CSVDoc::View::setupFileMenu()
{
    QMenu* file = menuBar()->addMenu(tr("File"));

    QAction* newGame = createMenuEntry("New Game", ":menu-new-game", file, "document-file-newgame");
    connect(newGame, &QAction::triggered, this, &View::newGameRequest);

    QAction* newAddon = createMenuEntry("New Addon", ":menu-new-addon", file, "document-file-newaddon");
    connect(newAddon, &QAction::triggered, this, &View::newAddonRequest);

    QAction* open = createMenuEntry("Open", ":menu-open", file, "document-file-open");
    connect(open, &QAction::triggered, this, &View::loadDocumentRequest);

    QAction* save = createMenuEntry("Save", ":menu-save", file, "document-file-save");
    connect(save, &QAction::triggered, this, &View::save);
    mSave = save;

    file->addSeparator();

    QAction* verify = createMenuEntry("Verify", ":menu-verify", file, "document-file-verify");
    connect(verify, &QAction::triggered, this, &View::verify);
    mVerify = verify;

    QAction* merge = createMenuEntry("Merge", ":menu-merge", file, "document-file-merge");
    connect(merge, &QAction::triggered, this, &View::merge);
    mMerge = merge;

    QAction* loadErrors = createMenuEntry("Error Log", ":error-log", file, "document-file-errorlog");
    connect(loadErrors, &QAction::triggered, this, &View::loadErrorLog);

    QAction* meta = createMenuEntry(CSMWorld::UniversalId::Type_MetaDatas, file, "document-file-metadata");
    connect(meta, &QAction::triggered, this, &View::addMetaDataSubView);

    // The mesh viewer (task 0043).
    QAction* openMesh = file->addAction(tr("Open Mesh..."));
    openMesh->setObjectName("document-file-openmesh");
    connect(openMesh, &QAction::triggered, this, &View::openMeshFile);

    // The read-only Skyrim SE cell viewer (task 0086, world model option 1).
    QAction* openSkyrimCell = file->addAction(tr("Open Skyrim Cell..."));
    openSkyrimCell->setObjectName("document-file-openskyrimcell");
    connect(openSkyrimCell, &QAction::triggered, this, &View::openSkyrimCell);

    // The LOD tile viewer (task 0055, stage S1e).
    QAction* openLod = file->addAction(tr("Open LOD Tile Pack..."));
    openLod->setObjectName("document-file-openlodpack");
    connect(openLod, &QAction::triggered, this, &View::openLodPack);

    file->addSeparator();

    QAction* close = createMenuEntry("Close", ":menu-close", file, "document-file-close");
    connect(close, &QAction::triggered, this, &View::close);

    QAction* exit = createMenuEntry("Exit", ":menu-exit", file, "document-file-exit");
    connect(exit, &QAction::triggered, this, &View::exit);

    connect(this, &View::exitApplicationRequest, &mViewManager, &ViewManager::exitApplication);
}

namespace
{

    void updateUndoRedoAction(QAction* action, const std::string& settingsKey)
    {
        QKeySequence seq;
        CSMPrefs::State::get().getShortcutManager().getSequence(settingsKey, seq);
        action->setShortcut(seq);
    }

}

void CSVDoc::View::undoActionChanged()
{
    updateUndoRedoAction(mUndo, "document-edit-undo");
}

void CSVDoc::View::redoActionChanged()
{
    updateUndoRedoAction(mRedo, "document-edit-redo");
}

void CSVDoc::View::setupEditMenu()
{
    QMenu* edit = menuBar()->addMenu(tr("Edit"));

    // Undo and Redo act on the stack touched last: the document's own, or the editor modules' (CSMDoc::UndoRouting).
    CSMDoc::UndoRouting& routing = mDocument->getUndoRouting();
    mUndo = routing.group().createUndoAction(this, tr("Undo"));
    setupShortcut("document-edit-undo", mUndo);
    connect(mUndo, &QAction::changed, this, &View::undoActionChanged);
    mUndo->setIcon(Misc::ScalableIcon::load(":menu-undo"));
    edit->addAction(mUndo);

    mRedo = routing.group().createRedoAction(this, tr("Redo"));
    connect(mRedo, &QAction::changed, this, &View::redoActionChanged);
    setupShortcut("document-edit-redo", mRedo);
    mRedo->setIcon(Misc::ScalableIcon::load(":menu-redo"));
    edit->addAction(mRedo);

    connect(&routing, &CSMDoc::UndoRouting::activeChanged, this, [this] { updateUndoRedoEnabled(); });
    connect(&routing.group(), &QUndoGroup::canUndoChanged, this, [this] { updateUndoRedoEnabled(); });
    connect(&routing.group(), &QUndoGroup::canRedoChanged, this, [this] { updateUndoRedoEnabled(); });
    // The focus moving into a module's own widgets (a property form) or out of them chooses the stack when nothing was
    // edited in between.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (now == nullptr || !isAncestorOf(now))
            return;
        bool inModuleUi = false;
        for (const QWidget* widget = now; widget != nullptr && !inModuleUi; widget = widget->parentWidget())
            inModuleUi = widget->property(CSVModule::ModuleUi::sOwnedProperty).toBool();
        mDocument->getUndoRouting().noteFocus(inModuleUi);
    });

    QAction* userSettings = createMenuEntry("Preferences", ":menu-preferences", edit, "document-edit-preferences");
    connect(userSettings, &QAction::triggered, this, &View::editSettingsRequest);

    QAction* search = createMenuEntry(CSMWorld::UniversalId::Type_Search, edit, "document-edit-search");
    connect(search, &QAction::triggered, this, &View::addSearchSubView);
}

void CSVDoc::View::setupViewMenu()
{
    // The record tables (World, Mechanics, Characters, Assets) are submenus of View, added by their own functions.
    QMenu* view = menuBar()->addMenu(tr("View"));
    mViewMenu = view;

    QAction* filters = createMenuEntry(CSMWorld::UniversalId::Type_Filters, view, "document-mechanics-filters");
    connect(filters, &QAction::triggered, this, &View::addFiltersSubView);

    view->addSeparator();
}

void CSVDoc::View::setupWorldMenu()
{
    QMenu* world = mViewMenu->addMenu(tr("World"));

    QAction* referenceables
        = createMenuEntry(CSMWorld::UniversalId::Type_Referenceables, world, "document-world-referencables");
    connect(referenceables, &QAction::triggered, this, &View::addReferenceablesSubView);

    QAction* references = createMenuEntry(CSMWorld::UniversalId::Type_References, world, "document-world-references");
    connect(references, &QAction::triggered, this, &View::addReferencesSubView);

    world->addSeparator();

    QAction* cells = createMenuEntry(CSMWorld::UniversalId::Type_Cells, world, "document-world-cells");
    connect(cells, &QAction::triggered, this, &View::addCellsSubView);

    QAction* lands = createMenuEntry(CSMWorld::UniversalId::Type_Lands, world, "document-world-lands");
    connect(lands, &QAction::triggered, this, &View::addLandsSubView);

    QAction* landTextures
        = createMenuEntry(CSMWorld::UniversalId::Type_LandTextures, world, "document-world-landtextures");
    connect(landTextures, &QAction::triggered, this, &View::addLandTexturesSubView);

    QAction* grid = createMenuEntry(CSMWorld::UniversalId::Type_Pathgrids, world, "document-world-pathgrid");
    connect(grid, &QAction::triggered, this, &View::addPathgridSubView);

    world->addSeparator();

    QAction* regions = createMenuEntry(CSMWorld::UniversalId::Type_Regions, world, "document-world-regions");
    connect(regions, &QAction::triggered, this, &View::addRegionsSubView);

    QAction* regionMap = createMenuEntry(CSMWorld::UniversalId::Type_RegionMap, world, "document-world-regionmap");
    connect(regionMap, &QAction::triggered, this, &View::addRegionMapSubView);
}

void CSVDoc::View::setupMechanicsMenu()
{
    QMenu* mechanics = mViewMenu->addMenu(tr("Mechanics"));

    QAction* scripts = createMenuEntry(CSMWorld::UniversalId::Type_Scripts, mechanics, "document-mechanics-scripts");
    connect(scripts, &QAction::triggered, this, &View::addScriptsSubView);

    QAction* startScripts
        = createMenuEntry(CSMWorld::UniversalId::Type_StartScripts, mechanics, "document-mechanics-startscripts");
    connect(startScripts, &QAction::triggered, this, &View::addStartScriptsSubView);

    QAction* globals = createMenuEntry(CSMWorld::UniversalId::Type_Globals, mechanics, "document-mechanics-globals");
    connect(globals, &QAction::triggered, this, &View::addGlobalsSubView);

    QAction* gmsts = createMenuEntry(CSMWorld::UniversalId::Type_Gmsts, mechanics, "document-mechanics-gamesettings");
    connect(gmsts, &QAction::triggered, this, &View::addGmstsSubView);

    mechanics->addSeparator();

    QAction* spells = createMenuEntry(CSMWorld::UniversalId::Type_Spells, mechanics, "document-mechanics-spells");
    connect(spells, &QAction::triggered, this, &View::addSpellsSubView);

    QAction* enchantments
        = createMenuEntry(CSMWorld::UniversalId::Type_Enchantments, mechanics, "document-mechanics-enchantments");
    connect(enchantments, &QAction::triggered, this, &View::addEnchantmentsSubView);

    QAction* magicEffects
        = createMenuEntry(CSMWorld::UniversalId::Type_MagicEffects, mechanics, "document-mechanics-magiceffects");
    connect(magicEffects, &QAction::triggered, this, &View::addMagicEffectsSubView);
}

void CSVDoc::View::setupCharacterMenu()
{
    QMenu* characters = mViewMenu->addMenu(tr("Characters"));

    QAction* skills = createMenuEntry(CSMWorld::UniversalId::Type_Skills, characters, "document-character-skills");
    connect(skills, &QAction::triggered, this, &View::addSkillsSubView);

    QAction* classes = createMenuEntry(CSMWorld::UniversalId::Type_Classes, characters, "document-character-classes");
    connect(classes, &QAction::triggered, this, &View::addClassesSubView);

    QAction* factions = createMenuEntry(CSMWorld::UniversalId::Type_Faction, characters, "document-character-factions");
    connect(factions, &QAction::triggered, this, &View::addFactionsSubView);

    QAction* races = createMenuEntry(CSMWorld::UniversalId::Type_Races, characters, "document-character-races");
    connect(races, &QAction::triggered, this, &View::addRacesSubView);

    QAction* birthsigns
        = createMenuEntry(CSMWorld::UniversalId::Type_Birthsigns, characters, "document-character-birthsigns");
    connect(birthsigns, &QAction::triggered, this, &View::addBirthsignsSubView);

    QAction* bodyParts
        = createMenuEntry(CSMWorld::UniversalId::Type_BodyParts, characters, "document-character-bodyparts");
    connect(bodyParts, &QAction::triggered, this, &View::addBodyPartsSubView);

    characters->addSeparator();

    QAction* topics = createMenuEntry(CSMWorld::UniversalId::Type_Topics, characters, "document-character-topics");
    connect(topics, &QAction::triggered, this, &View::addTopicsSubView);

    QAction* topicInfos
        = createMenuEntry(CSMWorld::UniversalId::Type_TopicInfos, characters, "document-character-topicinfos");
    connect(topicInfos, &QAction::triggered, this, &View::addTopicInfosSubView);

    characters->addSeparator();

    QAction* journals
        = createMenuEntry(CSMWorld::UniversalId::Type_Journals, characters, "document-character-journals");
    connect(journals, &QAction::triggered, this, &View::addJournalsSubView);

    QAction* journalInfos
        = createMenuEntry(CSMWorld::UniversalId::Type_JournalInfos, characters, "document-character-journalinfos");
    connect(journalInfos, &QAction::triggered, this, &View::addJournalInfosSubView);
}

void CSVDoc::View::setupAssetsMenu()
{
    QMenu* assets = mViewMenu->addMenu(tr("Assets"));

    QAction* reload = createMenuEntry("Reload", ":menu-reload", assets, "document-assets-reload");
    connect(reload, &QAction::triggered, &mDocument->getData(), &CSMWorld::Data::assetsChanged);

    assets->addSeparator();

    QAction* sounds = createMenuEntry(CSMWorld::UniversalId::Type_Sounds, assets, "document-assets-sounds");
    connect(sounds, &QAction::triggered, this, &View::addSoundsSubView);

    QAction* soundGens = createMenuEntry(CSMWorld::UniversalId::Type_SoundGens, assets, "document-assets-soundgens");
    connect(soundGens, &QAction::triggered, this, &View::addSoundGensSubView);

    assets->addSeparator(); // resources follow here

    QAction* meshes = createMenuEntry(CSMWorld::UniversalId::Type_Meshes, assets, "document-assets-meshes");
    connect(meshes, &QAction::triggered, this, &View::addMeshesSubView);

    QAction* icons = createMenuEntry(CSMWorld::UniversalId::Type_Icons, assets, "document-assets-icons");
    connect(icons, &QAction::triggered, this, &View::addIconsSubView);

    QAction* musics = createMenuEntry(CSMWorld::UniversalId::Type_Musics, assets, "document-assets-musics");
    connect(musics, &QAction::triggered, this, &View::addMusicsSubView);

    QAction* soundFiles = createMenuEntry(CSMWorld::UniversalId::Type_SoundsRes, assets, "document-assets-soundres");
    connect(soundFiles, &QAction::triggered, this, &View::addSoundsResSubView);

    QAction* textures = createMenuEntry(CSMWorld::UniversalId::Type_Textures, assets, "document-assets-textures");
    connect(textures, &QAction::triggered, this, &View::addTexturesSubView);

    QAction* videos = createMenuEntry(CSMWorld::UniversalId::Type_Videos, assets, "document-assets-videos");
    connect(videos, &QAction::triggered, this, &View::addVideosSubView);
}

void CSVDoc::View::setupDebugMenu()
{
    QMenu* debug = menuBar()->addMenu(tr("Simulation"));

    // Play, Pause and Step drive the editor's physics world (task 0030). Without a hub (no editor around the
    // window) they stay off.
    CSVRender::PhysicsHub* hub = CSVRender::PhysicsHub::get();
    QAction* play = debug->addAction(tr("Play"));
    QAction* pause = debug->addAction(tr("Pause"));
    QAction* step = debug->addAction(tr("Step"));
    QAction* dropBall = debug->addAction(tr("Drop Test Ball"));
    QAction* clearWorld = debug->addAction(tr("Clear Physics World"));
    QAction* drawBodies = debug->addAction(tr("Physics Debug Draw"));
    drawBodies->setCheckable(true);
    dropBall->setToolTip(tr("Adds a ground slab and a ball to the physics world, a test until cells fill it."));
    for (QAction* action : { play, pause, step, dropBall, clearWorld, drawBodies })
    {
        action->setEnabled(hub != nullptr);
        action->setStatusTip(action->toolTip());
    }
    if (hub != nullptr)
    {
        connect(play, &QAction::triggered, hub, &CSVRender::PhysicsHub::play);
        connect(pause, &QAction::triggered, hub, &CSVRender::PhysicsHub::pause);
        connect(step, &QAction::triggered, hub, &CSVRender::PhysicsHub::step);
        connect(dropBall, &QAction::triggered, hub, &CSVRender::PhysicsHub::dropTestBall);
        connect(clearWorld, &QAction::triggered, hub, &CSVRender::PhysicsHub::clearWorld);
        connect(drawBodies, &QAction::toggled, hub, &CSVRender::PhysicsHub::setDebugDraw);
        auto update = [=] {
            play->setEnabled(!hub->isPlaying());
            pause->setEnabled(hub->isPlaying());
            drawBodies->setChecked(hub->debugDrawEnabled());
        };
        connect(hub, &CSVRender::PhysicsHub::stateChanged, this, update);
        update();
    }
    debug->addSeparator();

    QAction* profiles = createMenuEntry(CSMWorld::UniversalId::Type_DebugProfiles, debug, "document-debug-profiles");
    connect(profiles, &QAction::triggered, this, &View::addDebugProfilesSubView);

    debug->addSeparator();

    mGlobalDebugProfileMenu = new GlobalDebugProfileMenu(
        &dynamic_cast<CSMWorld::IdTable&>(
            *mDocument->getData().getTableModel(CSMWorld::UniversalId::Type_DebugProfiles)),
        this);

    connect(mGlobalDebugProfileMenu, &GlobalDebugProfileMenu::triggered, this,
        [this](const std::string& profile) { this->run(profile, ""); });

    QAction* runDebug = debug->addMenu(mGlobalDebugProfileMenu);
    runDebug->setText(tr("Run OpenMW"));
    setupShortcut("document-debug-run", runDebug);
    runDebug->setIcon(Misc::ScalableIcon::load(":run-openmw"));

    QAction* stopDebug = createMenuEntry("Stop OpenMW", ":stop-openmw", debug, "document-debug-shutdown");
    connect(stopDebug, &QAction::triggered, this, &View::stop);
    mStopDebug = stopDebug;

    QAction* runLog = createMenuEntry(CSMWorld::UniversalId::Type_RunLog, debug, "document-debug-runlog");
    connect(runLog, &QAction::triggered, this, &View::addRunLogSubView);
}

void CSVDoc::View::setupExtensionsMenu()
{
    QMenu* extensions = menuBar()->addMenu(tr("Extensions"));
    const CSMExtension::EditorExtensionHost* host = mViewManager.getExtensionHost();

    QAction* manager = extensions->addAction(tr("Extension Manager..."));
    connect(manager, &QAction::triggered, this, [this, host] {
        if (mExtensionManager == nullptr)
            mExtensionManager = new ExtensionManagerDialog(host, this);
        else
            mExtensionManager->refresh();
        mExtensionManager->show();
        mExtensionManager->raise();
    });

    extensions->addSeparator();

    // The editor modules' commands (task 0031): one submenu per module, items by menuPath.
    if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get())
    {
        std::vector<std::string> moduleIds;
        for (const auto& command : modules->registry().commands())
            if (std::find(moduleIds.begin(), moduleIds.end(), command.mModule) == moduleIds.end())
                moduleIds.push_back(command.mModule);
        for (const std::string& moduleId : moduleIds)
        {
            QMenu* moduleMenu = extensions->addMenu(QString::fromStdString(moduleId));
            moduleMenu->setObjectName(QString::fromStdString("moduleMenu_" + moduleId));
            std::vector<std::pair<QAction*, std::string>> actions;
            for (const auto& command : modules->registry().commands())
            {
                if (command.mModule != moduleId)
                    continue;
                QMenu* target = moduleMenu;
                QString text = QString::fromStdString(command.mDesc.label);
                if (!command.mDesc.menuPath.empty())
                {
                    const QStringList parts
                        = QString::fromStdString(command.mDesc.menuPath).split('/', Qt::SkipEmptyParts);
                    for (int i = 0; i + 1 < parts.size(); ++i)
                    {
                        QMenu* sub = target->findChild<QMenu*>(parts[i], Qt::FindDirectChildrenOnly);
                        if (sub == nullptr)
                        {
                            sub = target->addMenu(parts[i]);
                            sub->setObjectName(parts[i]);
                        }
                        target = sub;
                    }
                    if (!parts.isEmpty())
                        text = parts.last();
                }
                QAction* action = target->addAction(text);
                action->setObjectName(QString::fromStdString("moduleCommand_" + command.mDesc.id));
                if (!command.mDesc.defaultShortcut.empty())
                    action->setShortcut(QKeySequence(QString::fromStdString(command.mDesc.defaultShortcut)));
                const std::string id = command.mDesc.id;
                connect(action, &QAction::triggered, this, [modules, id] { modules->registry().runCommand(id); });
                actions.emplace_back(action, id);
            }
            connect(moduleMenu, &QMenu::aboutToShow, this, [modules, actions] {
                for (const auto& [action, id] : actions)
                    action->setEnabled(modules->registry().isCommandEnabled(id));
            });
        }
        if (!moduleIds.empty())
            extensions->addSeparator();
        // A Mesh view with no model draws the active module asset's gizmos (task 0069 stage f: the body package
        // panel's regions and weld seams).
        QAction* moduleMesh = extensions->addAction(tr("Show Module Asset in Mesh Viewer"));
        moduleMesh->setObjectName("showModuleAssetInMeshViewer");
        connect(moduleMesh, &QAction::triggered, this,
            [this] { addSubView(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Mesh, std::string())); });
        extensions->addSeparator();
    }

    // The host's state, as menu text: what the host did at start-up, and one line for each extension.
    if (host == nullptr)
    {
        extensions->addAction(tr("No extension host"))->setEnabled(false);
        return;
    }
    extensions->addAction(QString::fromStdString(host->summary()))->setEnabled(false);
    for (const CSMExtension::ExtensionInfo& info : host->extensions())
    {
        QString state = tr("not loaded");
        if (info.mState == CSMExtension::ExtensionInfo::State::Active)
            state = tr("loaded");
        else if (info.mState == CSMExtension::ExtensionInfo::State::Failed)
            state = tr("failed: %1").arg(QString::fromStdString(info.mMessage));
        QAction* line = extensions->addAction(
            QString("%1: %2").arg(QString::fromStdString(info.mId).replace("&", "&&"), state.replace("&", "&&")));
        line->setEnabled(false);
    }
}

void CSVDoc::View::setupWindowMenu()
{
    QMenu* window = menuBar()->addMenu(tr("Window"));

    QAction* newWindow = createMenuEntry("New View", ":menu-new-window", window, "document-view-newview");
    connect(newWindow, &QAction::triggered, this, &View::newView);

    mShowStatusBar = createMenuEntry("Toggle Status Bar", ":menu-status-bar", window, "document-view-statusbar");
    connect(mShowStatusBar, &QAction::toggled, this, &View::toggleShowStatusBar);
    mShowStatusBar->setCheckable(true);
    mShowStatusBar->setChecked(CSMPrefs::get()["Windows"]["show-statusbar"].isTrue());

    window->addAction(mShowStatusBar);

    window->addSeparator();

    // Layout: the two arrangements, and a way back to the first-run docks of the one in use.
    QMenu* layout = window->addMenu(tr("Layout"));
    QActionGroup* group = new QActionGroup(this);
    const std::pair<const char*, CSMPrefs::EditorLayout> choices[]
        = { { "Eden Studio", CSMPrefs::EditorLayout::EdenStudio }, { "Classic", CSMPrefs::EditorLayout::Classic } };
    for (const auto& [name, value] : choices)
    {
        QAction* choice = layout->addAction(tr(name));
        choice->setCheckable(true);
        choice->setChecked(mShell->layout() == value);
        group->addAction(choice);
        connect(choice, &QAction::triggered, this, [this, value] { changeLayout(value); });
        connect(mShell, &StudioShell::layoutChanged, choice,
            [choice, value](CSMPrefs::EditorLayout current) { choice->setChecked(current == value); });
    }
    layout->addSeparator();
    QAction* reset = layout->addAction(tr("Reset Layout"));
    connect(reset, &QAction::triggered, this, [this] { mShell->resetLayout(); });

    // One toggle per panel of the Eden Studio layout; they have no meaning in Classic.
    QMenu* panels = window->addMenu(tr("Panels"));
    for (QDockWidget* dock : mShell->panels())
    {
        QAction* toggle = dock->toggleViewAction();
        panels->addAction(toggle);
        connect(mShell, &StudioShell::layoutChanged, toggle, [toggle](CSMPrefs::EditorLayout current) {
            toggle->setEnabled(current == CSMPrefs::EditorLayout::EdenStudio);
        });
        toggle->setEnabled(mShell->isStudio());
    }
}

void CSVDoc::View::setupHelpMenu()
{
    QMenu* help = menuBar()->addMenu(tr("Help"));

    QAction* helpInfo = createMenuEntry("Help", ":info", help, "document-help-help");
    connect(helpInfo, &QAction::triggered, this, &View::openHelp);

    QAction* tutorial = createMenuEntry("Tutorial", ":info", help, "document-help-tutorial");
    connect(tutorial, &QAction::triggered, this, &View::tutorial);

    QAction* about = createMenuEntry("About Eden Studio", ":info", help, "document-help-about");
    connect(about, &QAction::triggered, this, &View::infoAbout);

    QAction* aboutQt = createMenuEntry("About Qt", ":qt", help, "document-help-qt");
    connect(aboutQt, &QAction::triggered, this, &View::infoAboutQt);
}

QAction* CSVDoc::View::createMenuEntry(CSMWorld::UniversalId::Type type, QMenu* menu, const char* shortcutName)
{
    const std::string title = CSMWorld::UniversalId(type).getTypeName();
    QAction* entry = new QAction(QString::fromStdString(title), this);
    setupShortcut(shortcutName, entry);
    const std::string iconName = CSMWorld::UniversalId(type).getIcon();
    if (!iconName.empty() && iconName != ":placeholder")
        entry->setIcon(Misc::ScalableIcon::load(QString::fromStdString(iconName)));

    menu->addAction(entry);

    return entry;
}

QAction* CSVDoc::View::createMenuEntry(
    const std::string& title, const std::string& iconName, QMenu* menu, const char* shortcutName)
{
    QAction* entry = new QAction(QString::fromStdString(title), this);
    setupShortcut(shortcutName, entry);
    if (!iconName.empty() && iconName != ":placeholder")
        entry->setIcon(Misc::ScalableIcon::load(QString::fromStdString(iconName)));

    menu->addAction(entry);

    return entry;
}

void CSVDoc::View::setupUi()
{
    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupWorldMenu();
    setupMechanicsMenu();
    setupCharacterMenu();
    setupAssetsMenu();
    setupDebugMenu(); // the Simulation menu
    setupExtensionsMenu();
    setupWindowMenu();
    setupHelpMenu();
}

void CSVDoc::View::setupShortcut(const char* name, QAction* action)
{
    CSMPrefs::Shortcut* shortcut = new CSMPrefs::Shortcut(name, this);
    shortcut->associateAction(action);
}

void CSVDoc::View::updateTitle()
{
    std::ostringstream stream;

    stream << Files::pathToUnicodeString(mDocument->getSavePath().filename());

    if (mDocument->getState() & CSMDoc::State_Modified)
        stream << " *";

    if (mViewTotal > 1)
        stream << " [" << (mViewIndex + 1) << "/" << mViewTotal << "]";

    CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];

    bool hideTitle = windows["hide-subview"].isTrue() && mSubViews.size() == 1 && !mSubViews.at(0)->isFloating();

    if (hideTitle)
        stream << " - " << mSubViews.at(0)->getTitle();

    setWindowTitle(QString::fromUtf8(stream.str().c_str()));
}

void CSVDoc::View::updateSubViewIndices(SubView* view)
{
    CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];

    if (view && mSubViews.contains(view))
    {
        mSubViews.removeOne(view);

        // adjust (reduce) the scroll area (even floating), except when it is "Scrollbar Only"
        if (windows["mainwindow-scrollbar"].toString() == "Grow then Scroll")
            updateScrollbar();
    }

    updateSceneOpen();

    bool hideTitle = windows["hide-subview"].isTrue() && mSubViews.size() == 1 && !mSubViews.at(0)->isFloating();

    updateTitle();

    for (SubView* subView : mSubViews)
    {
        if (!subView->isFloating())
        {
            if (hideTitle)
            {
                subView->setTitleBarWidget(new QWidget(this));
                subView->setWindowTitle(QString::fromUtf8(subView->getTitle().c_str()));
            }
            else
            {
                delete subView->titleBarWidget();
                subView->setTitleBarWidget(nullptr);
            }
        }
    }
}

void CSVDoc::View::updateUndoRedoEnabled()
{
    const bool editing = !(mDocument->getState() & CSMDoc::State_Locked);
    mUndo->setEnabled(editing && mDocument->getUndoRouting().canUndo());
    mRedo->setEnabled(editing && mDocument->getUndoRouting().canRedo());
}

void CSVDoc::View::updateActions()
{
    bool editing = !(mDocument->getState() & CSMDoc::State_Locked);
    bool running = mDocument->getState() & CSMDoc::State_Running;

    for (std::vector<QAction*>::iterator iter(mEditingActions.begin()); iter != mEditingActions.end(); ++iter)
        (*iter)->setEnabled(editing);

    updateUndoRedoEnabled();

    mSave->setEnabled(!(mDocument->getState() & CSMDoc::State_Saving) && !running);
    mVerify->setEnabled(!(mDocument->getState() & CSMDoc::State_Verifying));

    mGlobalDebugProfileMenu->updateActions(running);
    mStopDebug->setEnabled(running);

    mMerge->setEnabled(mDocument->getContentFiles().size() > 1 && !(mDocument->getState() & CSMDoc::State_Merging));
}

CSVDoc::View::View(ViewManager& viewManager, CSMDoc::Document* document, int totalViews)
    : mViewManager(viewManager)
    , mDocument(document)
    , mViewIndex(totalViews - 1)
    , mViewTotal(totalViews)
    , mScroll(nullptr)
    , mScrollbarOnly(false)
{
    CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];

    int width = std::max(windows["default-width"].toInt(), 300);
    int height = std::max(windows["default-height"].toInt(), 300);

    resize(width, height);

    mSubViewWindow.setDockOptions(QMainWindow::AllowNestedDocks);

    mShell = new StudioShell(*this, mSubViewWindow,
        CSMPrefs::LayoutStore(CSMPrefs::State::get().getUserConfigPath() / CSMPrefs::layoutStateFileName));

    // Eden Studio puts the sub view area straight in the middle: its docks and the window's own size do the work
    // that the scroll area does for Classic.
    if (mShell->isStudio() || windows["mainwindow-scrollbar"].toString() == "Grow Only")
    {
        setCentralWidget(&mSubViewWindow);
    }
    else
    {
        createScrollArea();
    }

    // Eden Studio has side panels and a bottom row, which need more room than the old default window gave a table.
    if (mShell->isStudio())
        resize(std::max(width, 1360), std::max(height, 860));

    mOperations = new Operations;
    mOperations->setObjectName("operations"); // saveState needs it
    addDockWidget(Qt::BottomDockWidgetArea, mOperations);
    mShell->setOperationsDock(mOperations);
    mShell->start();

    // The workspace strip's second tab is the mesh viewer (task 0043): it shows the open one, or asks for a file.
    if (QDockWidget* strip = mShell->workspaces())
        if (QTabBar* tabs = strip->findChild<QTabBar*>("workspaceTabs"))
            connect(tabs, &QTabBar::currentChanged, this, [this, tabs](int index) {
                if (index != 1)
                    return;
                for (SubView* subView : mSubViews)
                    if (dynamic_cast<CSVWorld::MeshSubView*>(subView) != nullptr)
                    {
                        subView->setFocus();
                        subView->raise();
                        return;
                    }
                openMeshFile();
                if (tabs->currentIndex() == 1)
                    tabs->setCurrentIndex(0);
            });

    setContextMenuPolicy(Qt::NoContextMenu);

    updateTitle();

    setupUi();

    updateActions();

    CSVWorld::addSubViewFactories(mSubViewFactory);
    CSVTools::addSubViewFactories(mSubViewFactory);

    mSubViewFactory.add(CSMWorld::UniversalId::Type_RunLog, new SubViewFactory<RunLogSubView>);

    setupPanels();

    connect(mOperations, &Operations::abortOperation, this, &View::abortOperation);

    connect(&CSMPrefs::State::get(), &CSMPrefs::State::settingChanged, this, &View::settingChanged);
}

const CSMDoc::Document* CSVDoc::View::getDocument() const
{
    return mDocument;
}

CSMDoc::Document* CSVDoc::View::getDocument()
{
    return mDocument;
}

void CSVDoc::View::setIndex(int viewIndex, int totalViews)
{
    mViewIndex = viewIndex;
    mViewTotal = totalViews;
    updateTitle();
}

void CSVDoc::View::updateDocumentState()
{
    updateTitle();
    updateActions();

    static const int operations[] = {
        CSMDoc::State_Saving,
        CSMDoc::State_Verifying,
        CSMDoc::State_Searching,
        CSMDoc::State_Merging,
        // end marker
        -1,
    };

    int state = mDocument->getState();

    for (int i = 0; operations[i] != -1; ++i)
        if (!(state & operations[i]))
            mOperations->quitOperation(operations[i]);

    QList<CSVDoc::SubView*> subViews = findChildren<CSVDoc::SubView*>();

    for (QList<CSVDoc::SubView*>::iterator iter(subViews.begin()); iter != subViews.end(); ++iter)
        (*iter)->setEditLock(state & CSMDoc::State_Locked);

    if (mInspector != nullptr)
        mInspector->setEditLock(state & CSMDoc::State_Locked);
}

void CSVDoc::View::updateProgress(int current, int max, int type, int threads)
{
    mOperations->setProgress(current, max, type, threads);
}

void CSVDoc::View::addSubView(const CSMWorld::UniversalId& id, const std::string& hint)
{
    CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];

    bool isReferenceable = id.getClass() == CSMWorld::UniversalId::Class_RefRecord;

    // User setting to reuse sub views (on a per top level view basis)
    if (windows["reuse"].isTrue())
    {
        for (SubView* sb : mSubViews)
        {
            bool isSubViewReferenceable = sb->getUniversalId().getType() == CSMWorld::UniversalId::Type_Referenceable;

            if ((isReferenceable && isSubViewReferenceable && id.getId() == sb->getUniversalId().getId())
                || (!isReferenceable && id == sb->getUniversalId()))
            {
                sb->setFocus();
                if (!hint.empty())
                    sb->useHint(hint);
                return;
            }
        }
    }

    if (mScroll)
        QObject::connect(mScroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &View::moveScrollBarToEnd);

    // User setting for limiting the number of sub views per top level view.
    // Automatically open a new top level view if this number is exceeded
    //
    // If the sub view limit setting is one, the sub view title bar is hidden and the
    // text in the main title bar is adjusted accordingly
    if (mSubViews.size() >= windows["max-subviews"].toInt()) // create a new top level view
    {
        mViewManager.addView(mDocument, id, hint);

        return;
    }

    SubView* view = nullptr;
    if (isReferenceable)
    {
        view = mSubViewFactory.makeSubView(
            CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Referenceable, id), *mDocument);
    }
    else
    {
        view = mSubViewFactory.makeSubView(id, *mDocument);
    }
    assert(view);
    view->setParent(this);
    view->setEditLock(mDocument->getState() & CSMDoc::State_Locked);
    mSubViews.append(view); // only after assert

    int minWidth = windows["minimum-width"].toInt();
    view->setMinimumWidth(minWidth);

    view->setStatusBar(mShowStatusBar->isChecked());

    // Work out how to deal with additional subviews
    //
    // Policy for "Grow then Scroll":
    //
    // - Increase the horizontal width of the mainwindow until it becomes greater than or equal
    //   to the screen (monitor) width.
    // - Move the mainwindow position sideways if necessary to fit within the screen.
    // - Any more additions increases the size of the mSubViewWindow (horizontal scrollbar
    //   should become visible)
    // - Move the scroll bar to the newly added subview
    //
    mScrollbarOnly = windows["mainwindow-scrollbar"].toString() == "Scrollbar Only";

    updateWidth(true, minWidth);

    dockSubView(view);

    // Horizontal orientation doesn't respect layout direction, so we need to move the new view
    if (windows["subview-open-direction"].toString() == "Open Left" && mSubViews.size() > 1
        && mSubViewWindow.dockWidgetArea(view) == Qt::TopDockWidgetArea)
    {
        // Get list of top-area dock widget subviews sorted by coordinates
        QList<SubView*> orderedTopViews;
        for (const auto& windowView : mSubViews)
            if (windowView != view && mSubViewWindow.dockWidgetArea(windowView) == Qt::TopDockWidgetArea)
                orderedTopViews.append(windowView);
        if (!orderedTopViews.isEmpty())
        {
            std::sort(orderedTopViews.begin(), orderedTopViews.end(), [](auto* a, auto* b) {
                if (a->x() != b->x())
                    return a->x() < b->x();
                else if (a->y() != b->y())
                    return a->y() < b->y();
                return a->getUniversalId() < b->getUniversalId();
            });

            // Split twice to make the new view the leftmost.
            // If the leftmost view is nested, the new view will be added above it.
            mSubViewWindow.splitDockWidget(orderedTopViews[0], view, Qt::Orientation::Horizontal);
            mSubViewWindow.splitDockWidget(view, orderedTopViews[0], Qt::Orientation::Horizontal);
        }
    }

    updateSubViewIndices();

    connect(view, &SubView::focusId, this, &View::addSubView);

    connect(view, qOverload<SubView*>(&SubView::closeRequest), this, &View::closeRequest);

    connect(view, &SubView::updateTitle, this, &View::updateTitle);

    connect(view, &SubView::updateSubViewIndices, this, &View::updateSubViewIndices);

    CSVWorld::TableSubView* tableView = dynamic_cast<CSVWorld::TableSubView*>(view);
    if (tableView)
    {
        connect(this, &View::requestFocus, tableView, &CSVWorld::TableSubView::requestFocus);
    }

    CSVWorld::SceneSubView* sceneView = dynamic_cast<CSVWorld::SceneSubView*>(view);
    if (sceneView)
    {
        connect(sceneView, &CSVWorld::SceneSubView::requestFocus, this, &View::onRequestFocus);
    }

    if (CSMPrefs::State::get()["ID Tables"]["subview-new-window"].isTrue())
    {
        CSVWorld::DialogueSubView* dialogueView = dynamic_cast<CSVWorld::DialogueSubView*>(view);
        if (dialogueView)
            dialogueView->setFloating(true);

        CSVWorld::ScriptSubView* scriptView = dynamic_cast<CSVWorld::ScriptSubView*>(view);
        if (scriptView)
            scriptView->setFloating(true);
    }

    view->show();
    view->raise(); // in a tab group, the new sub view should be the visible tab
    updateSceneOpen();

    if (!hint.empty())
        view->useHint(hint);
}

void CSVDoc::View::moveScrollBarToEnd(int min, int max)
{
    if (mScroll)
    {
        CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];
        if (windows["subview-open-direction"].toString() == "Open Left")
            mScroll->horizontalScrollBar()->setValue(min);
        else
            mScroll->horizontalScrollBar()->setValue(max);

        QObject::disconnect(mScroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &View::moveScrollBarToEnd);
    }
}

void CSVDoc::View::settingChanged(const CSMPrefs::Setting* setting)
{
    if (*setting == "Windows/hide-subview")
        updateSubViewIndices(nullptr);
    else if (*setting == "Windows/mainwindow-scrollbar")
    {
        if (mShell->isStudio())
            return;
        if (setting->toString() != "Grow Only")
        {
            if (mScroll)
            {
                if (setting->toString() == "Scrollbar Only")
                {
                    mScrollbarOnly = true;
                    mSubViewWindow.setMinimumWidth(0);
                }
                else if (mScrollbarOnly)
                {
                    mScrollbarOnly = false;
                    updateScrollbar();
                }
            }
            else
            {
                createScrollArea();
            }
        }
        else if (mScroll)
        {
            mScroll->takeWidget();
            setCentralWidget(&mSubViewWindow);
            mScroll->deleteLater();
            mScroll = nullptr;
        }
    }
}

void CSVDoc::View::setupPanels()
{
    // The panels exist in both layouts; Classic keeps their docks hidden. Each dock owns its panel.
    auto setContent = [](QDockWidget* dock, QWidget* panel) {
        QWidget* placeholder = dock->widget();
        dock->setWidget(panel);
        delete placeholder;
    };

    AssetBrowser* browser = new AssetBrowser(*mDocument);
    setContent(mShell->assetBrowser(), browser);
    connect(browser, &AssetBrowser::openRequested, this, &View::addSubView);

    mInspector = new ContextInspector(*mDocument, [this] { return currentViewport(); });
    setContent(mShell->contextInspector(), mInspector);
    connect(mInspector, &ContextInspector::openRecordRequested, this,
        [this](const CSMWorld::UniversalId& id) { addSubView(id); });
    mInspector->setEditLock(mDocument->getState() & CSMDoc::State_Locked);

    setContent(mShell->outputLog(), new OutputLogPanel);
}

CSVRender::WorldspaceWidget* CSVDoc::View::currentViewport() const
{
    // The scene the user is working in: the one that has the keyboard focus, else the first one open.
    CSVWorld::SceneSubView* first = nullptr;
    for (SubView* subView : mSubViews)
    {
        auto* scene = dynamic_cast<CSVWorld::SceneSubView*>(subView);
        if (scene == nullptr)
            continue;
        if (first == nullptr)
            first = scene;
        if (QWidget* focus = QApplication::focusWidget(); focus != nullptr && scene->isAncestorOf(focus))
            return scene->getWorldspaceWidget();
    }
    return first != nullptr ? first->getWorldspaceWidget() : nullptr;
}

void CSVDoc::View::dockSubView(SubView* view)
{
    // Classic: every sub view in the top area, as OpenMW-CS always did.
    if (!mShell->isStudio())
    {
        mSubViewWindow.addDockWidget(Qt::TopDockWidgetArea, view);
        return;
    }

    // Eden Studio: 3D scenes are the viewport, in the top area. Everything else (record tables, editors, scripts)
    // is a workspace page: the bottom area, as tabs.
    if (dynamic_cast<CSVWorld::SceneSubView*>(view) != nullptr || dynamic_cast<CSVWorld::MeshSubView*>(view) != nullptr)
    {
        mSubViewWindow.addDockWidget(Qt::TopDockWidgetArea, view);
        return;
    }

    SubView* sibling = nullptr;
    for (SubView* other : mSubViews)
        if (other != view && !other->isFloating() && mSubViewWindow.dockWidgetArea(other) == Qt::BottomDockWidgetArea)
            sibling = other;

    mSubViewWindow.setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    mSubViewWindow.addDockWidget(Qt::BottomDockWidgetArea, view);
    if (sibling != nullptr)
        mSubViewWindow.tabifyDockWidget(sibling, view);
}

void CSVDoc::View::updateSceneOpen()
{
    bool open = false;
    for (SubView* subView : mSubViews)
        if (dynamic_cast<CSVWorld::SceneSubView*>(subView) != nullptr
            || dynamic_cast<CSVWorld::MeshSubView*>(subView) != nullptr)
            open = true;
    mShell->setSceneOpen(open);
}

void CSVDoc::View::changeLayout(CSMPrefs::EditorLayout layout)
{
    mShell->setLayout(layout);

    // The central widget: Eden Studio has the sub view area directly, Classic keeps the user's scroll setting.
    if (mShell->isStudio())
    {
        if (mScroll)
        {
            mScroll->takeWidget();
            setCentralWidget(&mSubViewWindow); // deletes the scroll area later
            mScroll = nullptr;
        }
        mSubViewWindow.setMinimumWidth(0);
    }
    else if (!mScroll && CSMPrefs::State::get()["Windows"]["mainwindow-scrollbar"].toString() != "Grow Only")
    {
        takeCentralWidget(); // setCentralWidget would delete the sub view area, which is a member
        createScrollArea();
    }

    // Sub views that are already open move to the places of the new layout.
    for (SubView* subView : mSubViews)
    {
        if (subView->isFloating())
            continue;
        mSubViewWindow.removeDockWidget(subView);
        dockSubView(subView);
        subView->show();
    }
    updateSceneOpen();
    updateSubViewIndices();
}

void CSVDoc::View::openMeshFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open Mesh"), QString(), tr("Models (*.nif *.dae)"));
    if (file.isEmpty())
        return;
    // The viewer reads through the virtual file system, so the file must be in a "meshes" folder of a data folder.
    const int at = file.toLower().lastIndexOf("/meshes/");
    if (at < 0)
    {
        QMessageBox::warning(this, tr("Open Mesh"),
            tr("The file must be inside a \"meshes\" folder of one of the data folders in use."));
        return;
    }
    addSubView(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Mesh, file.mid(at + 8).toStdString()));
}

void CSVDoc::View::openSkyrimCell()
{
    bool accepted = false;
    const QString editorId = QInputDialog::getText(this, tr("Open Skyrim Cell"),
        tr("Editor id of the cell (the load order is read from OPENEDEN_SKYRIMSE_PLUGINS):"), QLineEdit::Normal,
        QString(), &accepted);
    if (!accepted || editorId.trimmed().isEmpty())
        return;
    addSubView(
        CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Mesh, "skyrimcell:" + editorId.trimmed().toStdString()));
}

void CSVDoc::View::openLodPack()
{
    const QString file
        = QFileDialog::getOpenFileName(this, tr("Open LOD Tile Pack"), QString(), tr("Eden packs (*.edenpak)"));
    if (file.isEmpty())
        return;
    // A window of its own, owned by this one and deleted with it (Qt parent), so no handle is kept.
    auto* viewer = new CSVLod::TileViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    QString error;
    if (!viewer->openPack(file, error))
    {
        QMessageBox::warning(this, tr("Open LOD Tile Pack"), error);
        delete viewer;
        return;
    }
    viewer->show();
}

void CSVDoc::View::newView()
{
    mViewManager.addView(mDocument);
}

void CSVDoc::View::save()
{
    mDocument->save();
}

void CSVDoc::View::openHelp()
{
    Misc::HelpViewer::openHelp("manuals/openmw-cs/index.html");
}

void CSVDoc::View::tutorial()
{
    Misc::HelpViewer::openHelp("manuals/openmw-cs/tour.html");
}

void CSVDoc::View::infoAbout()
{
    // Get current OpenMW version
    QString versionInfo = (Version::getOpenmwVersionDescription() +
#if defined(__x86_64__) || defined(_M_X64)
        " (64-bit)")
                              .c_str();
#else
        " (32-bit)")
                              .c_str();
#endif

    // Get current year
    const auto copyrightInfo = Misc::timeToString(std::chrono::system_clock::now(), "Copyright © 2008-%Y OpenMW Team");

    QString aboutText
        = QString(
            "<p style=\"white-space: pre-wrap;\">"
            "<b><h2>Eden Studio</h2></b>"
            "%1\n\n"
            "%2\n\n"
            "%3\n\n"
            "<table>"
            "<tr><td>%4</td><td><a href=\"https://openmw.org\">https://openmw.org</a></td></tr>"
            "<tr><td>%5</td><td><a href=\"https://forum.openmw.org\">https://forum.openmw.org</a></td></tr>"
            "<tr><td>%6</td><td><a "
            "href=\"https://gitlab.com/OpenMW/openmw/issues\">https://gitlab.com/OpenMW/openmw/issues</a></td></tr>"
            "<tr><td>%7</td><td><a "
            "href=\"https://web.libera.chat/#openmw\">ircs://irc.libera.chat/#openmw</a></td></tr>"
            "</table>"
            "</p>")
              .arg(versionInfo,
                  tr("Eden Studio is the content and scene editor of OpenEden, a free and open source game engine "
                     "based on OpenMW."),
                  tr(copyrightInfo.c_str()), tr("Home Page:"), tr("Forum:"), tr("Bug Tracker:"), tr("IRC:"));

    QMessageBox::about(this, "About Eden Studio", aboutText);
}

void CSVDoc::View::infoAboutQt()
{
    QMessageBox::aboutQt(this);
}

void CSVDoc::View::verify()
{
    addSubView(mDocument->verify());
}

void CSVDoc::View::addGlobalsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Globals);
}

void CSVDoc::View::addGmstsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Gmsts);
}

void CSVDoc::View::addSkillsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Skills);
}

void CSVDoc::View::addClassesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Classes);
}

void CSVDoc::View::addFactionsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Factions);
}

void CSVDoc::View::addRacesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Races);
}

void CSVDoc::View::addSoundsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Sounds);
}

void CSVDoc::View::addScriptsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Scripts);
}

void CSVDoc::View::addRegionsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Regions);
}

void CSVDoc::View::addBirthsignsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Birthsigns);
}

void CSVDoc::View::addSpellsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Spells);
}

void CSVDoc::View::addCellsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Cells);
}

void CSVDoc::View::addReferenceablesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Referenceables);
}

void CSVDoc::View::addReferencesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_References);
}

void CSVDoc::View::addRegionMapSubView()
{
    addSubView(CSMWorld::UniversalId::Type_RegionMap);
}

void CSVDoc::View::addFiltersSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Filters);
}

void CSVDoc::View::addTopicsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Topics);
}

void CSVDoc::View::addJournalsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Journals);
}

void CSVDoc::View::addTopicInfosSubView()
{
    addSubView(CSMWorld::UniversalId::Type_TopicInfos);
}

void CSVDoc::View::addJournalInfosSubView()
{
    addSubView(CSMWorld::UniversalId::Type_JournalInfos);
}

void CSVDoc::View::addEnchantmentsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Enchantments);
}

void CSVDoc::View::addBodyPartsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_BodyParts);
}

void CSVDoc::View::addSoundGensSubView()
{
    addSubView(CSMWorld::UniversalId::Type_SoundGens);
}

void CSVDoc::View::addMeshesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Meshes);
}

void CSVDoc::View::addIconsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Icons);
}

void CSVDoc::View::addMusicsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Musics);
}

void CSVDoc::View::addSoundsResSubView()
{
    addSubView(CSMWorld::UniversalId::Type_SoundsRes);
}

void CSVDoc::View::addMagicEffectsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_MagicEffects);
}

void CSVDoc::View::addTexturesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Textures);
}

void CSVDoc::View::addVideosSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Videos);
}

void CSVDoc::View::addDebugProfilesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_DebugProfiles);
}

void CSVDoc::View::addRunLogSubView()
{
    addSubView(CSMWorld::UniversalId::Type_RunLog);
}

void CSVDoc::View::addLandsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Lands);
}

void CSVDoc::View::addLandTexturesSubView()
{
    addSubView(CSMWorld::UniversalId::Type_LandTextures);
}

void CSVDoc::View::addPathgridSubView()
{
    addSubView(CSMWorld::UniversalId::Type_Pathgrids);
}

void CSVDoc::View::addStartScriptsSubView()
{
    addSubView(CSMWorld::UniversalId::Type_StartScripts);
}

void CSVDoc::View::addSearchSubView()
{
    addSubView(mDocument->newSearch());
}

void CSVDoc::View::addMetaDataSubView()
{
    addSubView(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_MetaData, "sys::meta"));
}

void CSVDoc::View::abortOperation(int type)
{
    mDocument->abortOperation(type);
    updateActions();
}

CSVDoc::Operations* CSVDoc::View::getOperations() const
{
    return mOperations;
}

void CSVDoc::View::exit()
{
    emit exitApplicationRequest(this);
}

void CSVDoc::View::resizeViewWidth(int width)
{
    if (width >= 0)
        resize(width, geometry().height());
}

void CSVDoc::View::resizeViewHeight(int height)
{
    if (height >= 0)
        resize(geometry().width(), height);
}

void CSVDoc::View::toggleShowStatusBar(bool show)
{
    for (QObject* view : mSubViewWindow.children())
    {
        if (CSVDoc::SubView* subView = dynamic_cast<CSVDoc::SubView*>(view))
            subView->setStatusBar(show);
    }
}

void CSVDoc::View::toggleStatusBar(bool checked)
{
    mShowStatusBar->setChecked(checked);
}

void CSVDoc::View::loadErrorLog()
{
    addSubView(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_LoadErrorLog, 0));
}

void CSVDoc::View::run(const std::string& profile, const std::string& startupInstruction)
{
    mDocument->startRunning(profile, startupInstruction);
}

void CSVDoc::View::stop()
{
    mDocument->stopRunning();
}

void CSVDoc::View::closeRequest(SubView* subView)
{
    CSMPrefs::Category& windows = CSMPrefs::State::get()["Windows"];

    if (mSubViews.size() > 1 || mViewTotal <= 1 || !windows["hide-subview"].isTrue())
    {
        subView->deleteLater();
        mSubViews.removeOne(subView);
        updateSceneOpen();
    }
    else if (mViewManager.closeRequest(this))
        mViewManager.removeDocAndView(mDocument);
}

void CSVDoc::View::updateScrollbar()
{
    QRect rect;
    QWidget* topLevel = QApplication::topLevelAt(pos());
    if (topLevel)
        rect = topLevel->rect();
    else
        rect = this->rect();

    int newWidth = 0;
    for (int i = 0; i < mSubViews.size(); ++i)
    {
        newWidth += mSubViews[i]->width();
    }

    int frameWidth = frameGeometry().width() - width();

    if ((newWidth + frameWidth) >= rect.width())
        mSubViewWindow.setMinimumWidth(newWidth);
    else
        mSubViewWindow.setMinimumWidth(0);
}

void CSVDoc::View::merge()
{
    emit mergeDocument(mDocument);
}

void CSVDoc::View::updateWidth(bool isGrowLimit, int minSubViewWidth)
{
    QRect rect;
    if (isGrowLimit)
    {
        QScreen* screen = getWidgetScreen(pos());
        rect = screen->geometry();
    }
    else
        rect = desktopRect();

    if (!mScrollbarOnly && mScroll && mSubViews.size() > 1)
    {
        int newWidth = width() + minSubViewWidth;
        int frameWidth = frameGeometry().width() - width();
        if (newWidth + frameWidth <= rect.width())
        {
            resize(newWidth, height());
            // WARNING: below code assumes that the frame geometry expands to the right.
            // This doesn't conflict with subview-open-direction.
            if (x() > rect.width() - (newWidth + frameWidth))
                move(rect.width() - (newWidth + frameWidth), y()); // shift left to stay within the screen
        }
        else
        {
            // full width
            resize(rect.width() - frameWidth, height());
            mSubViewWindow.setMinimumWidth(mSubViewWindow.width() + minSubViewWidth);
            move(0, y());
        }
    }
}

void CSVDoc::View::createScrollArea()
{
    mScroll = new QScrollArea(this);
    mScroll->setWidgetResizable(true);
    mScroll->setWidget(&mSubViewWindow);
    setCentralWidget(mScroll);
}

void CSVDoc::View::onRequestFocus(const std::string& id)
{
    if (CSMPrefs::get()["3D Scene Editing"]["open-list-view"].isTrue())
    {
        addReferencesSubView();
        emit requestFocus(id);
    }
    else
    {
        addSubView(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Reference, id));
    }
}

QScreen* CSVDoc::View::getWidgetScreen(const QPoint& position)
{
    QScreen* screen = QApplication::screenAt(position);
    if (screen)
        return screen;

    const QList<QScreen*> screens = QApplication::screens();
    if (screens.isEmpty())
        throw std::runtime_error("No screens available");

    int closestDistance = std::numeric_limits<int>::max();
    for (QScreen* candidate : screens)
    {
        const QRect geometry = candidate->geometry();
        const int dx = position.x() - std::clamp(position.x(), geometry.left(), geometry.right());
        const int dy = position.y() - std::clamp(position.y(), geometry.top(), geometry.bottom());
        const int distance = dx * dx + dy * dy;

        if (distance < closestDistance)
        {
            closestDistance = distance;
            screen = candidate;
        }
    }

    if (screen == nullptr)
        screen = screens.first();

    return screen;
}
