#ifndef OPENEDEN_TEST_SUITE_NIF_NIFBUILDER_H
#define OPENEDEN_TEST_SUITE_NIF_NIFBUILDER_H

// Writers for small generated NIF files (task 0082). Nothing here is read from a game: every byte is written by the
// test that asks for it, so no game asset enters the repository.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Nif::Testing
{
    class ByteWriter
    {
    public:
        template <class T>
        ByteWriter& put(T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto* bytes = reinterpret_cast<const char*>(&value);
            mBytes.append(bytes, sizeof(T));
            return *this;
        }

        ByteWriter& raw(std::string_view text)
        {
            mBytes.append(text);
            return *this;
        }

        ByteWriter& append(const ByteWriter& other)
        {
            mBytes.append(other.mBytes);
            return *this;
        }

        // A string preceded by its 32-bit length (record type names, sized strings, the string table).
        ByteWriter& sized(std::string_view text)
        {
            put(static_cast<std::uint32_t>(text.size()));
            mBytes.append(text);
            return *this;
        }

        // A string preceded by its 8-bit length (the Bethesda header's export strings).
        ByteWriter& small(std::string_view text)
        {
            put(static_cast<std::uint8_t>(text.size()));
            mBytes.append(text);
            return *this;
        }

        ByteWriter& floats(std::initializer_list<float> values)
        {
            for (float value : values)
                put(value);
            return *this;
        }

        const std::string& bytes() const { return mBytes; }
        std::size_t size() const { return mBytes.size(); }

    private:
        std::string mBytes;
    };

    // A float as an IEEE half. Exact for the values tests use (small dyadic fractions).
    inline std::uint16_t toHalf(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const std::uint32_t sign = (bits >> 16) & 0x8000u;
        if ((bits & 0x7FFFFFFFu) == 0)
            return static_cast<std::uint16_t>(sign);
        const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        const std::uint32_t mantissa = bits & 0x7FFFFFu;
        if (exponent <= 0)
        {
            if (exponent < -10)
                return static_cast<std::uint16_t>(sign);
            const std::uint32_t full = mantissa | 0x800000u;
            const int shift = 14 - exponent;
            std::uint32_t half = full >> shift;
            if ((full >> (shift - 1)) & 1u)
                ++half;
            return static_cast<std::uint16_t>(sign | half);
        }
        if (exponent >= 31)
            return static_cast<std::uint16_t>(sign | 0x7C00u);
        std::uint32_t half = sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
        if (mantissa & 0x1000u)
            ++half;
        return static_cast<std::uint16_t>(half);
    }

    constexpr std::uint32_t version(std::uint8_t major, std::uint8_t minor, std::uint8_t patch, std::uint8_t rev)
    {
        return (static_cast<std::uint32_t>(major) << 24) | (static_cast<std::uint32_t>(minor) << 16)
            | (static_cast<std::uint32_t>(patch) << 8) | rev;
    }

    // One block of a Gamebryo file: its record type name and its bytes.
    struct Block
    {
        std::string mType;
        ByteWriter mData;
    };

    // A 20.2.0.7 file (Fallout 3 and later): header with the version, user version and Bethesda stream version, the
    // record type table, record sizes (the reader ignores them: zero), the string table, the blocks and the roots.
    struct GamebryoFile
    {
        std::uint32_t mUserVersion = 12;
        std::uint32_t mStreamVersion = 100;
        std::vector<std::string> mStrings;
        std::vector<Block> mBlocks;
        std::vector<std::int32_t> mRoots{ 0 };
        // Fields a refusal test corrupts.
        std::string mHeaderLine = "Gamebryo File Format, Version 20.2.0.7\n";
        std::uint32_t mVersion = version(20, 2, 0, 7);
        std::uint8_t mEndian = 1;

        // Index of a string in the table, added if it is new.
        std::uint32_t string(std::string_view text)
        {
            for (std::size_t i = 0; i < mStrings.size(); ++i)
            {
                if (mStrings[i] == text)
                    return static_cast<std::uint32_t>(i);
            }
            mStrings.emplace_back(text);
            return static_cast<std::uint32_t>(mStrings.size() - 1);
        }

        std::string build() const
        {
            ByteWriter out;
            out.raw(mHeaderLine);
            out.put(mVersion);
            out.put(mEndian);
            out.put(mUserVersion);
            out.put(static_cast<std::uint32_t>(mBlocks.size()));
            // The Bethesda stream header (read when the user version is 3 or more).
            out.put(mStreamVersion);
            out.small("author");
            out.small("process");
            out.small("export");
            if (mStreamVersion >= 103)
                out.small("path");
            std::vector<std::string> types;
            std::vector<std::uint16_t> indices;
            for (const Block& block : mBlocks)
            {
                std::size_t found = types.size();
                for (std::size_t i = 0; i < types.size(); ++i)
                {
                    if (types[i] == block.mType)
                        found = i;
                }
                if (found == types.size())
                    types.push_back(block.mType);
                indices.push_back(static_cast<std::uint16_t>(found));
            }
            out.put(static_cast<std::uint16_t>(types.size()));
            for (const std::string& type : types)
                out.sized(type);
            for (std::uint16_t index : indices)
                out.put(index);
            for (std::size_t i = 0; i < mBlocks.size(); ++i)
                out.put(static_cast<std::uint32_t>(0));
            out.put(static_cast<std::uint32_t>(mStrings.size()));
            std::uint32_t longest = 0;
            for (const std::string& text : mStrings)
                longest = std::max<std::uint32_t>(longest, static_cast<std::uint32_t>(text.size()));
            out.put(longest);
            for (const std::string& text : mStrings)
                out.sized(text);
            out.put(static_cast<std::uint32_t>(0)); // groups
            for (const Block& block : mBlocks)
                out.append(block.mData);
            out.put(static_cast<std::uint32_t>(mRoots.size()));
            for (std::int32_t root : mRoots)
                out.put(root);
            return out.bytes();
        }
    };

    // The NiAVObject part of a block for stream version 83 and later: name (a string table index), no extra data, no
    // controller, 32-bit flags, transform, no collision object.
    inline void writeAvObject(ByteWriter& out, std::uint32_t nameIndex, const std::array<float, 3>& translation = {},
        float scale = 1.f, const std::array<float, 9>& rotation = { 1, 0, 0, 0, 1, 0, 0, 0, 1 })
    {
        out.put(nameIndex);
        out.put(static_cast<std::uint32_t>(0)); // extra data list
        out.put(static_cast<std::int32_t>(-1)); // controller
        out.put(static_cast<std::uint32_t>(14)); // flags
        for (float value : translation)
            out.put(value);
        for (float value : rotation)
            out.put(value);
        out.put(scale);
        out.put(static_cast<std::int32_t>(-1)); // collision object
    }
}

#endif
