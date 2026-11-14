#include <game_skyrimse/survey_nif.hpp>

#include <algorithm>
#include <cstring>

namespace game_skyrimse::survey
{
    namespace
    {
        constexpr std::uint32_t kVersion20207 = 0x14020007u;
        constexpr std::size_t kMaxHeaderLine = 128;

        class Cursor
        {
        public:
            explicit Cursor(std::span<const std::byte> data)
                : mData(data)
            {
            }
            std::size_t remaining() const { return mData.size() - mPos; }
            bool take(void* out, std::size_t n)
            {
                if (remaining() < n)
                    return false;
                std::memcpy(out, mData.data() + mPos, n);
                mPos += n;
                return true;
            }
            bool skip(std::size_t n)
            {
                if (remaining() < n)
                    return false;
                mPos += n;
                return true;
            }
            // u8 length then that many bytes
            bool exportString()
            {
                std::uint8_t n = 0;
                return take(&n, 1) && skip(n);
            }
            std::span<const std::byte> mData;
            std::size_t mPos = 0;
        };

        NifHeader fail(NifHeader h, std::string why)
        {
            h.mOk = false;
            h.mError = std::move(why);
            return h;
        }

        std::string versionText(std::uint32_t v)
        {
            return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 0xFF) + "."
                + std::to_string((v >> 8) & 0xFF) + "." + std::to_string(v & 0xFF);
        }
    }

    NifHeader readNifHeader(std::span<const std::byte> data)
    {
        NifHeader h;
        Cursor c(data);
        // the header line, ended by a line feed
        std::size_t lineEnd = 0;
        const std::size_t scan = std::min(data.size(), kMaxHeaderLine);
        while (lineEnd < scan && data[lineEnd] != std::byte{ '\n' })
            ++lineEnd;
        if (lineEnd == scan)
            return fail(h, "no header line");
        const std::string line(reinterpret_cast<const char*>(data.data()), lineEnd);
        if (line.find("File Format, Version ") == std::string::npos)
            return fail(h, "not a NIF header line");
        c.mPos = lineEnd + 1;

        std::uint8_t endian = 1;
        std::uint32_t blocks = 0;
        if (!c.take(&h.mVersion, 4) || !c.take(&endian, 1) || !c.take(&h.mUserVersion, 4) || !c.take(&blocks, 4))
            return fail(h, "header cut short");
        if (h.mVersion != kVersion20207)
            return fail(h, "version " + versionText(h.mVersion) + " is not 20.2.0.7");
        if (endian == 0)
            return fail(h, "big endian");
        // a block has a type index of two bytes at least
        if (static_cast<std::uint64_t>(blocks) * 2 > c.remaining())
            return fail(h, "block count larger than the file");
        if (h.mUserVersion >= 3)
        {
            if (!c.take(&h.mStreamVersion, 4) || !c.exportString())
                return fail(h, "stream header cut short");
            bool ok = true;
            if (h.mStreamVersion >= 131)
                ok = c.skip(4);
            else
                ok = c.exportString();
            ok = ok && c.exportString();
            if (ok && h.mStreamVersion >= 103)
                ok = c.exportString();
            if (!ok)
                return fail(h, "stream header cut short");
        }
        std::uint16_t types = 0;
        if (!c.take(&types, 2))
            return fail(h, "type table cut short");
        h.mTypeNames.reserve(types);
        for (std::uint16_t i = 0; i < types; ++i)
        {
            std::uint32_t n = 0;
            if (!c.take(&n, 4) || n > c.remaining() || n > 256)
                return fail(h, "block type name cut short or too long");
            h.mTypeNames.emplace_back(reinterpret_cast<const char*>(c.mData.data() + c.mPos), n);
            c.mPos += n;
        }
        h.mBlockTypes.reserve(blocks);
        for (std::uint32_t i = 0; i < blocks; ++i)
        {
            std::uint16_t index = 0;
            if (!c.take(&index, 2))
                return fail(h, "block type indices cut short");
            index &= 0x7FFF;
            if (index >= h.mTypeNames.size())
                return fail(h, "block type index past the table");
            h.mBlockTypes.push_back(index);
        }
        h.mOk = true;
        return h;
    }

    void NifSurvey::add(const NifHeader& header)
    {
        ++mFiles;
        if (!header.mOk)
        {
            ++mHeaderFailures;
            ++mHeaderErrors[header.mError];
            return;
        }
        ++mVersions[versionText(header.mVersion) + " user " + std::to_string(header.mUserVersion) + " stream "
            + std::to_string(header.mStreamVersion)];
        std::set<std::uint16_t> seen;
        for (std::uint16_t index : header.mBlockTypes)
        {
            ++mBlocks;
            ++mBlocksByType[header.mTypeNames[index]];
            if (seen.insert(index).second)
                ++mFilesByType[header.mTypeNames[index]];
        }
    }

    void NifSurvey::noteReaderFailure(const std::string& message)
    {
        ++mReaderFailures;
        std::string kind = message;
        if (const std::size_t paren = kind.find(" ("); paren != std::string::npos)
            kind.resize(paren);
        if (kind.size() > 80)
            kind.resize(80);
        ++mReaderFailureKinds[kind];
        static const std::string prefix = "Unknown record type ";
        if (kind.rfind(prefix, 0) == 0 && kind.size() > prefix.size())
            mUnsupportedTypes.insert(kind.substr(prefix.size()));
    }
}
