#include "worldspacewidget.hpp"

#include "physicshub.hpp"

#include "../module/moduleui.hpp"

#include <algorithm>
#include <set>

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QToolTip>
#include <QWindow>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/view/render/editmode.hpp>
#include <apps/opencs/view/render/scenewidget.hpp>
#include <apps/opencs/view/widget/modebutton.hpp>

#include <components/esm/defs.hpp>
#include <components/misc/scalableicon.hpp>

#include "../../model/world/idtable.hpp"
#include "../../model/world/tablemimedata.hpp"
#include "../../model/world/universalid.hpp"

#include "../../model/prefs/shortcut.hpp"
#include "../../model/prefs/state.hpp"

#include "../widget/scenetoolmode.hpp"
#include "../widget/scenetoolrun.hpp"
#include "../widget/scenetooltoggle2.hpp"

#include "cameracontroller.hpp"
#include "instancemode.hpp"
#include "labelcamera.hpp"
#include "mask.hpp"
#include "object.hpp"
#include "objectmarker.hpp"
#include "orbitcameramode.hpp"
#include "pathgridmode.hpp"

CSVRender::WorldspaceWidget::WorldspaceWidget(CSMDoc::Document& document, QWidget* parent)
    : SceneWidget(document.getData(), parent, Qt::WindowFlags(), false)
    , mSceneElements(nullptr)
    , mRun(nullptr)
    , mDocument(document)
    , mInteractionMask(0)
    , mEditMode(nullptr)
    , mLocked(false)
    , mDragMode(InteractionType_None)
    , mDragging(false)
    , mDragX(0)
    , mDragY(0)
    , mSpeedMode(false)
    , mDragFactor(0)
    , mDragWheelFactor(0)
    , mDragShiftFactor(0)
    , mToolTipPos(-1, -1)
    , mShowToolTips(false)
    , mToolTipDelay(0)
    , mSelectedNavigationMode(0)
    , mSelectionMarker(std::make_unique<ObjectMarker>(this))
{
    setAcceptDrops(true);

    QAbstractItemModel* referenceables = document.getData().getTableModel(CSMWorld::UniversalId::Type_Referenceables);

    connect(referenceables, &QAbstractItemModel::dataChanged, this, &WorldspaceWidget::referenceableDataChanged);
    connect(referenceables, &QAbstractItemModel::rowsAboutToBeRemoved, this,
        &WorldspaceWidget::referenceableAboutToBeRemoved);
    connect(referenceables, &QAbstractItemModel::rowsInserted, this, &WorldspaceWidget::referenceableAdded);

    QAbstractItemModel* references = document.getData().getTableModel(CSMWorld::UniversalId::Type_References);

    connect(references, &QAbstractItemModel::dataChanged, this, &WorldspaceWidget::referenceDataChanged);
    connect(references, &QAbstractItemModel::rowsAboutToBeRemoved, this, &WorldspaceWidget::referenceAboutToBeRemoved);
    connect(references, &QAbstractItemModel::rowsInserted, this, &WorldspaceWidget::referenceAdded);

    QAbstractItemModel* pathgrids = document.getData().getTableModel(CSMWorld::UniversalId::Type_Pathgrids);

    connect(pathgrids, &QAbstractItemModel::dataChanged, this, &WorldspaceWidget::pathgridDataChanged);
    connect(pathgrids, &QAbstractItemModel::rowsAboutToBeRemoved, this, &WorldspaceWidget::pathgridAboutToBeRemoved);
    connect(pathgrids, &QAbstractItemModel::rowsInserted, this, &WorldspaceWidget::pathgridAdded);

    QAbstractItemModel* debugProfiles = document.getData().getTableModel(CSMWorld::UniversalId::Type_DebugProfiles);

    connect(debugProfiles, &QAbstractItemModel::dataChanged, this, &WorldspaceWidget::debugProfileDataChanged);
    connect(debugProfiles, &QAbstractItemModel::rowsAboutToBeRemoved, this,
        &WorldspaceWidget::debugProfileAboutToBeRemoved);

    mToolTipDelayTimer.setSingleShot(true);
    connect(&mToolTipDelayTimer, &QTimer::timeout, this, &WorldspaceWidget::showToolTip);

    CSMPrefs::get()["3D Scene Input"].update();
    CSMPrefs::get()["Tooltips"].update();

    // Shortcuts
    CSMPrefs::Shortcut* primaryEditShortcut
        = new CSMPrefs::Shortcut("scene-edit-primary", "scene-speed-modifier", CSMPrefs::Shortcut::SM_Detach, this);
    CSMPrefs::Shortcut* primaryOpenShortcut = new CSMPrefs::Shortcut("scene-open-primary", this);

    connect(primaryOpenShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &WorldspaceWidget::primaryOpen);
    connect(primaryEditShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &WorldspaceWidget::primaryEdit);
    connect(primaryEditShortcut, qOverload<bool>(&CSMPrefs::Shortcut::secondary), this, &WorldspaceWidget::speedMode);

    CSMPrefs::Shortcut* secondaryEditShortcut = new CSMPrefs::Shortcut("scene-edit-secondary", this);
    connect(
        secondaryEditShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &WorldspaceWidget::secondaryEdit);

    CSMPrefs::Shortcut* primarySelectShortcut = new CSMPrefs::Shortcut("scene-select-primary", this);
    connect(
        primarySelectShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this, &WorldspaceWidget::primarySelect);

    CSMPrefs::Shortcut* secondarySelectShortcut = new CSMPrefs::Shortcut("scene-select-secondary", this);
    connect(secondarySelectShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
        &WorldspaceWidget::secondarySelect);

    CSMPrefs::Shortcut* tertiarySelectShortcut = new CSMPrefs::Shortcut("scene-select-tertiary", this);
    connect(tertiarySelectShortcut, qOverload<bool>(&CSMPrefs::Shortcut::activated), this,
        &WorldspaceWidget::tertiarySelect);

    CSMPrefs::Shortcut* abortShortcut = new CSMPrefs::Shortcut("scene-edit-abort", this);
    connect(abortShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &WorldspaceWidget::abortDrag);

    connect(new CSMPrefs::Shortcut("scene-toggle-visibility", this), qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &WorldspaceWidget::toggleHiddenInstances);

    connect(new CSMPrefs::Shortcut("scene-unhide-all", this), qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &WorldspaceWidget::unhideAll);

    connect(new CSMPrefs::Shortcut("scene-clear-selection", this), qOverload<>(&CSMPrefs::Shortcut::activated), this,
        [this] { clearSelection(Mask_Reference); });

    CSMPrefs::Shortcut* switchPerspectiveShortcut = new CSMPrefs::Shortcut("scene-cam-cycle", this);
    connect(switchPerspectiveShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &WorldspaceWidget::cycleNavigationMode);

    connect(new CSMPrefs::Shortcut("scene-toggle-marker", this), qOverload<>(&CSMPrefs::Shortcut::activated), this,
        [this]() { mSelectionMarker->toggleVisibility(); });
}

