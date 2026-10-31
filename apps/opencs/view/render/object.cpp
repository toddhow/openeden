#include "object.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include <vsg/core/Visitor.h>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/ref.hpp>
#include <apps/opencs/model/world/refcollection.hpp>
#include <apps/opencs/model/world/refidcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include "../../model/world/cellcoordinates.hpp"
#include "../../model/world/commandmacro.hpp"
#include "../../model/world/commands.hpp"
#include "../../model/world/data.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/create.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "actor.hpp"
#include "mask.hpp"
#include "scenehost.hpp"

namespace
{
    // A model that does not exist comes back as Resource::Fallback's stand-in, as the error cube of the OSG editor stood in.
    const std::string sMissingModel = "marker_error.nif";

    /// As MWRender::findSwitches in the game: every child, also those switched off.
    class FindNightDaySwitches : public vsg::Visitor
    {
    public:
        std::vector<vsg::ref_ptr<vsg::Switch>> mFound;

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::Switch& node) override
        {
            std::string name;
            if (node.getValue("switch", name) && name == Constants::NightDayLabel)
                mFound.emplace_back(&node);
            for (auto& child : node.children)
                child.node->accept(*this);
        }
    };
}

CSVRender::ObjectTag::ObjectTag(CSVRender::Object* object)
    : TagBase(Mask_Reference)
    , mObject(object)
{
}

QString CSVRender::ObjectTag::getToolTip(bool /*hideBasics*/, const WorldspaceHitResult& /*hit*/) const
{
    return QString::fromUtf8(mObject->getReferenceableId().c_str());
}

void CSVRender::Object::update(bool compile)
{
    const CSMWorld::RefIdCollection& referenceables = mData.getReferenceables();
    const int typeIndex = referenceables.findColumnIndex(CSMWorld::Columns::ColumnId_RecordType);
    const int modelIndex = referenceables.findColumnIndex(CSMWorld::Columns::ColumnId_Model);

    int index = referenceables.searchId(mReferenceableId);
    const ESM::Light* light = nullptr;
    std::string model = sMissingModel;
    int recordType = 0;

    /// \todo check for Deleted state (error 1)
    if (index != -1)
    {
        recordType = referenceables.getData(index, typeIndex).toInt();
        model = referenceables.getData(index, modelIndex).toString().toUtf8().constData();

        if (recordType == CSMWorld::UniversalId::Type_Light)
        {
            light = &dynamic_cast<const CSMWorld::Record<ESM::Light>&>(referenceables.getRecord(index)).get();
            if (model.empty())
                model = "marker_light.nif";
        }

        if (recordType == CSMWorld::UniversalId::Type_CreatureLevelledList && model.empty())
            model = "marker_creature.nif";
    }

    std::unique_ptr<MWAnim::Object> animation;
    try
    {
        if (recordType == CSMWorld::UniversalId::Type_Npc || recordType == CSMWorld::UniversalId::Type_Creature)
        {
            if (!mActor)
                mActor = std::make_unique<Actor>(mReferenceableId, mData, mHost.animContext(), [this] { update(true); });
            animation = mActor->create();
        }
        else if (model.empty())
            throw std::runtime_error(mReferenceableId.getRefIdString() + " has no model");
        else
        {
            // As the classes whose useAnim is true in apps/openmw/mwclass: their models may be skinned, which needs bones.
            const bool useAnim = recordType == CSMWorld::UniversalId::Type_Door
                || recordType == CSMWorld::UniversalId::Type_Activator
                || recordType == CSMWorld::UniversalId::Type_Container
                || recordType == CSMWorld::UniversalId::Type_Light;
            animation = MWAnim::createObject(model, useAnim, light, mHost.animContext());
        }
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << e.what();
    }

    if (animation)
    {
        FindNightDaySwitches switches;
        animation->node()->accept(switches);
        mNightDaySwitches = std::move(switches.mFound);
        if (compile)
            mHost.compileContext()->compile(animation->node());
    }

    for (auto& child : mRootNode->children)
        mHost.compileContext()->detach(child.node);
    mRootNode->children.clear();
    mAnimation = std::move(animation);
    if (mAnimation)
        mRootNode->addChild(vsg::MASK_ALL, mAnimation->node());
    mNightDayApply = false;
    adjustTransform();
}

