#include "instancemode.hpp"

#include <QCursor>
#include <QDragEnterEvent>
#include <QPoint>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <utility>

#include "../../model/prefs/state.hpp"


#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/cellselection.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/refcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/editmode.hpp>
#include <apps/opencs/view/render/instancedragmodes.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

#include <components/esm/defs.hpp>
#include <components/misc/scalableicon.hpp>

#include "../../model/prefs/shortcut.hpp"
#include "../../model/world/commandmacro.hpp"
#include "../../model/world/commands.hpp"
#include "../../model/world/idtable.hpp"
#include "../../model/world/idtree.hpp"
#include "../../model/world/tablemimedata.hpp"

#include "../widget/scenetoolbar.hpp"
#include "../widget/scenetoolmode.hpp"

#include "camerapose.hpp"
#include "instancemath.hpp"
#include "mask.hpp"
#include "objectmarker.hpp"
#include "picking.hpp"
#include "scenehost.hpp"

#include "instancemovemode.hpp"
#include "instanceselectionmode.hpp"
#include "object.hpp"
#include "pagedworldspacewidget.hpp"
#include "worldspacewidget.hpp"

int CSVRender::InstanceMode::getSubModeFromId(const std::string& id) const
{
    return id == "move" ? 0 : (id == "rotate" ? 1 : 2);
}

vsg::vec3 CSVRender::InstanceMode::getSelectionCenter(const std::vector<vsg::ref_ptr<TagBase>>& selection) const
{
    vsg::vec3 center(0, 0, 0);
    int objectCount = 0;

    for (std::vector<vsg::ref_ptr<TagBase>>::const_iterator iter(selection.begin()); iter != selection.end(); ++iter)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(iter->get()))
        {
            const ESM::Position& position = objectTag->mObject->getPosition();
            center += vsg::vec3(position.pos[0], position.pos[1], position.pos[2]);

            ++objectCount;
        }
    }

    if (objectCount > 0)
        center = center / static_cast<float>(objectCount);

    return center;
}

vsg::vec3 CSVRender::InstanceMode::getScreenCoords(const vsg::vec3& pos)
{
    const WorldspaceWidget& widget = getWorldspaceWidget();
    const SceneHost& host = *widget.getHost();
    return InstanceMath::windowCoords(host.getProjectionMatrix(), host.getViewMatrix(), pos, widget.width(), widget.height());
}

vsg::vec3 CSVRender::InstanceMode::getProjectionSpaceCoords(const vsg::vec3& pos)
{
    const SceneHost& host = *getWorldspaceWidget().getHost();
    return InstanceMath::projectionCoords(host.getProjectionMatrix(), host.getViewMatrix(), pos);
}

vsg::vec3 CSVRender::InstanceMode::getMousePlaneCoords(const QPoint& point, const vsg::dvec3& dragStart)
{
    const WorldspaceWidget& widget = getWorldspaceWidget();
    const SceneHost& host = *widget.getHost();
    return InstanceMath::mousePlaneCoords(host.getProjectionMatrix(), host.getViewMatrix(),
        static_cast<double>(point.x()) / widget.width(), static_cast<double>(point.y()) / widget.height(), dragStart.z);
}

void CSVRender::InstanceMode::saveSelectionGroup(const int group)
{
    QStringList strings;
    QUndoStack& undoStack = getWorldspaceWidget().getDocument().getUndoStack();
    QVariant selectionObjects;
    CSMWorld::CommandMacro macro(undoStack, "Replace Selection Group");
    std::string groupName = "project::" + std::to_string(group);

    const auto& selection = getWorldspaceWidget().getSelection(Mask_Reference);
    const int selectionObjectsIndex
        = mSelectionGroups->findColumnIndex(CSMWorld::Columns::ColumnId_SelectionGroupObjects);

    if (dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
        groupName += "-ext";
    else
        groupName += "-" + getWorldspaceWidget().getCellId(vsg::vec3(0, 0, 0));

    CSMWorld::CreateCommand* newGroup = new CSMWorld::CreateCommand(*mSelectionGroups, groupName);

    newGroup->setType(CSMWorld::UniversalId::Type_SelectionGroup);

    for (const auto& object : selection)
        if (const CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(object.get()))
            strings << QString::fromStdString(objectTag->mObject->getReferenceId());

    selectionObjects.setValue(strings);

    newGroup->addValue(selectionObjectsIndex, selectionObjects);

    if (mSelectionGroups->getModelIndex(groupName, 0).row() != -1)
        macro.push(new CSMWorld::DeleteCommand(*mSelectionGroups, groupName));

    macro.push(newGroup);

    getWorldspaceWidget().clearSelection(Mask_Reference);
}

void CSVRender::InstanceMode::getSelectionGroup(const int group)
{
    std::string groupName = "project::" + std::to_string(group);
    std::vector<std::string> targets;

    const auto& selection = getWorldspaceWidget().getSelection(Mask_Reference);
    const int selectionObjectsIndex
        = mSelectionGroups->findColumnIndex(CSMWorld::Columns::ColumnId_SelectionGroupObjects);

    if (dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
        groupName += "-ext";
    else
        groupName += "-" + getWorldspaceWidget().getCellId(vsg::vec3(0, 0, 0));

    const QModelIndex groupSearch = mSelectionGroups->getModelIndex(groupName, selectionObjectsIndex);

    if (groupSearch.row() == -1)
        return;

    for (const QString& target : groupSearch.data().toStringList())
        targets.push_back(target.toStdString());

    if (!selection.empty())
        getWorldspaceWidget().clearSelection(Mask_Reference);

    getWorldspaceWidget().selectGroup(targets);
}

void CSVRender::InstanceMode::setDragAxis(const char axis)
{
    int newDragAxis;

    const std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);

    if (selection.empty())
        return;

    switch (axis)
    {
        case 'x':
            newDragAxis = 0;
            break;
        case 'y':
            newDragAxis = 1;
            break;
        case 'z':
            newDragAxis = 2;
            break;
        default:
            return;
    }

    if (newDragAxis == mDragAxis)
        newDragAxis = -1;

    if (mSubModeId == "move")
    {
        mObjectsAtDragStart.clear();

        for (const auto& object : selection)
            if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(object.get()))
            {
                const vsg::vec3 thisPoint = objectTag->mObject->getPosition().asVec3();
                mDragStart = thisPoint;
                mObjectsAtDragStart.emplace_back(thisPoint);
            }
    }
    mDragAxis = newDragAxis;
}

