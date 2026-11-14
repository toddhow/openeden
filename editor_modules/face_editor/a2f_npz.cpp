#include "a2f_npz.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::uint32_t kLocalSig = 0x04034b50;
        constexpr std::uint32_t kCentralSig = 0x02014b50;
        constexpr std::uint32_t kEndSig = 0x06054b50;
        constexpr std::size_t kLocalFixed = 30;
        constexpr std::size_t kCentralFixed = 46;
        constexpr std::size_t kEndFixed = 22;
        constexpr std::size_t kMaxComment = 65535;

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
            if (count == 0)
                return true;
            in.clear();
            in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            return static_cast<bool>(in.read(static_cast<char*>(dst), static_cast<std::streamsize>(count)));
        }

        bool endsWith(std::string_view s, std::string_view suffix)
        {
            return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
        }

        std::string at(const char* what, std::uint64_t offset)
        {
            return std::string(what) + " (offset " + std::to_string(offset) + ")";
        }

        /// The text after "'key':" in a numpy header dictionary, up to the next ',' or '}' outside parentheses/quotes.
        std::string_view headerValue(std::string_view header, std::string_view key)
        {
            const std::string quoted = "'" + std::string(key) + "'";
            std::size_t at = header.find(quoted);
            if (at == std::string_view::npos)
                return {};
            at += quoted.size();
            while (at < header.size() && (header[at] == ' ' || header[at] == ':'))
                ++at;
            std::size_t end = at;
            int depth = 0;
            bool quote = false;
            for (; end < header.size(); ++end)
            {
                const char c = header[end];
                if (c == '\'')
                    quote = !quote;
                else if (!quote && c == '(')
                    ++depth;
                else if (!quote && c == ')')
                    --depth;
                else if (!quote && depth == 0 && (c == ',' || c == '}'))
                    break;
            }
            std::string_view v = header.substr(at, end - at);
            while (!v.empty() && v.back() == ' ')
                v.remove_suffix(1);
            return v;
        }

        std::uint32_t crc32(std::string_view data)
        {
            static const auto table = [] {
                std::array<std::uint32_t, 256> t{};
                for (std::uint32_t i = 0; i < 256; ++i)
                {
                    std::uint32_t c = i;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[i] = c;
                }
                return t;
            }();
            std::uint32_t c = 0xFFFFFFFFu;
            for (const char ch : data)
                c = table[(c ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        void put16(std::string& s, std::uint32_t v)
        {
            s.push_back(static_cast<char>(v & 0xFF));
            s.push_back(static_cast<char>((v >> 8) & 0xFF));
        }

        void put32(std::string& s, std::uint32_t v)
        {
            put16(s, v & 0xFFFF);
            put16(s, v >> 16);
        }

        /// A .npy file: version 1.0 header padded with spaces to a multiple of 64 bytes and ended by a newline.
        std::string npyBytes(const std::string& descr, const std::vector<std::size_t>& shape, const std::string& data)
        {
            std::string shapeText = "(";
            for (std::size_t i = 0; i < shape.size(); ++i)
                shapeText += std::to_string(shape[i]) + (shape.size() == 1 ? "," : (i + 1 < shape.size() ? ", " : ""));
            shapeText += ")";
            std::string header = "{'descr': '" + descr + "', 'fortran_order': False, 'shape': " + shapeText + ", }";
            while ((10 + header.size() + 1) % 64 != 0)
                header.push_back(' ');
            header.push_back('\n');
            std::string out("\x93NUMPY\x01\x00", 8);
            put16(out, static_cast<std::uint32_t>(header.size()));
            out += header;
            out += data;
            return out;
        }
    }

    std::uint64_t NpyInfo::elementCount() const
    {
        std::uint64_t n = 1;
        for (const std::size_t d : mShape)
            n *= d;
        return n;
    }

    std::size_t NpyInfo::itemSize() const
    {
        if (mDescr == "<f4" || mDescr == "<i4")
            return 4;
        if (mDescr.size() >= 3 && mDescr[0] == '|' && mDescr[1] == 'S')
        {
            std::size_t n = 0;
            for (std::size_t i = 2; i < mDescr.size(); ++i)
            {
                if (mDescr[i] < '0' || mDescr[i] > '9' || n > 4096)
                    return 0;
                n = n * 10 + static_cast<std::size_t>(mDescr[i] - '0');
            }
            return n >= 1 && n <= 4096 ? n : 0;
        }
        return 0;
    }

    bool NpzFile::open(const fs::path& path, std::string& error)
    {
        mNames.clear();
        mDataStart.clear();
        mSize.clear();
        mPath = path;
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = path.generic_string() + ": cannot open the file";
            return false;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff end = in.tellg();
        if (end < 0)
        {
            error = path.generic_string() + ": cannot read the file";
            return false;
        }
        mFileSize = static_cast<std::uint64_t>(end);
        const std::string label = path.generic_string() + ": ";
        if (mFileSize < kEndFixed)
        {
            error = label + at("not a ZIP archive (too short)", mFileSize);
            return false;
        }
        const std::uint64_t tailSize = std::min<std::uint64_t>(mFileSize, kEndFixed + kMaxComment);
        std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
        if (!readAt(in, mFileSize - tailSize, tail.data(), tailSize))
        {
            error = label + "cannot read the end of the file";
            return false;
        }
        std::size_t eocd = tail.size();
        for (std::size_t i = tail.size() - kEndFixed + 1; i-- > 0;)
            if (u32(tail.data() + i) == kEndSig)
            {
                eocd = i;
                break;
            }
        if (eocd == tail.size())
        {
            error = label + at("not a ZIP archive (no end record)", mFileSize);
            return false;
        }
        const unsigned char* e = tail.data() + eocd;
        const std::uint32_t entries = u16(e + 10);
        const std::uint64_t cdSize = u32(e + 12);
        const std::uint64_t cdOffset = u32(e + 16);
        if (entries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu)
        {
            error = label + "ZIP64 archives are not supported";
            return false;
        }
        if (entries > NpzLimits::sMaxEntries)
        {
            error = label + at("too many entries", cdOffset);
            return false;
        }
        if (cdOffset + cdSize > mFileSize)
        {
            error = label + at("the central directory lies outside the file", cdOffset);
            return false;
        }
        std::vector<unsigned char> cd(static_cast<std::size_t>(cdSize));
        if (!readAt(in, cdOffset, cd.data(), cdSize))
        {
            error = label + at("cannot read the central directory", cdOffset);
            return false;
        }
        std::size_t pos = 0;
        for (std::uint32_t n = 0; n < entries; ++n)
        {
            if (pos + kCentralFixed > cd.size() || u32(cd.data() + pos) != kCentralSig)
            {
                error = label + at("bad central directory entry", cdOffset + pos);
                return false;
            }
            const unsigned char* c = cd.data() + pos;
            const std::uint32_t method = u16(c + 10);
            const std::uint64_t csize = u32(c + 20);
            const std::uint64_t usize = u32(c + 24);
            const std::size_t nameLen = u16(c + 28);
            const std::size_t extraLen = u16(c + 30);
            const std::size_t commentLen = u16(c + 32);
            const std::uint64_t localOffset = u32(c + 42);
            if (pos + kCentralFixed + nameLen + extraLen + commentLen > cd.size())
            {
                error = label + at("a central directory entry runs past its end", cdOffset + pos);
                return false;
            }
            std::string name(reinterpret_cast<const char*>(c + kCentralFixed), nameLen);
            pos += kCentralFixed + nameLen + extraLen + commentLen;
            if (!endsWith(name, ".npy"))
                continue;
            name.resize(name.size() - 4);
            if (method != 0)
            {
                error = label + "entry " + name + " is compressed (method " + std::to_string(method)
                    + "); only stored entries are supported (save with numpy.savez, not savez_compressed)";
                return false;
            }
            if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu)
            {
                error = label + "ZIP64 entries are not supported";
                return false;
            }
            unsigned char local[kLocalFixed];
            if (localOffset + kLocalFixed > mFileSize || !readAt(in, localOffset, local, kLocalFixed)
                || u32(local) != kLocalSig)
            {
                error = label + at(("bad local header of entry " + name).c_str(), localOffset);
                return false;
            }
            const std::uint64_t dataStart = localOffset + kLocalFixed + u16(local + 26) + u16(local + 28);
            if (dataStart + usize > mFileSize)
            {
                error = label + at(("entry " + name + " runs past the end of the file").c_str(), dataStart);
                return false;
            }
            if (mDataStart.count(name) != 0)
            {
                error = label + "entry " + name + " appears twice";
                return false;
            }
            mNames.push_back(name);
            mDataStart[name] = dataStart;
            mSize[name] = usize;
        }
        error.clear();
        return true;
    }

    bool NpzFile::has(std::string_view name) const
    {
        return mDataStart.find(name) != mDataStart.end();
    }

    bool NpzFile::info(std::string_view name, NpyInfo& out, std::string& error) const
    {
        const auto it = mDataStart.find(name);
        if (it == mDataStart.end())
        {
            error = mPath.generic_string() + ": no array named " + std::string(name) + " (missing)";
            return false;
        }
        const std::string label = mPath.generic_string() + ": array " + std::string(name) + ": ";
        const std::uint64_t start = it->second;
        const std::uint64_t size = mSize.find(name)->second;
        std::ifstream in(mPath, std::ios::binary);
        unsigned char magic[12] = {};
        if (size < 10 || !readAt(in, start, magic, std::min<std::uint64_t>(size, 12))
            || std::memcmp(magic, "\x93NUMPY", 6) != 0)
        {
            error = label + at("not an .npy array", start);
            return false;
        }
        std::uint64_t headerLen = 0, headerStart = 0;
        if (magic[6] == 1)
        {
            headerLen = u16(magic + 8);
            headerStart = 10;
        }
        else if (magic[6] == 2 || magic[6] == 3)
        {
            headerLen = u32(magic + 8);
            headerStart = 12;
        }
        else
        {
            error = label + at("unsupported .npy version", start);
            return false;
        }
        if (headerLen > NpzLimits::sMaxHeaderBytes || headerStart + headerLen > size)
        {
            error = label + at("the .npy header runs past the array", start);
            return false;
        }
        std::string header(static_cast<std::size_t>(headerLen), '\0');
        if (!readAt(in, start + headerStart, header.data(), headerLen))
        {
            error = label + "cannot read the header";
            return false;
        }
        NpyInfo info;
        std::string_view descr = headerValue(header, "descr");
        if (descr.size() < 2 || descr.front() != '\'' || descr.back() != '\'')
        {
            error = label + at("no descr in the header", start);
            return false;
        }
        info.mDescr = std::string(descr.substr(1, descr.size() - 2));
        info.mFortran = headerValue(header, "fortran_order") == "True";
        std::string_view shape = headerValue(header, "shape");
        if (shape.size() < 2 || shape.front() != '(' || shape.back() != ')')
        {
            error = label + at("no shape in the header", start);
            return false;
        }
        shape = shape.substr(1, shape.size() - 2);
        std::uint64_t product = 1;
        std::size_t i = 0;
        while (i < shape.size())
        {
            while (i < shape.size() && (shape[i] == ' ' || shape[i] == ','))
                ++i;
            if (i >= shape.size())
                break;
            std::uint64_t d = 0;
            const std::size_t digits = i;
            while (i < shape.size() && shape[i] >= '0' && shape[i] <= '9')
            {
                d = d * 10 + static_cast<std::uint64_t>(shape[i] - '0');
                if (d > (1ull << 40))
                {
                    error = label + "a dimension is too large";
                    return false;
                }
                ++i;
            }
            if (i == digits)
            {
                error = label + at("bad shape", start);
                return false;
            }
            if (d != 0 && product > std::numeric_limits<std::uint64_t>::max() / d)
            {
                error = label + "the shape is too large";
                return false;
            }
            product *= d;
            info.mShape.push_back(static_cast<std::size_t>(d));
        }
        info.mDataOffset = start + headerStart + headerLen;
        info.mDataBytes = size - headerStart - headerLen;
        const std::size_t item = info.itemSize();
        if (item != 0 && info.elementCount() * item != info.mDataBytes)
        {
            error = label + "the data size (" + std::to_string(info.mDataBytes) + " bytes) does not match the shape";
            return false;
        }
        out = std::move(info);
        error.clear();
        return true;
    }

    bool NpzFile::readBytes(
        const NpyInfo& info, std::uint64_t byteOffset, std::uint64_t count, void* dst, std::string& error) const
    {
        if (byteOffset > info.mDataBytes || count > info.mDataBytes - byteOffset)
        {
            error = mPath.generic_string() + ": a read lies outside the array";
            return false;
        }
        std::ifstream in(mPath, std::ios::binary);
        if (!in || !readAt(in, info.mDataOffset + byteOffset, dst, count))
        {
            error = mPath.generic_string() + ": cannot read the array data";
            return false;
        }
        return true;
    }

    namespace
    {
        bool checkWhole(const NpyInfo& info, std::string_view want, const std::string& label, std::string& error)
        {
            if (info.mDescr != want || info.mFortran)
            {
                error = label + "is " + info.mDescr + (info.mFortran ? " (Fortran order)" : "") + ", expected "
                    + std::string(want) + " in C order";
                return false;
            }
            if (info.mDataBytes > NpzLimits::sMaxArrayBytes)
            {
                error = label + "is too large to read whole";
                return false;
            }
            return true;
        }
    }

    bool NpzFile::readFloats(std::string_view name, std::vector<float>& out, NpyInfo& info, std::string& error) const
    {
        if (!this->info(name, info, error))
            return false;
        if (!checkWhole(info, "<f4", mPath.generic_string() + ": array " + std::string(name) + " ", error))
            return false;
        out.resize(static_cast<std::size_t>(info.mDataBytes / 4));
        return readBytes(info, 0, info.mDataBytes, out.data(), error);
    }

    bool NpzFile::readInt32s(
        std::string_view name, std::vector<std::int32_t>& out, NpyInfo& info, std::string& error) const
    {
        if (!this->info(name, info, error))
            return false;
        if (!checkWhole(info, "<i4", mPath.generic_string() + ": array " + std::string(name) + " ", error))
            return false;
        out.resize(static_cast<std::size_t>(info.mDataBytes / 4));
        return readBytes(info, 0, info.mDataBytes, out.data(), error);
    }

    bool NpzFile::readStrings(std::string_view name, std::vector<std::string>& out, std::string& error) const
    {
        NpyInfo info;
        if (!this->info(name, info, error))
            return false;
        const std::size_t item = info.itemSize();
        if (item == 0 || info.mDescr[0] != '|' || info.mDescr[1] != 'S' || info.mFortran)
        {
            error = mPath.generic_string() + ": array " + std::string(name) + " is " + info.mDescr
                + ", expected a byte string array";
            return false;
        }
        if (info.mDataBytes > NpzLimits::sMaxArrayBytes)
        {
            error = mPath.generic_string() + ": array " + std::string(name) + " is too large";
            return false;
        }
        std::string raw(static_cast<std::size_t>(info.mDataBytes), '\0');
        if (!readBytes(info, 0, info.mDataBytes, raw.data(), error))
            return false;
        out.clear();
        for (std::size_t i = 0; i + item <= raw.size(); i += item)
        {
            std::string s = raw.substr(i, item);
            while (!s.empty() && s.back() == '\0')
                s.pop_back();
            out.push_back(std::move(s));
        }
        return true;
    }

    std::string buildNpz(const std::vector<NpzEntryF32>& floats, const std::vector<NpzEntryI32>& ints,
        const std::vector<NpzEntryStr>& strings)
    {
        struct Entry
        {
            std::string mName, mBytes;
        };
        std::vector<Entry> entries;
        for (const auto& f : floats)
            entries.push_back({ f.mName,
                npyBytes("<f4", f.mShape,
                    std::string(reinterpret_cast<const char*>(f.mValues.data()), f.mValues.size() * 4)) });
        for (const auto& i : ints)
            entries.push_back({ i.mName,
                npyBytes("<i4", i.mShape,
                    std::string(reinterpret_cast<const char*>(i.mValues.data()), i.mValues.size() * 4)) });
        for (const auto& s : strings)
        {
            std::size_t width = 1;
            for (const auto& v : s.mValues)
                width = std::max(width, v.size());
            std::string data(s.mValues.size() * width, '\0');
            for (std::size_t k = 0; k < s.mValues.size(); ++k)
                std::memcpy(data.data() + k * width, s.mValues[k].data(), s.mValues[k].size());
            entries.push_back({ s.mName, npyBytes("|S" + std::to_string(width), { s.mValues.size() }, data) });
        }
        // The first array of the fixtures is written first, so reorder to the order the caller listed (floats, ints,
        // strings) - already the case. Local headers and data, then the central directory, then the end record.
        std::string out;
        std::vector<std::uint32_t> offsets;
        for (const Entry& e : entries)
        {
            offsets.push_back(static_cast<std::uint32_t>(out.size()));
            const std::string name = e.mName + ".npy";
            put32(out, kLocalSig);
            put16(out, 20);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, crc32(e.mBytes));
            put32(out, static_cast<std::uint32_t>(e.mBytes.size()));
            put32(out, static_cast<std::uint32_t>(e.mBytes.size()));
            put16(out, static_cast<std::uint32_t>(name.size()));
            put16(out, 0);
            out += name;
            out += e.mBytes;
        }
        const std::uint32_t cdStart = static_cast<std::uint32_t>(out.size());
        for (std::size_t k = 0; k < entries.size(); ++k)
        {
            const std::string name = entries[k].mName + ".npy";
            put32(out, kCentralSig);
            put16(out, 20);
            put16(out, 20);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, crc32(entries[k].mBytes));
            put32(out, static_cast<std::uint32_t>(entries[k].mBytes.size()));
            put32(out, static_cast<std::uint32_t>(entries[k].mBytes.size()));
            put16(out, static_cast<std::uint32_t>(name.size()));
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, 0);
            put32(out, offsets[k]);
            out += name;
        }
        const std::uint32_t cdSize = static_cast<std::uint32_t>(out.size()) - cdStart;
        put32(out, kEndSig);
        put16(out, 0);
        put16(out, 0);
        put16(out, static_cast<std::uint32_t>(entries.size()));
        put16(out, static_cast<std::uint32_t>(entries.size()));
        put32(out, cdSize);
        put32(out, cdStart);
        put16(out, 0);
        return out;
    }
}
