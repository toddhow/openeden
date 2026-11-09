#ifndef OPENEDEN_COMPONENTS_NIFEDIT_NIFPATCH_H
#define OPENEDEN_COMPONENTS_NIFEDIT_NIFPATCH_H

// Task 0046, stages a and b: edits as patches to named fields of records. A patch names a record by index, a field by
// name and gives the new value; applyPatches copies the original bytes and writes again only the fields patched. A
// record type or field that is not on the supported list is refused with an EditError, never written approximately.
//
// Versions: Morrowind's 4.0.0.0 and 4.0.0.2 only (decision D7 of the task). Strings are the file's own bytes (the
// legacy code page); converting to and from UTF-8 belongs to the caller.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "nifindex.hpp"

namespace NifEdit
{
    struct TextKey
    {
        float mTime = 0.f;
        std::string mText;
        friend bool operator==(const TextKey&, const TextKey&) = default;
    };

    /// string: a name, a texture file, an extra data string (raw file bytes, no NUL).
    /// double: a float field (scale, glossiness, alpha).
    /// int64: an integer field (flags, alpha threshold), range-checked against the field's width.
    /// array of 3: translation and colours. array of 9: a rotation matrix in the file's order (row by row).
    /// vector of TextKey: all the keys of a NiTextKeyExtraData.
    using PatchValue = std::variant<std::string, double, std::int64_t, std::array<float, 3>, std::array<float, 9>,
        std::vector<TextKey>>;

    struct FieldPatch
    {
        std::uint32_t mRecord = 0;
        std::string mField;
        PatchValue mValue;
    };

    /// The patches of a document: at most one per record and field, the last set wins, kept in the order they were
    /// first set.
    class PatchSet
    {
    public:
        void set(FieldPatch patch);
        /// True if there was a patch to remove.
        bool erase(std::uint32_t record, std::string_view field);
        const FieldPatch* find(std::uint32_t record, std::string_view field) const;
        bool empty() const { return mPatches.empty(); }
        const std::vector<FieldPatch>& patches() const { return mPatches; }

    private:
        std::vector<FieldPatch> mPatches;
    };

    /// The fields a record of this type can have patched in a file of this version; empty if the type or version is
    /// not supported.
    std::vector<std::string> editableFields(std::string_view type, std::uint32_t version);

    /// The value a field has in the file's own bytes. Throws EditError (unsupported type, field or version, or bytes
    /// that do not have the layout the type promises).
    PatchValue readField(std::string_view bytes, const FileIndex& index, std::uint32_t record, std::string_view field);

    /// Checks that the patch can be written: record and field supported, value of the right kind and in range. Throws
    /// EditError with the record, type and field named.
    void checkPatch(std::string_view bytes, const FileIndex& index, const FieldPatch& patch);

    /// The original bytes with every patched field written again and everything else copied. With no patches (or
    /// patches that set a field to the value it has) the result equals the input. Throws EditError.
    std::string applyPatches(std::string_view bytes, const FileIndex& index, const PatchSet& patches);
}

#endif