QString CSVRender::InstanceMode::getTooltip()
{
    return QString(
        "Instance editing"
        "<ul><li>Use {scene-select-primary} and {scene-select-secondary} to select and unselect instances</li>"
        "<li>Use {scene-edit-primary} to manipulate instances</li>"
        "<li>Use {scene-select-tertiary} to select a reference object and then {scene-edit-secondary} to snap "
        "selection relative to the reference object</li>"
        "<li>Use {scene-submode-move}, {scene-submode-rotate}, {scene-submode-scale} to change to move, "
        "rotate, and "
        "scale modes respectively</li>"
        "<li>Use {scene-axis-x}, {scene-axis-y}, and {scene-axis-z} to lock changes to X, Y, and Z axes "
        "respectively</li>"
        "<li>Use {scene-delete} to delete currently selected objects</li>"
        "<li>Use {scene-duplicate} to duplicate instances</li>"
        "<li>Use {scene-instance-drop} to drop instances</li></ul>");
}

CSVRender::InstanceMode::InstanceMode(
    WorldspaceWidget* worldspaceWidget, QWidget* parent)
    : EditMode(worldspaceWidget, Misc::ScalableIcon::load(":scenetoolbar/editing-instance"),
        Mask_Reference | Mask_Terrain, getTooltip(), parent)
    , mSubMode(nullptr)
    , mSubModeId("move")
    , mSelectionMode(nullptr)
    , mDragMode(DragMode_None)
    , mDragAxis(-1)
    , mLocked(false)
    , mUnitScaleDist(1)
{
    mSelectionGroups = dynamic_cast<CSMWorld::IdTable*>(
        worldspaceWidget->getDocument().getData().getTableModel(CSMWorld::UniversalId::Type_SelectionGroup));

    connect(this, &InstanceMode::requestFocus, worldspaceWidget, &WorldspaceWidget::requestFocus);

    CSMPrefs::Shortcut* deleteShortcut = new CSMPrefs::Shortcut("scene-delete", worldspaceWidget);
    connect(deleteShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &InstanceMode::deleteSelectedInstances);

    CSMPrefs::Shortcut* duplicateShortcut = new CSMPrefs::Shortcut("scene-duplicate", worldspaceWidget);

    connect(
        duplicateShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &InstanceMode::cloneSelectedInstances);

    connect(new CSMPrefs::Shortcut("scene-instance-drop", worldspaceWidget),
        qOverload<>(&CSMPrefs::Shortcut::activated), this, &InstanceMode::dropToCollision);

    for (short i = 0; i <= 9; i++)
    {
        connect(new CSMPrefs::Shortcut("scene-group-" + std::to_string(i), worldspaceWidget),
            qOverload<>(&CSMPrefs::Shortcut::activated), this, [this, i] { this->getSelectionGroup(i); });
        connect(new CSMPrefs::Shortcut("scene-save-" + std::to_string(i), worldspaceWidget),
            qOverload<>(&CSMPrefs::Shortcut::activated), this, [this, i] { this->saveSelectionGroup(i); });
    }

    connect(new CSMPrefs::Shortcut("scene-submode-move", worldspaceWidget), qOverload<>(&CSMPrefs::Shortcut::activated),
        this, [this] { mSubMode->setButton("move"); });

    connect(new CSMPrefs::Shortcut("scene-submode-scale", worldspaceWidget),
        qOverload<>(&CSMPrefs::Shortcut::activated), this, [this] { mSubMode->setButton("scale"); });

    connect(new CSMPrefs::Shortcut("scene-submode-rotate", worldspaceWidget),
        qOverload<>(&CSMPrefs::Shortcut::activated), this, [this] { mSubMode->setButton("rotate"); });

    for (const char axis : "xyz")
        connect(new CSMPrefs::Shortcut(std::string("scene-axis-") + axis, worldspaceWidget),
            qOverload<>(&CSMPrefs::Shortcut::activated), this, [this, axis] {
                this->setDragAxis(axis);
                std::string axisStr(1, toupper(axis));
                switch (getSubMode())
                {
                    case (Object::Mode_Move):
                        axisStr += "_Axis";
                        break;
                    case (Object::Mode_Rotate):
                        axisStr += "_Axis_Rot";
                        break;
                    case (Object::Mode_Scale):
                        axisStr += "_Axis_Scale";
                        break;
                }

                auto selectionMarker = getWorldspaceWidget().getSelectionMarker();

                if (mDragAxis != -1)
                    selectionMarker->updateMarkerHighlight(axisStr, axis - 'x');
                else
                    selectionMarker->resetMarkerHighlight();
            });
}

