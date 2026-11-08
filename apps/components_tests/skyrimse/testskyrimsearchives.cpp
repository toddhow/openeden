// Task 0086 stage c: generated Skyrim SE archives (BSA version 105 with LZ4 frames). No game data.
#include <game_skyrimse/pack_core.hpp>

#include <components/archivekinds/archivekinds.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "bsabuilder.hpp"

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace SkyrimSeTests;

    class SkyrimSeArchiveTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            mRoot = fs::temp_directory_path()
                / ("openeden_skyrimse_archives_"
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

    std::string skeletonBytes()
    {
        std::string content = "Gamebryo File Format, Version 20.2.0.7\n";
        for (int i = 0; i < 200; ++i)
            content += "bone " + std::to_string(i) + " NPC Spine [Spn" + std::to_string(i % 3) + "]\n";
        return content;
    }

    std::vector<ArchiveFile> sampleFiles()
    {
        return { { "meshes\\actors\\character\\character assets", "skeleton.nif", skeletonBytes(), true },
            { "meshes\\actors\\character\\character assets", "tiny.txt", "tiny", true },
            { "meshes\\armor\\iron", "cuirass_1.nif", std::string(5000, 'x'), true },
            { "scripts", "stored.pex", "stored without lz4", false } };
    }

    TEST_F(SkyrimSeArchiveTest, anLz4ArchiveIsListedAndItsEntriesAreRead)
    {
        const fs::path path = write("sample.bsa", buildArchive(sampleFiles()));
        ArchiveListing listing;
        std::string error;
        ASSERT_TRUE(readArchiveListing(path, listing, error)) << error;
        EXPECT_EQ(listing.mVersion, 105u);
        EXPECT_EQ(listing.mFiles.size(), 4u);
        EXPECT_NE(std::find(listing.mFiles.begin(), listing.mFiles.end(),
                      "meshes\\actors\\character\\character assets\\skeleton.nif"),
            listing.mFiles.end());

        std::string bytes;
        ASSERT_TRUE(readArchiveEntry(path, "meshes/actors/character/character assets/skeleton.nif", bytes, error))
            << error;
        EXPECT_EQ(bytes, skeletonBytes());
        ASSERT_TRUE(readArchiveEntry(path, "meshes/armor/iron/cuirass_1.nif", bytes, error)) << error;
        EXPECT_EQ(bytes, std::string(5000, 'x'));
        ASSERT_TRUE(readArchiveEntry(path, "scripts/stored.pex", bytes, error)) << error;
        EXPECT_EQ(bytes, "stored without lz4");
    }

    TEST_F(SkyrimSeArchiveTest, aMissingEntryIsAnErrorThatNamesTheArchive)
    {
        const fs::path path = write("sample.bsa", buildArchive(sampleFiles()));
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/nothing.nif", bytes, error));
        EXPECT_EQ(error.rfind("sample.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, anotherVersionAndNonArchivesAreRefusedWithLocatedMessages)
    {
        ArchiveListing listing;
        std::string error;
        const fs::path fo3 = write("fo3.bsa", buildArchive(sampleFiles(), 104));
        EXPECT_FALSE(readArchiveListing(fo3, listing, error));
        EXPECT_EQ(error.rfind("fo3.bsa", 0), 0u) << error;
        EXPECT_NE(error.find("104"), std::string::npos) << error;

        const fs::path junk = write("junk.bsa", std::string(200, 'j'));
        EXPECT_FALSE(readArchiveListing(junk, listing, error));
        EXPECT_EQ(error.rfind("junk.bsa", 0), 0u) << error;

        EXPECT_FALSE(readArchiveListing(mRoot / "absent.bsa", listing, error));
        EXPECT_EQ(error.rfind("absent.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, everyTruncationFailsWithALocatedErrorOrDoesNotCrash)
    {
        const std::string whole = buildArchive(sampleFiles());
        std::size_t listingFailures = 0;
        for (std::size_t length = 0; length < whole.size(); length += 7)
        {
            const fs::path path = write("cut.bsa", whole.substr(0, length));
            ArchiveListing listing;
            std::string error;
            if (!readArchiveListing(path, listing, error))
            {
                ++listingFailures;
                EXPECT_EQ(error.rfind("cut.bsa", 0), 0u) << "length " << length << ": " << error;
            }
            else
            {
                // the table was complete; an entry whose data is cut off must fail, not crash
                std::string bytes;
                std::string entryError;
                for (const std::string& name : listing.mFiles)
                {
                    std::string slashed = name;
                    std::replace(slashed.begin(), slashed.end(), '\\', '/');
                    if (!readArchiveEntry(path, slashed, bytes, entryError))
                    {
                        EXPECT_EQ(entryError.rfind("cut.bsa", 0), 0u) << entryError;
                    }
                }
            }
        }
        EXPECT_GT(listingFailures, 0u);
    }

    // ---- Review 4, finding 2: hostile sizes in a compressed archive (inherited reader, newly reachable) ----

    // Replaces the 4 byte "inflated size" in front of the first LZ4 frame of a generated archive.
    std::string withInflatedSize(std::string archive, std::uint32_t claimed)
    {
        const std::string magic("\x04\x22M\x18", 4);
        const std::size_t frame = archive.find(magic);
        EXPECT_NE(frame, std::string::npos);
        std::memcpy(&archive[frame - 4], &claimed, 4);
        return archive;
    }

    std::vector<ArchiveFile> oneFile()
    {
        return { { "meshes", "a.nif", std::string(3000, 'q'), true } };
    }

    TEST_F(SkyrimSeArchiveTest, aValidArchiveStillReadsAfterTheHardening)
    {
        const fs::path path = write("ok.bsa", buildArchive(oneFile()));
        std::string bytes;
        std::string error;
        ASSERT_TRUE(readArchiveEntry(path, "meshes/a.nif", bytes, error)) << error;
        EXPECT_EQ(bytes, std::string(3000, 'q'));
    }

    TEST_F(SkyrimSeArchiveTest, anInflatedSizeFarAboveWhatTheFrameCouldHoldIsRefusedNotAllocated)
    {
        // 150 MiB claimed for a frame of a few dozen bytes (LZ4 cannot expand more than about 255 to 1).
        const fs::path path = write("big.bsa", withInflatedSize(buildArchive(oneFile()), 150u * 1024 * 1024));
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/a.nif", bytes, error));
        EXPECT_EQ(error.rfind("big.bsa", 0), 0u) << error;
        EXPECT_TRUE(bytes.empty());
    }

    TEST_F(SkyrimSeArchiveTest, anInflatedSizeOfFourGibibytesIsRefused)
    {
        const fs::path path = write("huge.bsa", withInflatedSize(buildArchive(oneFile()), 0xFFFFFFF0u));
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/a.nif", bytes, error));
        EXPECT_EQ(error.rfind("huge.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, anInflatedSizeThatDisagreesWithTheFrameIsAnError)
    {
        // Smaller than the real content: the frame needs more room than was promised.
        const fs::path path = write("small.bsa", withInflatedSize(buildArchive(oneFile()), 100));
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/a.nif", bytes, error));
        // Larger than the content (but within the ratio): the frame ends early, which is not a valid entry either.
        const fs::path path2 = write("larger.bsa", withInflatedSize(buildArchive(oneFile()), 3200));
        EXPECT_FALSE(readArchiveEntry(path2, "meshes/a.nif", bytes, error));
    }

    TEST_F(SkyrimSeArchiveTest, aFileRecordPointingPastTheEndOfTheArchiveFailsOnRead)
    {
        std::string archive = buildArchive(oneFile());
        // The file record is the last 16 bytes before the file name block: hash(8) size(4) offset(4). Grow the size.
        const std::size_t nameBlock = archive.find(std::string("a.nif\0", 6));
        ASSERT_NE(nameBlock, std::string::npos);
        const std::size_t sizeField = nameBlock - 8;
        std::uint32_t size = 0;
        std::memcpy(&size, &archive[sizeField], 4);
        size += 100000;
        std::memcpy(&archive[sizeField], &size, 4);
        const fs::path path = write("past.bsa", archive);
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/a.nif", bytes, error));
        EXPECT_EQ(error.rfind("past.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, aFolderCountLargerThanTheFileCanHoldIsRefusedWithoutReserving)
    {
        std::string archive = buildArchive(oneFile());
        const std::uint32_t count = 0xFFFFFFFFu;
        std::memcpy(&archive[36 + 8], &count, 4); // the first folder record's file count
        const fs::path path = write("count.bsa", archive);
        ArchiveListing listing;
        std::string error;
        EXPECT_FALSE(readArchiveListing(path, listing, error));
        EXPECT_EQ(error.rfind("count.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, embeddedNamesLongerThanTheEntryAreRefusedNotUnderflowed)
    {
        std::string archive = buildArchive(oneFile());
        const std::uint32_t flags = 0x3 | 0x4 | 0x100; // folder names, file names, compress, embedded names
        std::memcpy(&archive[12], &flags, 4);
        const fs::path path = write("embedded.bsa", archive);
        std::string bytes;
        std::string error;
        EXPECT_FALSE(readArchiveEntry(path, "meshes/a.nif", bytes, error));
        EXPECT_EQ(error.rfind("embedded.bsa", 0), 0u) << error;
    }

    TEST_F(SkyrimSeArchiveTest, theArchiveKindsOpenAGeneratedSkyrimArchive)
    {
        VFS::registerBethesdaArchiveKinds();
        const fs::path path = write("kind.bsa", buildArchive(sampleFiles()));
        EXPECT_NE(VFS::tes4ArchiveFactory()(path, nullptr), nullptr);
        EXPECT_EQ(VFS::tes3ArchiveFactory()(path, nullptr), nullptr);
        EXPECT_EQ(VFS::ba2ArchiveFactory()(path, nullptr), nullptr);
    }
}