CSVRender::WorldspaceWidget::~WorldspaceWidget() = default;

void CSVRender::WorldspaceWidget::hostReady()
{
    SceneWidget::hostReady();
    // The editor's physics world, drawn in this view (task 0030). Neither exists in tools without an editor.
    if (PhysicsHub::get() != nullptr)
        mPhysicsView = std::make_unique<PhysicsDebugView>(*getHost());
    // The editor modules' overlay (task 0031): redrawn when the active asset or its data changes.
    if (CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get())
    {
        mModuleOverlay = std::make_unique<PhysicsDebugView>(*getHost(), false);
        connect(modules, &CSVModule::ModuleUi::changed, this, [this] { updateModuleOverlay(); });
        updateModuleOverlay();
    }
    mSelectionMarker->setHost(*getHost());
}

void CSVRender::WorldspaceWidget::scriptPress(InteractionType type, const QPoint& pos)
{
    handleInteractionPress(mousePick(pos, getInteractionMask()), type);
}

void CSVRender::WorldspaceWidget::scriptDrag(InteractionType type, const QPoint& from, const QPoint& to, int steps)
{
    EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());
    bool dragging = false;
    if (type == InteractionType_PrimaryEdit)
        dragging = editMode.primaryEditStartDrag(from);
    else if (type == InteractionType_SecondaryEdit)
        dragging = editMode.secondaryEditStartDrag(from);
    else if (type == InteractionType_PrimarySelect)
        dragging = editMode.primarySelectStartDrag(from);
    else if (type == InteractionType_SecondarySelect)
        dragging = editMode.secondarySelectStartDrag(from);
    if (!dragging)
        return;
    QPoint last = from;
    for (int i = 1; i <= steps; ++i)
    {
        const QPoint pos = from + (to - from) * i / steps;
        editMode.drag(pos, pos.x() - last.x(), last.y() - pos.y(), mDragFactor);
        last = pos;
    }
    editMode.dragCompleted(to);
}