void CSVRender::InstanceMode::activate(CSVWidget::SceneToolbar* toolbar)
{
    if (!mSubMode)
    {
        mSubMode = new CSVWidget::SceneToolMode(toolbar, "Edit Sub-Mode");
        mSubMode->addButton(new InstanceMoveMode(this), "move");
        mSubMode->addButton(":scenetoolbar/transform-rotate", "rotate",
            "Rotate selected instances"
            "<ul><li>Use {scene-edit-primary} to rotate instances freely</li>"
            "<li>Use {scene-edit-secondary} to rotate instances within the grid</li>"
            "<li>The center of the view acts as the axis of rotation</li>"
            "</ul>");
        mSubMode->addButton(":scenetoolbar/transform-scale", "scale",
            "Scale selected instances"
            "<ul><li>Use {scene-edit-primary} to scale instances freely</li>"
            "<li>Use {scene-edit-secondary} to scale instances along the grid</li>"
            "<li>The scaling rate is based on how close the start of a drag is to the center of the screen</li>"
            "</ul>");

        mSubMode->setButton(mSubModeId);

        connect(mSubMode, &CSVWidget::SceneToolMode::modeChanged, this, &InstanceMode::subModeChanged);
    }

    if (!mSelectionMode)
        mSelectionMode = new InstanceSelectionMode(toolbar, getWorldspaceWidget());

    mDragMode = DragMode_None;

    EditMode::activate(toolbar);

    toolbar->addTool(mSubMode);
    toolbar->addTool(mSelectionMode);

    std::string subMode = mSubMode->getCurrentId();

    getWorldspaceWidget().setSubMode(getSubModeFromId(subMode), Mask_Reference);
}

void CSVRender::InstanceMode::deactivate(CSVWidget::SceneToolbar* toolbar)
{
    mDragMode = DragMode_None;
    getWorldspaceWidget().reset(Mask_Reference);

    if (mSelectionMode)
    {
        toolbar->removeTool(mSelectionMode);
        delete mSelectionMode;
        mSelectionMode = nullptr;
    }

    if (mSubMode)
    {
        toolbar->removeTool(mSubMode);
        delete mSubMode;
        mSubMode = nullptr;
    }

    EditMode::deactivate(toolbar);
}

void CSVRender::InstanceMode::setEditLock(bool locked)
{
    mLocked = locked;

    if (mLocked)
        getWorldspaceWidget().abortDrag();
}

void CSVRender::InstanceMode::primaryEditPressed(const WorldspaceHitResult& hit)
{
    if (CSMPrefs::get()["3D Scene Input"]["context-select"].isTrue())
        primarySelectPressed(hit);
}

void CSVRender::InstanceMode::primaryOpenPressed(const WorldspaceHitResult& hit)
{
    if (hit.tag)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
        {
            const std::string refId = objectTag->mObject->getReferenceId();
            emit requestFocus(refId);
        }
    }
}

void CSVRender::InstanceMode::secondaryEditPressed(const WorldspaceHitResult& hit)
{
    if (CSMPrefs::get()["3D Scene Input"]["context-select"].isTrue())
        secondarySelectPressed(hit);
}

void CSVRender::InstanceMode::primarySelectPressed(const WorldspaceHitResult& hit)
{
    auto& worldspaceWidget = getWorldspaceWidget();

    worldspaceWidget.clearSelection(Mask_Reference);

    if (!hit.tag)
        return;

    if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
    {
        // hit an Object, select it
        CSVRender::Object* object = objectTag->mObject;
        object->setSelected(true);
        worldspaceWidget.getSelectionMarker()->addToSelectionHistory(object->getReferenceId());
    }
}

void CSVRender::InstanceMode::secondarySelectPressed(const WorldspaceHitResult& hit)
{
    if (!hit.tag)
        return;

    if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
    {
        // hit an Object, toggle its selection state
        CSVRender::Object* object = objectTag->mObject;
        object->setSelected(!object->getSelected());

        const auto selectionMarker = getWorldspaceWidget().getSelectionMarker();

        if (object->getSelected())
            selectionMarker->addToSelectionHistory(object->getReferenceId(), false);

        selectionMarker->updateSelectionMarker();
    }
}

void CSVRender::InstanceMode::tertiarySelectPressed(const WorldspaceHitResult& hit)
{
    if (auto* snapTarget
        = dynamic_cast<CSVRender::ObjectTag*>(getWorldspaceWidget().getSnapTarget(Mask_Reference).get()))
    {
        snapTarget->mObject->setSnapTarget(false);
    }

    if (!hit.tag)
        return;

    if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
    {
        // hit an Object, toggle its selection state
        CSVRender::Object* object = objectTag->mObject;
        object->setSnapTarget(!object->getSnapTarget());
    }
}

bool CSVRender::InstanceMode::primaryEditStartDrag(const QPoint& pos)
{
    if (mDragMode != DragMode_None || mLocked)
        return false;

    auto& worldspaceWidget = getWorldspaceWidget();

    WorldspaceHitResult hit = worldspaceWidget.mousePick(pos, worldspaceWidget.getInteractionMask());

    std::vector<vsg::ref_ptr<TagBase>> selection = worldspaceWidget.getSelection(Mask_Reference);
    if (selection.empty())
    {
        // Only change selection at the start of drag if no object is already selected
        if (hit.tag && CSMPrefs::get()["3D Scene Input"]["context-select"].isTrue())
        {
            worldspaceWidget.clearSelection(Mask_Reference);
            if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
            {
                CSVRender::Object* object = objectTag->mObject;
                object->setSelected(true);
                worldspaceWidget.getSelectionMarker()->addToSelectionHistory(object->getReferenceId());
            }
        }

        selection = worldspaceWidget.getSelection(Mask_Reference);
        if (selection.empty())
            return false;
    }

    mObjectsAtDragStart.clear();

    for (std::vector<vsg::ref_ptr<TagBase>>::iterator iter(selection.begin()); iter != selection.end(); ++iter)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(iter->get()))
        {
            if (mSubModeId == "move")
            {
                objectTag->mObject->setEdited(Object::Override_Position);
                const vsg::vec3 thisPoint = objectTag->mObject->getPosition().asVec3();
                mDragStart = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(thisPoint)));
                mObjectsAtDragStart.emplace_back(thisPoint);
                mDragMode = DragMode_Move;
            }
            else if (mSubModeId == "rotate")
            {
                objectTag->mObject->setEdited(Object::Override_Rotation);
                mDragMode = DragMode_Rotate;
            }
            else if (mSubModeId == "scale")
            {
                objectTag->mObject->setEdited(Object::Override_Scale);
                mDragMode = DragMode_Scale;

                // Calculate scale factor
                std::vector<vsg::ref_ptr<TagBase>> editedSelection = getWorldspaceWidget().getEdited(Mask_Reference);
                vsg::vec3 center = getScreenCoords(getSelectionCenter(editedSelection));

                int widgetHeight = getWorldspaceWidget().height();

                float dx = pos.x() - center.x;
                float dy = (widgetHeight - pos.y()) - center.y;

                mUnitScaleDist = std::sqrt(dx * dx + dy * dy);
            }
        }
    }

    if (CSVRender::ObjectMarkerTag* objectTag = dynamic_cast<CSVRender::ObjectMarkerTag*>(hit.tag.get()))
    {
        mDragAxis = objectTag->mAxis;
    }
    else
        mDragAxis = -1;

    return true;
}

