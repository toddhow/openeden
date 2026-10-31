#ifndef OPENCS_VIEW_OBJECT_MARKER_H
#define OPENCS_VIEW_OBJECT_MARKER_H

#include <string>
#include <string_view>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>

#include "object.hpp"
#include "scenehost.hpp"

namespace CSVRender
{
    class WorldspaceWidget;

    class ObjectMarkerTag : public ObjectTag
    {
    public:
        ObjectMarkerTag(CSVRender::Object* object, int axis, std::string node);

        int mAxis;
        /// The part of the marker, as the old file named its nodes: X_Axis, Y_Wall_Scale...
        std::string mNode;
    };

    /// The transform gizmo, drawn at the object last selected. Owned by its WorldspaceWidget. The geometry is
    /// selectionmarkergeometry.hpp's. It has one node in the scene and follows the object each frame, rather than hanging
    /// under the object as the OSG one did, so that no object's deletion can take it along.
    class ObjectMarker : public FrameListener
    {
        struct Part
        {
            std::string mName;
            int mAxis = 0;
            bool mAlpha = false;
            vsg::ref_ptr<vsg::vec3Array> mVertices;
            vsg::ref_ptr<vsg::vec4Array> mColours;
            /// A rotate ring's whole shape. mVertices holds the half facing the eye.
            std::vector<vsg::vec3> mFullVertices;
            vsg::ref_ptr<ObjectMarkerTag> mTag;
        };

        WorldspaceWidget* mWorldspaceWidget;
        SceneHost* mHost = nullptr;
        vsg::ref_ptr<vsg::Switch> mRootNode;
        vsg::ref_ptr<vsg::MatrixTransform> mTransform;
        vsg::ref_ptr<vsg::Switch> mModes;
        std::vector<Part> mParts;
        std::vector<vsg::vec3> mClipped;
        std::vector<std::string> mSelectionHistory;
        std::string mAttached;
        std::string mLastHitNode;
        int mHighlightAxis = -1;
        float mMarkerScale;
        int mSubMode;
        bool mUserVisible = true;
        bool mSuppressed = false;
        bool mFlipped = false;
        vsg::vec3 mLastRingEye;

        bool attachMarker(const std::string& refId);
        void removeFromSelectionHistory(const std::string& refId);
        void updateMask();
        void applyColours();
        void addParts(vsg::Switch& modes, int mode);
        void updateRings(const vsg::vec3& eye);

    public:
        explicit ObjectMarker(WorldspaceWidget* worldspaceWidget);
        ~ObjectMarker() override;

        ObjectMarker(ObjectMarker&) = delete;
        ObjectMarker& operator=(const ObjectMarker&) = delete;

        /// The view exists now, so the nodes can be built.
        void setHost(SceneHost& host);

        void frameUpdate(float dt) override;

        void toggleVisibility();

        /// Out of rays and off the screen meanwhile, as DropObjectHeightHandler wants it.
        void setSuppressed(bool suppressed);

        bool isAttached() const { return !mAttached.empty(); }

        /// Is a hit on the far half of the rotate rings, which the old clip plane hid?
        bool hitBehindMarker(const vsg::dvec3& hitPos) const;

        void detachMarker();

        void addToSelectionHistory(const std::string& refId, bool update = true);

        void updateSelectionMarker(const std::string& refId = std::string());

        void resetMarkerHighlight();

        void updateMarkerHighlight(const std::string_view hitNode, const int axis);

        void setSubMode(const int subMode);

        void updateScale(const float scale);
    };
}
#endif // OPENCS_VIEW_OBJECT_MARKER_H
