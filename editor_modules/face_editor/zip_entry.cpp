#include "zip_entry.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::uint32_t kEndSignature = 0x06054b50;
        constexpr std::uint32_t kCentralSignature = 0x02014b50;
        constexpr std::uint32_t kLocalSignature = 0x04034b50;
        constexpr std::size_t kEndSize = 22;
        constexpr std::size_t kCentralHeaderSize = 46;
        constexpr std::size_t kLocalHeaderSize = 30;
        constexpr std::uint64_t kMaxComment = 65535;
        constexpr std::uint64_t kMaxDirectoryBytes = 64u << 20;

        std::uint32_t u16(const unsigned char* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
        }
        std::uint32_t u32(const unsigned char* p)
        {
            return u16(p) | (u16(p + 2) << 16);
        }

        bool readAt(std::ifstream& in, std::uint64_t offset, void* dst, std::uint64_t count)
        {
            in.clear();
            in.seekg(static_cast<std::streamoff>(offset));
            in.read(static_cast<char*>(dst), static_cast<std::streamsize>(count));
            return static_cast<bool>(in) && static_cast<std::uint64_t>(in.gcount()) == count;
        }
    }

    bool readZipEntry(const fs::path& zipPath, std::string_view entryName, std::uint64_t maxBytes,
        std::vector<char>& out, std::string& error)
    {
        out.clear();
        const std::string where = zipPath.generic_string();
        std::error_code ec;
        const std::uint64_t fileSize = fs::file_size(zipPath, ec);
        if (ec)
        {
            error = where + ": cannot read the file (" + ec.message() + ")";
            return false;
        }
        if (fileSize < kEndSize)
        {
            error = where + ": not a zip file (too short)";
            return false;
        }
        std::ifstream in(zipPath, std::ios::binary);
        if (!in)
        {
            error = where + ": cannot open the file";
            return false;
        }

        // The end-of-central-directory record: the last 22 bytes, or earlier when the archive has a comment.
        const std::uint64_t tailSize = std::min<std::uint64_t>(fileSize, kEndSize + kMaxComment);
        std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
        if (!readAt(in, fileSize - tailSize, tail.data(), tailSize))
        {
            error = where + ": cannot read the file";
            return false;
        }
        std::size_t endAt = tail.size();
        for (std::size_t i = tail.size() - kEndSize + 1; i-- > 0;)
        {
            if (u32(&tail[i]) == kEndSignature && i + kEndSize + u16(&tail[i + 20]) == tail.size())
            {
                endAt = i;
                break;
            }
        }
        if (endAt == tail.size())
        {
            error = where + ": not a zip file (no end record)";
            return false;
        }
        const unsigned char* end = &tail[endAt];
        const std::uint64_t endOffset = fileSize - tailSize + endAt;
        const std::uint32_t diskNumber = u16(end + 4), directoryDisk = u16(end + 6);
        const std::uint32_t entriesHere = u16(end + 8), entriesTotal = u16(end + 10);
        const std::uint64_t directorySize = u32(end + 12), directoryOffset = u32(end + 16);
        if (entriesTotal == 0xFFFF || directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu)
        {
            error = where + ": zip64 archives are not supported";
            return false;
        }
        if (diskNumber != 0 || directoryDisk != 0 || entriesHere != entriesTotal)
        {
            error = where + ": multi-disk archives are not supported";
            return false;
        }
        if (directorySize > kMaxDirectoryBytes || directoryOffset + directorySize > endOffset)
        {
            error = where + ": the central directory is outside the file";
            return false;
        }
        std::vector<unsigned char> directory(static_cast<std::size_t>(directorySize));
        if (!readAt(in, directoryOffset, directory.data(), directorySize))
        {
            error = where + ": cannot read the central directory";
            return false;
        }

        struct Found
        {
            std::uint32_t mFlags = 0, mMethod = 0, mCrc = 0;
            std::uint64_t mCompressed = 0, mUncompressed = 0, mLocalOffset = 0;
        } found;
        bool haveEntry = false;
        std::size_t at = 0;
        for (std::uint32_t index = 0; index < entriesTotal; ++index)
        {
            if (at + kCentralHeaderSize > directory.size() || u32(&directory[at]) != kCentralSignature)
            {
                error = where + ": the central directory is damaged";
                return false;
            }
            const unsigned char* h = &directory[at];
            const std::size_t nameLength = u16(h + 28), extraLength = u16(h + 30), commentLength = u16(h + 32);
            const std::size_t total = kCentralHeaderSize + nameLength + extraLength + commentLength;
            if (at + total > directory.size())
            {
                error = where + ": the central directory is damaged";
                return false;
            }
            const std::string_view name(reinterpret_cast<const char*>(h + kCentralHeaderSize), nameLength);
            if (name == entryName)
            {
                found.mFlags = u16(h + 8);
                found.mMethod = u16(h + 10);
                found.mCrc = u32(h + 16);
                found.mCompressed = u32(h + 20);
                found.mUncompressed = u32(h + 24);
                found.mLocalOffset = u32(h + 42);
                haveEntry = true;
                if (found.mCompressed == 0xFFFFFFFFu || found.mUncompressed == 0xFFFFFFFFu
                    || found.mLocalOffset == 0xFFFFFFFFu)
                {
                    error = where + ": zip64 entries are not supported";
                    return false;
                }
                break;
            }
            at += total;
        }
        if (!haveEntry)
        {
            error = where + ": the archive has no entry \"" + std::string(entryName) + "\"";
            return false;
        }
        if ((found.mFlags & 1u) != 0)
        {
            error = where + ": the entry is encrypted";
            return false;
        }
        if (found.mMethod != 0 && found.mMethod != 8)
        {
            error = where + ": the entry uses compression method " + std::to_string(found.mMethod)
                + ", which is not supported (only stored and deflated)";
            return false;
        }
        if (found.mUncompressed > maxBytes)
        {
            error = where + ": the entry is larger than allowed (" + std::to_string(found.mUncompressed) + " bytes)";
            return false;
        }
        if (found.mMethod == 0 && found.mCompressed != found.mUncompressed)
        {
            error = where + ": a stored entry has two different sizes";
            return false;
        }
        if (found.mCompressed > maxBytes * 2 + 1024)
        {
            error = where + ": the entry is larger than allowed";
            return false;
        }

        unsigned char local[kLocalHeaderSize];
        if (found.mLocalOffset + kLocalHeaderSize > directoryOffset
            || !readAt(in, found.mLocalOffset, local, kLocalHeaderSize) || u32(local) != kLocalSignature)
        {
            error = where + ": the entry's header is damaged";
            return false;
        }
        const std::uint64_t dataStart = found.mLocalOffset + kLocalHeaderSize + u16(local + 26) + u16(local + 28);
        if (dataStart + found.mCompressed > directoryOffset)
        {
            error = where + ": the entry's data is outside the file";
            return false;
        }
        std::vector<char> packed(static_cast<std::size_t>(found.mCompressed));
        if (!packed.empty() && !readAt(in, dataStart, packed.data(), packed.size()))
        {
            error = where + ": the entry's data ends short";
            return false;
        }

        std::vector<char> result(static_cast<std::size_t>(found.mUncompressed));
        if (found.mMethod == 0)
        {
            result = std::move(packed);
        }
        else
        {
            z_stream z{};
            if (inflateInit2(&z, -15) != Z_OK)
            {
                error = "zlib could not start";
                return false;
            }
            z.next_in = reinterpret_cast<Bytef*>(packed.data());
            z.avail_in = static_cast<uInt>(packed.size());
            z.next_out = reinterpret_cast<Bytef*>(result.data());
            z.avail_out = static_cast<uInt>(result.size());
            const int status = inflate(&z, Z_FINISH);
            const auto produced = static_cast<std::uint64_t>(z.total_out);
            inflateEnd(&z);
            if (status != Z_STREAM_END || produced != found.mUncompressed)
            {
                error = where + ": the entry does not inflate to its recorded size";
                return false;
            }
        }
        const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(result.data()), static_cast<uInt>(result.size()));
        if (static_cast<std::uint32_t>(crc) != found.mCrc)
        {
            error = where + ": the entry fails its CRC-32 check";
            return false;
        }
        out = std::move(result);
        error.clear();
        return true;
    }
}