bool CSVRender::InstanceMode::secondaryEditStartDrag(const QPoint& pos)
{
    if (mDragMode != DragMode_None || mLocked)
        return false;

    auto& worldspaceWidget = getWorldspaceWidget();

    WorldspaceHitResult hit = worldspaceWidget.mousePick(pos, worldspaceWidget.getInteractionMask());

    std::vector<vsg::ref_ptr<TagBase>> selection = worldspaceWidget.getSelection(Mask_Reference);
    if (selection.empty())
    {
        // Only change selection at the start of drag if no object is already selected
        if (hit.tag && CSMPrefs::get()["3D Scene Input"]["context-select"].isTrue())
        {
            worldspaceWidget.clearSelection(Mask_Reference);
            if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(hit.tag.get()))
            {
                CSVRender::Object* object = objectTag->mObject;
                object->setSelected(true);
                worldspaceWidget.getSelectionMarker()->addToSelectionHistory(object->getReferenceId());
            }
        }

        selection = worldspaceWidget.getSelection(Mask_Reference);
        if (selection.empty())
            return false;
    }

    mObjectsAtDragStart.clear();

    for (std::vector<vsg::ref_ptr<TagBase>>::iterator iter(selection.begin()); iter != selection.end(); ++iter)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(iter->get()))
        {
            if (mSubModeId == "move")
            {
                objectTag->mObject->setEdited(Object::Override_Position);
                const vsg::vec3 thisPoint = objectTag->mObject->getPosition().asVec3();

                mDragStart = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(thisPoint)));
                mObjectsAtDragStart.emplace_back(thisPoint);
                mDragMode = DragMode_Move_Snap;
            }
            else if (mSubModeId == "rotate")
            {
                objectTag->mObject->setEdited(Object::Override_Rotation);
                mDragMode = DragMode_Rotate_Snap;
            }
            else if (mSubModeId == "scale")
            {
                objectTag->mObject->setEdited(Object::Override_Scale);
                mDragMode = DragMode_Scale_Snap;

                // Calculate scale factor
                std::vector<vsg::ref_ptr<TagBase>> editedSelection = worldspaceWidget.getEdited(Mask_Reference);
                vsg::vec3 center = getScreenCoords(getSelectionCenter(editedSelection));

                int widgetHeight = worldspaceWidget.height();

                float dx = pos.x() - center.x;
                float dy = (widgetHeight - pos.y()) - center.y;

                mUnitScaleDist = std::sqrt(dx * dx + dy * dy);
            }
        }
    }

    if (CSVRender::ObjectMarkerTag* objectTag = dynamic_cast<CSVRender::ObjectMarkerTag*>(hit.tag.get()))
    {
        mDragAxis = objectTag->mAxis;
    }
    else
        mDragAxis = -1;

    return true;
}

bool CSVRender::InstanceMode::primarySelectStartDrag(const QPoint& pos)
{
    if (mDragMode != DragMode_None || mLocked)
        return false;

    std::string primarySelectAction = CSMPrefs::get()["3D Scene Editing"]["primary-select-action"].toString();

    if (primarySelectAction == "Select only")
        mDragMode = DragMode_Select_Only;
    else if (primarySelectAction == "Add to selection")
        mDragMode = DragMode_Select_Add;
    else if (primarySelectAction == "Remove from selection")
        mDragMode = DragMode_Select_Remove;
    else if (primarySelectAction == "Invert selection")
        mDragMode = DragMode_Select_Invert;

    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
    mSelectionMode->setDragStart(hit.worldPos);

    return true;
}

bool CSVRender::InstanceMode::secondarySelectStartDrag(const QPoint& pos)
{
    if (mDragMode != DragMode_None || mLocked)
        return false;

    std::string secondarySelectAction = CSMPrefs::get()["3D Scene Editing"]["secondary-select-action"].toString();

    if (secondarySelectAction == "Select only")
        mDragMode = DragMode_Select_Only;
    else if (secondarySelectAction == "Add to selection")
        mDragMode = DragMode_Select_Add;
    else if (secondarySelectAction == "Remove from selection")
        mDragMode = DragMode_Select_Remove;
    else if (secondarySelectAction == "Invert selection")
        mDragMode = DragMode_Select_Invert;

    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
    mSelectionMode->setDragStart(hit.worldPos);

    return true;
}