void CSVRender::Object::frameUpdate(float dt)
{
    if (!mAnimation)
        return;
    mAnimation->update(dt);

    if (mNightDayApply != mHost.getNightDayApply() || mNightDayIndex != mHost.getNightDayIndex())
    {
        mNightDayApply = mHost.getNightDayApply();
        mNightDayIndex = mHost.getNightDayIndex();
        // With the preference off, the first child; the OSG editor kept the model's initial index, which VSG's reader
        // does not record.
        const unsigned int index = mNightDayApply ? mNightDayIndex : 0;
        for (auto& nightDay : mNightDaySwitches)
            for (std::size_t i = 0; i < nightDay->children.size(); ++i)
                nightDay->children[i].mask = i == (nightDay->children.size() > index ? index : 0) ? vsg::MASK_ALL : vsg::MASK_OFF;
    }
}

bool CSVRender::Object::isAnimated() const
{
    if (!mAnimation)
        return false;
    const auto& groups = mAnimation->autoPlay.groups;
    return std::any_of(groups.begin(), groups.end(), [](const auto& group) { return !group.controllers.empty(); });
}

void CSVRender::Object::adjustTransform()
{
    if (mReferenceId.empty() || !mAnimation)
        return;

    ESM::Position position = getPosition();
    auto* transform = mAnimation->transform();

    transform->translation = mForceBaseToZero ? vsg::vec3() : vsg::vec3(position.pos[0], position.pos[1], position.pos[2]);
    // As apps/openmw/mwworld/scene.cpp's makeInversedOrderObjectOsgQuat, which is the OSG editor's zr * yr * xr.
    transform->setAttitude(vsg::quat(Misc::VsgMath::makeRotateFromAxis(position.rot[0], vsg::vec3(-1, 0, 0))
        * Misc::VsgMath::makeRotateFromAxis(position.rot[1], vsg::vec3(0, -1, 0))
        * Misc::VsgMath::makeRotateFromAxis(position.rot[2], vsg::vec3(0, 0, -1))));
    const vsg::vec3 actorScale = mActor ? mActor->getScale() : vsg::vec3(1.f, 1.f, 1.f);
    const float scale = getScale();
    transform->scale = actorScale * scale;
    mHost.requestRedraw();
}

const CSMWorld::CellRef& CSVRender::Object::getReference() const
{
    if (mReferenceId.empty())
        throw std::logic_error("object does not represent a reference");

    return mData.getReferences().getRecord(mReferenceId).get();
}

CSVRender::Object::Object(CSMWorld::Data& data, SceneHost& host, vsg::Group& parent, const std::string& id,
    bool referenceable, bool forceBaseToZero, bool compile)
    : mData(data)
    , mHost(host)
    , mRootNode(vsg::Switch::create())
    , mTag(new ObjectTag(this))
    , mParentNode(parent)
    , mForceBaseToZero(forceBaseToZero)
{
    ESM::RefId refId = ESM::RefId::stringRefId(id);
    if (referenceable)
    {
        mReferenceableId = refId;
    }
    else
    {
        mReferenceId = refId;
        mReferenceableId = getReference().mRefID;
    }

    update(compile);

    mShownNode = SceneHost::maskNode(Mask_Reference, mRootNode);
    mParentNode.addChild(mShownNode);
}

CSVRender::Object::~Object()
{
    mActor.reset();
    auto& children = mParentNode.children;
    children.erase(std::remove_if(children.begin(), children.end(),
                       [&](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == mShownNode.get(); }),
        children.end());
    mHost.compileContext()->detach(mRootNode);
}