void CSVRender::WorldspaceWidget::settingChanged(const CSMPrefs::Setting* setting)
{
    if (*setting == "3D Scene Input/drag-factor")
        mDragFactor = setting->toDouble();
    else if (*setting == "3D Scene Input/drag-wheel-factor")
        mDragWheelFactor = setting->toDouble();
    else if (*setting == "3D Scene Input/drag-shift-factor")
        mDragShiftFactor = setting->toDouble();
    else if (*setting == "Rendering/object-marker-scale")
        mSelectionMarker->updateScale(setting->toDouble());
    else if (*setting == "Tooltips/scene-delay")
        mToolTipDelay = setting->toInt();
    else if (*setting == "Tooltips/scene")
        mShowToolTips = setting->isTrue();
    else
        SceneWidget::settingChanged(setting);
}

void CSVRender::WorldspaceWidget::useViewHint(const std::string& hint) {}

void CSVRender::WorldspaceWidget::selectDefaultNavigationMode()
{
    selectNavigationMode("1st");
}

void CSVRender::WorldspaceWidget::centerOrbitCameraOnSelection()
{
    std::vector<vsg::ref_ptr<TagBase>> selection = getSelection(Mask_Reference);

    for (std::vector<vsg::ref_ptr<TagBase>>::iterator it = selection.begin(); it != selection.end(); ++it)
    {
        if (CSVRender::ObjectTag* objectTag = static_cast<CSVRender::ObjectTag*>(it->get()))
        {
            mOrbitCamControl->setCenter(vsg::dvec3(objectTag->mObject->getPosition().asVec3()));
        }
    }
}

CSVWidget::SceneToolMode* CSVRender::WorldspaceWidget::makeNavigationSelector(CSVWidget::SceneToolbar* parent)
{
    CSVWidget::SceneToolMode* tool = new CSVWidget::SceneToolMode(parent, "Camera Mode");

    /// \todo replace icons
    /// \todo consider user-defined button-mapping
    tool->addButton(":scenetoolbar/1st-person", "1st",
        "First Person"
        "<ul><li>Camera is held upright</li>"
        "<li>Mouse-Look while holding {scene-navi-primary}</li>"
        "<li>Movement keys: {free-forward}(forward), {free-left}(left), {free-backward}(back), {free-right}(right)</li>"
        "<li>Strafing (also vertically) by holding {scene-navi-secondary}</li>"
        "<li>Mouse wheel moves the camera forward/backward</li>"
        "<li>Hold {scene-speed-modifier} to speed up movement</li>"
        "</ul>");
    tool->addButton(":scenetoolbar/free-camera", "free",
        "Free Camera"
        "<ul><li>Mouse-Look while holding {scene-navi-primary}</li>"
        "<li>Movement keys: {free-forward}(forward), {free-left}(left), {free-backward}(back), {free-right}(right)</li>"
        "<li>Roll camera with {free-roll-left} and {free-roll-right} keys</li>"
        "<li>Strafing (also vertically) by holding {scene-navi-secondary}</li>"
        "<li>Mouse wheel moves the camera forward/backward</li>"
        "<li>Hold {free-forward:mod} to speed up movement</li>"
        "</ul>");
    tool->addButton(
        new CSVRender::OrbitCameraMode(this, Misc::ScalableIcon::load(":scenetoolbar/orbiting-camera"),
            "Orbiting Camera"
            "<ul><li>Always facing the centre point</li>"
            "<li>Rotate around the centre point via {orbit-up}, {orbit-left}, {orbit-down}, {orbit-right} or by moving "
            "the mouse while holding {scene-navi-primary}</li>"
            "<li>Roll camera with {orbit-roll-left} and {orbit-roll-right} keys</li>"
            "<li>Strafing (also vertically) by holding {scene-navi-secondary} (includes relocation of the centre "
            "point)</li>"
            "<li>Mouse wheel moves camera away or towards centre point but can not pass through it</li>"
            "<li>Hold {scene-speed-modifier} to speed up movement</li>"
            "</ul>",
            tool),
        "orbit");

    mCameraMode = tool;

    connect(mCameraMode, &CSVWidget::SceneToolMode::modeChanged, this, &WorldspaceWidget::selectNavigationMode);

    return mCameraMode;
}

