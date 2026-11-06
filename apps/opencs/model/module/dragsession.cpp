#include "dragsession.hpp"

namespace CSMModule
{
    using openeden::editor::Vec3;

    DragSession::DragSession(
        openeden::editor::Gizmo& gizmo, std::string handleId, const Vec3& handlePosition, const PickRay& pressRay)
        : mGizmo(gizmo)
        , mHandleId(std::move(handleId))
        , mStart(handlePosition)
        , mPosition(handlePosition)
        , mNormal(pressRay.mDirection)
    {
        // The drag plane passes through the handle and faces the camera.
        if (const std::optional<Vec3> hit = rayPlaneHit(pressRay, handlePosition, mNormal))
        {
            mOffset = { handlePosition.x - hit->x, handlePosition.y - hit->y, handlePosition.z - hit->z };
            mValid = true;
        }
    }

    DragSession::~DragSession()
    {
        // Never leaves a preview in the document.
        try
        {
            undoPreview();
        }
        catch (...)
        {
        }
    }

    void DragSession::undoPreview()
    {
        if (mPreview)
        {
            // Dropped first: if undo() throws, the preview is not undone twice.
            std::unique_ptr<openeden::editor::Command> preview = std::move(mPreview);
            preview->undo();
        }
    }

    bool DragSession::update(const PickRay& ray)
    {
        if (!mValid || mEnded)
            return false;
        const std::optional<Vec3> hit = rayPlaneHit(ray, mStart, mNormal);
        if (!hit)
            return false;
        const Vec3 target{ hit->x + mOffset.x, hit->y + mOffset.y, hit->z + mOffset.z };

        // The previous preview goes first, so that the gizmo makes its new command from the original state. If the
        // gizmo throws, no preview stays applied; if it refuses the position, the old preview comes back.
        std::unique_ptr<openeden::editor::Command> previous = std::move(mPreview);
        if (previous)
            previous->undo();
        std::unique_ptr<openeden::editor::Command> command = mGizmo.drag(mHandleId, target);
        if (!command)
        {
            if (previous)
            {
                previous->redo();
                mPreview = std::move(previous);
            }
            return false;
        }
        command->redo();
        mPreview = std::move(command);
        mPosition = target;
        return true;
    }

    std::unique_ptr<openeden::editor::Command> DragSession::finish()
    {
        if (mEnded)
            return nullptr;
        mEnded = true;
        std::unique_ptr<openeden::editor::Command> command = std::move(mPreview);
        if (command)
            command->undo();
        return command;
    }

    void DragSession::cancel()
    {
        if (mEnded)
            return;
        mEnded = true;
        undoPreview();
    }
}
