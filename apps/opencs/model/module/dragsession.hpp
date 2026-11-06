#ifndef CSM_MODULE_DRAGSESSION_H
#define CSM_MODULE_DRAGSESSION_H

#include <memory>
#include <string>

#include <apps/opencs/model/module/overlaypick.hpp>
#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    /// One drag of a gizmo handle (task 0091 stage c), as pure state: the viewport feeds it camera rays, the gizmo
    /// makes the commands, and the host pushes ONE command at the end, so one drag is one undo step.
    ///
    /// The handle moves on the plane through its start position facing the camera (the press ray's direction), keeping
    /// the offset between where the press hit the plane and the handle. Each update asks the gizmo for the command to
    /// the new position (Gizmo::drag) and runs its redo() as a preview, after undoing the previous preview, so the
    /// document shows the drag without any step on Studio's undo stack. finish() undoes the preview and returns the
    /// last command, which the host pushes (and so runs redo() on) once; cancel() just undoes the preview.
    ///
    /// Ownership: borrows the gizmo (which must outlive the session); owns the command of the live preview. Main
    /// thread only. An exception from the gizmo leaves the session with no preview applied and propagates.
    class DragSession
    {
    public:
        /// \param pressRay the camera ray of the mouse press. valid() is false if it does not meet the plane.
        DragSession(openeden::editor::Gizmo& gizmo, std::string handleId, const openeden::editor::Vec3& handlePosition,
            const PickRay& pressRay);
        ~DragSession();

        DragSession(const DragSession&) = delete;
        DragSession& operator=(const DragSession&) = delete;

        bool valid() const { return mValid; }
        const std::string& handleId() const { return mHandleId; }

        /// Moves the preview to where \a ray meets the drag plane. False (nothing changed) when the ray is parallel to
        /// the plane or the gizmo refused the position (no command).
        bool update(const PickRay& ray);

        /// Whether a preview is applied now.
        bool changed() const { return static_cast<bool>(mPreview); }
        /// The position of the last accepted update (the start position before any).
        const openeden::editor::Vec3& position() const { return mPosition; }

        /// Undoes the preview and hands over its command, not applied: the host pushes it. Null if nothing changed.
        /// The session ends.
        std::unique_ptr<openeden::editor::Command> finish();
        /// Undoes the preview. The session ends.
        void cancel();

    private:
        void undoPreview();

        openeden::editor::Gizmo& mGizmo;
        std::string mHandleId;
        openeden::editor::Vec3 mStart;
        openeden::editor::Vec3 mPosition;
        openeden::editor::Vec3 mNormal;
        openeden::editor::Vec3 mOffset; ///< Handle position minus the press point on the plane.
        std::unique_ptr<openeden::editor::Command> mPreview;
        bool mValid = false;
        bool mEnded = false;
    };
}

#endif