CSVWidget::SceneToolToggle2* CSVRender::WorldspaceWidget::makeSceneVisibilitySelector(CSVWidget::SceneToolbar* parent)
{
    mSceneElements = new CSVWidget::SceneToolToggle2(
        parent, "Scene Element Visibility", ":scenetoolbar/scene-view-c", ":scenetoolbar/scene-view-");

    addVisibilitySelectorButtons(mSceneElements);

    mSceneElements->setSelectionMask(0xffffffff);

    connect(mSceneElements, &CSVWidget::SceneToolToggle2::selectionChanged, this,
        &WorldspaceWidget::elementSelectionChanged);

    return mSceneElements;
}

CSVWidget::SceneToolRun* CSVRender::WorldspaceWidget::makeRunTool(CSVWidget::SceneToolbar* parent)
{
    CSMWorld::IdTable& debugProfiles = dynamic_cast<CSMWorld::IdTable&>(
        *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_DebugProfiles));

    std::vector<std::string> profiles;

    int idColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    int stateColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_Modification);
    int defaultColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_DefaultProfile);

    int size = debugProfiles.rowCount();

    for (int i = 0; i < size; ++i)
    {
        int state = debugProfiles.data(debugProfiles.index(i, stateColumn)).toInt();

        const bool defaultValue = debugProfiles.data(debugProfiles.index(i, defaultColumn)).toInt();

        if (state != CSMWorld::RecordBase::State_Deleted && defaultValue)
            profiles.emplace_back(debugProfiles.data(debugProfiles.index(i, idColumn)).toString().toUtf8().constData());
    }

    std::sort(profiles.begin(), profiles.end());

    mRun = new CSVWidget::SceneToolRun(
        parent, "Run OpenMW from the current camera position", ":scenetoolbar/play", profiles);

    connect(mRun, &CSVWidget::SceneToolRun::runRequest, this, &WorldspaceWidget::runRequest);

    return mRun;
}

CSVWidget::SceneToolMode* CSVRender::WorldspaceWidget::makeEditModeSelector(CSVWidget::SceneToolbar* parent)
{
    mEditMode = new CSVWidget::SceneToolMode(parent, "Edit Mode");

    addEditModeSelectorButtons(mEditMode);

    connect(mEditMode, &CSVWidget::SceneToolMode::modeChanged, this, &WorldspaceWidget::editModeChanged);

    return mEditMode;
}

CSVRender::WorldspaceWidget::DropType CSVRender::WorldspaceWidget::getDropType(
    const std::vector<CSMWorld::UniversalId>& data)
{
    DropType output = Type_Other;

    for (std::vector<CSMWorld::UniversalId>::const_iterator iter(data.begin()); iter != data.end(); ++iter)
    {
        DropType type = Type_Other;

        if (iter->getType() == CSMWorld::UniversalId::Type_Cell
            || iter->getType() == CSMWorld::UniversalId::Type_Cell_Missing)
        {
            type = iter->getId()[0] == '#' ? Type_CellsExterior : Type_CellsInterior;
        }
        else if (iter->getType() == CSMWorld::UniversalId::Type_DebugProfile)
            type = Type_DebugProfile;

        if (iter == data.begin())
            output = type;
        else if (output != type) // mixed types -> ignore
            return Type_Other;
    }

    return output;
}

CSVRender::WorldspaceWidget::DropRequirements CSVRender::WorldspaceWidget::getDropRequirements(DropType type) const
{
    if (type == Type_DebugProfile)
        return canHandle;

    return ignored;
}

