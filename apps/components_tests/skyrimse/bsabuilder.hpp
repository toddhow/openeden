#ifndef OPENEDEN_TEST_SUITE_SKYRIMSE_BSABUILDER_H
#define OPENEDEN_TEST_SUITE_SKYRIMSE_BSABUILDER_H

// A generated Skyrim SE archive (BSA version 105, task 0086): folders, files, LZ4 frames behind a 4 byte size. The
// hash is the one components/bsa/compressedbsafile.cpp computes; the layout follows its readHeader.

#include <lz4frame.h>

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace SkyrimSeTests
{
    struct ArchiveFile
    {
        std::string mFolder; // lower case, with '\', no trailing separator: "meshes\actors"
        std::string mName; // lower case with extension: "skeleton.nif"
        std::string mContent;
        bool mCompress = true;
    };

    inline std::uint64_t tes4Hash(const std::string& stem, const std::string& extension)
    {
        if (stem.empty())
            return 0;
        const auto at = [&](std::size_t i) -> unsigned char { return static_cast<unsigned char>(stem[i]); };
        const std::size_t len = stem.size();
        std::uint64_t result = at(len - 1);
        if (len >= 3)
            result |= static_cast<std::uint64_t>(at(len - 2)) << 8;
        result |= static_cast<std::uint64_t>(len) << 16;
        result |= static_cast<std::uint32_t>(at(0) << 24);
        if (len >= 4)
        {
            std::uint32_t hash = 0;
            for (std::size_t i = 1; i <= len - 3; ++i)
                hash = hash * 0x1003f + at(i);
            result += static_cast<std::uint64_t>(hash) << 32;
        }
        if (extension.empty())
            return result;
        if (extension == ".kf")
            result |= 0x80;
        else if (extension == ".nif")
            result |= 0x8000;
        else if (extension == ".dds")
            result |= 0x8080;
        else if (extension == ".wav")
            result |= 0x80000000;
        std::uint32_t hash = 0;
        for (unsigned char c : extension)
            hash = hash * 0x1003f + c;
        result += static_cast<std::uint64_t>(hash) << 32;
        return result;
    }

    inline std::string lz4Frame(const std::string& content)
    {
        std::string out(LZ4F_compressFrameBound(content.size(), nullptr), '\0');
        const std::size_t size = LZ4F_compressFrame(out.data(), out.size(), content.data(), content.size(), nullptr);
        if (LZ4F_isError(size))
            throw std::runtime_error("LZ4F_compressFrame failed");
        out.resize(size);
        return out;
    }

    class Out
    {
    public:
        Out& u8(std::uint8_t v) { return raw(&v, 1); }
        Out& u32(std::uint32_t v) { return raw(&v, 4); }
        Out& u64(std::uint64_t v) { return raw(&v, 8); }
        Out& raw(const void* p, std::size_t n)
        {
            mData.append(static_cast<const char*>(p), n);
            return *this;
        }
        Out& text(const std::string& s)
        {
            mData += s;
            return *this;
        }
        std::string mData;
    };

    /// version: 105 for Skyrim SE. Archive flags: folder names, file names and (when `compressByDefault`) compress.
    inline std::string buildArchive(
        const std::vector<ArchiveFile>& files, std::uint32_t version = 105, bool compressByDefault = true)
    {
        std::map<std::string, std::vector<const ArchiveFile*>> folders;
        for (const ArchiveFile& f : files)
            folders[f.mFolder].push_back(&f);

        std::uint32_t folderNamesLength = 0;
        std::uint32_t fileNamesLength = 0;
        for (const auto& [name, list] : folders)
        {
            folderNamesLength += static_cast<std::uint32_t>(name.size() + 1);
            for (const ArchiveFile* f : list)
                fileNamesLength += static_cast<std::uint32_t>(f->mName.size() + 1);
        }
        const std::uint32_t folderRecordSize = version == 105 ? 24 : 16;
        std::uint64_t pos = 36 + folders.size() * folderRecordSize;
        // after the folder records: per folder one name (length byte + name + NUL) and its 16 byte file records
        std::uint64_t blockEnd = pos;
        for (const auto& [name, list] : folders)
            blockEnd += 1 + name.size() + 1 + list.size() * 16;
        std::uint64_t dataPos = blockEnd + fileNamesLength;

        // data first, so that offsets are known
        struct Placed
        {
            std::uint32_t mSize;
            std::uint32_t mOffset;
        };
        std::map<const ArchiveFile*, Placed> placed;
        std::string data;
        for (const auto& [name, list] : folders)
        {
            for (const ArchiveFile* f : list)
            {
                std::string stored;
                if (compressByDefault && f->mCompress)
                {
                    Out o;
                    o.u32(static_cast<std::uint32_t>(f->mContent.size())).text(lz4Frame(f->mContent));
                    stored = o.mData;
                }
                else
                {
                    stored = f->mContent;
                }
                std::uint32_t size = static_cast<std::uint32_t>(stored.size());
                // With the archive's Compress flag set a file is compressed unless its size carries bit 30
                if (compressByDefault && !f->mCompress)
                    size |= 0x40000000;
                placed[f] = { size, static_cast<std::uint32_t>(dataPos + data.size()) };
                data += stored;
            }
        }

        Out out;
        out.text("BSA").u8(0).u32(version).u32(36);
        out.u32(0x3 | (compressByDefault ? 0x4 : 0));
        out.u32(static_cast<std::uint32_t>(folders.size()));
        out.u32(static_cast<std::uint32_t>(files.size()));
        out.u32(folderNamesLength).u32(fileNamesLength).u32(0);

        std::uint64_t cursor = 36 + folders.size() * folderRecordSize;
        for (const auto& [name, list] : folders)
        {
            out.u64(tes4Hash(name, ""));
            out.u32(static_cast<std::uint32_t>(list.size()));
            if (version == 105)
                out.u32(0).u64(cursor + fileNamesLength);
            else
                out.u32(static_cast<std::uint32_t>(cursor + fileNamesLength));
            cursor += 1 + name.size() + 1 + list.size() * 16;
        }
        for (const auto& [name, list] : folders)
        {
            out.u8(static_cast<std::uint8_t>(name.size() + 1)).text(name).u8(0);
            for (const ArchiveFile* f : list)
            {
                const std::size_t dot = f->mName.rfind('.');
                const std::string stem = dot == std::string::npos ? f->mName : f->mName.substr(0, dot);
                const std::string ext = dot == std::string::npos ? "" : f->mName.substr(dot);
                out.u64(tes4Hash(stem, ext)).u32(placed[f].mSize).u32(placed[f].mOffset);
            }
        }
        for (const auto& [name, list] : folders)
        {
            for (const ArchiveFile* f : list)
                out.text(f->mName).u8(0);
        }
        out.text(data);
        return out.mData;
    }
}

#endif
