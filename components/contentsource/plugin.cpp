#include "plugin.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include <components/files/conversion.hpp>

namespace Content
{
    namespace
    {
        // Sizes of the fixed layouts, from the readers in this repository:
        // TES3 record header: type, size, an always-zero word and the flags (ESM::ESMReader::getRecHeader,
        // components/esm3/esmreader.cpp). HEDR: version, type, author[32], description[256], record count
        // (ESM3::Header::load, components/esm3/loadtes3.cpp).
        constexpr std::uint32_t sTes3RecordHeaderSize = 16;
        constexpr std::uint32_t sTes3HedrSize = 4 + 4 + 32 + 256 + 4;
        // The version of a TES3 header with no HEDR: ESM3::Header::clear sets VER_130 (components/esm3/loadtes3.cpp,
        // components/esm/common.hpp: 0x3fa66666, which is the float 1.3).
        constexpr float sTes3DefaultVersion = 1.3f;
        // TES4 family: type, size, flags, id, revision, then (Skyrim and later only) a form version and an unknown
        // word. ESM4::Reader's constructor tells the two apart by the first subrecord name after the 20 byte header:
        // "HEDR" there means 20 byte headers (components/esm4/reader.cpp, the constructor). Groups have the same size
        // as records (GroupTypeHeader in components/esm4/reader.hpp).
        constexpr std::uint32_t sTes4ShortHeaderSize = 20;
        constexpr std::uint32_t sTes4LongHeaderSize = 24;
        constexpr std::uint32_t sGrup = fourCC("GRUP");
        constexpr std::uint32_t sCompressedPrefixSize = 4;
        constexpr std::uint32_t sRecDeleted = 0x00000020; // ESM::FLAG_Deleted and ESM4::Rec_Deleted

        std::string hex(std::uint64_t value)
        {
            std::ostringstream out;
            out << "0x" << std::hex << value;
            return out.str();
        }