void CSVRender::Object::setSelected(bool selected, const vsg::vec4& color)
{
    mSelected = selected;

    if (mSnapTarget)
    {
        setSnapTarget(false);
    }
}

bool CSVRender::Object::getSelected() const
{
    return mSelected;
}

void CSVRender::Object::setSnapTarget(bool isSnapTarget)
{
    mSnapTarget = isSnapTarget;

    if (mSelected)
    {
        setSelected(false);
    }
}

bool CSVRender::Object::getSnapTarget() const
{
    return mSnapTarget;
}

vsg::ref_ptr<vsg::Switch> CSVRender::Object::getRootNode()
{
    return mRootNode;
}

bool CSVRender::Object::referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    const CSMWorld::RefIdCollection& referenceables = mData.getReferenceables();

    int index = referenceables.searchId(mReferenceableId);

    if (index != -1 && index >= topLeft.row() && index <= bottomRight.row())
    {
        update(true);
        return true;
    }

    return false;
}

bool CSVRender::Object::referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    const CSMWorld::RefIdCollection& referenceables = mData.getReferenceables();

    int index = referenceables.searchId(mReferenceableId);

    if (index != -1 && index >= start && index <= end)
    {
        // Deletion of referenceable-type objects is handled outside of Object.
        if (!mReferenceId.empty())
        {
            update(true);
            return true;
        }
    }

    return false;
}

bool CSVRender::Object::referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mReferenceId.empty())
        return false;

    const CSMWorld::RefCollection& references = mData.getReferences();

    int index = references.searchId(mReferenceId);

    if (index != -1 && index >= topLeft.row() && index <= bottomRight.row())
    {
        int columnIndex = references.findColumnIndex(CSMWorld::Columns::ColumnId_ReferenceableId);

        adjustTransform();

        if (columnIndex >= topLeft.column() && columnIndex <= bottomRight.row())
        {
            mReferenceableId
                = ESM::RefId::stringRefId(references.getData(index, columnIndex).toString().toUtf8().constData());

            update(true);
        }

        return true;
    }

    return false;
}

void CSVRender::Object::reloadAssets()
{
    update(true);
}

std::string CSVRender::Object::getReferenceId() const
{
    return mReferenceId.getRefIdString();
}

std::string CSVRender::Object::getReferenceableId() const
{
    return mReferenceableId.getRefIdString();
}

vsg::ref_ptr<CSVRender::TagBase> CSVRender::Object::getTag() const
{
    return mTag;
}

bool CSVRender::Object::isEdited() const
{
    return mOverrideFlags;
}

void CSVRender::Object::setEdited(int flags)
{
    bool discard = mOverrideFlags & ~flags;
    int added = flags & ~mOverrideFlags;

    mOverrideFlags = flags;

    if (added & Override_Position)
        for (int i = 0; i < 3; ++i)
            mPositionOverride.pos[i] = getReference().mPos.pos[i];

    if (added & Override_Rotation)
        for (int i = 0; i < 3; ++i)
            mPositionOverride.rot[i] = getReference().mPos.rot[i];

    if (added & Override_Scale)
        mScaleOverride = getReference().mScale;

    if (discard)
        adjustTransform();
}

ESM::Position CSVRender::Object::getPosition() const
{
    ESM::Position position = getReference().mPos;

    if (mOverrideFlags & Override_Position)
        for (int i = 0; i < 3; ++i)
            position.pos[i] = mPositionOverride.pos[i];

    if (mOverrideFlags & Override_Rotation)
        for (int i = 0; i < 3; ++i)
            position.rot[i] = mPositionOverride.rot[i];

    return position;
}

float CSVRender::Object::getScale() const
{
    return (mOverrideFlags & Override_Scale) ? mScaleOverride : getReference().mScale;
}

