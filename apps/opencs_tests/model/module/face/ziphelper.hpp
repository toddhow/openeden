#pragma once

// A tiny zip writer for the tests of task 0147 slice 3 (zip_entry, directml_install): stored and deflated entries, a
// central directory and the end record, written from the public zip format description. Not used by production code.

#include <zlib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ziphelper
{
    struct Entry
    {
        std::string mName;
        std::string mData;
        bool mDeflate = false;
        int mMethodOverride = -1; ///< a method number to write instead (for the unsupported-method test)
        std::int64_t mCrcDelta = 0; ///< added to the stored CRC (for the checksum test)
        std::int64_t mSizeDelta = 0; ///< added to the stored uncompressed size (for the size lie test)
    };

    inline void put16(std::string& s, std::uint32_t v)
    {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>((v >> 8) & 0xFF));
    }
    inline void put32(std::string& s, std::uint32_t v)
    {
        put16(s, v & 0xFFFF);
        put16(s, v >> 16);
    }

    inline std::string deflateRaw(const std::string& data)
    {
        z_stream z{};
        deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        std::string out(deflateBound(&z, static_cast<uLong>(data.size())) + 16, '\0');
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        z.avail_in = static_cast<uInt>(data.size());
        z.next_out = reinterpret_cast<Bytef*>(out.data());
        z.avail_out = static_cast<uInt>(out.size());
        deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        deflateEnd(&z);
        return out;
    }

    inline std::string makeZip(const std::vector<Entry>& entries)
    {
        std::string file, central;
        for (const Entry& e : entries)
        {
            const std::string body = e.mDeflate ? deflateRaw(e.mData) : e.mData;
            const std::uint32_t crc = static_cast<std::uint32_t>(
                crc32(0, reinterpret_cast<const Bytef*>(e.mData.data()), static_cast<uInt>(e.mData.size()))
                + e.mCrcDelta);
            const std::uint32_t method
                = e.mMethodOverride >= 0 ? static_cast<std::uint32_t>(e.mMethodOverride) : (e.mDeflate ? 8u : 0u);
            const auto usize = static_cast<std::uint32_t>(static_cast<std::int64_t>(e.mData.size()) + e.mSizeDelta);
            const auto offset = static_cast<std::uint32_t>(file.size());
            put32(file, 0x04034b50);
            put16(file, 20);
            put16(file, 0);
            put16(file, method);
            put16(file, 0);
            put16(file, 0x21);
            put32(file, crc);
            put32(file, static_cast<std::uint32_t>(body.size()));
            put32(file, usize);
            put16(file, static_cast<std::uint32_t>(e.mName.size()));
            put16(file, 0);
            file += e.mName;
            file += body;
            put32(central, 0x02014b50);
            put16(central, 20);
            put16(central, 20);
            put16(central, 0);
            put16(central, method);
            put16(central, 0);
            put16(central, 0x21);
            put32(central, crc);
            put32(central, static_cast<std::uint32_t>(body.size()));
            put32(central, usize);
            put16(central, static_cast<std::uint32_t>(e.mName.size()));
            put16(central, 0);
            put16(central, 0);
            put16(central, 0);
            put16(central, 0);
            put32(central, 0);
            put32(central, offset);
            central += e.mName;
        }
        const auto centralOffset = static_cast<std::uint32_t>(file.size());
        file += central;
        put32(file, 0x06054b50);
        put16(file, 0);
        put16(file, 0);
        put16(file, static_cast<std::uint32_t>(entries.size()));
        put16(file, static_cast<std::uint32_t>(entries.size()));
        put32(file, static_cast<std::uint32_t>(central.size()));
        put32(file, centralOffset);
        put16(file, 0);
        return file;
    }

    inline void writeFile(const std::filesystem::path& path, const std::string& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// Deterministic pseudo-random bytes (incompressible enough to make deflate do work).
    inline std::string noise(std::size_t n, std::uint32_t seed = 1)
    {
        std::string s(n, '\0');
        std::uint32_t x = seed * 2654435761u + 12345u;
        for (auto& c : s)
        {
            x = x * 1664525u + 1013904223u;
            c = static_cast<char>((x >> 24) & 0xFF);
        }
        return s;
    }
}