bool CSVRender::WorldspaceWidget::handleDrop(const std::vector<CSMWorld::UniversalId>& universalIdData, DropType type)
{
    if (type == Type_DebugProfile)
    {
        if (mRun)
        {
            for (std::vector<CSMWorld::UniversalId>::const_iterator iter(universalIdData.begin());
                 iter != universalIdData.end(); ++iter)
                mRun->addProfile(iter->getId());
        }

        return true;
    }

    return false;
}

unsigned int CSVRender::WorldspaceWidget::getVisibilityMask() const
{
    return mSceneElements->getSelectionMask();
}

void CSVRender::WorldspaceWidget::setInteractionMask(unsigned int mask)
{
    mInteractionMask = mask | Mask_CellMarker | Mask_CellArrow;
}

unsigned int CSVRender::WorldspaceWidget::getInteractionMask() const
{
    return mInteractionMask & getVisibilityMask();
}

void CSVRender::WorldspaceWidget::setEditLock(bool locked)
{
    dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent()).setEditLock(locked);
}

void CSVRender::WorldspaceWidget::addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool)
{
    tool->addButton(Button_Reference, Mask_Reference, "Instances");
    tool->addButton(Button_Water, Mask_Water, "Water");
    tool->addButton(Button_Pathgrid, Mask_Pathgrid, "Pathgrid");
}

void CSVRender::WorldspaceWidget::addEditModeSelectorButtons(CSVWidget::SceneToolMode* tool)
{
    tool->addButton(new InstanceMode(this, tool), "object");
    tool->addButton(new PathgridMode(this, tool), "pathgrid");
}

CSMDoc::Document& CSVRender::WorldspaceWidget::getDocument()
{
    return mDocument;
}

CSVRender::WorldspaceHitResult CSVRender::WorldspaceWidget::mousePick(
    const QPoint& localPos, unsigned int interactionMask) const
{
    const SceneHost* host = getHost();
    if (!host || width() <= 0 || height() <= 0)
        return WorldspaceHitResult{ false, nullptr, 0, 0, 0, vsg::dvec3() };

    const double x = static_cast<double>(localPos.x()) / width();
    const double y = static_cast<double>(localPos.y()) / height();
    const double placementDistance = CSMPrefs::get()["3D Scene Editing"]["distance"].toInt();
    const auto toHit = [](const Picking::Result& result) {
        return WorldspaceHitResult{ result.mHit, result.mTag, result.mIndex0, result.mIndex1, result.mIndex2,
            result.mWorldPos };
    };

    // The selection marker comes first wherever it is on the ray, or a few pixels from it, as it is drawn over
    // everything.
    if (interactionMask & Mask_Reference)
        if (const auto marker = mSelectionMarker->pick(x, y, width(), height()))
            return toHit(*marker);

    // What is left is the scene, with no marker: a far half of a ring, which the marker does not take, is not it
    // either.
    const std::vector<Picking::Result> hits = host->pickAll(x, y, interactionMask);
    for (const Picking::Result& hit : hits)
        if (!dynamic_cast<ObjectMarkerTag*>(hit.mTag.get()))
            return toHit(hit);
    return toHit(host->missAt(x, y, placementDistance));
}

CSVRender::EditMode* CSVRender::WorldspaceWidget::getEditMode()
{
    return mEditMode ? dynamic_cast<CSVRender::EditMode*>(mEditMode->getCurrent()) : nullptr;
}

void CSVRender::WorldspaceWidget::abortDrag()
{
    if (mDragging)
    {
        EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());

        editMode.dragAborted();
        mDragMode = InteractionType_None;
    }
}

void CSVRender::WorldspaceWidget::dragEnterEvent(QDragEnterEvent* event)
{
    const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData());
    if (!mime) // May happen when non-records (e.g. plain text) are dragged and dropped
        return;

    if (mime->fromDocument(mDocument))
    {
        if (mime->holdsType(CSMWorld::UniversalId::Type_Cell)
            || mime->holdsType(CSMWorld::UniversalId::Type_Cell_Missing)
            || mime->holdsType(CSMWorld::UniversalId::Type_DebugProfile))
        {
            // These drops are handled through the subview object.
            event->accept();
        }
        else
            dynamic_cast<EditMode&>(*mEditMode->getCurrent()).dragEnterEvent(event);
    }
}

