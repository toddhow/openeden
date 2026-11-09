#ifndef OPENEDEN_COMPONENTS_NIFEDIT_NIFINDEX_H
#define OPENEDEN_COMPONENTS_NIFEDIT_NIFINDEX_H

// Task 0046 (Eden Studio NIF editing), stage a: where every record lies in a NIF file's bytes. The index comes from
// the engine's own reader (Nif::Reader::parse with a FileLayout), so there is one parser, not two.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace NifEdit
{
    /// Every failure of this library: a file that cannot be indexed, an edit that cannot be located or written. The
    /// message names the record (index and type) and the field when there is one.
    class EditError : public std::runtime_error
    {
    public:
        explicit EditError(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };

    struct IndexedRecord
    {
        std::uint32_t mIndex = 0;
        std::string mType;
        /// The record starts here; before version 5.0.0.1 its type name is the first thing in the range.
        std::uint64_t mBegin = 0;
        /// The record's own data starts here (after the type name).
        std::uint64_t mBodyBegin = 0;
        std::uint64_t mEnd = 0;
    };

    struct FileIndex
    {
        std::uint32_t mVersion = 0;
        std::uint32_t mUserVersion = 0;
        std::uint32_t mBethVersion = 0;
        /// Bytes [0, mHeaderEnd) are the header, copied as they are.
        std::uint64_t mHeaderEnd = 0;
        /// Bytes [mFooterBegin, mSize) are the root table, copied as they are.
        std::uint64_t mFooterBegin = 0;
        std::uint64_t mSize = 0;
        std::vector<IndexedRecord> mRecords;
        /// Record index of each root, -1 for a root that points nowhere.
        std::vector<std::int32_t> mRoots;
    };

    /// Reads the file with the engine's reader (Morrowind versions only) and returns where its records lie. Throws
    /// Nif::Exception when the reader refuses the file, EditError when the ranges do not tile the file.
    FileIndex indexFile(std::string_view bytes);
}

#endif
