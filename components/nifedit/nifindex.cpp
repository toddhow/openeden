#include "nifindex.hpp"

#include <memory>
#include <sstream>

#include <components/nif/niffile.hpp>
#include <components/vfs/pathutil.hpp>

namespace NifEdit
{
    FileIndex indexFile(std::string_view bytes)
    {
        constexpr VFS::Path::NormalizedView path("nifedit");

        Nif::NIFFile file(path);
        Nif::Reader reader(file, nullptr);
        Nif::FileLayout layout;
        // The options are explicit: the process-wide default may have been changed by a caller.
        reader.parse(std::make_unique<std::istringstream>(std::string(bytes), std::ios::in | std::ios::binary),
            Nif::ReadOptions{}, &layout);

        FileIndex index;
        index.mVersion = file.mVersion;
        index.mUserVersion = file.mUserVersion;
        index.mBethVersion = file.mBethVersion;
        index.mHeaderEnd = layout.mHeaderEnd;
        index.mFooterBegin = layout.mFooterBegin;
        index.mSize = bytes.size();

        if (layout.mRecords.size() != file.mRecords.size())
            throw EditError("NIF index: the reader reported " + std::to_string(layout.mRecords.size())
                + " record ranges for " + std::to_string(file.mRecords.size()) + " records");

        std::uint64_t expectedBegin = layout.mHeaderEnd;
        for (std::size_t i = 0; i < layout.mRecords.size(); ++i)
        {
            const Nif::RecordSpan& span = layout.mRecords[i];
            if (span.mBegin != expectedBegin || span.mBodyBegin < span.mBegin || span.mEnd < span.mBodyBegin
                || span.mEnd > bytes.size())
                throw EditError("NIF index: record " + std::to_string(i) + " has an impossible range "
                    + std::to_string(span.mBegin) + ".." + std::to_string(span.mEnd));
            IndexedRecord record;
            record.mIndex = span.mIndex;
            record.mType = file.mRecords[i]->mRecordName;
            record.mBegin = span.mBegin;
            record.mBodyBegin = span.mBodyBegin;
            record.mEnd = span.mEnd;
            index.mRecords.push_back(std::move(record));
            expectedBegin = span.mEnd;
        }
        if (expectedBegin != layout.mFooterBegin || layout.mFooterBegin > bytes.size())
            throw EditError("NIF index: the records do not end where the root table starts");

        for (const Nif::Record* root : file.mRoots)
            index.mRoots.push_back(root == nullptr ? -1 : static_cast<std::int32_t>(root->mRecordIndex));
        return index;
    }
}
