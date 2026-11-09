// Review round 2026-11-09, lane B (items B1 to B5): the compressed BSA reader and the Skyrim SE pack's entry read, run
// over generated hostile archives. Nothing here reads game data; every archive is built in memory or written to a
// temporary folder.

#include <game_skyrimse/pack_core.hpp>

#include <components/bsa/compressedbsafile.hpp>

#include <gtest/gtest.h>

#include <zlib.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
// psapi must follow windows.h
#include <psapi.h>
#endif

#include "bsabuilder.hpp"

namespace
{
    namespace fs = std::filesystem;
    using namespace SkyrimSeTests;

    class BsaHardeningTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            mRoot = fs::temp_directory_path()
                / ("openeden_bsa_hardening_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mRoot);
        }
        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(mRoot, ignored);
        }
        fs::path write(const std::string& name, const std::string& bytes) const
        {
            const fs::path path = mRoot / name;
            std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return path;
        }
        fs::path mRoot;
    };

    /// Private bytes of this process, or 0 where not known.
    std::uint64_t privateBytes()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)))
            return counters.PrivateUsage;
        return 0;
#else
        std::ifstream statm("/proc/self/statm");
        std::uint64_t size = 0;
        std::uint64_t resident = 0;
        if (statm >> size >> resident)
            return resident * 4096;
        return 0;
