// Task 0050, stage a: characterisation tests. They state how the virtual file system behaves today (priority,
// name matching, lookups) on generated folders and generated BSA archives. They use only the VFS API that exists at
// the fork point and must pass on unchanged code, before and after the later stages.

#include "vfstestutil.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        struct VFSCharacterisationTest : Test
        {
            ScratchDir mTemp;
            Manager mVfs;
        };

        // ---- folders ----

        TEST_F(VFSCharacterisationTest, laterFolderShouldBeatEarlierFolder)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeFile(first / "meshes" / "x.txt", "first");
            writeFile(second / "meshes" / "x.txt", "second");
            writeFile(first / "only" / "first.txt", "only first");
            writeFile(second / "only" / "second.txt", "only second");

            registerAll(mVfs, { first, second }, {});

            EXPECT_EQ(contentOf(mVfs, "meshes/x.txt"), "second");
            EXPECT_EQ(contentOf(mVfs, "only/first.txt"), "only first");
            EXPECT_EQ(contentOf(mVfs, "only/second.txt"), "only second");
        }

        TEST_F(VFSCharacterisationTest, folderOrderShouldDecideNotTheNames)
        {
            const auto a = mTemp.makeDir("a");
            const auto b = mTemp.makeDir("b");
            writeFile(a / "x.txt", "a");
            writeFile(b / "x.txt", "b");

            registerAll(mVfs, { b, a }, {});

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "a");
        }

        TEST_F(VFSCharacterisationTest, sameFolderListedTwiceShouldBeRegisteredOnce)
        {
            const auto a = mTemp.makeDir("a");
            writeFile(a / "x.txt", "a");

            registerAll(mVfs, { a, a }, {});

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "a");
            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "x.txt" });
        }

        TEST_F(VFSCharacterisationTest, withoutLooseFilesFoldersShouldNotBeRegistered)
        {
            const auto a = mTemp.makeDir("a");
            writeFile(a / "x.txt", "a");

            registerAll(mVfs, { a }, {}, false);

            EXPECT_FALSE(mVfs.exists(Path::Normalized("x.txt")));
            EXPECT_THAT(listAllNames(mVfs), IsEmpty());
        }

        // ---- archives ----

        TEST_F(VFSCharacterisationTest, laterArchiveShouldBeatEarlierArchive)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "one.bsa", { { "x.txt", "one" }, { "only1.txt", "o1" } });
            writeBsa(data / "two.bsa", { { "x.txt", "two" }, { "only2.txt", "o2" } });

            registerAll(mVfs, { data }, { "one.bsa", "two.bsa" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "two");
            EXPECT_EQ(contentOf(mVfs, "only1.txt"), "o1");
            EXPECT_EQ(contentOf(mVfs, "only2.txt"), "o2");
        }

        TEST_F(VFSCharacterisationTest, archiveListOrderShouldDecideNotTheNames)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "one.bsa", { { "x.txt", "one" } });
            writeBsa(data / "two.bsa", { { "x.txt", "two" } });

            registerAll(mVfs, { data }, { "two.bsa", "one.bsa" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "one");
        }

        TEST_F(VFSCharacterisationTest, archiveNamesShouldBeFoundWithoutRegardToCase)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "Mixed.BSA", { { "x.txt", "mixed" } });

            registerAll(mVfs, { data }, { "mixed.bsa" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "mixed");
        }

        TEST_F(VFSCharacterisationTest, missingArchiveShouldThrow)
        {
            const auto data = mTemp.makeDir("data");
            EXPECT_THROW(registerAll(mVfs, { data }, { "nothere.bsa" }), std::runtime_error);
        }

        TEST_F(VFSCharacterisationTest, fileThatIsNotAnArchiveShouldThrowWhenListedAsArchive)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "bad.bsa", "this is not an archive at all");

            EXPECT_THROW(registerAll(mVfs, { data }, { "bad.bsa" }), std::runtime_error);
        }

        // ---- folders against archives ----

        TEST_F(VFSCharacterisationTest, laterFolderShouldBeatArchiveInEarlierFolder)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeBsa(first / "base.bsa", { { "x.txt", "archive" }, { "archiveonly.txt", "ao" } });
            writeFile(second / "x.txt", "loose");

            registerAll(mVfs, { first, second }, { "base.bsa" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "loose");
            EXPECT_EQ(contentOf(mVfs, "archiveonly.txt"), "ao");
        }

        TEST_F(VFSCharacterisationTest, earlierFolderShouldStillBeatArchiveInLaterFolder)
        {
            // Every loose folder beats every archive, whatever folder the archive file lives in.
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeFile(first / "x.txt", "loose");
            writeBsa(second / "base.bsa", { { "x.txt", "archive" } });

            registerAll(mVfs, { first, second }, { "base.bsa" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "loose");
        }

        TEST_F(VFSCharacterisationTest, looseFileShouldBeatArchiveEntryOfDifferentCaseAndSlashes)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "Meshes\\Dir\\A.NIF", "archive" } });
            writeFile(data / "meshes" / "dir" / "a.nif", "loose");

            registerAll(mVfs, { data }, { "base.bsa" });

            EXPECT_EQ(contentOf(mVfs, "meshes/dir/a.nif"), "loose");
            EXPECT_EQ(listNames(mVfs, "meshes"), std::vector<std::string>{ "meshes/dir/a.nif" });
        }

        TEST_F(VFSCharacterisationTest, archivesWithoutFoldersShouldWork)
        {
            // Loose files off, archive on: the archive is the only source.
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "x.txt", "archive" } });
            writeFile(data / "x.txt", "loose");

            registerAll(mVfs, { data }, { "base.bsa" }, false);

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "archive");
            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "x.txt" });
        }

        // ---- names: case and slashes ----

        TEST_F(VFSCharacterisationTest, caseAndSlashDifferencesShouldBeTheSameNameForLooseFiles)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "Textures" / "Tex.DDS", "tex");

            registerAll(mVfs, { data }, {});

            EXPECT_EQ(contentOf(mVfs, "textures/tex.dds"), "tex");
            EXPECT_EQ(contentOf(mVfs, "TEXTURES/TEX.DDS"), "tex");
            EXPECT_EQ(contentOf(mVfs, "Textures\\Tex.dds"), "tex");
            EXPECT_EQ(contentOf(mVfs, "textures//tex.dds"), "tex");
            EXPECT_EQ(contentOf(mVfs, "/textures/tex.dds"), "tex");
            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "textures/tex.dds" });
        }

        TEST_F(VFSCharacterisationTest, caseAndSlashDifferencesShouldBeTheSameNameForArchiveEntries)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "Meshes\\X\\Foo.NIF", "foo" }, { "meshes/y/bar.nif", "bar" } });

            registerAll(mVfs, { data }, { "base.bsa" }, false);

            EXPECT_EQ(contentOf(mVfs, "meshes/x/foo.nif"), "foo");
            EXPECT_EQ(contentOf(mVfs, "MESHES\\X\\FOO.NIF"), "foo");
            EXPECT_EQ(contentOf(mVfs, "Meshes\\y/Bar.nif"), "bar");
            EXPECT_THAT(listAllNames(mVfs), ElementsAre("meshes/x/foo.nif", "meshes/y/bar.nif"));
        }

        TEST_F(VFSCharacterisationTest, entriesOfTwoArchivesDifferingOnlyInCaseShouldBeOneName)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "one.bsa", { { "Meshes\\A.nif", "one" } });
            writeBsa(data / "two.bsa", { { "meshes/a.NIF", "two" } });

            registerAll(mVfs, { data }, { "one.bsa", "two.bsa" }, false);

            EXPECT_EQ(contentOf(mVfs, "meshes/a.nif"), "two");
            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "meshes/a.nif" });
        }

        TEST_F(VFSCharacterisationTest, folderWithTwoFilesDifferingOnlyInCaseShouldKeepOneOfThem)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "dup.TXT", "upper");
            writeFile(data / "dup.txt", "lower");

            std::size_t onDisk = 0;
            for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(data))
                ++onDisk;
            if (onDisk < 2)
                GTEST_SKIP() << "this file system is case insensitive, two files differing only in case cannot exist";

            registerAll(mVfs, { data }, {});

            // FileSystemArchive warns and keeps whichever it finds first; which one that is depends on the file
            // system's iteration order, so only the number of names and the content being one of the two is stated.
            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "dup.txt" });
            EXPECT_THAT(contentOf(mVfs, "dup.txt"), AnyOf("upper", "lower"));
        }

        TEST_F(VFSCharacterisationTest, caseInsensitiveFileSystemShouldKeepTheDiskNameAsOneKey)
        {
            // Works on both kinds of file system: a single file whose name has capitals.
            const auto data = mTemp.makeDir("data");
            writeFile(data / "Only.TXT", "only");

            registerAll(mVfs, { data }, {});

            EXPECT_EQ(listAllNames(mVfs), std::vector<std::string>{ "only.txt" });
        }

        // ---- lookups ----

        TEST_F(VFSCharacterisationTest, missingNameShouldThrowFromGetAndBeNullFromFind)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "x.txt", "x");
            registerAll(mVfs, { data }, {});

            EXPECT_THROW(mVfs.get(Path::Normalized("nothere.txt")), std::runtime_error);
            EXPECT_THROW(mVfs.get(Path::NormalizedView("nothere.txt")), std::runtime_error);
            EXPECT_EQ(mVfs.find(Path::NormalizedView("nothere.txt")), nullptr);
            EXPECT_FALSE(mVfs.exists(Path::Normalized("nothere.txt")));
            EXPECT_FALSE(mVfs.exists(Path::NormalizedView("nothere.txt")));
            EXPECT_THROW(mVfs.getLastModified(Path::NormalizedView("nothere.txt")), std::runtime_error);
            EXPECT_THROW(mVfs.getStem(Path::NormalizedView("nothere.txt")), std::runtime_error);
            EXPECT_EQ(mVfs.getArchive(Path::Normalized("nothere.txt")), "");
        }

        TEST_F(VFSCharacterisationTest, existingNameShouldBeFoundByEveryOverload)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "x.txt", "x");
            registerAll(mVfs, { data }, {});

            const Path::Normalized name("x.txt");
            EXPECT_TRUE(mVfs.exists(name));
            EXPECT_TRUE(mVfs.exists(Path::NormalizedView(name)));
            EXPECT_NE(mVfs.find(Path::NormalizedView(name)), nullptr);
            EXPECT_EQ(readAll(mVfs.get(name)), "x");
            EXPECT_EQ(readAll(mVfs.get(Path::NormalizedView(name))), "x");
        }

        TEST_F(VFSCharacterisationTest, everyOpenShouldReturnAnIndependentStream)
        {
            const auto data = mTemp.makeDir("data");
            const std::string content = patternContent(5000);
            writeFile(data / "x.bin", content);
            writeBsa(data / "base.bsa", { { "y.bin", content }, { "z.bin", "zzz" } });
            registerAll(mVfs, { data }, { "base.bsa" });

            for (const char* name : { "x.bin", "y.bin" })
            {
                Files::IStreamPtr a = mVfs.get(Path::Normalized(name));
                Files::IStreamPtr b = mVfs.get(Path::Normalized(name));
                ASSERT_NE(a, nullptr);
                ASSERT_NE(b, nullptr);
                ASSERT_NE(a.get(), b.get());

                // Interleaved reads must not disturb each other.
                std::string fromA;
                std::string fromB;
                char buffer[100];
                for (std::size_t i = 0; i < content.size(); i += sizeof(buffer))
                {
                    a->read(buffer, sizeof(buffer));
                    fromA.append(buffer, static_cast<std::size_t>(a->gcount()));
                    b->read(buffer, sizeof(buffer));
                    fromB.append(buffer, static_cast<std::size_t>(b->gcount()));
                }
                EXPECT_EQ(fromA, content) << name;
                EXPECT_EQ(fromB, content) << name;
            }
        }

        TEST_F(VFSCharacterisationTest, archiveEntriesShouldNotReadIntoNeighbouringEntries)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "a.txt", "AAAA" }, { "b.txt", "BBBBBB" }, { "c.txt", "CC" } });
            registerAll(mVfs, { data }, { "base.bsa" }, false);

            EXPECT_EQ(contentOf(mVfs, "a.txt"), "AAAA");
            EXPECT_EQ(contentOf(mVfs, "b.txt"), "BBBBBB");
            EXPECT_EQ(contentOf(mVfs, "c.txt"), "CC");
        }

        // ---- getArchive ----

        TEST_F(VFSCharacterisationTest, getArchiveShouldDescribeTheWinningSource)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeBsa(first / "base.bsa", { { "both.txt", "b" }, { "archiveonly.txt", "a" } });
            writeFile(second / "both.txt", "loose");
            writeFile(first / "firstonly.txt", "f");

            registerAll(mVfs, { first, second }, { "base.bsa" });

            const std::string both = mVfs.getArchive(Path::Normalized("both.txt"));
            EXPECT_TRUE(both.starts_with("DIR: ")) << both;
            EXPECT_NE(both.find(second.filename().string()), std::string::npos) << both;

            const std::string archiveOnly = mVfs.getArchive(Path::Normalized("archiveonly.txt"));
            EXPECT_TRUE(archiveOnly.starts_with("BSA: ")) << archiveOnly;
            EXPECT_NE(archiveOnly.find("base.bsa"), std::string::npos) << archiveOnly;

            const std::string firstOnly = mVfs.getArchive(Path::Normalized("firstonly.txt"));
            EXPECT_TRUE(firstOnly.starts_with("DIR: ")) << firstOnly;
            EXPECT_NE(firstOnly.find(first.filename().string()), std::string::npos) << firstOnly;
        }

        // ---- directory ranges ----

        TEST_F(VFSCharacterisationTest, recursiveDirectoryRangeShouldListNamesInOrderOnce)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeBsa(first / "base.bsa", { { "Meshes\\b.nif", "1" }, { "Meshes\\sub\\c.nif", "2" }, { "x.txt", "3" } });
            writeFile(second / "meshes" / "a.nif", "4");
            writeFile(second / "meshes" / "b.nif", "5");
            writeFile(second / "textures" / "t.dds", "6");

            registerAll(mVfs, { first, second }, { "base.bsa" });

            EXPECT_THAT(listNames(mVfs, "meshes"), ElementsAre("meshes/a.nif", "meshes/b.nif", "meshes/sub/c.nif"));
            EXPECT_THAT(listNames(mVfs, "MESHES"), ElementsAre("meshes/a.nif", "meshes/b.nif", "meshes/sub/c.nif"));
            EXPECT_THAT(listNames(mVfs, "Meshes\\sub"), ElementsAre("meshes/sub/c.nif"));
            EXPECT_THAT(listNames(mVfs, "meshes/sub/"), ElementsAre("meshes/sub/c.nif"));
            EXPECT_THAT(listNames(mVfs, "textures"), ElementsAre("textures/t.dds"));
            EXPECT_THAT(listNames(mVfs, "nothere"), IsEmpty());

            // Empty prefix and the argument-less overload list everything (the archive files themselves are loose
            // files of their folder as well).
            const std::vector<std::string> all = listAllNames(mVfs);
            EXPECT_EQ(listNames(mVfs, ""), all);
            EXPECT_TRUE(std::is_sorted(all.begin(), all.end()));
            EXPECT_EQ(std::set<std::string>(all.begin(), all.end()).size(), all.size());
            EXPECT_THAT(all,
                IsSupersetOf({ "base.bsa", "meshes/a.nif", "meshes/b.nif", "meshes/sub/c.nif", "textures/t.dds",
                    "x.txt" }));
        }

        TEST_F(VFSCharacterisationTest, recursiveDirectoryRangeShouldAcceptNormalizedViews)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "meshes" / "a.nif", "1");
            writeFile(data / "textures" / "t.dds", "2");
            registerAll(mVfs, { data }, {});

            std::vector<std::string> names;
            for (const Path::Normalized& name : mVfs.getRecursiveDirectoryIterator(Path::NormalizedView("meshes")))
                names.push_back(name.value());
            EXPECT_THAT(names, ElementsAre("meshes/a.nif"));

            std::size_t count = 0;
            for ([[maybe_unused]] const Path::Normalized& name :
                mVfs.getRecursiveDirectoryIterator(Path::NormalizedView("")))
                ++count;
            EXPECT_EQ(count, 2u);
        }

        TEST_F(VFSCharacterisationTest, recursiveDirectoryRangeShouldMatchByPrefixNotByFolder)
        {
            // The range is every name that starts with the given text, so "meshes" also lists "meshes2/...".
            // This is today's behaviour (Manager::getRecursiveDirectoryIterator uses lower_bound and starts_with);
            // it is pinned here so that a change is a decision, not an accident.
            const auto data = mTemp.makeDir("data");
            writeFile(data / "meshes" / "a.nif", "1");
            writeFile(data / "meshes2" / "b.nif", "2");
            registerAll(mVfs, { data }, {});

            EXPECT_THAT(listNames(mVfs, "meshes"), ElementsAre("meshes/a.nif", "meshes2/b.nif"));
            EXPECT_THAT(listNames(mVfs, "meshes/"), ElementsAre("meshes/a.nif"));
        }

        TEST_F(VFSCharacterisationTest, DISABLED_recursiveDirectoryRangeForOnlySlashes)
        {
            // LATENT BUG, not a spec difference: Manager::getRecursiveDirectoryIterator(std::string_view) with a path
            // that normalizes to the empty string but is not empty ("/" or "//") reaches `++normalized.back()` on an
            // empty string. Disabled because that is undefined behaviour on today's code. A stage that touches this
            // function should enable it and make the answer "everything", like the empty path.
            const auto data = mTemp.makeDir("data");
            writeFile(data / "a.txt", "1");
            registerAll(mVfs, { data }, {});

            EXPECT_EQ(listNames(mVfs, "/"), listAllNames(mVfs));
        }

        // ---- time stamps and stems ----

        TEST_F(VFSCharacterisationTest, lastModifiedShouldBeTheFileSystemTimeOfTheWinner)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeFile(first / "x.txt", "first");
            writeFile(second / "x.txt", "second");
            writeFile(second / "y.txt", "y");
            const auto now = std::filesystem::file_time_type::clock::now();
            std::filesystem::last_write_time(first / "x.txt", now - std::chrono::hours(5));
            std::filesystem::last_write_time(second / "x.txt", now - std::chrono::hours(3));
            std::filesystem::last_write_time(second / "y.txt", now - std::chrono::hours(1));

            registerAll(mVfs, { first, second }, {});

            EXPECT_EQ(mVfs.getLastModified(Path::NormalizedView("x.txt")),
                std::filesystem::last_write_time(second / "x.txt"));
            EXPECT_EQ(mVfs.getLastModified(Path::NormalizedView("y.txt")),
                std::filesystem::last_write_time(second / "y.txt"));
            EXPECT_LT(mVfs.getLastModified(Path::NormalizedView("x.txt")),
                mVfs.getLastModified(Path::NormalizedView("y.txt")));
        }

        TEST_F(VFSCharacterisationTest, lastModifiedOfArchiveEntryShouldBeTheTimeOfTheArchiveFile)
        {
            const auto data = mTemp.makeDir("data");
            const auto bsa = writeBsa(data / "base.bsa", { { "x.txt", "x" } });
            std::filesystem::last_write_time(
                bsa, std::filesystem::file_time_type::clock::now() - std::chrono::hours(7));

            registerAll(mVfs, { data }, { "base.bsa" }, false);

            EXPECT_EQ(mVfs.getLastModified(Path::NormalizedView("x.txt")), std::filesystem::last_write_time(bsa));
        }

        TEST_F(VFSCharacterisationTest, stemShouldBeTheFileNameWithoutTheLastExtensionAndKeepItsCase)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "dir" / "Foo.Bar.TXT", "x");
            writeBsa(data / "base.bsa", { { "Meshes\\Dir\\Baz.NIF", "y" }, { "noext", "z" } });
            registerAll(mVfs, { data }, { "base.bsa" });

            EXPECT_EQ(mVfs.getStem(Path::NormalizedView("dir/foo.bar.txt")), "Foo.Bar");
            EXPECT_EQ(mVfs.getStem(Path::NormalizedView("meshes/dir/baz.nif")), "Baz");
            EXPECT_EQ(mVfs.getStem(Path::NormalizedView("noext")), "noext");
        }

        // ---- reset and rebuild ----

        TEST_F(VFSCharacterisationTest, resetShouldEmptyTheManagerAndRebuildingShouldGiveTheSameAnswers)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeFile(first / "x.txt", "first");
            writeFile(second / "x.txt", "second");

            registerAll(mVfs, { first, second }, {});
            const std::vector<std::string> before = listAllNames(mVfs);
            ASSERT_EQ(contentOf(mVfs, "x.txt"), "second");

            mVfs.reset();
            EXPECT_THAT(listAllNames(mVfs), IsEmpty());
            EXPECT_FALSE(mVfs.exists(Path::Normalized("x.txt")));

            registerAll(mVfs, { first, second }, {});
            EXPECT_EQ(listAllNames(mVfs), before);
            EXPECT_EQ(contentOf(mVfs, "x.txt"), "second");
        }

        TEST_F(VFSCharacterisationTest, buildingTheIndexTwiceShouldNotChangeAnything)
        {
            const auto first = mTemp.makeDir("first");
            const auto second = mTemp.makeDir("second");
            writeFile(first / "x.txt", "first");
            writeFile(second / "x.txt", "second");
            registerAll(mVfs, { first, second }, {});
            const std::vector<std::string> before = listAllNames(mVfs);

            mVfs.buildIndex();

            EXPECT_EQ(listAllNames(mVfs), before);
            EXPECT_EQ(contentOf(mVfs, "x.txt"), "second");
        }

        TEST_F(VFSCharacterisationTest, archivesAddedByHandShouldFollowRegistrationOrder)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "one.bsa", { { "x.txt", "bsa" } });
            writeFile(data / "folder" / "x.txt", "folder");

            // A folder registered before an archive is overridden by it: the order of addArchive decides, the kind
            // of source does not. (registerArchives happens to register archives before folders.)
            mVfs.addArchive(std::make_unique<FileSystemArchive>(data / "folder"));
            mVfs.addArchive(makeBsaArchive(data / "one.bsa", nullptr));
            mVfs.buildIndex();

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "bsa");
            EXPECT_TRUE(mVfs.getArchive(Path::Normalized("x.txt")).starts_with("BSA: "));
        }

        TEST_F(VFSCharacterisationTest, archiveObjectsShouldListAndContainTheirOwnFiles)
        {
            const auto data = mTemp.makeDir("data");
            writeBsa(data / "one.bsa", { { "Meshes\\A.nif", "a" }, { "b.txt", "b" } });
            writeFile(data / "folder" / "Sub" / "C.txt", "c");

            const std::unique_ptr<Archive> bsa = makeBsaArchive(data / "one.bsa", nullptr);
            FileSystemArchive folder(data / "folder");

            const FileMap bsaFiles = listFiles(*bsa);
            const FileMap folderFiles = listFiles(folder);

            EXPECT_EQ(bsaFiles.size(), 2u);
            EXPECT_TRUE(bsaFiles.contains(Path::NormalizedView("meshes/a.nif")));
            EXPECT_TRUE(bsaFiles.contains(Path::NormalizedView("b.txt")));
            EXPECT_TRUE(bsa->contains(Path::NormalizedView("meshes/a.nif")));
            EXPECT_FALSE(bsa->contains(Path::NormalizedView("sub/c.txt")));

            EXPECT_EQ(folderFiles.size(), 1u);
            EXPECT_TRUE(folderFiles.contains(Path::NormalizedView("sub/c.txt")));
            EXPECT_TRUE(folder.contains(Path::NormalizedView("sub/c.txt")));
            EXPECT_FALSE(folder.contains(Path::NormalizedView("meshes/a.nif")));

            EXPECT_TRUE(bsa->getDescription().starts_with("BSA: "));
            EXPECT_TRUE(folder.getDescription().starts_with("DIR: "));
        }
    }
}
