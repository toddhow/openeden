// Task 0083 stage 3: the Bethesda archive kinds (TES3 BSA, TES4-family BSA, BA2) are VFS archive factories registered
// by components/archivekinds, not code inside components/vfs. Written from the stage 3 specification: a factory decides
// by magic bytes and returns nullptr for a file of another kind; VFS::makeArchive knows only the added factories and
// the Eden pack reader; registerBethesdaArchiveKinds() adds the three and may be called twice.
//
// All archives are generated here as raw bytes. The last test reads the user's installed game data when the
// environment variable OPENEDEN_ARCHIVEKINDS_CHECK_DIRS names folders (read only, aggregates printed); without it the
// test is skipped.

#include "../vfs/vfsfakes.hpp"

#include <components/archivekinds/archivekinds.hpp>
#include <components/archivekinds/bsaarchive.hpp>
#include <components/vfs/archivefactory.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        void appendLe32(std::string& out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
        }

        // An archive with no files, in the compressed BSA layout: "BSA\0", the version, the folder record offset
        // (36, the end of the header), then the six other header numbers, all zero.
        std::string emptyTes4Bytes(std::uint32_t version)
        {
            std::string out("BSA\0", 4);
            appendLe32(out, version);
            appendLe32(out, 36);
            for (int i = 0; i < 6; ++i)
                appendLe32(out, 0);
            return out;
        }

        // A BA2 with no files: "BTDX", version 1, the type, a count of 0 and the name table at offset 24.
        std::string emptyBa2Bytes(const char* type)
        {
            std::string out("BTDX", 4);
            appendLe32(out, 1);
            out.append(type, 4);
            appendLe32(out, 0);
            appendLe32(out, 24);
            appendLe32(out, 0);
            return out;
        }

        ArchiveFactory neverClaims(int* asked = nullptr)
        {
            return
                [asked](const std::filesystem::path&, const ToUTF8::StatelessUtf8Encoder*) -> std::unique_ptr<Archive> {
                    if (asked != nullptr)
                        ++*asked;
                    return nullptr;
                };
        }

        struct ArchiveKindsTest : Test
        {
            ScratchDir mTemp;

            void SetUp() override { clearArchiveFactories(); }
            void TearDown() override { clearArchiveFactories(); }

            std::filesystem::path tes3(const char* name = "one.bsa")
            {
                return writeBsa(mTemp.path() / name, { { "Meshes/A.nif", "aaa" }, { "b.txt", "bb" } });
            }

            std::filesystem::path tes4(std::uint32_t version = 104, const char* name = "two.bsa")
            {
                writeFile(mTemp.path() / name, emptyTes4Bytes(version));
                return mTemp.path() / name;
            }

            std::filesystem::path ba2(const char* type = "GNRL", const char* name = "three.ba2")
            {
                writeFile(mTemp.path() / name, emptyBa2Bytes(type));
                return mTemp.path() / name;
            }

            std::filesystem::path text(const char* name = "words.bsa")
            {
                writeFile(mTemp.path() / name, "this is neither a BSA nor a BA2, whatever its name says");
                return mTemp.path() / name;
            }
        };

        // ---- the three factories, one by one ----

        TEST_F(ArchiveKindsTest, tes3FactoryShouldOpenAnUncompressedBsaAndListItsFiles)
        {
            const std::unique_ptr<Archive> archive = tes3ArchiveFactory()(tes3(), nullptr);

            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(archive->contains(Path::NormalizedView("meshes/a.nif")));
            EXPECT_TRUE(archive->contains(Path::NormalizedView("b.txt")));
            EXPECT_TRUE(archive->getDescription().starts_with("BSA: "));
            EXPECT_EQ(listFiles(*archive).size(), 2u);
        }

        TEST_F(ArchiveKindsTest, tes3FactoryShouldReturnNullptrForEveryOtherKind)
        {
            EXPECT_EQ(tes3ArchiveFactory()(tes4(), nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(tes4(105, "ssee.bsa"), nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(ba2(), nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(ba2("DX10", "tex.ba2"), nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(text(), nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, tes4FactoryShouldOpenTheVersionsTheReaderSupports)
        {
            for (const std::uint32_t version : { 103u, 104u, 105u })
            {
                const std::unique_ptr<Archive> archive = tes4ArchiveFactory()(tes4(version, "v.bsa"), nullptr);
                ASSERT_NE(archive, nullptr) << version;
                EXPECT_TRUE(archive->getDescription().starts_with("BSA: ")) << version;
            }
        }

        TEST_F(ArchiveKindsTest, tes4FactoryShouldClaimAnUnsupportedVersionAndLetTheReaderRefuseIt)
        {
            // The magic is right, so the file is of this kind; the reader's own error names it.
            const std::filesystem::path path = tes4(7, "old.bsa");

            try
            {
                (void)tes4ArchiveFactory()(path, nullptr);
                FAIL() << "an unsupported compressed BSA version was accepted";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("old.bsa"), std::string::npos) << e.what();
            }
        }

        TEST_F(ArchiveKindsTest, tes4FactoryShouldReturnNullptrForEveryOtherKind)
        {
            EXPECT_EQ(tes4ArchiveFactory()(tes3(), nullptr), nullptr);
            EXPECT_EQ(tes4ArchiveFactory()(ba2(), nullptr), nullptr);
            EXPECT_EQ(tes4ArchiveFactory()(text(), nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, ba2FactoryShouldOpenAGeneralArchive)
        {
            const std::unique_ptr<Archive> archive = ba2ArchiveFactory()(ba2(), nullptr);

            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(archive->getDescription().starts_with("BSA: "));
            EXPECT_TRUE(listFiles(*archive).empty());
        }

        TEST_F(ArchiveKindsTest, ba2FactoryShouldClaimATextureArchiveToo)
        {
            // A DX10 archive's own reader may refuse an empty one; what matters is that this factory answers (an
            // archive or an error naming the file), never nullptr.
            const std::filesystem::path path = ba2("DX10", "tex.ba2");
            try
            {
                EXPECT_NE(ba2ArchiveFactory()(path, nullptr), nullptr);
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("tex.ba2"), std::string::npos) << e.what();
            }
        }

        TEST_F(ArchiveKindsTest, ba2FactoryShouldReturnNullptrForEveryOtherKind)
        {
            EXPECT_EQ(ba2ArchiveFactory()(tes3(), nullptr), nullptr);
            EXPECT_EQ(ba2ArchiveFactory()(tes4(), nullptr), nullptr);
            EXPECT_EQ(ba2ArchiveFactory()(text(), nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, aBtdxFileOfAnUnknownTypeShouldBelongToNoFactory)
        {
            writeFile(mTemp.path() / "odd.ba2", emptyBa2Bytes("ZZZZ"));
            EXPECT_EQ(ba2ArchiveFactory()(mTemp.path() / "odd.ba2", nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(mTemp.path() / "odd.ba2", nullptr), nullptr);
            EXPECT_EQ(tes4ArchiveFactory()(mTemp.path() / "odd.ba2", nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, factoriesShouldDecideByMagicNotByExtension)
        {
            // A TES3 archive under a name that says nothing, a text file under names that say BSA and BA2.
            EXPECT_NE(tes3ArchiveFactory()(tes3("archive.dat"), nullptr), nullptr);
            EXPECT_EQ(tes3ArchiveFactory()(text("fake.bsa"), nullptr), nullptr);
            EXPECT_EQ(ba2ArchiveFactory()(text("fake.ba2"), nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, factoriesShouldReturnNullptrForAMissingOrTooShortFile)
        {
            writeFile(mTemp.path() / "tiny.bsa", "abc");
            const std::vector<ArchiveFactory> factories
                = { tes3ArchiveFactory(), tes4ArchiveFactory(), ba2ArchiveFactory() };
            for (const ArchiveFactory& factory : factories)
            {
                EXPECT_EQ(factory(mTemp.path() / "tiny.bsa", nullptr), nullptr);
                EXPECT_EQ(factory(mTemp.path() / "missing.bsa", nullptr), nullptr);
            }
        }

        TEST_F(ArchiveKindsTest, aBrokenFileOfAKnownKindShouldThrowAndNameTheFile)
        {
            // The compressed magic and version, and nothing after the first eight bytes plus a folder offset.
            writeFile(mTemp.path() / "cut-short.bsa", emptyTes4Bytes(104).substr(0, 12));

            try
            {
                (void)tes4ArchiveFactory()(mTemp.path() / "cut-short.bsa", nullptr);
                FAIL() << "a truncated compressed BSA was accepted";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("cut-short.bsa"), std::string::npos) << e.what();
            }
        }

        // ---- registration ----

        TEST_F(ArchiveKindsTest, nothingIsRegisteredUntilTheProgramAsks)
        {
            EXPECT_EQ(archiveFactoryCount(), 0u);
            EXPECT_THROW((void)makeArchive(tes3(), nullptr), std::runtime_error);
            EXPECT_THROW((void)makeArchive(tes4(), nullptr), std::runtime_error);
            EXPECT_THROW((void)makeArchive(ba2(), nullptr), std::runtime_error);
        }

        TEST_F(ArchiveKindsTest, unknownArchiveErrorShouldNameTheFile)
        {
            registerBethesdaArchiveKinds();
            try
            {
                (void)makeArchive(text("not-an-archive.bsa"), nullptr);
                FAIL() << "a text file was taken for an archive";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("not-an-archive.bsa"), std::string::npos) << e.what();
            }
        }

        TEST_F(ArchiveKindsTest, registeringTheKindsShouldMakeMakeArchiveOpenAllOfThem)
        {
            registerBethesdaArchiveKinds();

            const std::unique_ptr<Archive> a = makeArchive(tes3(), nullptr);
            const std::unique_ptr<Archive> b = makeArchive(tes4(), nullptr);
            const std::unique_ptr<Archive> c = makeArchive(ba2(), nullptr);

            ASSERT_NE(a, nullptr);
            ASSERT_NE(b, nullptr);
            ASSERT_NE(c, nullptr);
            EXPECT_TRUE(a->contains(Path::NormalizedView("b.txt")));
        }

        TEST_F(ArchiveKindsTest, registeringTwiceShouldAddNothingTheSecondTime)
        {
            registerBethesdaArchiveKinds();
            const std::size_t once = archiveFactoryCount();
            EXPECT_EQ(once, 3u);

            registerBethesdaArchiveKinds();

            EXPECT_EQ(archiveFactoryCount(), once);
        }

        TEST_F(ArchiveKindsTest, registeringAfterClearingShouldAddThemAgain)
        {
            registerBethesdaArchiveKinds();
            clearArchiveFactories();
            EXPECT_EQ(archiveFactoryCount(), 0u);

            registerBethesdaArchiveKinds();

            EXPECT_EQ(archiveFactoryCount(), 3u);
            EXPECT_NE(makeArchive(tes3(), nullptr), nullptr);
        }

        TEST_F(ArchiveKindsTest, registeredKindsShouldBeAskedAfterFactoriesAddedBefore)
        {
            int asked = 0;
            addArchiveFactory(neverClaims(&asked));
            registerBethesdaArchiveKinds();

            ASSERT_NE(makeArchive(tes3(), nullptr), nullptr);
            EXPECT_EQ(asked, 1);
        }

        TEST_F(ArchiveKindsTest, registerArchivesShouldOpenFallbackArchivesOfEveryKind)
        {
            registerBethesdaArchiveKinds();
            const std::filesystem::path data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "x.txt", "bsa" } });
            writeFile(data / "new.bsa", emptyTes4Bytes(104));
            writeFile(data / "fo4.ba2", emptyBa2Bytes("GNRL"));
            Manager vfs;

            registerAll(vfs, { data }, { "base.bsa", "new.bsa", "fo4.ba2" }, false);

            EXPECT_EQ(contentOf(vfs, "x.txt"), "bsa");
            EXPECT_EQ(vfs.getArchiveCount(), 3u);
        }

        TEST_F(ArchiveKindsTest, registerArchivesWithoutTheKindsShouldStopWithAnErrorNamingTheArchive)
        {
            const std::filesystem::path data = mTemp.makeDir("data");
            writeBsa(data / "base.bsa", { { "x.txt", "bsa" } });
            Manager vfs;

            try
            {
                const Files::Collections collections({ data });
                registerArchives(&vfs, collections, { "base.bsa" }, false, nullptr);
                FAIL() << "a BSA was opened with no archive kind registered";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("base.bsa"), std::string::npos) << e.what();
            }
        }

        // ---- the keyed registration in VFS ----

        TEST_F(ArchiveKindsTest, addArchiveFactoryOnceShouldIgnoreARepeatedKey)
        {
            int asked = 0;

            EXPECT_TRUE(addArchiveFactoryOnce("k", neverClaims(&asked)));
            EXPECT_FALSE(addArchiveFactoryOnce("k", neverClaims(&asked)));
            EXPECT_TRUE(addArchiveFactoryOnce("other", neverClaims(&asked)));
            EXPECT_EQ(archiveFactoryCount(), 2u);

            EXPECT_THROW((void)makeArchive(text(), nullptr), std::runtime_error);
            EXPECT_EQ(asked, 2);
        }

        TEST_F(ArchiveKindsTest, clearingShouldForgetTheKeys)
        {
            ASSERT_TRUE(addArchiveFactoryOnce("k", neverClaims()));
            clearArchiveFactories();

            EXPECT_TRUE(addArchiveFactoryOnce("k", neverClaims()));
        }

        TEST_F(ArchiveKindsTest, plainAddArchiveFactoryShouldNotUseKeysAndAlwaysAdd)
        {
            addArchiveFactory(neverClaims());
            addArchiveFactory(neverClaims());

            EXPECT_EQ(archiveFactoryCount(), 2u);
        }

        // ---- installed game data (read only; skipped unless asked for) ----

        TEST_F(ArchiveKindsTest, installedArchivesShouldOpenThroughTheFactoriesAndListWhatTheReadersList)
        {
            const char* dirs = std::getenv("OPENEDEN_ARCHIVEKINDS_CHECK_DIRS");
            if (dirs == nullptr)
            {
                GTEST_SKIP() << "OPENEDEN_ARCHIVEKINDS_CHECK_DIRS is not set";
            }
            registerBethesdaArchiveKinds();

            const std::string all(dirs);
            std::size_t start = 0;
            std::map<std::string, std::pair<unsigned, std::uint64_t>> perKind;
            while (start <= all.size())
            {
                const std::size_t end = all.find(';', start);
                const std::filesystem::path dir(all.substr(start, end == std::string::npos ? end : end - start));
                start = end == std::string::npos ? all.size() + 1 : end + 1;
                if (dir.empty() || !std::filesystem::is_directory(dir))
                {
                    continue;
                }
                for (const auto& entry : std::filesystem::directory_iterator(dir))
                {
                    const std::string ext = entry.path().extension().string();
                    if (ext != ".bsa" && ext != ".ba2")
                    {
                        continue;
                    }
                    // The reference: the reader chosen by the old rule (makeBsaArchive, kept in
                    // archivekinds/bsaarchive.hpp), against the one the factory list chooses.
                    const std::unique_ptr<Archive> reference = makeBsaArchive(entry.path(), nullptr);
                    const std::unique_ptr<Archive> viaFactory = makeArchive(entry.path(), nullptr);
                    ASSERT_NE(reference, nullptr);
                    ASSERT_NE(viaFactory, nullptr);
                    const FileMap a = listFiles(*reference);
                    const FileMap b = listFiles(*viaFactory);
                    EXPECT_EQ(a.size(), b.size()) << entry.path();
                    auto ia = a.begin();
                    auto ib = b.begin();
                    for (; ia != a.end() && ib != b.end(); ++ia, ++ib)
                    {
                        EXPECT_EQ(ia->first.value(), ib->first.value()) << entry.path();
                    }
                    auto& slot = perKind[ext];
                    ++slot.first;
                    slot.second += b.size();
                }
            }
            for (const auto& [ext, count] : perKind)
            {
                std::cout << "[archivekinds] " << ext << ": " << count.first << " archives, " << count.second
                          << " files\n";
            }
        }
    }
}
