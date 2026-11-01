#ifndef CREATIVEMOTION_EDITOR_PROPERTY_COMMANDS_H
#define CREATIVEMOTION_EDITOR_PROPERTY_COMMANDS_H

#include <openeden/editor_module/types.hpp>

#include <string>

namespace creativemotion_editor
{
    /// The undo step of one number in a document: sets \a *target to the new value on redo, back on undo, marks the
    /// document dirty on redo. Edits of the same property share a merge ID, so dragging a field is one step. The
    /// target is a member of the document that made the command and outlives it (the document owns its commands'
    /// lifetime through the editor's undo stack, which the editor empties before it closes a document).
    class SetNumberCommand final : public openeden::editor::Command
    {
    public:
        SetNumberCommand(std::string label, double* target, bool* dirty, double newValue, int mergeId)
            : mLabel(std::move(label))
            , mTarget(target)
            , mDirty(dirty)
            , mOld(*target)
            , mNew(newValue)
            , mMergeId(mergeId)
        {
        }

        std::string_view label() const override { return mLabel; }
        void redo() override
        {
            *mTarget = mNew;
            *mDirty = true;
        }
        void undo() override
        {
            *mTarget = mOld;
            *mDirty = true; // the file no longer matches what was saved
        }
        int mergeId() const override { return mMergeId; }
        bool mergeWith(const openeden::editor::Command& next) override
        {
            const auto* other = dynamic_cast<const SetNumberCommand*>(&next);
            if (other == nullptr || other->mTarget != mTarget)
                return false;
            mNew = other->mNew;
            return true;
        }

    private:
        std::string mLabel;
        double* mTarget;
        bool* mDirty;
        double mOld;
        double mNew;
        int mMergeId;
    };
}

#endif
