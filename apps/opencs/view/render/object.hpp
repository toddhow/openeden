#ifndef OPENCS_VIEW_OBJECT_H
#define OPENCS_VIEW_OBJECT_H

#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>

#include "tagbase.hpp"

class QModelIndex;

namespace MWAnim
{
    class Object;
}

namespace CSMWorld
{
    class Data;
    struct CellRef;
    class CommandMacro;
}

namespace CSVRender
{
    class Actor;
    class Object;
    class SceneHost;
    struct WorldspaceHitResult;

    // An object to attach as user data to the osg::Node, allows us to get an Object back from a Node when we are doing
    // a ray query
    class ObjectTag : public TagBase
    {
    public:
        // Inside this class, a plain Object means its base class's base, vsg::Object.
        ObjectTag(CSVRender::Object* object);

        CSVRender::Object* mObject;

        QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const override;
    };

    /// One instance or referenceable in a view. Owned by its Cell or PreviewWidget. Its nodes hang from a group that the
    /// owner gives it, and the owner decides when that group is compiled, so that a cell compiles its objects at once.
    class Object
    {
    public:
        enum OverrideFlags
        {
            Override_Position = 1,
            Override_Rotation = 2,
            Override_Scale = 4
        };

        enum SubMode
        {
            Mode_Move,
            Mode_Rotate,
            Mode_Scale,
            Mode_None,
        };

        enum Axis
        {
            Axis_X,
            Axis_Y,
            Axis_Z
        };

    private:
        CSMWorld::Data& mData;
        SceneHost& mHost;
        ESM::RefId mReferenceId;
        ESM::RefId mReferenceableId;
        /// Shows for Mask_Reference; its child is the model's node, replaced by update().
        vsg::ref_ptr<vsg::Switch> mRootNode;
        vsg::ref_ptr<TagBase> mTag;
        std::unique_ptr<MWAnim::Object> mAnimation;
        std::unique_ptr<Actor> mActor;
        std::vector<vsg::ref_ptr<vsg::Switch>> mNightDaySwitches;
        unsigned int mNightDayIndex = 0;
        bool mNightDayApply = false;
        bool mSelected{ false };
        bool mSnapTarget{ false };
        vsg::Group& mParentNode;
        bool mForceBaseToZero;
        ESM::Position mPositionOverride;
        float mScaleOverride{ 1.f };
        int mOverrideFlags{ 0 };

        /// Not implemented
        Object(const Object&);

        /// Not implemented
        Object& operator=(const Object&);

        /// Update model
        /// \param compile whether the root is in a view already, so that what it replaces must be compiled here.
        void update(bool compile);

        /// Adjust position, orientation and scale
        void adjustTransform();

        /// Throws an exception if *this was constructed with referenceable
        const CSMWorld::CellRef& getReference() const;

    public:
        /// \param forceBaseToZero If this is a reference ignore the coordinates and place
        /// it at 0, 0, 0 instead.
        /// \param compile compile and show it now: parent is in the scene. Otherwise its owner compiles parent.
        Object(CSMWorld::Data& data, SceneHost& host, vsg::Group& parent, const std::string& id, bool referenceable,
            bool forceBaseToZero = false, bool compile = false);

        ~Object();

        /// Runs animations that play by themselves, and follows the view's day and night mode.
        void frameUpdate(float dt);

        /// Mark the object as selected. Stage C7 brings back the outline.
        void setSelected(bool selected, const vsg::vec4& color = vsg::vec4(1, 1, 1, 1));

        bool getSelected() const;

        /// Mark Object as "snap target"
        void setSnapTarget(bool isSnapTarget);

        bool getSnapTarget() const;

        /// Get object node with GUI graphics
        vsg::ref_ptr<vsg::Switch> getRootNode();

        /// \return Did this call result in a modification of the visual representation of
        /// this object?
        bool referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        /// \return Did this call result in a modification of the visual representation of
        /// this object?
        bool referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        /// \return Did this call result in a modification of the visual representation of
        /// this object?
        bool referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        /// Reloads the underlying asset
        void reloadAssets();

        /// Returns an empty string if this is a refereceable-type object.
        std::string getReferenceId() const;

        std::string getReferenceableId() const;

        vsg::ref_ptr<TagBase> getTag() const;

        /// Is there currently an editing operation running on this object?
        bool isEdited() const;

        void setEdited(int flags);

        ESM::Position getPosition() const;

        float getScale() const;

        /// Set override position.
        void setPosition(const float position[3]);

        /// Set override rotation
        void setRotation(const float rotation[3]);

        /// Set override scale
        void setScale(float scale);

        /// Apply override changes via command and end edit mode
        void apply(CSMWorld::CommandMacro& commands);

        /// Erase all overrides and restore the visual representation of the object to its
        /// true state.
        void reset();
    };
}

#endif
