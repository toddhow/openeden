#include "nifdocument.hpp"

#include <functional>
#include <utility>

namespace NifEdit
{
    namespace
    {
        // What a field reads back as after the value was written: a number goes through a 32-bit float.
        PatchValue asStored(const PatchValue& value)
        {
            if (const auto* number = std::get_if<double>(&value))
                return static_cast<double>(static_cast<float>(*number));
            if (const auto* keys = std::get_if<std::vector<TextKey>>(&value))
                return *keys;
            return value;
        }
    }

    Document Document::open(std::string bytes)
    {
        Document document;
        document.mIndex = indexFile(bytes);
        document.mOriginal = std::move(bytes);
        // The guarantee of the task: with no edits the saved file is the opened one.
        if (document.build() != document.mOriginal)
            throw EditError("NIF edit: this file cannot be edited exactly (an unchanged save differs from it)");
        return document;
    }

    PatchValue Document::originalValue(std::uint32_t record, std::string_view field) const
    {
        return readField(mOriginal, mIndex, record, field);
    }

    std::optional<PatchValue> Document::patchValue(std::uint32_t record, std::string_view field) const
    {
        if (const FieldPatch* patch = mPatches.find(record, field))
            return patch->mValue;
        return std::nullopt;
    }

    PatchValue Document::value(std::uint32_t record, std::string_view field) const
    {
        if (const FieldPatch* patch = mPatches.find(record, field))
            return patch->mValue;
        return originalValue(record, field);
    }

    void Document::set(std::uint32_t record, std::string_view field, PatchValue value)
    {
        FieldPatch patch{ record, std::string(field), std::move(value) };
        checkPatch(mOriginal, mIndex, patch);
        if (asStored(patch.mValue) == originalValue(record, field))
            mPatches.erase(record, field);
        else
            mPatches.set(std::move(patch));
        ++mGeneration;
    }

    void Document::restore(std::uint32_t record, std::string_view field, std::optional<PatchValue> previous)
    {
        if (previous)
            mPatches.set(FieldPatch{ record, std::string(field), std::move(*previous) });
        else
            mPatches.erase(record, field);
        ++mGeneration;
    }

    std::string Document::build() const
    {
        return applyPatches(mOriginal, mIndex, mPatches);
    }

    std::string Document::buildChecked() const
    {
        std::string bytes = build();
        FileIndex saved;
        try
        {
            saved = indexFile(bytes);
        }
        catch (const std::exception& e)
        {
            throw EditError(std::string("NIF edit: the edited file does not read back: ") + e.what());
        }
        if (saved.mRecords.size() != mIndex.mRecords.size())
            throw EditError("NIF edit: the edited file has a different number of records");
        for (std::size_t i = 0; i < saved.mRecords.size(); ++i)
        {
            if (saved.mRecords[i].mType != mIndex.mRecords[i].mType)
                throw EditError("NIF edit: record " + std::to_string(i) + " changed its type");
        }
        if (saved.mRoots != mIndex.mRoots || saved.mVersion != mIndex.mVersion)
            throw EditError("NIF edit: the edited file has different roots or version");
        for (const FieldPatch& patch : mPatches.patches())
        {
            if (readField(bytes, saved, patch.mRecord, patch.mField) != asStored(patch.mValue))
                throw EditError("NIF edit: record " + std::to_string(patch.mRecord) + ", field '" + patch.mField
                    + "' does not read back as set");
        }
        return bytes;
    }

    SetFieldCommand::SetFieldCommand(
        Document& document, std::uint32_t record, std::string field, PatchValue value, bool mergeable)
        : mDocument(&document)
        , mRecord(record)
        , mField(std::move(field))
        , mValue(std::move(value))
        , mMergeable(mergeable)
    {
    }

    std::string SetFieldCommand::label() const
    {
        return "Set " + mField + " of record " + std::to_string(mRecord);
    }

    void SetFieldCommand::redo()
    {
        // The state to go back to is taken the first time only; a redo after an undo finds it unchanged.
        if (!mHavePrevious)
        {
            mPrevious = mDocument->patchValue(mRecord, mField);
            mHavePrevious = true;
        }
        mDocument->set(mRecord, mField, mValue);
    }

    void SetFieldCommand::undo()
    {
        mDocument->restore(mRecord, mField, mPrevious);
    }

    int SetFieldCommand::mergeId() const
    {
        if (!mMergeable)
            return -1;
        const std::size_t mixed = mRecord * 31u + std::hash<std::string>{}(mField);
        return static_cast<int>(mixed & 0x7FFFFFFF);
    }

    bool SetFieldCommand::mergeWith(const SetFieldCommand& next)
    {
        if (!mMergeable || !next.mMergeable || next.mRecord != mRecord || next.mField != mField)
            return false;
        mValue = next.mValue;
        return true;
    }

    void UndoStack::push(std::unique_ptr<SetFieldCommand> command)
    {
        command->redo();
        // Anything that could be redone is gone; if the clean state was in that branch it is gone too.
        if (mClean > mIndex)
            mCleanValid = false;
        mCommands.resize(mIndex);
        if (!mCommands.empty() && mCommands.back()->mergeWith(*command))
        {
            // The merged step now holds the later value; it is no longer the state that was saved.
            if (mClean == mIndex)
                mCleanValid = false;
            return;
        }
        mCommands.push_back(std::move(command));
        ++mIndex;
    }

    void UndoStack::undo()
    {
        if (!canUndo())
            return;
        mCommands[--mIndex]->undo();
    }

    void UndoStack::redo()
    {
        if (!canRedo())
            return;
        mCommands[mIndex++]->redo();
    }
}
