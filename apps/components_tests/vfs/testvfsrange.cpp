// Task 0050, stage d: File::size and File::readRange on loose files, uncompressed BSA entries and the default
// implementation (a File that implements only open()), and Manager::getSize and Manager::readRange. Written from
// the spec's "Interface fixed for implementation":
//   - readRange(offset, size, destination) is valid when offset <= size() and size <= size() - offset;
//   - a zero-length read is valid at any offset up to and including size();
//   - anything else, and any short read, throws std::runtime_error naming the file;
//   - nothing is read or written outside [destination, destination + size);
//   - it is safe to call from many threads at once on the same object.

#include "vfsfakes.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        constexpr std::uint64_t maxOffset = std::numeric_limits<std::uint64_t>::max();
        constexpr std::size_t maxSize = std::numeric_limits<std::size_t>::max();

        enum class Kind
        {
            Loose,
            Bsa,
            OpenOnly,
        };

        struct Subject
        {
            std::string mContent;
            std::filesystem::path mPath; // the file on disk (loose file or archive), empty for OpenOnly
            std::unique_ptr<Archive> mArchive; // keeps BSA entries alive
            std::unique_ptr<File> mOwned;
            File* mFile = nullptr;
            OpenOnlyFile* mOpenOnly = nullptr;
            std::size_t mBytesAfterTarget = 0; // BSA only: size of the entry stored after the target
        };

        constexpr std::size_t guardSize = 16;
        constexpr unsigned char guardByte = 0xAB;

        // Reads through readRange into a buffer with guard bytes around the destination and checks the guards.
        std::string readChecked(File& file, std::uint64_t offset, std::size_t size)
        {
            std::vector<unsigned char> buffer(size + 2 * guardSize, guardByte);
            file.readRange(offset, size, buffer.data() + guardSize);
            for (std::size_t i = 0; i < guardSize; ++i)
            {
                EXPECT_EQ(buffer[i], guardByte) << "guard before the destination was written, offset " << offset
                                                << " size " << size;
                EXPECT_EQ(buffer[guardSize + size + i], guardByte)
                    << "guard after the destination was written, offset " << offset << " size " << size;
            }
            return std::string(reinterpret_cast<const char*>(buffer.data()) + guardSize, size);
        }

        std::string readWithOpen(File& file, std::uint64_t offset, std::size_t size)
        {
            const Files::IStreamPtr stream = file.open();
            stream->seekg(static_cast<std::streamoff>(offset));
            std::string out(size, '\0');
            stream->read(out.data(), static_cast<std::streamsize>(size));
            EXPECT_EQ(static_cast<std::size_t>(stream->gcount()), size);
            return out;
        }

        struct VFSRangeTest : TestWithParam<Kind>
        {
            ScratchDir mTemp;
            unsigned mCounter = 0;

            Subject make(std::size_t size)
            {
                Subject subject;
                subject.mContent = patternContent(size, 5);
                const std::string id = std::to_string(mCounter++);
                switch (GetParam())
                {
                    case Kind::Loose:
                    {
                        subject.mPath = mTemp.path() / ("loose" + id + ".bin");
                        writeFile(subject.mPath, subject.mContent);
                        subject.mOwned = std::make_unique<FileSystemArchiveFile>(subject.mPath);
                        subject.mFile = subject.mOwned.get();
                        break;
                    }
                    case Kind::Bsa:
                    {
                        subject.mPath = mTemp.path() / ("pack" + id + ".bsa");
                        const std::string after = patternContent(77, 11);
                        writeBsa(subject.mPath,
                            { { "pre.bin", patternContent(123, 9) }, { "target.bin", subject.mContent },
                                { "post.bin", after } });
                        subject.mBytesAfterTarget = after.size();
                        subject.mArchive = makeBsaArchive(subject.mPath, nullptr);
                        const FileMap files = listFiles(*subject.mArchive);
                        subject.mFile = files.at(Path::Normalized("target.bin"));
                        break;
                    }
                    case Kind::OpenOnly:
                    {
                        auto file = std::make_unique<OpenOnlyFile>(subject.mContent);
                        subject.mOpenOnly = file.get();
                        subject.mOwned = std::move(file);
                        subject.mFile = subject.mOwned.get();
                        break;
                    }
                }
                return subject;
            }

            // True if the message names the file in one of the ways this kind of file can: loose files by their
            // file name, archive entries by the archive's file name or the entry name.
            bool namesTheFile(const Subject& subject, const std::string& message) const
            {
                switch (GetParam())
                {
                    case Kind::Loose:
                    case Kind::Bsa:
                    {
                        const std::string fileName = subject.mPath.filename().string();
                        return message.find(fileName) != std::string::npos
                            || (GetParam() == Kind::Bsa && message.find("target.bin") != std::string::npos);
                    }
                    case Kind::OpenOnly:
                        return true; // such a file has no name of its own to report
                }
                return false;
            }
        };

        std::string kindName(const TestParamInfo<Kind>& info)
        {
            switch (info.param)
            {
                case Kind::Loose:
                    return "LooseFile";
                case Kind::Bsa:
                    return "BsaEntry";
                case Kind::OpenOnly:
                    return "DefaultImplementation";
            }
            return "Unknown";
        }

        TEST_P(VFSRangeTest, sizeShouldBeTheContentSize)
        {
            for (const std::size_t size : { std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(1000),
                     std::size_t(65536), std::size_t(100000) })
            {
                Subject subject = make(size);
                EXPECT_EQ(subject.mFile->size(), size) << size;
            }
        }

        TEST_P(VFSRangeTest, defaultSizeShouldOpenAStream)
        {
            if (GetParam() != Kind::OpenOnly)
                GTEST_SKIP() << "only the default implementation counts opens";
            Subject subject = make(500);
            ASSERT_EQ(subject.mOpenOnly->openCount(), 0u);
            EXPECT_EQ(subject.mFile->size(), 500u);
            EXPECT_GE(subject.mOpenOnly->openCount(), 1u);
            const std::size_t before = subject.mOpenOnly->openCount();
            EXPECT_EQ(readChecked(*subject.mFile, 10, 20), subject.mContent.substr(10, 20));
            EXPECT_GT(subject.mOpenOnly->openCount(), before);
        }

        TEST_P(VFSRangeTest, wholeContentShouldBeReadInOneCall)
        {
            Subject subject = make(1000);
            EXPECT_EQ(readChecked(*subject.mFile, 0, 1000), subject.mContent);
        }

        TEST_P(VFSRangeTest, largeContentShouldBeReadAcrossInternalBufferBoundaries)
        {
            Subject subject = make(200000);
            EXPECT_EQ(readChecked(*subject.mFile, 0, 200000), subject.mContent);
            EXPECT_EQ(readChecked(*subject.mFile, 12345, 130000), subject.mContent.substr(12345, 130000));
            EXPECT_EQ(readChecked(*subject.mFile, 199990, 10), subject.mContent.substr(199990, 10));
        }

        TEST_P(VFSRangeTest, rangesInsideTheContentShouldBeRead)
        {
            Subject subject = make(1000);
            struct Range
            {
                std::uint64_t mOffset;
                std::size_t mSize;
            };
            for (const Range range : { Range{ 0, 1 }, Range{ 0, 999 }, Range{ 1, 999 }, Range{ 500, 250 },
                     Range{ 999, 1 }, Range{ 123, 1 }, Range{ 256, 256 }, Range{ 4, 4 } })
                EXPECT_EQ(readChecked(*subject.mFile, range.mOffset, range.mSize),
                    subject.mContent.substr(static_cast<std::size_t>(range.mOffset), range.mSize))
                    << range.mOffset << "+" << range.mSize;
        }

        TEST_P(VFSRangeTest, rangeEndingExactlyAtTheEndShouldBeRead)
        {
            Subject subject = make(1000);
            EXPECT_EQ(readChecked(*subject.mFile, 990, 10), subject.mContent.substr(990));
            EXPECT_EQ(readChecked(*subject.mFile, 999, 1), subject.mContent.substr(999));
            EXPECT_EQ(readChecked(*subject.mFile, 0, 1000), subject.mContent);
        }

        TEST_P(VFSRangeTest, zeroLengthReadsShouldBeValidUpToAndIncludingTheSize)
        {
            Subject subject = make(1000);
            for (const std::uint64_t offset : { std::uint64_t(0), std::uint64_t(1), std::uint64_t(500),
                     std::uint64_t(999), std::uint64_t(1000) })
                EXPECT_EQ(readChecked(*subject.mFile, offset, 0), "") << offset;
        }

        TEST_P(VFSRangeTest, zeroLengthReadPastTheEndShouldThrow)
        {
            Subject subject = make(1000);
            char byte = 0;
            EXPECT_THROW(subject.mFile->readRange(1001, 0, &byte), std::runtime_error);
            EXPECT_THROW(subject.mFile->readRange(1000000, 0, &byte), std::runtime_error);
            EXPECT_THROW(subject.mFile->readRange(maxOffset, 0, &byte), std::runtime_error);
            EXPECT_EQ(byte, 0);
        }

        TEST_P(VFSRangeTest, emptyFileShouldAllowOnlyTheEmptyRead)
        {
            Subject subject = make(0);
            char byte = 0;
            EXPECT_EQ(subject.mFile->size(), 0u);
            EXPECT_NO_THROW(subject.mFile->readRange(0, 0, &byte));
            EXPECT_THROW(subject.mFile->readRange(0, 1, &byte), std::runtime_error);
            EXPECT_THROW(subject.mFile->readRange(1, 0, &byte), std::runtime_error);
            EXPECT_THROW(subject.mFile->readRange(1, 1, &byte), std::runtime_error);
            EXPECT_EQ(byte, 0);
        }

        TEST_P(VFSRangeTest, readsPastTheEndShouldThrowAndNeverBeShort)
        {
            Subject subject = make(1000);
            // The buffer is larger than the content: a failing call may write some bytes before it notices, but
            // never more than the content has.
            std::vector<char> buffer(4096);
            struct Range
            {
                std::uint64_t mOffset;
                std::size_t mSize;
            };
            for (const Range range : { Range{ 1000, 1 }, Range{ 999, 2 }, Range{ 0, 1001 }, Range{ 500, 501 },
                     Range{ 1001, 1 }, Range{ 5000, 10 }, Range{ 0, 2000 } })
            {
                try
                {
                    subject.mFile->readRange(range.mOffset, range.mSize, buffer.data());
                    ADD_FAILURE() << "no exception for " << range.mOffset << "+" << range.mSize;
                }
                catch (const std::runtime_error& e)
                {
                    EXPECT_TRUE(namesTheFile(subject, e.what())) << e.what();
                }
            }
        }

        TEST_P(VFSRangeTest, offsetPlusSizeOverflowShouldThrow)
        {
            Subject subject = make(1000);
            std::vector<char> buffer(4096);
            struct Range
            {
                std::uint64_t mOffset;
                std::size_t mSize;
            };
            // The sum of offset and size wraps around in 64 bits in every one of these.
            for (const Range range : { Range{ maxOffset, 1 }, Range{ maxOffset, 2 }, Range{ maxOffset - 3, 8 },
                     Range{ maxOffset - 1, maxSize }, Range{ 1, maxSize }, Range{ 999, maxSize },
                     Range{ 0, maxSize }, Range{ 0, maxSize - 1 }, Range{ 500, maxSize - 100 },
                     Range{ maxOffset / 2 + 1, static_cast<std::size_t>(maxOffset / 2 + 1) },
                     Range{ maxOffset, maxSize } })
                EXPECT_THROW(subject.mFile->readRange(range.mOffset, range.mSize, buffer.data()), std::runtime_error)
                    << range.mOffset << "+" << range.mSize;
        }

        TEST_P(VFSRangeTest, resultShouldEqualTheOpenStreamBytesForManyWindows)
        {
            Subject subject = make(3000);
            const std::size_t sizes[] = { 1, 2, 3, 7, 64, 100, 511 };
            for (std::size_t offset = 0; offset < 3000; offset += 61)
                for (const std::size_t size : sizes)
                {
                    if (offset + size > 3000)
                        continue;
                    const std::string viaRange = readChecked(*subject.mFile, offset, size);
                    EXPECT_EQ(viaRange, readWithOpen(*subject.mFile, offset, size)) << offset << "+" << size;
                    EXPECT_EQ(viaRange, subject.mContent.substr(offset, size)) << offset << "+" << size;
                }
            EXPECT_EQ(readChecked(*subject.mFile, 0, 3000), readAll(*subject.mFile->open()));
        }

        TEST_P(VFSRangeTest, readingShouldNotDisturbOpenStreamsOrOtherReads)
        {
            Subject subject = make(2000);
            Files::IStreamPtr stream = subject.mFile->open();
            char first[100];
            stream->read(first, sizeof(first));
            ASSERT_EQ(stream->gcount(), 100);

            EXPECT_EQ(readChecked(*subject.mFile, 1500, 300), subject.mContent.substr(1500, 300));

            char second[100];
            stream->read(second, sizeof(second));
            ASSERT_EQ(stream->gcount(), 100);
            EXPECT_EQ(std::string(first, 100), subject.mContent.substr(0, 100));
            EXPECT_EQ(std::string(second, 100), subject.mContent.substr(100, 100));
        }

        TEST_P(VFSRangeTest, sizeShouldNotChangeAfterReads)
        {
            Subject subject = make(777);
            const std::uint64_t before = subject.mFile->size();
            (void)readChecked(*subject.mFile, 3, 100);
            EXPECT_EQ(subject.mFile->size(), before);
            EXPECT_EQ(before, 777u);
        }

        TEST_P(VFSRangeTest, fileThatShrinksAfterSizeWasAskedShouldThrowOnReadingTheMissingPart)
        {
            Subject subject = make(1000);
            ASSERT_EQ(subject.mFile->size(), 1000u);
            std::vector<unsigned char> buffer(1000);

            switch (GetParam())
            {
                case Kind::Loose:
                {
                    std::error_code ec;
                    std::filesystem::resize_file(subject.mPath, 400, ec);
                    ASSERT_FALSE(ec) << ec.message();
                    break;
                }
                case Kind::Bsa:
                {
                    // Cut the archive in the middle of the target entry (it is followed by one more entry).
                    std::error_code ec;
                    const std::uintmax_t full = std::filesystem::file_size(subject.mPath);
                    std::filesystem::resize_file(subject.mPath, full - subject.mBytesAfterTarget - 600, ec);
                    ASSERT_FALSE(ec) << ec.message();
                    break;
                }
                case Kind::OpenOnly:
                    subject.mOpenOnly->setContent(subject.mContent.substr(0, 400));
                    break;
            }

            try
            {
                subject.mFile->readRange(0, 1000, buffer.data());
                ADD_FAILURE() << "reading the whole file after it shrank did not throw";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_TRUE(namesTheFile(subject, e.what())) << e.what();
            }
            EXPECT_THROW(subject.mFile->readRange(900, 100, buffer.data()), std::runtime_error);
            // The part that is still there can still be read.
            EXPECT_NO_THROW(subject.mFile->readRange(0, 400, buffer.data()));
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), 400), subject.mContent.substr(0, 400));
        }

        TEST_P(VFSRangeTest, fileThatIsGoneShouldThrow)
        {
            if (GetParam() == Kind::OpenOnly)
                GTEST_SKIP() << "an in-memory file cannot disappear";
            Subject subject = make(100);
            subject.mFile->size(); // no error expected here
            char buffer[100];
            std::error_code ec;
            std::filesystem::remove(subject.mPath, ec);
            ASSERT_FALSE(ec) << ec.message();
            EXPECT_THROW(subject.mFile->readRange(0, 100, buffer), std::runtime_error);
        }

        TEST_P(VFSRangeTest, manyThreadsShouldReadTheSameObjectAtOnce)
        {
            Subject subject = make(50000);
            constexpr unsigned threadCount = 8;
            constexpr unsigned iterations = 400;
            std::atomic<unsigned> failures{ 0 };
            std::atomic<bool> go{ false };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < threadCount; ++t)
            {
                threads.emplace_back([&, t] {
                    std::mt19937 random(1234 + t);
                    std::vector<char> buffer(4096);
                    while (!go)
                        std::this_thread::yield();
                    for (unsigned i = 0; i < iterations; ++i)
                    {
                        const std::size_t size = random() % 4000;
                        const std::size_t offset = random() % (50000 - size + 1);
                        try
                        {
                            subject.mFile->readRange(offset, size, buffer.data());
                            if (std::memcmp(buffer.data(), subject.mContent.data() + offset, size) != 0)
                                ++failures;
                        }
                        catch (...)
                        {
                            ++failures;
                        }
                    }
                });
            }
            go = true;
            for (std::thread& thread : threads)
                thread.join();
            EXPECT_EQ(failures.load(), 0u);
        }

        INSTANTIATE_TEST_SUITE_P(
            Kinds, VFSRangeTest, Values(Kind::Loose, Kind::Bsa, Kind::OpenOnly), kindName);

        // ---- BSA specifics ----

        TEST(VFSRangeBsaTest, entriesShouldBeReadFromTheirOwnPlaceInTheArchive)
        {
            ScratchDir temp;
            const std::vector<BsaEntry> entries = { { "a.bin", patternContent(300, 1) },
                { "b.bin", patternContent(500, 2) }, { "c.bin", patternContent(200, 3) }, { "empty.bin", "" } };
            const auto path = writeBsa(temp.path() / "pack.bsa", entries);
            const std::unique_ptr<Archive> archive = makeBsaArchive(path, nullptr);
            const FileMap files = listFiles(*archive);
            ASSERT_EQ(files.size(), entries.size());

            for (const BsaEntry& entry : entries)
            {
                File& file = *files.at(Path::Normalized(entry.mName));
                EXPECT_EQ(file.size(), entry.mContent.size()) << entry.mName;
                EXPECT_EQ(readChecked(file, 0, entry.mContent.size()), entry.mContent) << entry.mName;
                if (entry.mContent.size() > 10)
                    EXPECT_EQ(readChecked(file, 5, 5), entry.mContent.substr(5, 5)) << entry.mName;
                // Reading one byte past the entry would read its neighbour in the archive: it must throw instead.
                std::vector<char> big(2048);
                EXPECT_THROW(file.readRange(0, entry.mContent.size() + 1, big.data()), std::runtime_error)
                    << entry.mName;
                EXPECT_THROW(file.readRange(entry.mContent.size(), 1, big.data()), std::runtime_error) << entry.mName;
            }
        }

        TEST(VFSRangeBsaTest, entryThatPointsPastTheEndOfTheArchiveShouldBeAnError)
        {
            ScratchDir temp;
            // Size field of the first (and only) entry changed to be larger than the archive.
            std::string bytes = makeBsaBytes({ { "a.bin", "12345678" } });
            const std::uint32_t tooBig = 100000;
            std::memcpy(bytes.data() + 12, &tooBig, 4);
            writeFile(temp.path() / "bad.bsa", bytes);

            // Either the archive is refused when it is opened (what the reader does today) or the read throws; in
            // no case may a read succeed or return bytes from outside the archive.
            auto act = [&] {
                const std::unique_ptr<Archive> archive = makeBsaArchive(temp.path() / "bad.bsa", nullptr);
                const FileMap files = listFiles(*archive);
                std::vector<char> buffer(tooBig);
                files.begin()->second->readRange(0, tooBig, buffer.data());
            };
            EXPECT_THROW(act(), std::runtime_error);
        }

        TEST(VFSRangeBsaTest, entryWithOffsetPastTheEndOfTheArchiveShouldBeAnError)
        {
            ScratchDir temp;
            std::string bytes = makeBsaBytes({ { "a.bin", "12345678" } });
            const std::uint32_t farAway = 0x7fffff00;
            std::memcpy(bytes.data() + 16, &farAway, 4);
            writeFile(temp.path() / "bad.bsa", bytes);

            auto act = [&] {
                const std::unique_ptr<Archive> archive = makeBsaArchive(temp.path() / "bad.bsa", nullptr);
                const FileMap files = listFiles(*archive);
                char buffer[8];
                files.begin()->second->readRange(0, 8, buffer);
            };
            EXPECT_THROW(act(), std::runtime_error);
        }

        TEST(VFSRangeLooseTest, readRangeShouldWorkOnAFileWithSpacesInItsName)
        {
            ScratchDir temp;
            const auto path = temp.path() / "name with spaces.bin";
            const std::string content = patternContent(300);
            writeFile(path, content);
            FileSystemArchiveFile file(path);

            EXPECT_EQ(file.size(), 300u);
            EXPECT_EQ(readChecked(file, 100, 100), content.substr(100, 100));
        }

        // ---- default implementation details ----

        TEST(VFSRangeDefaultTest, fakeFileWithAHashShouldStillUseTheDefaultRangeImplementation)
        {
            FakeFile file("hello world", makeHash(1));
            EXPECT_EQ(file.size(), 11u);
            EXPECT_EQ(readChecked(file, 6, 5), "world");
            EXPECT_EQ(readChecked(file, 11, 0), "");
            char buffer[16] = {};
            EXPECT_THROW(file.readRange(6, 6, buffer), std::runtime_error);
        }

        TEST(VFSRangeDefaultTest, contentHashShouldDefaultToNothing)
        {
            OpenOnlyFile file("x");
            EXPECT_FALSE(file.getContentHash().has_value());
            ScratchDir temp;
            writeFile(temp.path() / "x.bin", "x");
            FileSystemArchiveFile loose(temp.path() / "x.bin");
            EXPECT_FALSE(loose.getContentHash().has_value());
            FakeFile hashed("x", makeHash(9));
            ASSERT_TRUE(hashed.getContentHash().has_value());
            EXPECT_EQ(*hashed.getContentHash(), makeHash(9));
        }

        // ---- through the manager ----

        struct VFSRangeManagerTest : Test
        {
            ScratchDir mTemp;
            Manager mVfs;
            std::string mLooseX = patternContent(900, 1);
            std::string mBsaX = patternContent(700, 2);
            std::string mBsaY = patternContent(1200, 3);
            std::string mLooseZ = patternContent(64, 4);

            void SetUp() override
            {
                const auto data = mTemp.makeDir("data");
                writeBsa(data / "base.bsa", { { "X.BIN", mBsaX }, { "Sub\\Y.bin", mBsaY } });
                writeFile(data / "x.bin", mLooseX);
                writeFile(data / "z.bin", mLooseZ);
                registerAll(mVfs, { data }, { "base.bsa" });
            }
        };

        TEST_F(VFSRangeManagerTest, getSizeShouldBeTheSizeOfTheWinningFile)
        {
            EXPECT_EQ(mVfs.getSize(Path::NormalizedView("x.bin")), mLooseX.size());
            EXPECT_EQ(mVfs.getSize(Path::NormalizedView("sub/y.bin")), mBsaY.size());
            EXPECT_EQ(mVfs.getSize(Path::NormalizedView("z.bin")), mLooseZ.size());
        }

        TEST_F(VFSRangeManagerTest, readRangeShouldReadFromTheWinningFile)
        {
            std::string out(100, '\0');
            mVfs.readRange(Path::NormalizedView("x.bin"), 50, 100, out.data());
            EXPECT_EQ(out, mLooseX.substr(50, 100));

            mVfs.readRange(Path::NormalizedView("sub/y.bin"), 1100, 100, out.data());
            EXPECT_EQ(out, mBsaY.substr(1100, 100));
        }

        TEST_F(VFSRangeManagerTest, unknownNameShouldThrowRuntimeError)
        {
            char byte = 0;
            EXPECT_THROW(mVfs.getSize(Path::NormalizedView("nothere.bin")), std::runtime_error);
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("nothere.bin"), 0, 1, &byte), std::runtime_error);
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("nothere.bin"), 0, 0, &byte), std::runtime_error);
            EXPECT_THROW(mVfs.getSize(Path::NormalizedView("")), std::runtime_error);
            EXPECT_THROW(mVfs.getSize(Path::NormalizedView("../x.bin")), std::runtime_error);
        }

        TEST_F(VFSRangeManagerTest, rangeErrorsShouldThrowRuntimeError)
        {
            char buffer[64] = {};
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), 0, 65, buffer), std::runtime_error);
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), 65, 0, buffer), std::runtime_error);
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), maxOffset, 1, buffer), std::runtime_error);
            EXPECT_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), 1, maxSize, buffer), std::runtime_error);
            EXPECT_NO_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), 64, 0, buffer));
            EXPECT_NO_THROW(mVfs.readRange(Path::NormalizedView("z.bin"), 0, 64, buffer));
        }

        TEST_F(VFSRangeManagerTest, readRangeShouldEqualTheOpenStreamForEveryEntry)
        {
            const std::vector<std::string> names = listAllNames(mVfs);
            ASSERT_GE(names.size(), 4u); // x.bin, z.bin, sub/y.bin and base.bsa itself
            for (const std::string& name : names)
            {
                const Path::Normalized normalized(name);
                const std::string viaStream = readAll(mVfs.get(normalized));
                ASSERT_EQ(mVfs.getSize(normalized), viaStream.size()) << name;

                std::string viaRange(viaStream.size(), '\0');
                mVfs.readRange(normalized, 0, viaRange.size(), viaRange.data());
                EXPECT_EQ(viaRange, viaStream) << name;

                if (viaStream.size() > 20)
                {
                    std::string window(10, '\0');
                    mVfs.readRange(normalized, 7, window.size(), window.data());
                    EXPECT_EQ(window, viaStream.substr(7, 10)) << name;
                }
            }
        }

        TEST_F(VFSRangeManagerTest, caseAndSlashVariantsOfANameShouldReadTheSameRange)
        {
            std::string a(30, '\0');
            std::string b(30, '\0');
            mVfs.readRange(Path::Normalized("SUB\\Y.BIN"), 10, 30, a.data());
            mVfs.readRange(Path::NormalizedView("sub/y.bin"), 10, 30, b.data());
            EXPECT_EQ(a, b);
            EXPECT_EQ(a, mBsaY.substr(10, 30));
        }

        TEST(VFSRangeManagerFakeTest, thirdKindOfArchiveShouldBeReadableThroughTheManager)
        {
            Manager vfs;
            FakeArchive& fake = addFakeArchive(vfs, "fake");
            fake.add("a/b.txt", "0123456789");
            vfs.buildIndex();

            EXPECT_EQ(vfs.getSize(Path::NormalizedView("a/b.txt")), 10u);
            char out[4];
            vfs.readRange(Path::NormalizedView("a/b.txt"), 3, 4, out);
            EXPECT_EQ(std::string(out, 4), "3456");
            EXPECT_THROW(vfs.readRange(Path::NormalizedView("a/b.txt"), 8, 4, out), std::runtime_error);
        }
    }
}
