// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// Helpers shared by the test files. The tests compare the error CLASS (and, where the corpus says so, the offset),
// never message text.
#ifndef EDENARCHIVE_TESTS_TESTHELP_HPP
#define EDENARCHIVE_TESTS_TESTHELP_HPP

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <edenarchive/archive.hpp>
#include <edenarchive/error.hpp>
#include <edenarchive/source.hpp>

#include "corpuscases.hpp"
#include "eapackmodel.hpp"

namespace EdenTestSuite
{
    using EdenTest::Bytes;

    /// The class name of section 14.1, "E_RANGE" and so on.
    inline std::string className(const EdenArchive::Error& error)
    {
        return EdenArchive::errorClassName(error.mClass);
    }

    /// True when `error` is one of the listed classes; an empty list accepts any class except None.
    inline bool classIs(const EdenArchive::Error& error, const std::vector<std::string>& classes)
    {
        if (error.mClass == EdenArchive::ErrorClass::None)
            return false;
        if (classes.empty())
            return true;
        return std::find(classes.begin(), classes.end(), className(error)) != classes.end();
    }

    inline std::string joined(const std::vector<std::string>& classes)
    {
        std::string out;
        for (const std::string& name : classes)
            out += (out.empty() ? "" : "|") + name;
        return out.empty() ? "(any)" : out;
    }

    inline std::unique_ptr<EdenArchive::Archive> openBytes(
        const Bytes& bytes, EdenArchive::Error& error, const EdenArchive::OpenOptions& options = {})
    {
        return EdenArchive::Archive::open(EdenArchive::makeMemorySource(bytes), options, error);
    }

    /// Opens and fails the test if the pack does not open.
    inline std::unique_ptr<EdenArchive::Archive> openOrFail(
        const Bytes& bytes, const EdenArchive::OpenOptions& options = {})
    {
        EdenArchive::Error error;
        std::unique_ptr<EdenArchive::Archive> archive = openBytes(bytes, error, options);
        EXPECT_NE(archive, nullptr) << error.describe();
        EXPECT_TRUE(error.ok()) << error.describe();
        return archive;
    }

    inline Bytes toBytes(const std::vector<std::uint8_t>& v)
    {
        return v;
    }

    inline EdenTest::Digest toDigest(const EdenArchive::Hash& hash)
    {
        EdenTest::Digest out{};
        std::copy(hash.begin(), hash.end(), out.begin());
        return out;
    }

    /// Reads a whole file; empty on failure.
    inline Bytes readFileBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    inline bool writeFileBytes(const std::filesystem::path& path, const Bytes& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        return static_cast<bool>(stream);
    }

    /// A temporary folder with a unique name below the system's temporary folder, removed on destruction.
    class TempDir
    {
    public:
        TempDir()
        {
            static std::atomic<unsigned> counter{ 0 };
            const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
            std::string name = "edenarchive-test-";
            name += info != nullptr ? info->test_suite_name() : "none";
            name += "-";
            name += info != nullptr ? info->name() : "none";
            name += "-" + std::to_string(counter++) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
            for (char& c : name)
            {
                const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
                if (!ok)
                    c = '_';
            }
            mPath = std::filesystem::temp_directory_path() / name;
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
            std::filesystem::create_directories(mPath);
        }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
        }

        const std::filesystem::path& path() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    /// Lists the names (file and folder names, recursively, '/' separators) below a folder, sorted.
    inline std::vector<std::string> listTree(const std::filesystem::path& root)
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            out.push_back(std::filesystem::relative(it->path(), root).generic_string());
        std::sort(out.begin(), out.end());
        return out;
    }

    /// Checks everything the corpus says about the entries of an opened pack: addresses, types, sizes, content hash,
    /// lookups by path and key, provenance. With `readAll` false the content is not read.
    inline void checkEntries(
        const EdenArchive::Archive& archive, const std::vector<EdenTest::ExpectedEntry>& expected, bool readAll = true)
    {
        ASSERT_EQ(archive.entryCount(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const EdenTest::ExpectedEntry& want = expected[i];
            SCOPED_TRACE("entry " + std::to_string(i));
            const EdenArchive::EntryInfo entry = archive.entry(i);
            EXPECT_EQ(entry.mIndex, i);
            EXPECT_EQ(entry.mHasPath, want.mPath.has_value());
            if (want.mPath.has_value())
            {
                    EXPECT_EQ(std::string(entry.mPath), *want.mPath);
            }
            EXPECT_EQ(entry.mHasKey, want.mKey.has_value());
            if (want.mKey.has_value())
            {
                    EXPECT_EQ(entry.mKey, *want.mKey);
            }
            EXPECT_EQ(entry.mType, want.mType);
            EXPECT_EQ(entry.mSize, want.mContent.size());
            EXPECT_EQ(entry.mChunkCount, want.mChunkCount);
            if (want.mReadable)
            {
                    EXPECT_EQ(toDigest(entry.mContentHash), EdenTest::blake3Of(want.mContent));
            }
            EXPECT_EQ(entry.mHasProvenance, want.mHasProvenance);
            if (want.mHasProvenance)
            {
                const auto provenance = archive.provenance(i);
                ASSERT_TRUE(provenance.has_value());
                EXPECT_EQ(std::string(provenance->mToolId), want.mToolId);
                EXPECT_EQ(Bytes(provenance->mParameters.begin(), provenance->mParameters.end()), want.mParameters);
            }
            else
                EXPECT_FALSE(archive.provenance(i).has_value());

            if (want.mPath.has_value())
            {
                const auto found = archive.findPath(*want.mPath);
                ASSERT_TRUE(found.has_value()) << *want.mPath;
                EXPECT_EQ(*found, i);
            }
            if (want.mKey.has_value())
            {
                const auto found = archive.findKey(*want.mKey);
                ASSERT_TRUE(found.has_value());
                EXPECT_EQ(*found, i);
            }

            if (readAll && want.mReadable)
            {
                std::vector<std::uint8_t> content;
                const EdenArchive::Error error = archive.readEntry(i, content);
                EXPECT_TRUE(error.ok()) << error.describe();
                EXPECT_EQ(content, want.mContent);
                EXPECT_TRUE(archive.verifyEntry(i).ok());
            }
        }
    }
}

#endif