void CSVRender::WorldspaceWidget::dragMoveEvent(QDragMoveEvent* event)
{
    const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData());
    if (!mime) // May happen when non-records (e.g. plain text) are dragged and dropped
        return;

    if (mime->fromDocument(mDocument))
    {
        if (mime->holdsType(CSMWorld::UniversalId::Type_Cell)
            || mime->holdsType(CSMWorld::UniversalId::Type_Cell_Missing)
            || mime->holdsType(CSMWorld::UniversalId::Type_DebugProfile))
        {
            // These drops are handled through the subview object.
            event->accept();
        }
        else
            dynamic_cast<EditMode&>(*mEditMode->getCurrent()).dragMoveEvent(event);
    }
}

void CSVRender::WorldspaceWidget::dropEvent(QDropEvent* event)
{
    const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData());
    if (!mime) // May happen when non-records (e.g. plain text) are dragged and dropped
        return;

    if (mime->fromDocument(mDocument))
    {
        if (mime->holdsType(CSMWorld::UniversalId::Type_Cell)
            || mime->holdsType(CSMWorld::UniversalId::Type_Cell_Missing)
            || mime->holdsType(CSMWorld::UniversalId::Type_DebugProfile))
        {
            emit dataDropped(mime->getData());
        }
        else
            dynamic_cast<EditMode&>(*mEditMode->getCurrent()).dropEvent(event);
    }
}

void CSVRender::WorldspaceWidget::updateModuleOverlay()
{
    CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
    if (modules == nullptr || !mModuleOverlay || getHost() == nullptr)
        return;
    // Module coordinates are relative to what is under the middle of the view when the data changes (else the camera's
    // centre), so that a module's gizmo shows up where the user is looking: the assets have no place in the world of
    // their own.
    // The origin is chosen once for each asset: an edit must not move the gizmo.
    if (mOverlayGeneration != modules->generation())
    {
        mOverlayGeneration = modules->generation();
        const std::optional<vsg::dvec3> hit = getHost()->pickNearest(0.5, 0.5, ~0u);
        const vsg::dvec3 centre = hit ? *hit : getHost()->getCameraCenter();
        mOverlayOrigin[0] = static_cast<float>(centre.x);
        mOverlayOrigin[1] = static_cast<float>(centre.y);
        mOverlayOrigin[2] = static_cast<float>(centre.z);
    }
    mOverlayData = modules->overlay(mOverlayOrigin);
    showOverlayWithLabels();
}

void CSVRender::WorldspaceWidget::frameUpdate(float dt)
{
    SceneWidget::frameUpdate(dt);
    // Labels are screen-space text: laid out again when the camera moved.
    if (mModuleOverlay && !mOverlayData.mLabels.empty() && getHost() != nullptr
        && getHost()->getViewMatrix() != mLabelViewMatrix)
        showOverlayWithLabels();
}

void CSVRender::WorldspaceWidget::showOverlayWithLabels()
{
    if (!mModuleOverlay || getHost() == nullptr)
        return;
    if (mOverlayData.mLabels.empty())
    {
        mModuleOverlay->show(mOverlayData);
        return;
    }
    CSMModule::OverlayData shown = mOverlayData;
    mLabelViewMatrix = getHost()->getViewMatrix();
    CSMModule::addLabelLines(
        shown, makeLabelCamera(mLabelViewMatrix, getHost()->getProjectionMatrix(), static_cast<double>(height())));
    mModuleOverlay->show(shown);
}

void CSVRender::WorldspaceWidget::runRequest(const std::string& profile)
{
    mDocument.startRunning(profile, getStartupInstruction());
}

void CSVRender::WorldspaceWidget::debugProfileDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (!mRun)
        return;

    CSMWorld::IdTable& debugProfiles = dynamic_cast<CSMWorld::IdTable&>(
        *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_DebugProfiles));

    int idColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    int stateColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_Modification);

    for (int i = topLeft.row(); i <= bottomRight.row(); ++i)
    {
        int state = debugProfiles.data(debugProfiles.index(i, stateColumn)).toInt();

        // As of version 0.33 this case can not happen because debug profiles exist only in
        // project or session scope, which means they will never be in deleted state. But we
        // are adding the code for the sake of completeness and to avoid surprises if debug
        // profile ever get extended to content scope.
        if (state == CSMWorld::RecordBase::State_Deleted)
            mRun->removeProfile(debugProfiles.data(debugProfiles.index(i, idColumn)).toString().toUtf8().constData());
    }
}