void CSVRender::Object::setPosition(const float position[3])
{
    mOverrideFlags |= Override_Position;

    for (int i = 0; i < 3; ++i)
        mPositionOverride.pos[i] = position[i];

    adjustTransform();
}

void CSVRender::Object::setRotation(const float rotation[3])
{
    mOverrideFlags |= Override_Rotation;

    for (int i = 0; i < 3; ++i)
        mPositionOverride.rot[i] = rotation[i];

    adjustTransform();
}

void CSVRender::Object::setScale(float scale)
{
    mOverrideFlags |= Override_Scale;

    mScaleOverride = std::clamp(scale, 0.5f, 2.0f);

    adjustTransform();
}

void CSVRender::Object::apply(CSMWorld::CommandMacro& commands)
{
    const CSMWorld::RefCollection& collection = mData.getReferences();
    QAbstractItemModel* model = mData.getTableModel(CSMWorld::UniversalId::Type_References);

    int recordIndex = collection.getIndex(mReferenceId);

    if (mOverrideFlags & Override_Position)
    {
        // Do cell check first so positions can be compared
        const CSMWorld::CellRef& ref = collection.getRecord(recordIndex).get();

        if (CSMWorld::CellCoordinates::isExteriorCell(ref.mCell))
        {
            // Find cell index at new position
            std::pair<int, int> cellIndex
                = CSMWorld::CellCoordinates::coordinatesToCellIndex(mPositionOverride.pos[0], mPositionOverride.pos[1]);
            std::pair<int, int> originalIndex = ref.getCellIndex();

            int cellColumn = collection.findColumnIndex(
                static_cast<CSMWorld::Columns::ColumnId>(CSMWorld::Columns::ColumnId_Cell));
            int origCellColumn = collection.findColumnIndex(
                static_cast<CSMWorld::Columns::ColumnId>(CSMWorld::Columns::ColumnId_OriginalCell));

            if (cellIndex != originalIndex)
            {
                /// \todo figure out worldspace (not important until multiple worldspaces are supported)
                std::string origCellId = CSMWorld::CellCoordinates(originalIndex).getId("");
                std::string cellId = CSMWorld::CellCoordinates(cellIndex).getId("");

                commands.push(new CSMWorld::ModifyCommand(
                    *model, model->index(recordIndex, origCellColumn), QString::fromUtf8(origCellId.c_str())));
                commands.push(new CSMWorld::ModifyCommand(
                    *model, model->index(recordIndex, cellColumn), QString::fromUtf8(cellId.c_str())));
                // NOTE: refnum is not modified for moving a reference to another cell
            }
        }

        for (int i = 0; i < 3; ++i)
        {
            int column = collection.findColumnIndex(
                static_cast<CSMWorld::Columns::ColumnId>(CSMWorld::Columns::ColumnId_PositionXPos + i));

            commands.push(
                new CSMWorld::ModifyCommand(*model, model->index(recordIndex, column), mPositionOverride.pos[i]));
        }
    }

    if (mOverrideFlags & Override_Rotation)
    {
        for (int i = 0; i < 3; ++i)
        {
            int column = collection.findColumnIndex(
                static_cast<CSMWorld::Columns::ColumnId>(CSMWorld::Columns::ColumnId_PositionXRot + i));

            commands.push(new CSMWorld::ModifyCommand(
                *model, model->index(recordIndex, column), Misc::VsgMath::radiansToDegrees(mPositionOverride.rot[i])));
        }
    }

    if (mOverrideFlags & Override_Scale)
    {
        int column = collection.findColumnIndex(CSMWorld::Columns::ColumnId_Scale);

        commands.push(new CSMWorld::ModifyCommand(*model, model->index(recordIndex, column), mScaleOverride));
    }

    mOverrideFlags = 0;
}

void CSVRender::Object::reset()
{
    mOverrideFlags = 0;
    adjustTransform();
}