void CSVRender::InstanceMode::drag(const QPoint& pos, int diffX, int diffY, double speedFactor)
{
    vsg::vec3 offset;
    vsg::dquat rotation;

    std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getEdited(Mask_Reference);
    auto* snapTarget = dynamic_cast<CSVRender::ObjectTag*>(getWorldspaceWidget().getSnapTarget(Mask_Reference).get());

    if (mDragMode == DragMode_Move || mDragMode == DragMode_Move_Snap)
    {
    }
    else if (mDragMode == DragMode_Rotate || mDragMode == DragMode_Rotate_Snap)
    {
        const SceneHost& host = *getWorldspaceWidget().getHost();
        const vsg::dvec3 eye = host.getCameraEye();
        const vsg::dvec3 centre = host.getCameraCenter();

        float angle;
        vsg::dvec3 axis;

        if (mDragAxis == -1)
        {
            // Free rotate
            float rotationFactor = CSMPrefs::get()["3D Scene Input"]["rotate-factor"].toDouble() * speedFactor;

            const vsg::dvec3 camForward = centre - eye;
            vsg::dvec3 screenDir = CameraPose::eyeToWorldDirection(host.getViewMatrix(), vsg::dvec3(diffX, diffY, 0));
            Misc::VsgMath::normalizeInPlace(screenDir);

            angle = static_cast<float>(std::sqrt(diffX * diffX + diffY * diffY) * rotationFactor);
            axis = vsg::cross(screenDir, camForward);
        }
        else
        {
            // Global axis rotation
            const vsg::dvec3 camBack = eye - centre;

            for (int i = 0; i < 3; ++i)
            {
                if (i == mDragAxis)
                    axis[i] = 1;
                else
                    axis[i] = 0;
            }

            // Flip axis if facing opposite side
            if (vsg::dot(camBack, axis) < 0)
                axis = axis * -1.0;

            // Convert coordinate system
            const vsg::vec3 screenCenter = getScreenCoords(getSelectionCenter(selection));

            int widgetHeight = getWorldspaceWidget().height();

            float newX = pos.x() - screenCenter.x;
            float newY = (widgetHeight - pos.y()) - screenCenter.y;

            float oldX = newX - diffX;
            float oldY = newY - diffY; // diffY appears to already be flipped

            vsg::vec3 oldVec(oldX, oldY, 0);
            Misc::VsgMath::normalizeInPlace(oldVec);

            vsg::vec3 newVec(newX, newY, 0);
            Misc::VsgMath::normalizeInPlace(newVec);

            // Find angle and axis of rotation
            angle = static_cast<float>(std::acos(std::clamp(vsg::dot(oldVec, newVec), -1.0f, 1.0f)) * speedFactor);
            if ((vsg::dot(vsg::dvec3(vsg::cross(oldVec, newVec)), camBack) < 0) ^ (camBack.z < 0))
                angle *= -1;
        }

        rotation = Misc::VsgMath::makeRotateFromAxis(angle, axis);
        if (std::isnan(rotation.x) || std::isnan(rotation.y) || std::isnan(rotation.z) || std::isnan(rotation.w))
            rotation = vsg::dquat(); // NaN flush
    }
    else if (mDragMode == DragMode_Scale || mDragMode == DragMode_Scale_Snap)
    {
        vsg::vec3 center = getScreenCoords(getSelectionCenter(selection));

        // Calculate scaling distance/rate
        int widgetHeight = getWorldspaceWidget().height();

        float dx = pos.x() - center.x;
        float dy = (widgetHeight - pos.y()) - center.y;

        float dist = std::sqrt(dx * dx + dy * dy);
        float scale = dist / mUnitScaleDist;

        // Only uniform scaling is currently supported
        offset = vsg::vec3(scale, scale, scale);
    }
    else if (mSelectionMode->getCurrentId() == "cube-centre")
    {
        const vsg::vec3 mousePlanePoint = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(vsg::vec3(mSelectionMode->getDragStart()))));
        mSelectionMode->drawSelectionCubeCentre(mousePlanePoint);
        return;
    }
    else if (mSelectionMode->getCurrentId() == "cube-corner")
    {
        const vsg::vec3 mousePlanePoint = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(vsg::vec3(mSelectionMode->getDragStart()))));
        mSelectionMode->drawSelectionCubeCorner(mousePlanePoint);
        return;
    }
    else if (mSelectionMode->getCurrentId() == "sphere")
    {
        const vsg::vec3 mousePlanePoint = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(vsg::vec3(mSelectionMode->getDragStart()))));
        mSelectionMode->drawSelectionSphere(mousePlanePoint);
        return;
    }


    // Multiselect rotation needs to be applied to transform matrixes, not angles,
    //  and we need them to have a collective center.
    // For the sake of consistency we take the average of each origin in the selection.
    // This is invariant under rotation and reselection, so it's predictable to the user.
    vsg::vec3 center(0, 0, 0);
    float numCenters = 0.0f;
    for (auto& item : selection)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(item.get()))
        {
            ESM::Position position = objectTag->mObject->getPosition();
            center.x += position.pos[0];
            center.y += position.pos[1];
            center.z += position.pos[2];
            numCenters += 1.0f;
        }
    }
    if (numCenters > 0.0f)
        center /= numCenters;

    // Apply
    int i = 0;
    for (auto& item : selection)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(item.get()))
        {
            if (mDragMode == DragMode_Move || mDragMode == DragMode_Move_Snap)
            {
                ESM::Position position = objectTag->mObject->getPosition();
                const vsg::vec3 mousePos = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(mDragStart)));
                const float addToX = mousePos.x - mDragStart.x;
                const float addToY = mousePos.y - mDragStart.y;
                const float addToZ = mousePos.z - mDragStart.z;
                position.pos[0] = mObjectsAtDragStart[i].x + addToX;
                position.pos[1] = mObjectsAtDragStart[i].y + addToY;
                position.pos[2] = mObjectsAtDragStart[i].z + addToZ;

                if (mDragMode == DragMode_Move_Snap)
                {
                    double snap = CSMPrefs::get()["3D Scene Editing"]["gridsnap-movement"].toDouble();

                    if (snapTarget)
                    {
                        const vsg::vec3 translation(addToX, addToY, addToZ);

                        auto snapTargetPosition = snapTarget->mObject->getPosition();
                        auto newPosition = InstanceMath::snapRelativeToTarget(mObjectsAtDragStart[i],
                            snapTargetPosition.asVec3(), snapTargetPosition.asRotationVec3(), translation, snap);

                        position.pos[0] = newPosition[0];
                        position.pos[1] = newPosition[1];
                        position.pos[2] = newPosition[2];
                    }
                    else
                    {
                        position.pos[0] = InstanceMath::roundToMult(position.pos[0], snap);
                        position.pos[1] = InstanceMath::roundToMult(position.pos[1], snap);
                        position.pos[2] = InstanceMath::roundToMult(position.pos[2], snap);
                    }
                }

                // XYZ-locking
                if (mDragAxis != -1)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        if (j != mDragAxis)
                            position.pos[j] = mObjectsAtDragStart[i][j];
                    }
                }

                objectTag->mObject->setPosition(position.pos);
            }
            else if (mDragMode == DragMode_Rotate || mDragMode == DragMode_Rotate_Snap)
            {
                // Only one item: use basic rotation logic to ensure no drift
                if (numCenters == 1.0f)
                {
                    ESM::Position position = objectTag->mObject->getPosition();

                    const vsg::dquat currentRot = InstanceMath::eulerToQuat(vsg::vec3(position.rot[0], position.rot[1], position.rot[2]));
                    const vsg::dquat combined = currentRot * rotation;

                    const vsg::vec3 euler = InstanceMath::quatToEuler(combined);
                    // There appears to be a very rare rounding error that can cause asin to return NaN
                    if (!Misc::VsgMath::isNaN(euler))
                    {
                        position.rot[0] = euler.x;
                        position.rot[1] = euler.y;
                        position.rot[2] = euler.z;
                    }

                    objectTag->mObject->setRotation(position.rot);
                }
                else
                {
                    // Used both as a read and as a write.
                    ESM::Position position = objectTag->mObject->getPosition();

                    // Group rotation needs to be able to move objects relative to each other.
                    vsg::vec3 newPosition(position.pos[0], position.pos[1], position.pos[2]);
                    vsg::vec3 euler(position.rot[0], position.rot[1], position.rot[2]);
                    InstanceMath::rotateAroundCentre(newPosition, euler, center, rotation);

                    position.rot[0] = euler.x;
                    position.rot[1] = euler.y;
                    position.rot[2] = euler.z;
                    position.pos[0] = newPosition.x;
                    position.pos[1] = newPosition.y;
                    position.pos[2] = newPosition.z;
                    objectTag->mObject->setPosition(position.pos);
                    objectTag->mObject->setRotation(position.rot);
                }
            }
            else if (mDragMode == DragMode_Scale || mDragMode == DragMode_Scale_Snap)
            {
                // Reset scale
                objectTag->mObject->setEdited(0);
                objectTag->mObject->setEdited(Object::Override_Scale);

                float scale = objectTag->mObject->getScale();
                scale *= offset.x;

                if (mDragMode == DragMode_Scale_Snap)
                {
                    scale = InstanceMath::roundToMult(
                        scale, CSMPrefs::get()["3D Scene Editing"]["gridsnap-scale"].toDouble());
                }

                objectTag->mObject->setScale(scale);
            }
        }
        i++;
    }
}

