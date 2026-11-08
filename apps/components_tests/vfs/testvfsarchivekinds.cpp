// Task 0050, stage f: a third kind of archive next to folders and BSAs, and the archive factory list
// (components/vfs/archivefactory.hpp). The test-only "magic" archive is recognised by the first bytes of a file, never
// by its extension, and registered through the same fallback-archive list as BSAs, which is how packs will be
// registered later (task 0052). Written from the spec's "Interface fixed for implementation".

#include "vfsfakes.hpp"

#include <components/toutf8/toutf8.hpp>
#include <components/vfs/archivefactory.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        constexpr std::string_view packMagic = "FAKEPACK";

        // File layout: the 8 magic bytes, then one entry per line: "name|content".
        std::string makePackBytes(const std::vector<std::pair<std::string, std::string>>& entries)
        {
            std::string bytes(packMagic);
            for (const auto& [name, content] : entries)
                bytes += name + "|" + content + "\n";
            return bytes;
        }

        // The test-only third kind of archive. Its files carry a content hash (of their size) so that identical
        // duplicates can be tested; it is a FakeArchive that fills itself from a file.
        class MagicArchive : public FakeArchive
        {
        public:
            MagicArchive(const std::filesystem::path& path, const std::string& tag)
                : FakeArchive(tag + ": " + path.string())
            {
                std::ifstream stream(path, std::ios::binary);
                std::string all((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                std::istringstream lines(all.substr(packMagic.size()));
                std::string line;
                while (std::getline(lines, line))
                {
                    const std::size_t bar = line.find('|');
                    if (bar == std::string::npos)
                        throw std::runtime_error("broken test pack " + path.string());
                    const std::string content = line.substr(bar + 1);
                    add(line.substr(0, bar), content, makeHash(static_cast<std::uint8_t>(content.size())));
                }
            }
        };

        bool hasMagic(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            char head[8] = {};
            stream.read(head, sizeof(head));
            return stream.gcount() == 8 && std::string_view(head, 8) == packMagic;
        }

        ArchiveFactory magicFactory(std::vector<std::string>* log = nullptr, std::string tag = "PACK",
            const ToUTF8::StatelessUtf8Encoder** seenEncoder = nullptr)
        {
            return [log, tag, seenEncoder](const std::filesystem::path& path,
                       const ToUTF8::StatelessUtf8Encoder* encoder) -> std::unique_ptr<Archive> {
                if (log != nullptr)
                    log->push_back(tag + ":" + path.filename().string());
                if (seenEncoder != nullptr)
                    *seenEncoder = encoder;
                if (!hasMagic(path))
                    return nullptr;
                return std::make_unique<MagicArchive>(path, tag);
            };
        }

        struct VFSArchiveKindsTest : Test
        {
            ScratchDir mTemp;
            Manager mVfs;

            void SetUp() override { clearArchiveFactories(); }
            void TearDown() override { clearArchiveFactories(); }
        };

        // ---- a third kind of source in the manager ----

        TEST_F(VFSArchiveKindsTest, thirdKindShouldTakePartInPriorityInEveryRegistrationOrder)
        {
            const auto folder = mTemp.makeDir("folder");
            writeFile(folder / "x.txt", "folder");
            const auto bsaPath = writeBsa(mTemp.path() / "one.bsa", { { "x.txt", "bsa" } });

            // Six orders of folder (F), BSA (B) and fake archive (K): the last registered wins.
            const std::string orders[] = { "FBK", "FKB", "BFK", "BKF", "KFB", "KBF" };
            for (const std::string& order : orders)
            {
                Manager vfs;
                for (const char kind : order)
                {
                    if (kind == 'F')
                        vfs.addArchive(std::make_unique<FileSystemArchive>(folder));
                    else if (kind == 'B')
                        vfs.addArchive(makeBsaArchive(bsaPath, nullptr));
                    else
                    {
                        auto fake = std::make_unique<FakeArchive>("fake");
                        fake->add("x.txt", "fake");
                        vfs.addArchive(std::move(fake));
                    }
                }
                vfs.buildIndex();

                const std::string winner = order.back() == 'F' ? "folder" : order.back() == 'B' ? "bsa" : "fake";
                EXPECT_EQ(contentOf(vfs, "x.txt"), winner) << order;

                // The report lists all three, winner first, then the losers by decreasing priority.
                const std::vector<Override> overrides = vfs.getOverrides();
                ASSERT_EQ(overrides.size(), 1u) << order;
                EXPECT_EQ(overrides[0].mWinner.mArchive, 2u) << order;
                ASSERT_EQ(overrides[0].mLosers.size(), 2u) << order;
                EXPECT_EQ(overrides[0].mLosers[0].mArchive, 1u) << order;
                EXPECT_EQ(overrides[0].mLosers[1].mArchive, 0u) << order;
                const std::string prefix = order.back() == 'F' ? "DIR:" : order.back() == 'B' ? "BSA:" : "fake";
                EXPECT_EQ(vfs.getArchiveDescription(2).substr(0, 4), prefix) << order;
            }
        }

        TEST_F(VFSArchiveKindsTest, thirdKindShouldTakePartInCountsLikeTheOthers)
        {
            const auto folder = mTemp.makeDir("folder");
            writeFile(folder / "x.txt", "folder");
            writeFile(folder / "folderonly.txt", "f");
            const auto bsaPath = writeBsa(mTemp.path() / "one.bsa", { { "x.txt", "bsa" }, { "y.txt", "bsa" } });
            mVfs.addArchive(makeBsaArchive(bsaPath, nullptr));
            FakeArchive& fake = addFakeArchive(mVfs, "fake");
            fake.add("x.txt", "fake");
            fake.add("y.txt", "fake");
            mVfs.addArchive(std::make_unique<FileSystemArchive>(folder));
            mVfs.buildIndex();

            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 3u);
            // x.txt: folder wins over fake over bsa. y.txt: fake wins over bsa.
            EXPECT_EQ(counts[0].mOverridden, 2u); // bsa loses x and y
            EXPECT_EQ(counts[0].mOverriding, 0u);
            EXPECT_EQ(counts[1].mOverridden, 1u); // fake loses x
            EXPECT_EQ(counts[1].mOverriding, 1u); // and wins y
            EXPECT_EQ(counts[2].mOverridden, 0u);
            EXPECT_EQ(counts[2].mOverriding, 1u); // folder wins x
            EXPECT_EQ(counts[1].mDescription, "fake");
        }

        TEST_F(VFSArchiveKindsTest, thirdKindWithEqualHashesShouldBeAnIdenticalDuplicateNotAConflict)
        {
            FakeArchive& low = addFakeArchive(mVfs, "low");
            low.add("same.txt", "x", makeHash(4));
            low.add("different.txt", "x", makeHash(4));
            FakeArchive& high = addFakeArchive(mVfs, "high");
            high.add("same.txt", "x", makeHash(4));
            high.add("different.txt", "y", makeHash(5));
            mVfs.buildIndex();

            ASSERT_EQ(mVfs.getOverrides().size(), 2u);
            ASSERT_EQ(mVfs.getConflicts().size(), 1u);
            EXPECT_EQ(mVfs.getConflicts()[0].mPath.value(), "different.txt");
            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            EXPECT_EQ(counts[0].mIdenticalDuplicates, 1u);
            EXPECT_EQ(counts[1].mIdenticalDuplicates, 1u);
        }

        // ---- the factory list ----

        // Task 0083 stage 3: VFS no longer has a built-in Bethesda reader. This replaces the test that pinned the
        // fallback
        // ("makeArchiveShouldFallBackToBsaWhenNoFactoryIsAdded"); the Bethesda kinds are factories now.
        TEST_F(VFSArchiveKindsTest, makeArchiveShouldKnowNoBsaWhenNoFactoryIsAdded)
        {
            const auto path = writeBsa(mTemp.path() / "one.bsa", { { "A.TXT", "a" } });

            EXPECT_THROW((void)makeArchive(path, nullptr), std::runtime_error);

            registerBethesdaArchiveKinds();
            const std::unique_ptr<Archive> archive = makeArchive(path, nullptr);

            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(archive->contains(Path::NormalizedView("a.txt")));
            EXPECT_TRUE(archive->getDescription().starts_with("BSA: "));
        }

        TEST_F(VFSArchiveKindsTest, factoriesShouldBeAskedInTheOrderTheyWereAddedAndNullptrShouldFallThrough)
        {
            std::vector<std::string> log;
            addArchiveFactory([&log](const std::filesystem::path& path,
                                  const ToUTF8::StatelessUtf8Encoder*) -> std::unique_ptr<Archive> {
                log.push_back("first:" + path.filename().string());
                return nullptr;
            });
            addArchiveFactory(magicFactory(&log, "second"));
            addArchiveFactory(magicFactory(&log, "third"));

            writeFile(mTemp.path() / "p.pack", makePackBytes({ { "a.txt", "1" } }));
            const auto bsaPath = writeBsa(mTemp.path() / "b.bsa", { { "b.txt", "2" } });

            const std::unique_ptr<Archive> pack = makeArchive(mTemp.path() / "p.pack", nullptr);
            ASSERT_NE(pack, nullptr);
            // The second factory recognised it, so the third was never asked.
            EXPECT_THAT(log, ElementsAre("first:p.pack", "second:p.pack"));
            EXPECT_TRUE(pack->getDescription().starts_with("second: "));

            log.clear();
            registerBethesdaArchiveKinds();
            const std::unique_ptr<Archive> bsa = makeArchive(bsaPath, nullptr);
            ASSERT_NE(bsa, nullptr);
            // The test's factories did not recognise the BSA: every one was asked, in order, then the registered
            // Bethesda kinds took it.
            EXPECT_THAT(log, ElementsAre("first:b.bsa", "second:b.bsa", "third:b.bsa"));
            EXPECT_TRUE(bsa->getDescription().starts_with("BSA: "));
        }

        TEST_F(VFSArchiveKindsTest, firstFactoryThatRecognisesAFileShouldWin)
        {
            addArchiveFactory(magicFactory(nullptr, "early"));
            addArchiveFactory(magicFactory(nullptr, "late"));
            writeFile(mTemp.path() / "p.pack", makePackBytes({ { "a.txt", "1" } }));

            const std::unique_ptr<Archive> pack = makeArchive(mTemp.path() / "p.pack", nullptr);

            ASSERT_NE(pack, nullptr);
            EXPECT_TRUE(pack->getDescription().starts_with("early: ")) << pack->getDescription();
        }

        TEST_F(VFSArchiveKindsTest, factoryShouldReceiveThePathAndTheEncoderGivenToMakeArchive)
        {
            const ToUTF8::StatelessUtf8Encoder* seen = reinterpret_cast<const ToUTF8::StatelessUtf8Encoder*>(1);
            addArchiveFactory(magicFactory(nullptr, "pack", &seen));
            writeFile(mTemp.path() / "p.pack", makePackBytes({}));

            ASSERT_NE(makeArchive(mTemp.path() / "p.pack", nullptr), nullptr);
            EXPECT_EQ(seen, nullptr);
        }

        TEST_F(VFSArchiveKindsTest, unknownFileShouldThrowAndNameTheFile)
        {
            addArchiveFactory(magicFactory());
            writeFile(mTemp.path() / "mystery-file-name.xyz", "this is neither a pack nor a bsa");

            try
            {
                (void)makeArchive(mTemp.path() / "mystery-file-name.xyz", nullptr);
                FAIL() << "no exception for a file nobody recognises";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("mystery-file-name.xyz"), std::string::npos) << e.what();
            }
        }

        TEST_F(VFSArchiveKindsTest, tooShortFileShouldThrowAndNameTheFile)
        {
            writeFile(mTemp.path() / "tiny-file.bsa", "abc");

            try
            {
                (void)makeArchive(mTemp.path() / "tiny-file.bsa", nullptr);
                FAIL() << "no exception for a file that is too short for any header";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("tiny-file.bsa"), std::string::npos) << e.what();
            }
        }

        TEST_F(VFSArchiveKindsTest, factoryThatThrowsShouldStopTheSearchAndPropagate)
        {
            std::vector<std::string> log;
            addArchiveFactory([&log](const std::filesystem::path& path,
                                  const ToUTF8::StatelessUtf8Encoder*) -> std::unique_ptr<Archive> {
                log.push_back("broken:" + path.filename().string());
                throw std::runtime_error("broken pack " + path.filename().string());
            });
            addArchiveFactory(magicFactory(&log, "later"));
            writeFile(mTemp.path() / "p.pack", makePackBytes({}));

            try
            {
                (void)makeArchive(mTemp.path() / "p.pack", nullptr);
                FAIL() << "the factory's exception was swallowed";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("broken pack p.pack"), std::string::npos) << e.what();
            }
            EXPECT_THAT(log, ElementsAre("broken:p.pack"));
        }

        TEST_F(VFSArchiveKindsTest, clearingTheFactoriesShouldRemoveTheThirdKind)
        {
            addArchiveFactory(magicFactory());
            writeFile(mTemp.path() / "p.pack", makePackBytes({ { "a.txt", "1" } }));
            const auto bsaPath = writeBsa(mTemp.path() / "b.bsa", { { "b.txt", "2" } });
            ASSERT_NE(makeArchive(mTemp.path() / "p.pack", nullptr), nullptr);

            clearArchiveFactories();

            EXPECT_THROW((void)makeArchive(mTemp.path() / "p.pack", nullptr), std::runtime_error);
            registerBethesdaArchiveKinds();
            EXPECT_NE(makeArchive(bsaPath, nullptr), nullptr);
            // Clearing an empty list is fine.
            EXPECT_NO_THROW(clearArchiveFactories());
        }

        // ---- registered through fallback-archive, like BSAs ----

        TEST_F(VFSArchiveKindsTest, archiveRecognisedByContentShouldBeRegisteredThroughTheFallbackArchiveList)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            writeFile(data / "stuff.dat", makePackBytes({ { "x.txt", "pack" }, { "packonly.txt", "p" } }));
            writeBsa(data / "base.bsa", { { "x.txt", "bsa" }, { "bsaonly.txt", "b" } });

            registerAll(mVfs, { data }, { "base.bsa", "stuff.dat" }, false);

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "pack");
            EXPECT_EQ(contentOf(mVfs, "packonly.txt"), "p");
            EXPECT_EQ(contentOf(mVfs, "bsaonly.txt"), "b");
            EXPECT_TRUE(mVfs.getArchive(Path::Normalized("x.txt")).starts_with("PACK: "));
            EXPECT_TRUE(mVfs.getArchive(Path::Normalized("bsaonly.txt")).starts_with("BSA: "));
        }

        TEST_F(VFSArchiveKindsTest, laterEntryInTheListShouldWinWhetherItIsAPackOrABsa)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            writeFile(data / "stuff.dat", makePackBytes({ { "x.txt", "pack" } }));
            writeBsa(data / "base.bsa", { { "x.txt", "bsa" } });

            registerAll(mVfs, { data }, { "stuff.dat", "base.bsa" }, false);

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "bsa");
            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 1u);
            EXPECT_EQ(overrides[0].mWinner.mArchive, 1u);
            ASSERT_EQ(overrides[0].mLosers.size(), 1u);
            EXPECT_EQ(overrides[0].mLosers[0].mArchive, 0u);
        }

        TEST_F(VFSArchiveKindsTest, looseFoldersShouldBeatPacksLikeTheyBeatBsas)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            writeFile(data / "stuff.dat", makePackBytes({ { "x.txt", "pack" }, { "y.txt", "pack" } }));
            writeFile(data / "x.txt", "loose");

            registerAll(mVfs, { data }, { "stuff.dat" });

            EXPECT_EQ(contentOf(mVfs, "x.txt"), "loose");
            EXPECT_EQ(contentOf(mVfs, "y.txt"), "pack");
        }

        TEST_F(VFSArchiveKindsTest, recognitionShouldUseContentNotTheExtension)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            // A pack that has the extension of a BSA, and a BSA that has the extension of a pack.
            writeFile(data / "looks-like-bsa.bsa", makePackBytes({ { "from-pack.txt", "pack" } }));
            writeBsa(data / "looks-like-pack.dat", { { "from-bsa.txt", "bsa" } });

            registerAll(mVfs, { data }, { "looks-like-bsa.bsa", "looks-like-pack.dat" }, false);

            EXPECT_EQ(contentOf(mVfs, "from-pack.txt"), "pack");
            EXPECT_EQ(contentOf(mVfs, "from-bsa.txt"), "bsa");
            EXPECT_TRUE(mVfs.getArchive(Path::Normalized("from-pack.txt")).starts_with("PACK: "));
            EXPECT_TRUE(mVfs.getArchive(Path::Normalized("from-bsa.txt")).starts_with("BSA: "));
        }

        TEST_F(VFSArchiveKindsTest, withoutAFactoryAPackInTheListShouldBeAnErrorNamingTheFile)
        {
            const auto data = mTemp.makeDir("data");
            writeFile(data / "stuff-unreadable.dat", makePackBytes({ { "x.txt", "pack" } }));

            try
            {
                registerAll(mVfs, { data }, { "stuff-unreadable.dat" }, false);
                FAIL() << "a file of an unregistered kind was accepted";
            }
            catch (const std::runtime_error& e)
            {
                EXPECT_NE(std::string(e.what()).find("stuff-unreadable.dat"), std::string::npos) << e.what();
            }
        }

        TEST_F(VFSArchiveKindsTest, missingArchiveInTheListShouldStillThrowWhenFactoriesAreRegistered)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            EXPECT_THROW(registerAll(mVfs, { data }, { "nothere.dat" }), std::runtime_error);
        }

        TEST_F(VFSArchiveKindsTest, packFilesShouldBeReadableThroughTheRangeInterfaceByDefault)
        {
            addArchiveFactory(magicFactory());
            const auto data = mTemp.makeDir("data");
            writeFile(data / "stuff.dat", makePackBytes({ { "x.txt", "0123456789" } }));
            registerAll(mVfs, { data }, { "stuff.dat" }, false);

            EXPECT_EQ(mVfs.getSize(Path::NormalizedView("x.txt")), 10u);
            char out[3];
            mVfs.readRange(Path::NormalizedView("x.txt"), 4, 3, out);
            EXPECT_EQ(std::string(out, 3), "456");
        }
    }
}
