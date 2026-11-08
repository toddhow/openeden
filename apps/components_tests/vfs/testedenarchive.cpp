// Task 0052, stage g: the Eden Archive adapter (components/vfs/edenarchive.hpp) as a VFS::Archive. Written from the
// task text and the header alone, with packs made by the independent builder of edenarchive/testsupport (which shares
// no code with the library), so a mistake in the library's reader cannot hide in both sides of a test.
//
// What is checked: the factory's yes/no/throw behaviour, registration through the archive list (fallback-archive=),
// listing and lookup, priority against loose folders and other archives, File::size/open/readRange, the stored
// content hash, the conflict report, damage that stays inside one file, threads, and the archive description.

#include "vfsfakes.hpp"

#include <components/vfs/archivefactory.hpp>
#include <components/vfs/edenarchive.hpp>

#include <eacodec.hpp>
#include <eapackmodel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        std::string toString(const EdenTest::Bytes& bytes)
        {
            return std::string(bytes.begin(), bytes.end());
        }

        void writeBytes(const std::filesystem::path& path, const EdenTest::Bytes& bytes)
        {
            writeFile(path, toString(bytes));
        }

        EdenTest::EntrySpec fileEntry(
            std::string path, EdenTest::Bytes content, EdenTest::CodecChoice codec = EdenTest::CodecChoice::Store)
        {
            EdenTest::EntrySpec entry;
            entry.mPath = std::move(path);
            entry.mContent = std::move(content);
            entry.mCodec = codec;
            return entry;
        }

        // The pack used by most tests: several kinds of entry, chunked at 4096 bytes.
        struct SamplePack
        {
            EdenTest::Bytes mXTxt = EdenTest::bytesOf("pack x");
            EdenTest::Bytes mBig = EdenTest::patternBytes(12000, 21);
            EdenTest::Bytes mText = EdenTest::textBytes(9000);
            EdenTest::Bytes mHidden = EdenTest::bytesOf("only reachable by key");

            std::vector<std::string> names() const { return { "dir/big.bin", "dir/text.txt", "empty.txt", "x.txt" }; }

            EdenTest::PackSpec spec() const
            {
                EdenTest::PackSpec pack;
                pack.mChunkSize = 4096;
                pack.mEntries.push_back(fileEntry("x.txt", mXTxt));
                pack.mEntries.push_back(fileEntry("dir/big.bin", mBig));
                pack.mEntries.push_back(fileEntry("dir/text.txt", mText, EdenTest::CodecChoice::Zstd));
                pack.mEntries.push_back(fileEntry("empty.txt", {}));
                EdenTest::EntrySpec keyOnly;
                keyOnly.mKey = 0x77;
                keyOnly.mContent = mHidden;
                pack.mEntries.push_back(keyOnly);
                return pack;
            }

            EdenTest::Bytes bytes() const { return EdenTest::buildPack(spec()); }
        };

        struct VFSEdenArchiveTest : Test
        {
            ScratchDir mTemp;
            Manager mVfs;
            SamplePack mSample;

            void SetUp() override
            {
                clearArchiveFactories();
                registerBethesdaArchiveKinds();
                registerEdenArchiveFactory();
            }

            void TearDown() override { clearArchiveFactories(); }

            std::filesystem::path writePack(const std::filesystem::path& path, const EdenTest::Bytes& bytes)
            {
                writeBytes(path, bytes);
                return path;
            }

            std::unique_ptr<Archive> openSample(const std::string& name = "mod.pack")
            {
                const auto path = writePack(mTemp.path() / name, mSample.bytes());
                std::unique_ptr<Archive> archive = makeEdenArchive(path, nullptr);
                EXPECT_NE(archive, nullptr);
                return archive;
            }

            static FileMap listing(Archive& archive)
            {
                FileMap files;
                archive.listResources(files);
                return files;
            }

            static std::vector<std::string> namesOf(const FileMap& files)
            {
                std::vector<std::string> out;
                for (const auto& [name, file] : files)
                    out.push_back(name.value());
                std::sort(out.begin(), out.end());
                return out;
            }
        };

        // ---- the factory ----

        TEST_F(VFSEdenArchiveTest, factoryShouldReturnNullptrForFilesThatAreNotPacks)
        {
            const auto bsa = writeBsa(mTemp.path() / "base.bsa", { { "a.txt", "a" } });
            writeFile(mTemp.path() / "empty.dat", "");
            writeFile(mTemp.path() / "short.dat", "EDENP");
            writeFile(mTemp.path() / "text.dat", "this is just text, not an archive of any kind");
            writeFile(mTemp.path() / "wrongcase.dat", "edenpak\x01 and more bytes follow here, not a pack");
            writeFile(mTemp.path() / "almost.dat", "EDENPAJ\x01 and more bytes follow here, not a pack");

            for (const auto& path : { bsa, mTemp.path() / "empty.dat", mTemp.path() / "short.dat",
                     mTemp.path() / "text.dat", mTemp.path() / "wrongcase.dat", mTemp.path() / "almost.dat" })
            {
                SCOPED_TRACE(path.filename().string());
                std::unique_ptr<Archive> archive;
                EXPECT_NO_THROW(archive = makeEdenArchive(path, nullptr));
                EXPECT_EQ(archive, nullptr);
            }
        }

        TEST_F(VFSEdenArchiveTest, factoryShouldNotDecideByTheExtension)
        {
            // A pack with an unrelated extension is still a pack; a BSA with the pack extension is still not one.
            const auto pack = writePack(mTemp.path() / "pack-in-disguise.txt", mSample.bytes());
            EXPECT_NE(makeEdenArchive(pack, nullptr), nullptr);
            const auto bsa = writeBsa(mTemp.path() / "disguise.edenpak", { { "a.txt", "a" } });
            EXPECT_EQ(makeEdenArchive(bsa, nullptr), nullptr);
            const std::unique_ptr<Archive> viaList = makeArchive(bsa, nullptr);
            ASSERT_NE(viaList, nullptr);
            EXPECT_TRUE(viaList->getDescription().starts_with("BSA: "));
        }

        TEST_F(VFSEdenArchiveTest, factoryShouldClaimTheSevenBytesOfAnyGenerationAndThrowAnErrorNamingTheFile)
        {
            const EdenTest::Bytes good = mSample.bytes();

            struct Broken
            {
                std::string mName;
                EdenTest::Bytes mBytes;
            };
            std::vector<Broken> broken;
            {
                EdenTest::Bytes generation2 = good;
                generation2[7] = 2; // a later container generation: unsupported, not "not a pack"
                broken.push_back({ "generation2.pack", generation2 });
                EdenTest::Bytes generation0 = good;
                generation0[7] = 0;
                broken.push_back({ "generation0.pack", generation0 });
                broken.push_back({ "sevenbytes.pack", EdenTest::bytesOf("EDENPAK") });
                broken.push_back({ "truncated.pack", EdenTest::Bytes(good.begin(), good.begin() + 1000) });
                broken.push_back({ "cutfooter.pack", EdenTest::Bytes(good.begin(), good.end() - 16) });
                EdenTest::Bytes checksum = good;
                checksum[0x190] ^= 1; // a reserved header byte: the header checksum no longer matches
                broken.push_back({ "checksum.pack", checksum });
                EdenTest::Bytes version = good;
                version[10] = 2; // version 0.2
                broken.push_back({ "version.pack", version });
                broken.push_back({ "randombody.pack", [&] {
                                      EdenTest::Bytes random = EdenTest::patternBytes(5000, 8);
                                      std::copy(good.begin(), good.begin() + 8, random.begin());
                                      return random;
                                  }() });
            }
            for (const Broken& item : broken)
            {
                SCOPED_TRACE(item.mName);
                const auto path = writePack(mTemp.path() / item.mName, item.mBytes);
                try
                {
                    (void)makeEdenArchive(path, nullptr);
                    FAIL() << "a broken pack was accepted or ignored";
                }
                catch (const std::runtime_error& e)
                {
                    EXPECT_NE(std::string(e.what()).find(item.mName), std::string::npos) << e.what();
                }
                // And the same through the factory list.
                try
                {
                    (void)makeArchive(path, nullptr);
                    FAIL() << "makeArchive did not throw";
                }
                catch (const std::runtime_error& e)
                {
                    EXPECT_NE(std::string(e.what()).find(item.mName), std::string::npos) << e.what();
                }
            }
        }

        TEST_F(VFSEdenArchiveTest, makeArchiveShouldOpenPacksAndStillOpenBsas)
        {
            const auto packPath = writePack(mTemp.path() / "mod.pack", mSample.bytes());
            const auto bsaPath = writeBsa(mTemp.path() / "base.bsa", { { "b.txt", "b" } });

            const std::unique_ptr<Archive> pack = makeArchive(packPath, nullptr);
            ASSERT_NE(pack, nullptr);
            EXPECT_TRUE(pack->contains(Path::NormalizedView("x.txt")));
            const std::unique_ptr<Archive> bsa = makeArchive(bsaPath, nullptr);
            ASSERT_NE(bsa, nullptr);
            EXPECT_TRUE(bsa->getDescription().starts_with("BSA: "));
            EXPECT_TRUE(bsa->contains(Path::NormalizedView("b.txt")));
        }

        // ---- listing and lookup ----

        TEST_F(VFSEdenArchiveTest, listResourcesShouldListExactlyThePathEntriesAsNormalizedPaths)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            const FileMap files = listing(*archive);
            EXPECT_EQ(namesOf(files), mSample.names());
            for (const auto& [name, file] : files)
            {
                EXPECT_NE(file, nullptr) << name.value();
                EXPECT_TRUE(Path::isNormalized(name.value())) << name.value();
            }
            // The key-only entry has no name and so is not a resource.
            EXPECT_EQ(files.size(), 4u);
        }

        TEST_F(VFSEdenArchiveTest, containsShouldAgreeWithTheListing)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            const FileMap files = listing(*archive);
            for (const auto& [name, file] : files)
                EXPECT_TRUE(archive->contains(name)) << name.value();
            for (const char* missing :
                { "x", "x.tx", "x.txtx", "dir", "dir/big", "y.txt", "dir/other.bin", "empty.tx" })
                EXPECT_FALSE(archive->contains(Path::NormalizedView(missing))) << missing;
        }

        TEST_F(VFSEdenArchiveTest, anEmptyPackShouldListNothing)
        {
            const auto path = writePack(mTemp.path() / "empty.pack", EdenTest::buildPack(EdenTest::PackSpec{}));
            const std::unique_ptr<Archive> archive = makeEdenArchive(path, nullptr);
            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(listing(*archive).empty());
            EXPECT_FALSE(archive->contains(Path::NormalizedView("x.txt")));
        }

        TEST_F(VFSEdenArchiveTest, aPackWithOnlyKeyedEntriesShouldListNothing)
        {
            EdenTest::PackSpec spec;
            EdenTest::EntrySpec entry;
            entry.mKey = 5;
            entry.mContent = EdenTest::bytesOf("k");
            spec.mEntries.push_back(entry);
            const auto path = writePack(mTemp.path() / "keys.pack", EdenTest::buildPack(spec));
            const std::unique_ptr<Archive> archive = makeEdenArchive(path, nullptr);
            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(listing(*archive).empty());
        }

        // ---- registration and priority ----

        TEST_F(VFSEdenArchiveTest, aPackInTheArchiveListShouldProvideItsFilesLikeABsa)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "mod.pack", mSample.bytes());
            writeBsa(data / "base.bsa", { { "bsaonly.txt", "b" }, { "x.txt", "bsa x" } });

            registerAll(mVfs, { data }, { "base.bsa", "mod.pack" }, false);

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "pack x"); // the later archive wins
            EXPECT_EQ(contentOf(mVfs, "bsaonly.txt"), "b");
            EXPECT_EQ(contentOf(mVfs, "dir/big.bin"), toString(mSample.mBig));
            EXPECT_EQ(contentOf(mVfs, "dir/text.txt"), toString(mSample.mText));
            EXPECT_EQ(contentOf(mVfs, "empty.txt"), "");
            EXPECT_TRUE(mVfs.exists(Path::Normalized("empty.txt")));
            EXPECT_EQ(contentOf(mVfs, "hidden"), "<missing>");
            EXPECT_EQ(mVfs.getArchiveCount(), 2u);
            EXPECT_FALSE(mVfs.getArchive(Path::Normalized("x.txt")).empty());
        }

        TEST_F(VFSEdenArchiveTest, laterEntryInTheListShouldWinWhetherItIsAPackOrABsa)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "mod.pack", mSample.bytes());
            writeBsa(data / "base.bsa", { { "x.txt", "bsa x" } });

            Manager packLast;
            registerAll(packLast, { data }, { "base.bsa", "mod.pack" }, false);
            EXPECT_EQ(contentOf(packLast, "x.txt"), "pack x");

            Manager bsaLast;
            registerAll(bsaLast, { data }, { "mod.pack", "base.bsa" }, false);
            EXPECT_EQ(contentOf(bsaLast, "x.txt"), "bsa x");
            // Files only the pack has stay available either way.
            EXPECT_EQ(contentOf(bsaLast, "dir/big.bin"), toString(mSample.mBig));
        }

        TEST_F(VFSEdenArchiveTest, laterPackShouldOverrideAnEarlierPack)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "first.pack", mSample.bytes());
            EdenTest::PackSpec other;
            other.mEntries.push_back(fileEntry("x.txt", EdenTest::bytesOf("second pack x")));
            writePack(data / "second.pack", EdenTest::buildPack(other));

            registerAll(mVfs, { data }, { "first.pack", "second.pack" }, false);
            EXPECT_EQ(contentOf(mVfs, "x.txt"), "second pack x");
            EXPECT_EQ(contentOf(mVfs, "empty.txt"), "");
            EXPECT_EQ(contentOf(mVfs, "dir/big.bin"), toString(mSample.mBig));
        }

        TEST_F(VFSEdenArchiveTest, aLooseFolderShouldOverrideAPackAndAPackShouldOverrideAnEarlierFolder)
        {
            // registerArchives adds archives first and folders after them, so a folder wins over a pack.
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writePack(first / "mod.pack", mSample.bytes());
            writeFile(second / "x.txt", "loose x");
            writeFile(first / "dir" / "big.bin", "loose in the first folder");

            registerAll(mVfs, { first, second }, { "mod.pack" });
            EXPECT_EQ(contentOf(mVfs, "x.txt"), "loose x");
            // The first folder is registered after the pack too, so its loose file wins as well.
            EXPECT_EQ(contentOf(mVfs, "dir/big.bin"), "loose in the first folder");
            EXPECT_EQ(contentOf(mVfs, "dir/text.txt"), toString(mSample.mText));

            // Added by hand in the other order, the pack is the later source and wins.
            Manager vfs;
            vfs.addArchive(std::make_unique<FileSystemArchive>(second));
            vfs.addArchive(makeEdenArchive(first / "mod.pack", nullptr));
            vfs.buildIndex();
            EXPECT_EQ(contentOf(vfs, "x.txt"), "pack x");
        }

        // ---- File ----

        TEST_F(VFSEdenArchiveTest, fileSizeAndOpenShouldGiveTheUncompressedContent)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            FileMap files = listing(*archive);
            struct Want
            {
                const char* mName;
                const EdenTest::Bytes* mContent;
            };
            const EdenTest::Bytes empty;
            const Want wants[] = { { "x.txt", &mSample.mXTxt }, { "dir/big.bin", &mSample.mBig },
                { "dir/text.txt", &mSample.mText }, { "empty.txt", &empty } };
            for (const Want& want : wants)
            {
                SCOPED_TRACE(want.mName);
                File* file = files.at(Path::Normalized(want.mName));
                ASSERT_NE(file, nullptr);
                EXPECT_EQ(file->size(), want.mContent->size());
                EXPECT_EQ(readAll(file->open()), toString(*want.mContent));
                // Safe to open again and to read twice.
                EXPECT_EQ(readAll(file->open()), toString(*want.mContent));
            }
        }

        TEST_F(VFSEdenArchiveTest, openedStreamShouldSupportSeekingAcrossChunks)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            FileMap files = listing(*archive);
            File* file = files.at(Path::Normalized("dir/big.bin"));
            const std::string content = toString(mSample.mBig);
            const Files::IStreamPtr stream = file->open();
            for (const std::size_t at :
                { std::size_t{ 5000 }, std::size_t{ 4095 }, std::size_t{ 0 }, std::size_t{ 11990 } })
            {
                stream->clear();
                stream->seekg(static_cast<std::streamoff>(at));
                std::string got(10, '\0');
                stream->read(got.data(), 10);
                EXPECT_EQ(static_cast<std::size_t>(stream->gcount()), 10u) << at;
                EXPECT_EQ(got, content.substr(at, 10)) << at;
            }
        }

        TEST_F(VFSEdenArchiveTest, readRangeShouldFollowTheFileInterfaceRules)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            FileMap files = listing(*archive);
            const std::string content = toString(mSample.mBig); // 12000 bytes in chunks of 4096
            File* file = files.at(Path::Normalized("dir/big.bin"));
            ASSERT_EQ(file->size(), content.size());

            constexpr std::size_t guard = 8;
            const auto read = [&](File& f, std::uint64_t offset, std::size_t size) {
                std::vector<unsigned char> buffer(size + 2 * guard, 0xAB);
                f.readRange(offset, size, buffer.data() + guard);
                for (std::size_t i = 0; i < guard; ++i)
                {
                    EXPECT_EQ(buffer[i], 0xAB) << "write before the destination";
                    EXPECT_EQ(buffer[guard + size + i], 0xAB) << "write after the destination";
                }
                return std::string(reinterpret_cast<const char*>(buffer.data()) + guard, size);
            };

            for (const std::uint64_t offset : { 0u, 1u, 4095u, 4096u, 4097u, 8191u, 8192u, 11999u, 12000u })
            {
                for (const std::size_t size : { 0u, 1u, 2u, 4095u, 4096u, 4097u, 8192u, 12000u })
                {
                    if (offset + size > content.size())
                        continue;
                    EXPECT_EQ(read(*file, offset, size), content.substr(offset, size)) << offset << "+" << size;
                }
            }
            // Not valid: past the end, in any way.
            std::vector<unsigned char> destination(16, 0xCD);
            EXPECT_THROW(file->readRange(12001, 0, destination.data()), std::runtime_error);
            EXPECT_THROW(file->readRange(11999, 2, destination.data()), std::runtime_error);
            EXPECT_THROW(file->readRange(0, 12001, destination.data()), std::runtime_error);
            EXPECT_THROW(file->readRange(12000, 1, destination.data()), std::runtime_error);
            EXPECT_THROW(
                file->readRange(std::numeric_limits<std::uint64_t>::max(), 16, destination.data()), std::runtime_error);
            EXPECT_THROW(
                file->readRange(1, std::numeric_limits<std::size_t>::max(), destination.data()), std::runtime_error);
            for (const unsigned char byte : destination)
                EXPECT_EQ(byte, 0xCD) << "a rejected range must not write";

            // An empty entry: only the zero-length read at offset 0 is valid.
            File* empty = files.at(Path::Normalized("empty.txt"));
            EXPECT_EQ(empty->size(), 0u);
            EXPECT_NO_THROW(empty->readRange(0, 0, destination.data()));
            EXPECT_THROW(empty->readRange(0, 1, destination.data()), std::runtime_error);
            EXPECT_THROW(empty->readRange(1, 0, destination.data()), std::runtime_error);
        }

        TEST_F(VFSEdenArchiveTest, managerShouldAnswerSizeAndRangeForPackFiles)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "mod.pack", mSample.bytes());
            registerAll(mVfs, { data }, { "mod.pack" }, false);
            EXPECT_EQ(mVfs.getSize(Path::NormalizedView("dir/big.bin")), mSample.mBig.size());
            std::vector<unsigned char> buffer(100);
            mVfs.readRange(Path::NormalizedView("dir/big.bin"), 4090, 100, buffer.data());
            EXPECT_EQ(std::vector<unsigned char>(buffer.begin(), buffer.end()),
                std::vector<unsigned char>(mSample.mBig.begin() + 4090, mSample.mBig.begin() + 4190));
            EXPECT_THROW(
                mVfs.readRange(Path::NormalizedView("dir/big.bin"), 11950, 100, buffer.data()), std::runtime_error);
        }

        // ---- content hash and the conflict report ----

        TEST_F(VFSEdenArchiveTest, packFilesShouldCarryTheBlake3OfTheirContent)
        {
            const std::unique_ptr<Archive> archive = openSample();
            ASSERT_NE(archive, nullptr);
            FileMap files = listing(*archive);
            struct Want
            {
                const char* mName;
                const EdenTest::Bytes* mContent;
            };
            const EdenTest::Bytes empty;
            const Want wants[] = { { "x.txt", &mSample.mXTxt }, { "dir/big.bin", &mSample.mBig },
                { "dir/text.txt", &mSample.mText }, { "empty.txt", &empty } };
            for (const Want& want : wants)
            {
                SCOPED_TRACE(want.mName);
                const std::optional<ContentHash> hash = files.at(Path::Normalized(want.mName))->getContentHash();
                ASSERT_TRUE(hash.has_value());
                const EdenTest::Digest independent = EdenTest::blake3Of(*want.mContent);
                EXPECT_TRUE(std::equal(hash->begin(), hash->end(), independent.begin()));
            }
        }

        TEST_F(VFSEdenArchiveTest, looseFilesAndBsaEntriesShouldHaveNoStoredHash)
        {
            // Neither source stores a hash; only a pack does (file.hpp).
            const auto data = mTemp.makeDir("data");
            writeFile(data / "loose.txt", "loose");
            const auto bsaPath = writeBsa(data / "base.bsa", { { "b.txt", "b" } });

            FileSystemArchive folder(data);
            const FileMap looseFiles = listing(folder);
            ASSERT_TRUE(looseFiles.contains(Path::Normalized("loose.txt")));
            EXPECT_FALSE(looseFiles.at(Path::Normalized("loose.txt"))->getContentHash().has_value());

            const std::unique_ptr<Archive> bsa = makeBsaArchive(bsaPath, nullptr);
            ASSERT_NE(bsa, nullptr);
            FileMap bsaFiles;
            bsa->listResources(bsaFiles);
            ASSERT_TRUE(bsaFiles.contains(Path::Normalized("b.txt")));
            EXPECT_FALSE(bsaFiles.at(Path::Normalized("b.txt"))->getContentHash().has_value());
        }

        TEST_F(VFSEdenArchiveTest, equalContentInTwoPacksShouldBeAnIdenticalDuplicateNotAConflict)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "first.pack", mSample.bytes());
            EdenTest::PackSpec other;
            other.mChunkSize = 8192; // another chunking of the same bytes: the content hash does not depend on it
            other.mEntries.push_back(fileEntry("x.txt", mSample.mXTxt));
            other.mEntries.push_back(fileEntry("dir/big.bin", mSample.mBig, EdenTest::CodecChoice::Zstd));
            other.mEntries.push_back(fileEntry("dir/text.txt", EdenTest::bytesOf("different text")));
            writePack(data / "second.pack", EdenTest::buildPack(other));

            registerAll(mVfs, { data }, { "first.pack", "second.pack" }, false);

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 3u);
            std::set<std::string> identical;
            std::set<std::string> different;
            for (const Override& item : overrides)
                (item.mIdentical ? identical : different).insert(item.mPath.value());
            EXPECT_EQ(identical, (std::set<std::string>{ "dir/big.bin", "x.txt" }));
            EXPECT_EQ(different, (std::set<std::string>{ "dir/text.txt" }));
            const std::vector<Override> conflicts = mVfs.getConflicts();
            ASSERT_EQ(conflicts.size(), 1u);
            EXPECT_EQ(conflicts[0].mPath.value(), "dir/text.txt");
            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 2u);
            EXPECT_EQ(counts[0].mIdenticalDuplicates, 2u);
            EXPECT_EQ(counts[1].mIdenticalDuplicates, 2u);
        }

        TEST_F(VFSEdenArchiveTest, aPackEntryAndALooseFileWithTheSameContentShouldStillBeAConflict)
        {
            // A loose file has no stored hash (hashing it would read it whole), so the report cannot call the two
            // identical even if the bytes are equal; this is the behaviour the VFS tests of task 0050 fix for every
            // source without a hash.
            const auto data = mTemp.makeDir("data");
            writePack(data / "mod.pack", mSample.bytes());
            writeBytes(data / "x.txt", mSample.mXTxt);
            registerAll(mVfs, { data }, { "mod.pack" });

            const std::vector<Override> overrides = mVfs.getOverrides();
            const auto found = std::find_if(
                overrides.begin(), overrides.end(), [](const Override& item) { return item.mPath.value() == "x.txt"; });
            ASSERT_NE(found, overrides.end());
            EXPECT_FALSE(found->mIdentical);
            ASSERT_EQ(found->mLosers.size(), 1u);
            EXPECT_TRUE(found->mLosers[0].mFile->getContentHash().has_value()) << "the pack entry is the loser";
            EXPECT_FALSE(found->mWinner.mFile->getContentHash().has_value()) << "the loose file is the winner";
            EXPECT_EQ(mVfs.getConflicts().size(), overrides.size());
        }

        // ---- damage stays inside one file ----

        TEST_F(VFSEdenArchiveTest, aDamagedChunkShouldFailOnlyTheReadsOfThatFile)
        {
            EdenTest::PackSpec spec = mSample.spec();
            EdenTest::Bytes bytes = EdenTest::buildPack(spec);
            // Entries in order: dir/big.bin (3 chunks, block at 0x1000), dir/text.txt, empty.txt, x.txt, then the key.
            // Damage the middle chunk of dir/big.bin: bytes 4096 to 8191 of its content, stored at 0x1000 + 4096.
            bytes[0x1000 + 4096 + 17] ^= 0x40;
            const auto path = writePack(mTemp.path() / "damaged.pack", bytes);
            const std::unique_ptr<Archive> archive = makeEdenArchive(path, nullptr);
            ASSERT_NE(archive, nullptr) << "damage inside a chunk is not found at open";
            FileMap files = listing(*archive);
            File* big = files.at(Path::Normalized("dir/big.bin"));
            const std::string content = toString(mSample.mBig);

            EXPECT_EQ(big->size(), content.size()) << "size comes from the index";
            std::string got(100, '\0');
            for (int round = 0; round < 2; ++round)
            {
                EXPECT_NO_THROW(big->readRange(0, 100, got.data()));
                EXPECT_EQ(got, content.substr(0, 100));
                EXPECT_NO_THROW(big->readRange(9000, 100, got.data()));
                EXPECT_EQ(got, content.substr(9000, 100));
                EXPECT_THROW(big->readRange(4096, 100, got.data()), std::runtime_error);
                EXPECT_THROW(big->readRange(4090, 20, got.data()), std::runtime_error);
                std::string whole(content.size(), '\0');
                EXPECT_THROW(big->readRange(0, content.size(), whole.data()), std::runtime_error);
            }

            // Reading the whole damaged file through a stream must not give back the original bytes.
            bool failed = false;
            try
            {
                const Files::IStreamPtr stream = big->open();
                const std::string all = readAll(*stream);
                failed = all != content || stream->fail();
            }
            catch (const std::exception&)
            {
                failed = true;
            }
            EXPECT_TRUE(failed);

            // Every other file reads fine, before and after.
            EXPECT_EQ(readAll(files.at(Path::Normalized("x.txt"))->open()), toString(mSample.mXTxt));
            EXPECT_EQ(readAll(files.at(Path::Normalized("dir/text.txt"))->open()), toString(mSample.mText));
            EXPECT_EQ(readAll(files.at(Path::Normalized("empty.txt"))->open()), "");
            EXPECT_TRUE(files.at(Path::Normalized("x.txt"))->getContentHash().has_value());
        }

        // ---- threads ----

        TEST_F(VFSEdenArchiveTest, readRangeOnOneFileShouldBeSafeFromSeveralThreads)
        {
            const auto data = mTemp.makeDir("data");
            writePack(data / "mod.pack", mSample.bytes());
            registerAll(mVfs, { data }, { "mod.pack" }, false);

            struct Target
            {
                std::string mName;
                const EdenTest::Bytes* mContent;
            };
            const std::vector<Target> targets
                = { { "dir/big.bin", &mSample.mBig }, { "dir/text.txt", &mSample.mText }, { "x.txt", &mSample.mXTxt } };

            std::atomic<unsigned> failures{ 0 };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < 8; ++t)
            {
                threads.emplace_back([&, t] {
                    EdenTest::SplitMix gen(0xBEEF0000ull + t);
                    for (unsigned i = 0; i < 300; ++i)
                    {
                        const Target& target = targets[gen.below(targets.size())];
                        const EdenTest::Bytes& content = *target.mContent;
                        const std::uint64_t offset = gen.below(content.size() + 1);
                        const std::size_t size = gen.below(content.size() - offset + 1);
                        std::string got(size, '\0');
                        try
                        {
                            mVfs.readRange(Path::NormalizedView(target.mName.c_str()), offset, size, got.data());
                        }
                        catch (const std::exception&)
                        {
                            ++failures;
                            continue;
                        }
                        if (got != toString(content).substr(offset, size))
                            ++failures;
                        if (i % 40 == 0
                            && readAll(mVfs.find(Path::NormalizedView(target.mName.c_str()))) != toString(content))
                            ++failures;
                    }
                });
            }
            for (std::thread& thread : threads)
                thread.join();
            EXPECT_EQ(failures.load(), 0u);
        }

        // ---- description ----

        TEST_F(VFSEdenArchiveTest, descriptionShouldNameThePackFileAndNotUseTheRetiredShortName)
        {
            const std::unique_ptr<Archive> archive = openSample("my-mod.pack");
            ASSERT_NE(archive, nullptr);
            const std::string description = archive->getDescription();
            EXPECT_NE(description.find("my-mod.pack"), std::string::npos) << description;
            EXPECT_EQ(description.find("EdenPak"), std::string::npos) << description;
            EXPECT_EQ(description.find("EDENPAK"), std::string::npos) << description;
            std::string lower = description;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            EXPECT_EQ(lower.find("edenpak"), std::string::npos) << description;

            // The manager reports the same text.
            const auto data = mTemp.makeDir("data");
            writePack(data / "other.pack", mSample.bytes());
            registerAll(mVfs, { data }, { "other.pack" }, false);
            ASSERT_EQ(mVfs.getArchiveCount(), 1u);
            EXPECT_NE(mVfs.getArchiveDescription(0).find("other.pack"), std::string::npos);
        }
    }
}