void CSVRender::WorldspaceWidget::debugProfileAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (parent.isValid())
        return;

    if (!mRun)
        return;

    CSMWorld::IdTable& debugProfiles = dynamic_cast<CSMWorld::IdTable&>(
        *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_DebugProfiles));

    int idColumn = debugProfiles.findColumnIndex(CSMWorld::Columns::ColumnId_Id);

    for (int i = start; i <= end; ++i)
    {
        mRun->removeProfile(debugProfiles.data(debugProfiles.index(i, idColumn)).toString().toUtf8().constData());
    }
}

void CSVRender::WorldspaceWidget::editModeChanged(const std::string& id)
{
    dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent()).setEditLock(mLocked);
    mDragging = false;
    mDragMode = InteractionType_None;
}

void CSVRender::WorldspaceWidget::showToolTip()
{
    if (mShowToolTips)
    {
        QPoint pos = QCursor::pos();

        WorldspaceHitResult hit = mousePick(mapFromGlobal(pos), getInteractionMask());
        if (hit.tag)
        {
            bool hideBasics = CSMPrefs::get()["Tooltips"]["scene-hide-basic"].isTrue();
            QToolTip::showText(pos, hit.tag->getToolTip(hideBasics, hit), this);
        }
    }
}

void CSVRender::WorldspaceWidget::elementSelectionChanged()
{
    setVisibilityMask(getVisibilityMask());
    flagAsModified();
    updateOverlay();
}

void CSVRender::WorldspaceWidget::updateOverlay() {}

void CSVRender::WorldspaceWidget::handleMarkerHighlight(const QPoint& pos)
{
    const SceneHost* host = getHost();
    if (!host || !mSelectionMarker->isAttached() || width() <= 0 || height() <= 0)
        return;

    if (const auto hit = mSelectionMarker->pick(
            static_cast<double>(pos.x()) / width(), static_cast<double>(pos.y()) / height(), width(), height()))
    {
        if (const auto* marker = dynamic_cast<ObjectMarkerTag*>(hit->mTag.get()))
        {
            mSelectionMarker->updateMarkerHighlight(marker->mNode, marker->mAxis);
            return;
        }
    }
    mSelectionMarker->resetMarkerHighlight();
}

void CSVRender::WorldspaceWidget::mouseMoveEvent(QMouseEvent* event)
{
    dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent()).mouseMoveEvent(event);

    if (mDragging)
    {
        QPoint pos = event->position().toPoint();
        int diffX = pos.x() - mDragX;
        int diffY = (height() - pos.y()) - mDragY;

        mDragX = pos.x();
        mDragY = height() - pos.y();

        double factor = mDragFactor;

        if (mSpeedMode)
            factor *= mDragShiftFactor;

        EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());

        editMode.drag(event->position().toPoint(), diffX, diffY, factor);
    }
    else if (mDragMode != InteractionType_None)
    {
        EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());

        if (mDragMode == InteractionType_PrimaryEdit)
            mDragging = editMode.primaryEditStartDrag(mDragPressPos);
        else if (mDragMode == InteractionType_SecondaryEdit)
            mDragging = editMode.secondaryEditStartDrag(mDragPressPos);
        else if (mDragMode == InteractionType_PrimarySelect)
            mDragging = editMode.primarySelectStartDrag(mDragPressPos);
        else if (mDragMode == InteractionType_SecondarySelect)
            mDragging = editMode.secondarySelectStartDrag(mDragPressPos);

        if (mDragging)
        {
            mDragX = event->position().x();
            mDragY = height() - event->position().y();
        }
    }
    else
    {
        if (event->globalPosition().toPoint() != mToolTipPos)
        {
            mToolTipPos = event->globalPosition().toPoint();

            if (mShowToolTips)
            {
                QToolTip::hideText();
                mToolTipDelayTimer.start(mToolTipDelay);
            }
        }

        handleMarkerHighlight(event->position().toPoint());
        SceneWidget::mouseMoveEvent(event);
    }
}

