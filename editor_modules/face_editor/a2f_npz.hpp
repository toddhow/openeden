#pragma once

// A reader for the parts of NumPy's .npz/.npy files that the Audio2Face downloads use (task 0147 slice 2): a ZIP
// archive whose entries are STORED (not compressed) .npy arrays of little-endian float32, int32 or fixed-width byte
// strings. Written from the public format descriptions (NumPy's NEP 1 for .npy, the ZIP APPNOTE for the archive),
// not from NVIDIA's code. Untrusted input: every offset and size is checked against the file and against caps before
// anything is allocated; nothing throws. Pure: standard library only.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace face_editor
{
    namespace NpzLimits
    {
        inline constexpr std::size_t sMaxEntries = 4096;
        inline constexpr std::size_t sMaxHeaderBytes = 65536;
        inline constexpr std::uint64_t sMaxArrayBytes = 1ull << 30; // 1 GiB per array read whole
    }

    struct NpyInfo
    {
        std::string mDescr; ///< "<f4", "<i4", "|S19", ...
        std::vector<std::size_t> mShape;
        bool mFortran = false;
        std::uint64_t mDataOffset = 0; ///< in the file
        std::uint64_t mDataBytes = 0;

        std::uint64_t elementCount() const;
        /// 4 for "<f4"/"<i4", n for "|Sn", 0 when the descriptor is not one of those.
        std::size_t itemSize() const;
    };

    class NpzFile
    {
    public:
        /// Reads the central directory. False with \a error ("what (offset N)") when the file is not a stored ZIP of
        /// .npy entries within the caps.
        bool open(const std::filesystem::path& path, std::string& error);

        /// Entry names without the ".npy" suffix, in file order.
        const std::vector<std::string>& names() const { return mNames; }
        bool has(std::string_view name) const;

        /// The array's header (shape, dtype, where its data is). \a name is without ".npy".
        bool info(std::string_view name, NpyInfo& out, std::string& error) const;

        /// Reads [byteOffset, byteOffset + count) of the array's data into \a dst.
        bool readBytes(
            const NpyInfo& info, std::uint64_t byteOffset, std::uint64_t count, void* dst, std::string& error) const;

        /// The whole array as float32 / int32 (little-endian, C order). \a info is filled too.
        bool readFloats(std::string_view name, std::vector<float>& out, NpyInfo& info, std::string& error) const;
        bool readInt32s(std::string_view name, std::vector<std::int32_t>& out, NpyInfo& info, std::string& error) const;
        /// An array of fixed-width byte strings ("|S19"); trailing NULs are dropped.
        bool readStrings(std::string_view name, std::vector<std::string>& out, std::string& error) const;

    private:
        std::filesystem::path mPath;
        std::uint64_t mFileSize = 0;
        std::vector<std::string> mNames;
        std::map<std::string, std::uint64_t, std::less<>> mDataStart; ///< entry data offset (past the local header)
        std::map<std::string, std::uint64_t, std::less<>> mSize;
    };

    /// Builds a stored-entries ZIP of .npy arrays in memory (tests and fixtures). Every array is C order little-endian.
    struct NpzEntryF32
    {
        std::string mName;
        std::vector<std::size_t> mShape;
        std::vector<float> mValues;
    };
    struct NpzEntryI32
    {
        std::string mName;
        std::vector<std::size_t> mShape;
        std::vector<std::int32_t> mValues;
    };
    struct NpzEntryStr
    {
        std::string mName;
        std::vector<std::string> mValues;
    };
    std::string buildNpz(const std::vector<NpzEntryF32>& floats, const std::vector<NpzEntryI32>& ints,
        const std::vector<NpzEntryStr>& strings);
}