        std::uint32_t readU32(const char* p)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, p, 4);
            return v;
        }

        std::uint16_t readU16(const char* p)
        {
            std::uint16_t v = 0;
            std::memcpy(&v, p, 2);
            return v;
        }

        std::uint64_t readU64(const char* p)
        {
            std::uint64_t v = 0;
            std::memcpy(&v, p, 8);
            return v;
        }

        float readF32(const char* p)
        {
            float v = 0;
            std::memcpy(&v, p, 4);
            return v;
        }

        // The text up to the first NUL (or the whole span).
        std::string textUpToNul(const char* p, std::size_t size)
        {
            const char* end = static_cast<const char*>(std::memchr(p, '\0', size));
            return std::string(p, end != nullptr ? static_cast<std::size_t>(end - p) : size);
        }

        Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        // Reads exactly `size` bytes at `offset`; false if the file ends first.
        bool readAt(std::istream& stream, std::uint64_t offset, void* destination, std::size_t size)
        {
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!stream)
            {
                return false;
            }
            stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
            return static_cast<std::size_t>(stream.gcount()) == size;
        }

        // What was measured (task 0083 stage log, toddhow's installed games, headers only):
        //   Skyrim SE   80 plugins (Skyrim.esm, the DLC masters, Creation Club): 24 byte headers, form version 44, HEDR
        //               version 1.7 or 1.71; and 479 plugins of a mod list, 8 of them form version 43 with HEDR 1.7.
        //   Fallout 4   20 plugins: 24 byte headers, form version 131, HEDR version 0.95 or 1.0.
        //   Fallout NV  11 plugins: 24 byte headers, form version 2 or 15, HEDR version 1.32 to 1.34.
        // Oblivion, Fallout 3 and Skyrim LE are not installed here and are not named: they stay Unknown.
        GameGuess guessGame(const Header& header)
        {
            if (header.mFamily == Family::Tes3)
            {
                return GameGuess::Morrowind;
            }
            if (header.mRecordHeaderSize != sTes4LongHeaderSize)
            {
                return GameGuess::Unknown;
            }
            if ((header.mFormVersion == 43 || header.mFormVersion == 44) && header.mVersion > 1.69f
                && header.mVersion < 1.72f)
            {
                return GameGuess::SkyrimSE;
            }
            if (header.mFormVersion == 131 && header.mVersion > 0.94f && header.mVersion < 1.01f)
            {
                return GameGuess::Fallout4;
            }
            if ((header.mFormVersion == 2 || header.mFormVersion == 15) && header.mVersion > 1.31f
                && header.mVersion < 1.35f)
            {
                return GameGuess::FalloutNV;
            }
            return GameGuess::Unknown;
        }

        // Parses the subrecords of a TES3 header record. Returns an error message or an empty string.
        std::string parseTes3Header(const std::vector<char>& data, Header& header, const Limits& limits)
        {
            header.mVersion = sTes3DefaultVersion;
            std::size_t pos = 0;
            while (pos < data.size())
            {
                if (data.size() - pos < 8)
                {
                    return "a subrecord header is cut short";
                }
                const std::uint32_t name = readU32(&data[pos]);
                const std::uint64_t size = readU32(&data[pos + 4]);
                pos += 8;
                if (size > data.size() - pos)
                {
                    return "a subrecord of " + std::to_string(size) + " bytes runs past the header record";
                }
                const char* body = &data[pos];
                if (name == fourCC("HEDR"))
                {
                    if (size < sTes3HedrSize)
                    {
                        return "HEDR is " + std::to_string(size) + " bytes, expected at least "
                            + std::to_string(sTes3HedrSize);
                    }
                    header.mVersion = readF32(body);
                    header.mFlags = readU32(body + 4);
                    header.mAuthor = textUpToNul(body + 8, 32);
                    header.mDescription = textUpToNul(body + 40, 256);
                    header.mDeclaredRecords = readU32(body + 296);
                }
                else if (name == fourCC("MAST"))
                {
                    if (header.mMasters.size() >= limits.mMaxMasters)
                    {
                        return "too many masters (limit " + std::to_string(limits.mMaxMasters) + ")";
                    }
                    MasterFile master;
                    master.mName = textUpToNul(body, static_cast<std::size_t>(size));
                    header.mMasters.push_back(std::move(master));
                }
                else if (name == fourCC("DATA"))
                {
                    if (!header.mMasters.empty() && size == 8)
                    {
                        header.mMasters.back().mSize = readU64(body);
                    }
                }
                pos += static_cast<std::size_t>(size);
            }
            // HEDR is optional, as ESM3::Header::load and ESM4::Header::load read it: a file without one still loads.
            return std::string();
        }

        // The TES4 header record's subrecords: sizes are 16 bit, and XXXX gives the 32 bit size of the next one.
        std::string parseTes4Header(const std::vector<char>& data, Header& header, const Limits& limits)
        {
            std::size_t pos = 0;
            std::uint64_t extendedSize = 0;
            bool haveExtended = false;
            while (pos < data.size())
            {
                if (data.size() - pos < 6)
                {
                    return "a subrecord header is cut short";
                }
                const std::uint32_t name = readU32(&data[pos]);
                std::uint64_t size = readU16(&data[pos + 4]);
                pos += 6;
                if (haveExtended)
                {
                    size = extendedSize;
                    haveExtended = false;
                }
                if (size > data.size() - pos)
                {
                    return "a subrecord of " + std::to_string(size) + " bytes runs past the header record";
                }
                const char* body = &data[pos];
                if (name == fourCC("XXXX"))
                {
                    if (size != 4)
                    {
                        return "XXXX is not 4 bytes";
                    }
                    extendedSize = readU32(body);
                    haveExtended = true;
                }
                else if (name == fourCC("HEDR"))
                {
                    if (size < 12)
                    {
                        return "HEDR is " + std::to_string(size) + " bytes, expected at least 12";
                    }
                    header.mVersion = readF32(body);
                    header.mDeclaredRecords = readU32(body + 4);
                }
                else if (name == fourCC("CNAM"))
                {
                    header.mAuthor = textUpToNul(body, static_cast<std::size_t>(size));
                }
                else if (name == fourCC("SNAM"))
                {
                    header.mDescription = textUpToNul(body, static_cast<std::size_t>(size));
                }
                else if (name == fourCC("MAST"))
                {
                    if (header.mMasters.size() >= limits.mMaxMasters)
                    {
                        return "too many masters (limit " + std::to_string(limits.mMaxMasters) + ")";
                    }
                    MasterFile master;
                    master.mName = textUpToNul(body, static_cast<std::size_t>(size));
                    header.mMasters.push_back(std::move(master));
                }
                else if (name == fourCC("DATA"))
                {
                    if (!header.mMasters.empty() && size == 8)
                    {
                        header.mMasters.back().mSize = readU64(body);
                    }
                }
                pos += static_cast<std::size_t>(size);
            }
            // HEDR is optional, as ESM3::Header::load and ESM4::Header::load read it: a file without one still loads.
            return std::string();
        }

        class SummaryVisitor : public Visitor
        {
        public:
            explicit SummaryVisitor(Summary& summary)
                : mSummary(summary)
            {
            }

            bool onGroupBegin(const GroupInfo& group) override
            {
                ++mSummary.mGroups;
                ++mSummary.mGroupsByType[group.mType];
                mSummary.mMaxGroupDepth = std::max(mSummary.mMaxGroupDepth, group.mDepth + 1);
                return true;
            }

            bool onRecord(const RawRecord& record) override
            {
                ++mSummary.mRecords;
                ++mSummary.mRecordsByType[record.mType];
                if (record.isCompressed())
                {
                    ++mSummary.mCompressed;
                }
                if ((record.mFlags & sRecDeleted) != 0)
                {
                    ++mSummary.mDeleted;
                }
                return true;
            }

        private:
            Summary& mSummary;
        };
    }

    bool Header::isMaster() const
    {
        if (mFamily == Family::Tes3)
        {
            return (mFlags & 1) != 0;
        }
        return (mFlags & sRecMaster) != 0;
    }

    bool Header::isLight() const
    {
        return mFamily == Family::Tes4 && (mFlags & sRecLight) != 0;
    }

    bool Header::isLocalized() const
    {
        return mFamily == Family::Tes4 && (mFlags & sRecLocalized) != 0;
    }

    std::string Error::describe() const
    {
        std::string text = mFile + ": offset " + hex(mOffset);
        if (mRecordType != 0)
        {
            text += ": record " + fourCCToString(mRecordType);
        }
        return text + ": " + mMessage;
    }

    std::string fourCCToString(std::uint32_t type)
    {
        std::string text(4, '?');
        for (int i = 0; i < 4; ++i)
        {
            const char c = static_cast<char>((type >> (8 * i)) & 0xFF);
            text[static_cast<std::size_t>(i)] = (c >= 0x20 && c < 0x7F) ? c : '?';
        }
        return text;
    }

    std::optional<Family> probeFamily(std::istream& stream)
    {
        char first[4] = {};
        stream.clear();
        stream.seekg(0);
        stream.read(first, sizeof(first));
        if (stream.gcount() != static_cast<std::streamsize>(sizeof(first)))
        {
            stream.clear();
            return std::nullopt;
        }
        stream.clear();
        stream.seekg(0);
        const std::uint32_t magic = readU32(first);
        if (magic == fourCC("TES3"))
            return Family::Tes3;
        if (magic == fourCC("TES4"))
            return Family::Tes4;
        return std::nullopt;
    }

    std::unique_ptr<Plugin> Plugin::open(
        std::unique_ptr<std::istream> stream, std::string name, Error& error, const Limits& limits)
    {
        if (stream == nullptr)
        {
            error = makeError(name, 0, 0, "no stream");
            return nullptr;
        }
        stream->clear();
        stream->seekg(0, std::ios::end);
        const std::streamoff end = stream->tellg();
        if (!*stream || end < 0)
        {
            error = makeError(name, 0, 0, "cannot determine the file size");
            return nullptr;
        }
        const std::uint64_t fileSize = static_cast<std::uint64_t>(end);

        char first[24] = {};
        if (fileSize < 16 || !readAt(*stream, 0, first, 16))
        {
            error = makeError(name, 0, 0, "the file is too short to be a plugin");
            return nullptr;
        }

        std::unique_ptr<Plugin> plugin(new Plugin());
        plugin->mName = name;
        plugin->mLimits = limits;
        plugin->mFileSize = fileSize;
        Header& header = plugin->mHeader;
        const std::uint32_t magic = readU32(first);

        std::uint32_t recordHeaderSize = 0;
        if (magic == fourCC("TES3"))
        {
            header.mFamily = Family::Tes3;
            recordHeaderSize = sTes3RecordHeaderSize;
        }
        else if (magic == fourCC("TES4"))
        {
            header.mFamily = Family::Tes4;
            if (fileSize < sTes4LongHeaderSize || !readAt(*stream, 0, first, sTes4LongHeaderSize))
            {
                error = makeError(name, 0, magic, "the TES4 record header is cut short");
                return nullptr;
            }
            recordHeaderSize = readU32(first + 20) == fourCC("HEDR") ? sTes4ShortHeaderSize : sTes4LongHeaderSize;
            if (recordHeaderSize == sTes4LongHeaderSize)
            {
                header.mFormVersion = readU16(first + 20);
            }
            header.mFlags = readU32(first + 8);
        }
        else
        {
            error = makeError(name, 0, 0, "not a TES3 or TES4 plugin (first four bytes " + hex(magic) + ")");
            return nullptr;
        }
        header.mRecordHeaderSize = recordHeaderSize;

        const std::uint64_t dataSize = readU32(first + 4);
        if (dataSize > fileSize - recordHeaderSize)
        {
            error = makeError(name, 0, magic,
                "the header record of " + std::to_string(dataSize) + " bytes runs past the end of the file");
            return nullptr;
        }
        if (dataSize > limits.mMaxHeaderRecord)
        {
            error = makeError(name, 0, magic,
                "the header record of " + std::to_string(dataSize) + " bytes is over the limit of "
                    + std::to_string(limits.mMaxHeaderRecord));
            return nullptr;
        }
        std::vector<char> data(static_cast<std::size_t>(dataSize));
        if (!data.empty() && !readAt(*stream, recordHeaderSize, data.data(), data.size()))
        {
            error = makeError(name, recordHeaderSize, magic, "the header record could not be read");
            return nullptr;
        }
        header.mFirstRecordOffset = recordHeaderSize + dataSize;

        const std::string problem = header.mFamily == Family::Tes3 ? parseTes3Header(data, header, limits)
                                                                   : parseTes4Header(data, header, limits);
        if (!problem.empty())
        {
            error = makeError(name, recordHeaderSize, magic, problem);
            return nullptr;
        }
        header.mGame = guessGame(header);
        plugin->mStream = std::move(stream);
        return plugin;
    }

    std::unique_ptr<Plugin> Plugin::openFile(const std::filesystem::path& path, Error& error, const Limits& limits)
    {
        const std::string name = Files::pathToUnicodeString(path.filename());
        std::unique_ptr<std::istream> stream;
        try
        {
            auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
            if (!file->is_open())
            {
                error = makeError(name, 0, 0, "cannot open the file");
                return nullptr;
            }
            stream = std::move(file);
        }
        catch (const std::exception& e)
        {
            error = makeError(name, 0, 0, std::string("cannot open the file: ") + e.what());
            return nullptr;
        }
        return open(std::move(stream), name, error, limits);
    }

    std::optional<Error> Plugin::walk(Visitor& visitor, const WalkOptions& options) const
    {
        try
        {
            return mHeader.mFamily == Family::Tes3 ? walkTes3(visitor, options) : walkTes4(visitor, options);
        }
        catch (const std::exception& e)
        {
            // Nothing should throw here (the visitor is the only foreign code), but an exception must not cross the
            // boundary.
            return makeError(mName, 0, 0, std::string("exception while reading: ") + e.what());
        }
    }

    std::optional<Error> Plugin::walkTes3(Visitor& visitor, const WalkOptions& options) const
    {
        std::vector<std::byte> buffer;
        std::uint64_t pos = mHeader.mFirstRecordOffset;
        while (pos < mFileSize)
        {
            char header[sTes3RecordHeaderSize] = {};
            if (mFileSize - pos < sTes3RecordHeaderSize)
            {
                std::uint32_t type = 0;
                if (mFileSize - pos >= 4 && !readAt(*mStream, pos, &type, 4))
                {
                    type = 0;
                }
                return makeError(mName, pos, type, "the record header is cut short");
            }
            if (!readAt(*mStream, pos, header, sTes3RecordHeaderSize))
            {
                return makeError(mName, pos, 0, "the record header could not be read");
            }
            const std::uint32_t type = readU32(header);
            const std::uint64_t dataSize = readU32(header + 4);
            const std::uint32_t flags = readU32(header + 12);
            const std::uint64_t dataStart = pos + sTes3RecordHeaderSize;
            if (dataSize > mFileSize - dataStart)
            {
                return makeError(mName, pos, type,
                    "the record data of " + std::to_string(dataSize) + " bytes runs past the end of the file");
            }
            if (dataSize > mLimits.mMaxRecordData)
            {
                return makeError(mName, pos, type,
                    "the record data of " + std::to_string(dataSize) + " bytes is over the limit of "
                        + std::to_string(mLimits.mMaxRecordData));
            }

            RawRecord record;
            record.mType = type;
            record.mFlags = flags;
            record.mOffset = pos;
            record.mDataSize = dataSize;
            if (options.mReadData)
            {
                buffer.resize(static_cast<std::size_t>(dataSize));
                if (dataSize != 0 && !readAt(*mStream, dataStart, buffer.data(), buffer.size()))
                {
                    return makeError(mName, pos, type, "the record data could not be read");
                }
                record.mData = std::span<const std::byte>(buffer.data(), buffer.size());
                if (buffer.size() >= 8 && readU32(reinterpret_cast<const char*>(buffer.data())) == fourCC("NAME"))
                {
                    const std::uint64_t nameSize = readU32(reinterpret_cast<const char*>(buffer.data()) + 4);
                    if (nameSize <= buffer.size() - 8)
                    {
                        const char* text = reinterpret_cast<const char*>(buffer.data()) + 8;
                        const char* nul
                            = static_cast<const char*>(std::memchr(text, '\0', static_cast<std::size_t>(nameSize)));
                        record.mId = std::string_view(text,
                            nul != nullptr ? static_cast<std::size_t>(nul - text) : static_cast<std::size_t>(nameSize));
                    }
                }
            }
            if (!visitor.onRecord(record))
            {
                return std::nullopt;
            }
            pos = dataStart + dataSize;
        }
        return std::nullopt;
    }

    std::optional<Error> Plugin::walkTes4(Visitor& visitor, const WalkOptions& options) const
    {
        struct Frame
        {
            GroupInfo mInfo;
            std::uint64_t mEnd = 0;
        };
        const std::uint32_t headerSize = mHeader.mRecordHeaderSize;
        std::vector<Frame> stack;
        std::vector<std::byte> buffer;
        std::uint64_t pos = mHeader.mFirstRecordOffset;
        for (;;)
        {
            while (!stack.empty() && pos == stack.back().mEnd)
            {
                const GroupInfo finished = stack.back().mInfo;
                stack.pop_back();
                if (!visitor.onGroupEnd(finished))
                {
                    return std::nullopt;
                }
            }
            const std::uint64_t parentEnd = stack.empty() ? mFileSize : stack.back().mEnd;
            if (pos >= parentEnd)
            {
                break;
            }

            char header[sTes4LongHeaderSize] = {};
            if (parentEnd - pos < headerSize)
            {
                std::uint32_t type = 0;
                if (parentEnd - pos >= 4 && !readAt(*mStream, pos, &type, 4))
                {
                    type = 0;
                }
                return makeError(mName, pos, type, "the record header runs past its group or the end of the file");
            }
            if (!readAt(*mStream, pos, header, headerSize))
            {
                return makeError(mName, pos, 0, "the record header could not be read");
            }
            const std::uint32_t type = readU32(header);

            if (type == sGrup)
            {
                const std::uint64_t groupSize = readU32(header + 4);
                if (groupSize < headerSize)
                {
                    return makeError(mName, pos, type,
                        "the group size " + std::to_string(groupSize) + " is smaller than its own header");
                }
                if (groupSize > parentEnd - pos)
                {
                    return makeError(mName, pos, type,
                        "the group of " + std::to_string(groupSize)
                            + " bytes runs past its parent group or the end of the file");
                }
                if (stack.size() >= mLimits.mMaxGroupDepth)
                {
                    return makeError(mName, pos, type,
                        "groups nested too deep (limit " + std::to_string(mLimits.mMaxGroupDepth) + ")");
                }
                Frame frame;
                frame.mInfo.mLabel = readU32(header + 8);
                frame.mInfo.mType = static_cast<std::int32_t>(readU32(header + 12));
                frame.mInfo.mOffset = pos;
                frame.mInfo.mSize = groupSize;
                frame.mInfo.mDepth = static_cast<std::uint32_t>(stack.size());
                frame.mEnd = pos + groupSize;
                stack.push_back(frame);
                if (!visitor.onGroupBegin(frame.mInfo))
                {
                    return std::nullopt;
                }
                pos += headerSize;
                continue;
            }

            const std::uint64_t dataSize = readU32(header + 4);
            const std::uint32_t flags = readU32(header + 8);
            const std::uint64_t dataStart = pos + headerSize;
            if (dataSize > parentEnd - dataStart)
            {
                return makeError(mName, pos, type,
                    "the record data of " + std::to_string(dataSize)
                        + " bytes runs past its group or the end of the file");
            }
            if (dataSize > mLimits.mMaxRecordData)
            {
                return makeError(mName, pos, type,
                    "the record data of " + std::to_string(dataSize) + " bytes is over the limit of "
                        + std::to_string(mLimits.mMaxRecordData));
            }

            RawRecord record;
            record.mType = type;
            record.mFormId = readU32(header + 12);
            record.mFlags = flags;
            record.mOffset = pos;
            record.mDataSize = dataSize;
            if ((flags & sRecCompressed) != 0)
            {
                if (dataSize < sCompressedPrefixSize)
                {
                    return makeError(mName, pos, type, "a compressed record is too short for its size prefix");
                }
                char prefix[4] = {};
                if (!readAt(*mStream, dataStart, prefix, 4))
                {
                    return makeError(mName, pos, type, "the compressed size prefix could not be read");
                }
                const std::uint32_t inflated = readU32(prefix);
                if (inflated > mLimits.mMaxUncompressed)
                {
                    return makeError(mName, pos, type,
                        "the record claims " + std::to_string(inflated) + " bytes when inflated, over the limit of "
                            + std::to_string(mLimits.mMaxUncompressed));
                }
                record.mUncompressedSize = inflated;
            }
            if (options.mReadData)
            {
                buffer.resize(static_cast<std::size_t>(dataSize));
                if (dataSize != 0 && !readAt(*mStream, dataStart, buffer.data(), buffer.size()))
                {
                    return makeError(mName, pos, type, "the record data could not be read");
                }
                record.mData = std::span<const std::byte>(buffer.data(), buffer.size());
            }
            if (!visitor.onRecord(record))
            {
                return std::nullopt;
            }
            pos = dataStart + dataSize;
        }
        return std::nullopt;
    }

    std::optional<Error> summarise(const Plugin& plugin, Summary& summary)
    {
        SummaryVisitor visitor(summary);
        WalkOptions options;
        options.mReadData = false;
        return plugin.walk(visitor, options);
    }
}
