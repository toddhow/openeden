#ifndef OPENEDEN_COMPONENTS_NIFEDIT_NIFDOCUMENT_H
#define OPENEDEN_COMPONENTS_NIFEDIT_NIFDOCUMENT_H

// Task 0046, stage c: a NIF opened for editing. The document owns the file's original bytes, their index and the set
// of patches; it never changes the bytes. Edits are commands on an undo stack whose method names match the Editor
// Module API's Command (label, redo, undo, mergeId, mergeWith), so that a thin adapter in the editor can push them
// on the editor's one undo stack. Nothing here touches a window, a GPU or a file.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nifindex.hpp"
#include "nifpatch.hpp"

namespace NifEdit
{
    class Document
    {
    public:
        /// Indexes the bytes and checks that the file can be saved back unchanged. Throws Nif::Exception when the
        /// reader refuses the file, EditError when its layout cannot be indexed.
        static Document open(std::string bytes);

        const std::string& original() const { return mOriginal; }
        const FileIndex& index() const { return mIndex; }
        const PatchSet& patches() const { return mPatches; }

        /// The value now: the patch if there is one, else what the file holds.
        PatchValue value(std::uint32_t record, std::string_view field) const;
        /// The value in the original file.
        PatchValue originalValue(std::uint32_t record, std::string_view field) const;
        std::optional<PatchValue> patchValue(std::uint32_t record, std::string_view field) const;

        /// Sets a patch, or removes it when the value equals the file's own. Throws EditError (and changes nothing)
        /// when the record, field or value is not acceptable.
        void set(std::uint32_t record, std::string_view field, PatchValue value);
        /// Puts a patch back exactly as it was (nullopt: no patch). Used by undo.
        void restore(std::uint32_t record, std::string_view field, std::optional<PatchValue> previous);

        /// The edited file's bytes. No patches: the original bytes.
        std::string build() const;

        /// build() plus the checks of stage e: the result is read again with the engine's reader, has the same
        /// records (count and types) and roots, and every patched field reads back as set. Throws EditError.
        std::string buildChecked() const;

        /// Changes whenever the patches change.
        std::uint64_t generation() const { return mGeneration; }
        bool dirty() const { return mGeneration != mSavedGeneration; }
        void markSaved() { mSavedGeneration = mGeneration; }

    private:
        Document() = default;

        std::string mOriginal;
        FileIndex mIndex;
        PatchSet mPatches;
        std::uint64_t mGeneration = 0;
        std::uint64_t mSavedGeneration = 0;
    };

    /// Sets one field. redo() sets it, undo() puts back what was there before. Two commands for the same record and
    /// field that the caller marks as one gesture (a dragged number) merge into one undo step.
    class SetFieldCommand
    {
    public:
        SetFieldCommand(Document& document, std::uint32_t record, std::string field, PatchValue value,
            bool mergeable = false);

        std::string label() const;
        void redo();
        void undo();
        /// A value >= 0 when the command may be merged: the same for the same record and field.
        int mergeId() const;
        /// True if next (a later command for the same record and field) was absorbed.
        bool mergeWith(const SetFieldCommand& next);

        std::uint32_t record() const { return mRecord; }
        const std::string& field() const { return mField; }
        const PatchValue& value() const { return mValue; }

    private:
        Document* mDocument;
        std::uint32_t mRecord;
        std::string mField;
        PatchValue mValue;
        std::optional<PatchValue> mPrevious;
        bool mHavePrevious = false;
        bool mMergeable;
    };

    /// The undo stack of a document. The clean state is where the file was last saved.
    class UndoStack
    {
    public:
        /// Applies the command (redo), then keeps it; a mergeable command merges into the top one. Drops the redo
        /// branch. If the command's redo throws, nothing is pushed and the exception goes on.
        void push(std::unique_ptr<SetFieldCommand> command);
        bool canUndo() const { return mIndex > 0; }
        bool canRedo() const { return mIndex < mCommands.size(); }
        void undo();
        void redo();
        std::size_t size() const { return mCommands.size(); }
        void setClean() { mClean = mIndex; mCleanValid = true; }
        bool isClean() const { return mCleanValid && mClean == mIndex; }

    private:
        std::vector<std::unique_ptr<SetFieldCommand>> mCommands;
        std::size_t mIndex = 0;
        std::size_t mClean = 0;
        bool mCleanValid = true;
    };
}

#endif