void CSVRender::WorldspaceWidget::wheelEvent(QWheelEvent* event)
{
    if (mDragging)
    {
        double factor = mDragWheelFactor;

        if (mSpeedMode)
            factor *= mDragShiftFactor;

        EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());
        editMode.dragWheel(event->angleDelta().y(), factor);
    }
    else
        SceneWidget::wheelEvent(event);
}

void CSVRender::WorldspaceWidget::handleInteractionPress(const WorldspaceHitResult& hit, InteractionType type)
{
    EditMode& editMode = dynamic_cast<CSVRender::EditMode&>(*mEditMode->getCurrent());

    if (type == InteractionType_PrimaryEdit)
        editMode.primaryEditPressed(hit);
    else if (type == InteractionType_SecondaryEdit)
        editMode.secondaryEditPressed(hit);
    else if (type == InteractionType_PrimarySelect)
        editMode.primarySelectPressed(hit);
    else if (type == InteractionType_SecondarySelect)
        editMode.secondarySelectPressed(hit);
    else if (type == InteractionType_TertiarySelect)
        editMode.tertiarySelectPressed(hit);
    else if (type == InteractionType_PrimaryOpen)
        editMode.primaryOpenPressed(hit);
}

void CSVRender::WorldspaceWidget::primaryOpen(bool activate)
{
    handleInteraction(InteractionType_PrimaryOpen, activate);
}

void CSVRender::WorldspaceWidget::primaryEdit(bool activate)
{
    handleInteraction(InteractionType_PrimaryEdit, activate);
}

void CSVRender::WorldspaceWidget::secondaryEdit(bool activate)
{
    handleInteraction(InteractionType_SecondaryEdit, activate);
}

void CSVRender::WorldspaceWidget::primarySelect(bool activate)
{
    handleInteraction(InteractionType_PrimarySelect, activate);
}

void CSVRender::WorldspaceWidget::secondarySelect(bool activate)
{
    handleInteraction(InteractionType_SecondarySelect, activate);
}

void CSVRender::WorldspaceWidget::tertiarySelect(bool activate)
{
    handleInteraction(InteractionType_TertiarySelect, activate);
}

void CSVRender::WorldspaceWidget::speedMode(bool activate)
{
    mSpeedMode = activate;
}

void CSVRender::WorldspaceWidget::toggleHiddenInstances()
{
    const std::vector<vsg::ref_ptr<TagBase>> selection = getSelection(Mask_Reference);

    if (selection.empty())
        return;

    const CSVRender::ObjectTag* firstSelection = static_cast<CSVRender::ObjectTag*>(selection.begin()->get());
    assert(firstSelection != nullptr);

    const bool hide = !firstSelection->mObject->isHidden();

    for (const auto& object : selection)
        if (const auto objectTag = static_cast<CSVRender::ObjectTag*>(object.get()))
            objectTag->mObject->setHidden(hide);
}

void CSVRender::WorldspaceWidget::cycleNavigationMode()
{
    switch (++mSelectedNavigationMode)
    {
        case (CameraMode::FirstPerson):
            mCameraMode->setButton("1st");
            break;
        case (CameraMode::Orbit):
            mCameraMode->setButton("orbit");
            break;
        case (CameraMode::Free):
            mCameraMode->setButton("free");
            break;
        default:
            mCameraMode->setButton("1st");
            mSelectedNavigationMode = 0;
            break;
    }
}

void CSVRender::WorldspaceWidget::handleInteraction(InteractionType type, bool activate)
{
    if (activate)
    {
        if (!mDragging)
        {
            mDragMode = type;
            mDragPressPos = mapFromGlobal(QCursor::pos());
        }
    }
    else
    {
        mDragMode = InteractionType_None;

        if (mDragging)
        {
            EditMode* editMode = getEditMode();
            editMode->dragCompleted(mapFromGlobal(QCursor::pos()));
            mDragging = false;
        }
        else
        {
            WorldspaceHitResult hit = mousePick(mapFromGlobal(QCursor::pos()), getInteractionMask());
            handleInteractionPress(hit, type);
        }
    }
}