void CSVRender::InstanceMode::dragCompleted(const QPoint& pos)
{
    std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getEdited(Mask_Reference);

    auto* snapTarget = dynamic_cast<CSVRender::ObjectTag*>(getWorldspaceWidget().getSnapTarget(Mask_Reference).get());

    QUndoStack& undoStack = getWorldspaceWidget().getDocument().getUndoStack();

    QString description;

    switch (mDragMode)
    {
        case DragMode_Move:
            description = "Move Instances";
            break;
        case DragMode_Rotate:
            description = "Rotate Instances";
            break;
        case DragMode_Scale:
            description = "Scale Instances";
            break;
        case DragMode_Select_Only:
            handleSelectDrag(pos);
            return;
            break;
        case DragMode_Select_Add:
            handleSelectDrag(pos);
            return;
            break;
        case DragMode_Select_Remove:
            handleSelectDrag(pos);
            return;
            break;
        case DragMode_Select_Invert:
            handleSelectDrag(pos);
            return;
            break;
        case DragMode_Move_Snap:
            description = "Move Instances";
            break;
        case DragMode_Rotate_Snap:
            description = "Rotate Instances";
            break;
        case DragMode_Scale_Snap:
            description = "Scale Instances";
            break;
        case DragMode_None:
            break;
    }

    CSMWorld::CommandMacro macro(undoStack, description);

    // Is this even supposed to be here?
    for (std::vector<vsg::ref_ptr<TagBase>>::iterator iter(selection.begin()); iter != selection.end(); ++iter)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(iter->get()))
        {
            if (mDragMode == DragMode_Rotate_Snap)
            {
                ESM::Position position = objectTag->mObject->getPosition();
                double snap = CSMPrefs::get()["3D Scene Editing"]["gridsnap-rotation"].toDouble();

                float xOffset = 0;
                float yOffset = 0;
                float zOffset = 0;

                if (snapTarget)
                {
                    auto snapTargetPosition = snapTarget->mObject->getPosition();
                    auto rotation = snapTargetPosition.rot;
                    if (rotation)
                    {
                        xOffset = remainder(rotation[0], Misc::VsgMath::inDegrees(snap));
                        yOffset = remainder(rotation[1], Misc::VsgMath::inDegrees(snap));
                        zOffset = remainder(rotation[2], Misc::VsgMath::inDegrees(snap));
                    }
                }

                position.rot[0]
                    = InstanceMath::roundToMult(position.rot[0], Misc::VsgMath::inDegrees(snap)) + xOffset;
                position.rot[1]
                    = InstanceMath::roundToMult(position.rot[1], Misc::VsgMath::inDegrees(snap)) + yOffset;
                position.rot[2]
                    = InstanceMath::roundToMult(position.rot[2], Misc::VsgMath::inDegrees(snap)) + zOffset;

                objectTag->mObject->setRotation(position.rot);
            }

            objectTag->mObject->apply(macro);
        }
    }

    mObjectsAtDragStart.clear();
    mDragMode = DragMode_None;
}