#endif
    }

    std::string patternContent(std::size_t size)
    {
        std::string out;
        while (out.size() < size)
            out += "abcdefghijklmnop";
        out.resize(size);
        return out;
    }

    std::size_t findFrame(const std::string& archive)
    {
        const std::string magic("\x04\x22M\x18", 4);
        const std::size_t frame = archive.find(magic);
        EXPECT_NE(frame, std::string::npos);
        return frame;
    }

    /// Sets the match offset of the first LZ4 sequence to 65535, before the start of the output: the frame header is
    /// valid, the block is not, so
    /// LZ4F_decompress fails after it has allocated its buffers.
    std::string withFarMatchOffset(std::string archive)
    {
        const std::size_t frame = findFrame(archive);
        const std::size_t literals = archive.find("abcdefghijklmnop", frame);
        EXPECT_NE(literals, std::string::npos);
        archive[literals + 16] = static_cast<char>(0xFF);
        archive[literals + 17] = static_cast<char>(0xFF);
        return archive;
    }

    std::vector<ArchiveFile> oneFile(std::size_t size = 300000)
    {
        return { { "meshes", "a.nif", patternContent(size), true } };
    }

    std::string readFirst(Bsa::CompressedBSAFile& archive)
    {
        const Bsa::BSAFile::FileStruct& file = archive.getList().front();
        const Files::IStreamPtr stream = archive.getFile(&file);
        std::string out((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        return out;
    }

    // ---- B1: the LZ4 context is freed on the throw path ----

    TEST_F(BsaHardeningTest, theFarOffsetFrameIsRefusedWithAnLz4Error)
    {
        // Pins that the corruption below really reaches LZ4F_decompress's error path (not a size check before it).
        const fs::path path = write("zero.bsa", withFarMatchOffset(buildArchive(oneFile())));
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        try
        {
            readFirst(archive);
            FAIL() << "the damaged frame was accepted";
        }
        catch (const std::exception& e)
        {
            EXPECT_NE(std::string(e.what()).find("LZ4 decompression error"), std::string::npos) << e.what();
        }
    }

    TEST_F(BsaHardeningTest, anLz4ErrorDoesNotLeakTheDecompressionContext)
    {
        if (privateBytes() == 0)
            GTEST_SKIP() << "no process memory counter on this platform";
        const fs::path path = write("leak.bsa", withFarMatchOffset(buildArchive(oneFile())));
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        const auto once = [&] {
            try
            {
                readFirst(archive);
            }
            catch (const std::exception&)
            {
            }
        };
        for (int i = 0; i < 50; ++i) // warm up the allocator
            once();
        const std::uint64_t before = privateBytes();
        constexpr int iterations = 4000;
        for (int i = 0; i < iterations; ++i)
            once();
        const std::uint64_t grown = privateBytes() - std::min(before, privateBytes());
        // A leaked context is at least a few KiB (its input and output buffers are 64 KiB blocks): 4000 of them
        // are tens of MiB. An allocator that is merely warm grows by a few MiB at most.
        EXPECT_LT(grown, 8u * 1024 * 1024) << "private bytes grew by " << grown << " over " << iterations << " reads";
    }

    // ---- B2: a zlib entry must inflate to exactly the promised size ----

    /// Replaces the stored data of the only file of a generated version 104 archive with a zlib stream of
    /// `actual` that promises `promised` bytes, and fixes the file record's size.
    std::string withZlibEntry(const std::string& original, const std::string& content, const std::string& actual,
        std::uint32_t promised)
    {
        const std::string frame = lz4Frame(content);
        const std::size_t storedSize = 4 + frame.size();
        std::string archive = original;
        const std::size_t dataStart = archive.size() - storedSize;
        archive.resize(dataStart);

        uLongf bound = compressBound(static_cast<uLong>(actual.size()));
        std::string z(bound, '\0');
        EXPECT_EQ(::compress(reinterpret_cast<Bytef*>(z.data()), &bound, reinterpret_cast<const Bytef*>(actual.data()),
                      static_cast<uLong>(actual.size())),
            Z_OK);
        z.resize(bound);
        Out o;
        o.u32(promised).text(z);
        archive += o.mData;

        // The file record: the 16 bytes before the file name block; its size field is the 4 bytes after the hash.
        const std::size_t nameBlock = archive.find(std::string("a.nif\0", 6));
        EXPECT_NE(nameBlock, std::string::npos);
        const std::uint32_t size = static_cast<std::uint32_t>(o.mData.size());
        std::memcpy(&archive[nameBlock - 8], &size, 4);
        return archive;
    }

    std::vector<std::string> readAllOf(Bsa::CompressedBSAFile& archive)
    {
        std::vector<std::string> out;
        for (const Bsa::BSAFile::FileStruct& file : archive.getList())
        {
            const Files::IStreamPtr stream = archive.getFile(&file);
            out.emplace_back((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        }
        return out;
    }

    TEST_F(BsaHardeningTest, aZlibEntryThatInflatesToExactlyItsPromisedSizeIsRead)
    {
        const std::string content = patternContent(3000);
        const std::string archive
            = withZlibEntry(buildArchive({ { "meshes", "a.nif", content, true } }, 104), content, content, 3000);
        const fs::path path = write("z_ok.bsa", archive);
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        const std::vector<std::string> files = readAllOf(bsa);
        ASSERT_EQ(files.size(), 1u);
        EXPECT_EQ(files[0], content);
    }

    TEST_F(BsaHardeningTest, aZlibEntryThatInflatesToLessThanPromisedIsRefused)
    {
        const std::string content = patternContent(3000);
        const std::string actual = patternContent(100);
        // Promises 3000 bytes, the stream holds 100: the rest of the buffer would be handed out unwritten.
        const std::string archive
            = withZlibEntry(buildArchive({ { "meshes", "a.nif", content, true } }, 104), content, actual, 3000);
        const fs::path path = write("z_short.bsa", archive);
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        EXPECT_THROW(readAllOf(bsa), std::exception);
    }

    // ---- B3: LZ4 cannot expand more than about 255 to 1 ----

    TEST_F(BsaHardeningTest, anLz4ClaimBeyondTheLz4RatioIsRefusedBeforeDecoding)
    {
        const std::string archive = buildArchive(oneFile(3000));
        std::string claimed = archive;
        const std::size_t frame = findFrame(claimed);
        // Compressed bytes the reader sees: everything after the 4 byte size, to the end of the archive.
        const std::size_t compressed = claimed.size() - frame;
        const std::uint32_t claim = static_cast<std::uint32_t>(compressed * 400); // inside zlib's 1032, outside 255
        std::memcpy(&claimed[frame - 4], &claim, 4);
        const fs::path path = write("ratio.bsa", claimed);
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        try
        {
            readAllOf(bsa);
            FAIL() << "accepted";
        }
        catch (const std::exception& e)
        {
            EXPECT_NE(std::string(e.what()).find("cannot hold"), std::string::npos) << e.what();
        }
    }

    TEST_F(BsaHardeningTest, aRealHighlyCompressibleLz4EntryIsStillRead)
    {
        // 8 MiB of zeros is about the densest data LZ4 can make (close to 255 to 1).
        const std::string zeros(8u * 1024 * 1024, '\0');
        const fs::path path = write("dense.bsa", buildArchive({ { "meshes", "z.nif", zeros, true } }));
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        const std::vector<std::string> files = readAllOf(bsa);
        ASSERT_EQ(files.size(), 1u);
        EXPECT_EQ(files[0].size(), zeros.size());
    }

    TEST_F(BsaHardeningTest, theEntryReadReturnsALargeEntryIntact)
    {
        // pack_core's read streams the entry; a multi-megabyte entry must come back byte for byte.
        std::string content;
        for (std::size_t i = 0; i < 6u * 1024 * 1024; ++i)
            content.push_back(static_cast<char>((i * 2654435761u) >> 13));
        const fs::path path = write("large.bsa", buildArchive({ { "meshes", "big.nif", content, true } }));
        std::string bytes;
        std::string error;
        ASSERT_TRUE(game_skyrimse::readArchiveEntry(path, "meshes/big.nif", bytes, error)) << error;
        EXPECT_TRUE(bytes == content);
    }

    // ---- B4: record counts are bounded by the file size ----

    TEST_F(BsaHardeningTest, aFolderCountLargerThanTheFileCanHoldIsAnLocatedErrorNotAnAllocation)
    {
        std::string archive = buildArchive(oneFile(3000));
        const std::uint32_t count = 0xFFFFFFFFu;
        std::memcpy(&archive[16], &count, 4); // the header's folder count
        const fs::path path = write("folders.bsa", archive);
        Bsa::CompressedBSAFile bsa;
        try
        {
            bsa.open(path);
            FAIL() << "accepted";
        }
        catch (const std::exception& e)
        {
            EXPECT_NE(std::string(e.what()).find("folder"), std::string::npos) << e.what();
            EXPECT_EQ(std::string(e.what()).find("alloc"), std::string::npos) << e.what();
        }
    }

    // ---- B5: duplicate hashes are reported ----

    TEST_F(BsaHardeningTest, aCleanArchiveReportsNoDuplicateHashes)
    {
        const fs::path path = write("clean.bsa", buildArchive(oneFile(3000)));
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        EXPECT_EQ(bsa.duplicateHashCount(), 0u);
    }

    TEST_F(BsaHardeningTest, twoFilesWithOneHashInAFolderAreCounted)
    {
        // Two names with the same hash cannot be built honestly; build two files and give the second record the
        // first's hash.
        std::string archive = buildArchive({ { "meshes", "a.nif", "one", true }, { "meshes", "b.nif", "two", true } });
        const std::size_t nameBlock = archive.find(std::string("a.nif\0b.nif\0", 12));
        ASSERT_NE(nameBlock, std::string::npos);
        // Two 16 byte file records precede the names: copy the first record's hash over the second's.
        std::memcpy(&archive[nameBlock - 16], &archive[nameBlock - 32], 8);
        const fs::path path = write("dupfile.bsa", archive);
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        EXPECT_EQ(bsa.duplicateHashCount(), 1u);
        EXPECT_EQ(bsa.getList().size(), 1u); // the behaviour is unchanged: the later record replaces the earlier
    }

    TEST_F(BsaHardeningTest, twoFoldersWithOneHashAreCounted)
    {
        std::string archive = buildArchive({ { "meshes", "a.nif", "one", true }, { "scripts", "b.pex", "two", true } });
        // Folder records start at 36, 24 bytes each (version 105): copy the first folder's hash over the second's.
        std::memcpy(&archive[36 + 24], &archive[36], 8);
        const fs::path path = write("dupfolder.bsa", archive);
        Bsa::CompressedBSAFile bsa;
        bsa.open(path);
        EXPECT_EQ(bsa.duplicateHashCount(), 1u);
    }
}