void CSVRender::InstanceMode::dragAborted()
{
    getWorldspaceWidget().reset(Mask_Reference);
    mDragMode = DragMode_None;
}

void CSVRender::InstanceMode::dragWheel(int diff, double speedFactor)
{
    if (mDragMode == DragMode_Move || mDragMode == DragMode_Move_Snap)
    {
        const SceneHost& host = *getWorldspaceWidget().getHost();
        vsg::vec3 offset(host.getCameraCenter() - host.getCameraEye());
        Misc::VsgMath::normalizeInPlace(offset);
        offset = offset * static_cast<float>(diff * speedFactor);

        std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getEdited(Mask_Reference);
        auto snapTarget
            = dynamic_cast<CSVRender::ObjectTag*>(getWorldspaceWidget().getSnapTarget(Mask_Reference).get());

        int j = 0;

        for (std::vector<vsg::ref_ptr<TagBase>>::iterator iter(selection.begin()); iter != selection.end(); ++iter, j++)
        {
            if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(iter->get()))
            {
                ESM::Position position = objectTag->mObject->getPosition();
                auto preMovedObjectPosition = position.asVec3();
                for (int i = 0; i < 3; ++i)
                    position.pos[i] += offset[i];

                if (mDragMode == DragMode_Move_Snap)
                {
                    double snap = CSMPrefs::get()["3D Scene Editing"]["gridsnap-movement"].toDouble();

                    if (snapTarget)
                    {
                        const vsg::vec3 translation(static_cast<float>(snap), static_cast<float>(snap), static_cast<float>(snap));

                        auto snapTargetPosition = snapTarget->mObject->getPosition();
                        auto newPosition = InstanceMath::snapRelativeToTarget(preMovedObjectPosition,
                            snapTargetPosition.asVec3(), snapTargetPosition.asRotationVec3(), translation, snap);

                        position.pos[0] = newPosition[0];
                        position.pos[1] = newPosition[1];
                        position.pos[2] = newPosition[2];
                    }
                    else
                    {
                        position.pos[0] = InstanceMath::roundToMult(position.pos[0], snap);
                        position.pos[1] = InstanceMath::roundToMult(position.pos[1], snap);
                        position.pos[2] = InstanceMath::roundToMult(position.pos[2], snap);
                    }
                }

                objectTag->mObject->setPosition(position.pos);
                const vsg::vec3 thisPoint(position.pos[0], position.pos[1], position.pos[2]);
                mDragStart = getMousePlaneCoords(
                    getWorldspaceWidget().mapFromGlobal(QCursor::pos()), vsg::dvec3(getProjectionSpaceCoords(thisPoint)));
                mObjectsAtDragStart[j] = thisPoint;
            }
        }
    }
}

void CSVRender::InstanceMode::dragEnterEvent(QDragEnterEvent* event)
{
    if (const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData()))
    {
        if (!mime->fromDocument(getWorldspaceWidget().getDocument()))
            return;

        if (mime->holdsType(CSMWorld::UniversalId::Type_Referenceable))
            event->accept();
    }
}

void CSVRender::InstanceMode::dropEvent(QDropEvent* event)
{
    if (const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData()))
    {
        CSMDoc::Document& document = getWorldspaceWidget().getDocument();

        if (!mime->fromDocument(document))
            return;

        WorldspaceHitResult hit
            = getWorldspaceWidget().mousePick(event->position().toPoint(), getWorldspaceWidget().getInteractionMask());

        std::string cellId = getWorldspaceWidget().getCellId(vsg::vec3(hit.worldPos));

        CSMWorld::IdTree& cellTable
            = dynamic_cast<CSMWorld::IdTree&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Cells));

        const bool noCell = document.getData().getCells().searchId(ESM::RefId::stringRefId(cellId)) == -1;

        if (noCell)
        {
            std::string mode = CSMPrefs::get()["3D Scene Editing"]["outside-drop"].toString();

            // target cell does not exist
            if (mode == "Discard")
                return;

            if (mode == "Create cell and insert")
            {
                std::unique_ptr<CSMWorld::CreateCommand> createCommand(new CSMWorld::CreateCommand(cellTable, cellId));

                int parentIndex = cellTable.findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
                int index = cellTable.findNestedColumnIndex(parentIndex, CSMWorld::Columns::ColumnId_Interior);
                createCommand->addNestedValue(parentIndex, index, false);

                document.getUndoStack().push(createCommand.release());

                if (CSVRender::PagedWorldspaceWidget* paged
                    = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
                {
                    CSMWorld::CellSelection selection = paged->getCellSelection();
                    selection.add(CSMWorld::CellCoordinates::fromId(cellId).first);
                    paged->setCellSelection(selection);
                }
            }
        }
        else if (CSVRender::PagedWorldspaceWidget* paged
            = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
        {
            CSMWorld::CellSelection selection = paged->getCellSelection();
            if (!selection.has(CSMWorld::CellCoordinates::fromId(cellId).first))
            {
                // target cell exists, but is not shown
                std::string mode = CSMPrefs::get()["3D Scene Editing"]["outside-visible-drop"].toString();

                if (mode == "Discard")
                    return;

                if (mode == "Show cell and insert")
                {
                    selection.add(CSMWorld::CellCoordinates::fromId(cellId).first);
                    paged->setCellSelection(selection);
                }
            }
        }

        CSMWorld::IdTable& referencesTable = dynamic_cast<CSMWorld::IdTable&>(
            *document.getData().getTableModel(CSMWorld::UniversalId::Type_References));

        bool dropped = false;

        std::vector<CSMWorld::UniversalId> ids = mime->getData();

        for (std::vector<CSMWorld::UniversalId>::const_iterator iter(ids.begin()); iter != ids.end(); ++iter)
            if (mime->isReferencable(iter->getType()))
            {
                // create reference
                std::unique_ptr<CSMWorld::CreateCommand> createCommand(
                    new CSMWorld::CreateCommand(referencesTable, document.getData().getReferences().getNewId()));

                createCommand->addValue(referencesTable.findColumnIndex(CSMWorld::Columns::ColumnId_Cell),
                    QString::fromUtf8(cellId.c_str()));
                createCommand->addValue(
                    referencesTable.findColumnIndex(CSMWorld::Columns::ColumnId_PositionXPos), hit.worldPos.x);
                createCommand->addValue(
                    referencesTable.findColumnIndex(CSMWorld::Columns::ColumnId_PositionYPos), hit.worldPos.y);
                createCommand->addValue(
                    referencesTable.findColumnIndex(CSMWorld::Columns::ColumnId_PositionZPos), hit.worldPos.z);
                createCommand->addValue(referencesTable.findColumnIndex(CSMWorld::Columns::ColumnId_ReferenceableId),
                    QString::fromUtf8(iter->getId().c_str()));

                document.getUndoStack().push(createCommand.release());

                dropped = true;
            }

        if (dropped)
            event->accept();
    }
}

int CSVRender::InstanceMode::getSubMode() const
{
    return mSubMode ? getSubModeFromId(mSubMode->getCurrentId()) : 0;
}

void CSVRender::InstanceMode::subModeChanged(const std::string& id)
{
    mSubModeId = id;
    getWorldspaceWidget().abortDrag();
    getWorldspaceWidget().setSubMode(getSubModeFromId(id), Mask_Reference);
}

void CSVRender::InstanceMode::handleSelectDrag(const QPoint& pos)
{
    const vsg::vec3 mousePlanePoint = getMousePlaneCoords(pos, vsg::dvec3(getProjectionSpaceCoords(vsg::vec3(mSelectionMode->getDragStart()))));
    mSelectionMode->dragEnded(vsg::dvec3(mousePlanePoint), mDragMode);
    mDragMode = DragMode_None;
}

void CSVRender::InstanceMode::deleteSelectedInstances()
{
    std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);
    if (selection.empty())
        return;

    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& referencesTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_References));
    QUndoStack& undoStack = document.getUndoStack();

    CSMWorld::CommandMacro macro(undoStack, "Delete Instances");
    for (vsg::ref_ptr<TagBase> tag : selection)
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
            macro.push(new CSMWorld::DeleteCommand(referencesTable, objectTag->mObject->getReferenceId()));

    getWorldspaceWidget().clearSelection(Mask_Reference);
}

void CSVRender::InstanceMode::cloneSelectedInstances()
{
    std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);
    if (selection.empty())
        return;

    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& referencesTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_References));
    QUndoStack& undoStack = document.getUndoStack();

    CSMWorld::CommandMacro macro(undoStack, "Clone Instances");
    for (vsg::ref_ptr<TagBase> tag : selection)
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
        {
            macro.push(new CSMWorld::CloneCommand(referencesTable, objectTag->mObject->getReferenceId(),
                document.getData().getReferences().getNewId(), CSMWorld::UniversalId::Type_Reference));
        }
}

void CSVRender::InstanceMode::dropInstance(CSVRender::Object* object, float dropHeight)
{
    object->setEdited(Object::Override_Position);
    ESM::Position position = object->getPosition();
    position.pos[2] -= dropHeight;
    object->setPosition(position.pos);
}

float CSVRender::InstanceMode::calculateDropHeight(CSVRender::Object* object, float objectHeight)
{
    const vsg::dvec3 point(object->getPosition().asVec3());

    vsg::dvec3 start = point;
    start.z += objectHeight;
    vsg::dvec3 end = point;
    end.z = std::numeric_limits<float>::lowest();

    vsg::dvec3 hit;
    if (Picking::pickNearest(getWorldspaceWidget().getHost()->contentRoot(), Picking::Ray{ start, end },
            vsg::Mask(Mask_Terrain | Mask_Reference), hit))
    {
        const float collisionLevel = static_cast<float>(hit.z);
        return static_cast<float>(point.z) - collisionLevel + objectHeight;
    }

    return 0.0f;
}

void CSVRender::InstanceMode::dropToCollision()
{
    std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);
    if (selection.empty())
        return;

    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    QUndoStack& undoStack = document.getUndoStack();

    CSMWorld::CommandMacro macro(undoStack, "Drop objects to collision");

    DropObjectHeightHandler dropObjectDataHandler(&getWorldspaceWidget());

    int counter = 0;
    for (vsg::ref_ptr<TagBase> tag : selection)
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
        {
            float objectHeight = dropObjectDataHandler.mObjectHeights[counter++];
            float dropHeight = calculateDropHeight(objectTag->mObject, objectHeight);
            dropInstance(objectTag->mObject, dropHeight);
            objectTag->mObject->apply(macro);
        }
}

CSVRender::DropObjectHeightHandler::DropObjectHeightHandler(WorldspaceWidget* worldspacewidget)
    : mWorldspaceWidget(worldspacewidget)
{
    mWorldspaceWidget->getSelectionMarker()->setSuppressed(true);
    std::vector<vsg::ref_ptr<TagBase>> selection = mWorldspaceWidget->getSelection(Mask_Reference);
    for (vsg::ref_ptr<TagBase> tag : selection)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
        {
            mObjectHeights.emplace_back(objectTag->mObject->getBottomOffset());
            mHidden.emplace_back(objectTag->mObject->isHidden());
            objectTag->mObject->setHidden(true);
        }
    }
}

CSVRender::DropObjectHeightHandler::~DropObjectHeightHandler()
{
    std::vector<vsg::ref_ptr<TagBase>> selection = mWorldspaceWidget->getSelection(Mask_Reference);
    std::size_t counter = 0;
    for (vsg::ref_ptr<TagBase> tag : selection)
    {
        if (CSVRender::ObjectTag* objectTag = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
            objectTag->mObject->setHidden(mHidden[counter++]);
    }
    mWorldspaceWidget->getSelectionMarker()->setSuppressed(false);
}
