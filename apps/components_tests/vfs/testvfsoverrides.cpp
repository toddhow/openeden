// Task 0050, stage c: the conflict report (Manager::getOverrides, getConflicts, getOverrideCounts). Written from
// the spec's "Interface fixed for implementation". Uses in-memory archives so that hashes can be set.

#include "vfsfakes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        // A report entry without pointers, comparable between two managers that own different objects.
        struct Flat
        {
            std::string mPath;
            std::size_t mWinner;
            std::vector<std::size_t> mLosers;
            bool mIdentical;

            friend bool operator==(const Flat&, const Flat&) = default;
        };

        std::ostream& operator<<(std::ostream& stream, const Flat& value)
        {
            stream << value.mPath << " winner=" << value.mWinner << " losers=[";
            for (std::size_t loser : value.mLosers)
                stream << loser << ' ';
            return stream << "] identical=" << value.mIdentical;
        }

        std::vector<Flat> flatten(const std::vector<Override>& overrides)
        {
            std::vector<Flat> result;
            for (const Override& item : overrides)
            {
                Flat flat{ item.mPath.value(), item.mWinner.mArchive, {}, item.mIdentical };
                for (const Provider& loser : item.mLosers)
                    flat.mLosers.push_back(loser.mArchive);
                result.push_back(std::move(flat));
            }
            return result;
        }

        struct VFSOverridesTest : Test
        {
            Manager mVfs;
            std::vector<FakeArchive*> mArchives;

            FakeArchive& layer(const std::string& description)
            {
                mArchives.push_back(&addFakeArchive(mVfs, description));
                return *mArchives.back();
            }

            // Three layers with a path in all of them, one in the outer two, one in the lower two, one in a single
            // layer. No hashes.
            void buildThreeLayers()
            {
                FakeArchive& l0 = layer("layer0");
                FakeArchive& l1 = layer("layer1");
                FakeArchive& l2 = layer("layer2");
                l0.add("a.txt", "a0");
                l1.add("a.txt", "a1");
                l2.add("a.txt", "a2");
                l0.add("b.txt", "b0");
                l2.add("b.txt", "b2");
                l1.add("c.txt", "c1");
                l0.add("d.txt", "d0");
                l1.add("d.txt", "d1");
                mVfs.buildIndex();
            }
        };

        TEST_F(VFSOverridesTest, emptyManagerShouldHaveNoReport)
        {
            mVfs.buildIndex();
            EXPECT_THAT(mVfs.getOverrides(), IsEmpty());
            EXPECT_THAT(mVfs.getConflicts(), IsEmpty());
            EXPECT_THAT(mVfs.getOverrideCounts(), IsEmpty());
            EXPECT_EQ(mVfs.getArchiveCount(), 0u);
            EXPECT_THROW(mVfs.getArchiveDescription(0), std::out_of_range);
        }

        TEST_F(VFSOverridesTest, singleArchiveShouldHaveNoOverrides)
        {
            FakeArchive& only = layer("only");
            only.add("a.txt", "a");
            only.add("b.txt", "b");
            mVfs.buildIndex();

            EXPECT_THAT(mVfs.getOverrides(), IsEmpty());
            EXPECT_THAT(mVfs.getConflicts(), IsEmpty());
            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 1u);
            EXPECT_EQ(counts[0].mArchive, 0u);
            EXPECT_EQ(counts[0].mDescription, "only");
            EXPECT_EQ(counts[0].mOverridden, 0u);
            EXPECT_EQ(counts[0].mOverriding, 0u);
            EXPECT_EQ(counts[0].mIdenticalDuplicates, 0u);
        }

        TEST_F(VFSOverridesTest, archivesWithDisjointNamesShouldHaveNoOverrides)
        {
            layer("l0").add("a.txt", "a");
            layer("l1").add("b.txt", "b");
            layer("l2").add("c.txt", "c");
            mVfs.buildIndex();

            EXPECT_THAT(mVfs.getOverrides(), IsEmpty());
            EXPECT_THAT(mVfs.getConflicts(), IsEmpty());
            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 3u);
            for (const SourceOverrideCount& count : counts)
            {
                EXPECT_EQ(count.mOverridden, 0u);
                EXPECT_EQ(count.mOverriding, 0u);
                EXPECT_EQ(count.mIdenticalDuplicates, 0u);
            }
        }

        TEST_F(VFSOverridesTest, archiveCountAndDescriptionsShouldFollowRegistrationOrder)
        {
            layer("first");
            layer("second");
            layer("third");
            mVfs.buildIndex();

            EXPECT_EQ(mVfs.getArchiveCount(), 3u);
            EXPECT_EQ(mVfs.getArchiveDescription(0), "first");
            EXPECT_EQ(mVfs.getArchiveDescription(1), "second");
            EXPECT_EQ(mVfs.getArchiveDescription(2), "third");
            EXPECT_THROW(mVfs.getArchiveDescription(3), std::out_of_range);
            EXPECT_THROW(mVfs.getArchiveDescription(1000), std::out_of_range);
        }

        TEST_F(VFSOverridesTest, threeLayersShouldGiveWinnerAndLosersInPriorityOrderSortedByPath)
        {
            buildThreeLayers();

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 3u);

            EXPECT_EQ(overrides[0].mPath.value(), "a.txt");
            EXPECT_EQ(overrides[1].mPath.value(), "b.txt");
            EXPECT_EQ(overrides[2].mPath.value(), "d.txt");

            // a.txt is in all three layers: the last wins, the losers go from the highest priority down.
            EXPECT_EQ(overrides[0].mWinner.mArchive, 2u);
            EXPECT_EQ(overrides[0].mWinner.mFile, mArchives[2]->find("a.txt"));
            ASSERT_EQ(overrides[0].mLosers.size(), 2u);
            EXPECT_EQ(overrides[0].mLosers[0].mArchive, 1u);
            EXPECT_EQ(overrides[0].mLosers[0].mFile, mArchives[1]->find("a.txt"));
            EXPECT_EQ(overrides[0].mLosers[1].mArchive, 0u);
            EXPECT_EQ(overrides[0].mLosers[1].mFile, mArchives[0]->find("a.txt"));
            EXPECT_FALSE(overrides[0].mIdentical);

            // b.txt skips the middle layer.
            EXPECT_EQ(overrides[1].mWinner.mArchive, 2u);
            EXPECT_EQ(overrides[1].mWinner.mFile, mArchives[2]->find("b.txt"));
            ASSERT_EQ(overrides[1].mLosers.size(), 1u);
            EXPECT_EQ(overrides[1].mLosers[0].mArchive, 0u);
            EXPECT_EQ(overrides[1].mLosers[0].mFile, mArchives[0]->find("b.txt"));

            // d.txt is in the lower two layers.
            EXPECT_EQ(overrides[2].mWinner.mArchive, 1u);
            EXPECT_EQ(overrides[2].mWinner.mFile, mArchives[1]->find("d.txt"));
            ASSERT_EQ(overrides[2].mLosers.size(), 1u);
            EXPECT_EQ(overrides[2].mLosers[0].mArchive, 0u);

            // c.txt has one provider and is not reported.
            for (const Override& item : overrides)
            {
                EXPECT_NE(item.mPath.value(), "c.txt");
            }
        }

        TEST_F(VFSOverridesTest, winnerOfTheReportShouldBeTheFileTheManagerOpens)
        {
            buildThreeLayers();

            for (const Override& item : mVfs.getOverrides())
            {
                const Files::IStreamPtr stream = mVfs.get(item.mPath);
                ASSERT_NE(stream, nullptr);
                EXPECT_EQ(readAll(*stream), readAll(*item.mWinner.mFile->open())) << item.mPath;
                for (const Provider& loser : item.mLosers)
                {
                    EXPECT_NE(readAll(*loser.mFile->open()), readAll(*item.mWinner.mFile->open())) << item.mPath;
                }
            }
        }

        TEST_F(VFSOverridesTest, noHashesShouldMeanAConflictNotIdentical)
        {
            buildThreeLayers();

            EXPECT_EQ(mVfs.getConflicts().size(), 3u);
            EXPECT_EQ(flatten(mVfs.getConflicts()), flatten(mVfs.getOverrides()));
            for (const Override& item : mVfs.getOverrides())
            {
                EXPECT_FALSE(item.mIdentical) << item.mPath;
            }
        }

        TEST_F(VFSOverridesTest, namesThatDifferOnlyInCaseOrSlashesShouldBeOnePath)
        {
            layer("l0").add("Meshes\\Dir\\A.NIF", "0");
            layer("l1").add("meshes/dir/a.nif", "1");
            mVfs.buildIndex();

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 1u);
            EXPECT_EQ(overrides[0].mPath.value(), "meshes/dir/a.nif");
            EXPECT_EQ(overrides[0].mWinner.mArchive, 1u);
        }

        // ---- identical content ----

        TEST_F(VFSOverridesTest, equalHashesInAllProvidersShouldBeIdenticalAndNotAConflict)
        {
            layer("l0").add("same.txt", "x", makeHash(7));
            layer("l1").add("same.txt", "x", makeHash(7));
            layer("l2").add("same.txt", "x", makeHash(7));
            mVfs.buildIndex();

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 1u);
            EXPECT_TRUE(overrides[0].mIdentical);
            EXPECT_EQ(overrides[0].mWinner.mArchive, 2u);
            EXPECT_EQ(overrides[0].mLosers.size(), 2u);
            EXPECT_THAT(mVfs.getConflicts(), IsEmpty());
        }

        TEST_F(VFSOverridesTest, aProviderWithoutAHashShouldMakeItAConflict)
        {
            layer("l0").add("p.txt", "x", makeHash(7));
            layer("l1").add("p.txt", "x"); // no hash
            mVfs.buildIndex();

            ASSERT_EQ(mVfs.getOverrides().size(), 1u);
            EXPECT_FALSE(mVfs.getOverrides()[0].mIdentical);
            EXPECT_EQ(mVfs.getConflicts().size(), 1u);
        }

        TEST_F(VFSOverridesTest, aWinnerWithoutAHashShouldMakeItAConflict)
        {
            layer("l0").add("p.txt", "x", makeHash(7));
            layer("l1").add("p.txt", "x", makeHash(7));
            layer("l2").add("p.txt", "x"); // the winner has no hash
            mVfs.buildIndex();

            EXPECT_FALSE(mVfs.getOverrides()[0].mIdentical);
            EXPECT_EQ(mVfs.getConflicts().size(), 1u);
        }

        TEST_F(VFSOverridesTest, differentHashesShouldBeAConflict)
        {
            layer("l0").add("p.txt", "x", makeHash(1));
            layer("l1").add("p.txt", "y", makeHash(2));
            mVfs.buildIndex();

            ASSERT_EQ(mVfs.getOverrides().size(), 1u);
            EXPECT_FALSE(mVfs.getOverrides()[0].mIdentical);
            EXPECT_EQ(mVfs.getConflicts().size(), 1u);
        }

        TEST_F(VFSOverridesTest, oneDifferingHashAmongEqualOnesShouldBeAConflict)
        {
            layer("l0").add("p.txt", "x", makeHash(1));
            layer("l1").add("p.txt", "x", makeHash(1));
            layer("l2").add("p.txt", "z", makeHash(9));
            layer("l3").add("q.txt", "x", makeHash(1));
            layer("l4").add("q.txt", "x", makeHash(1));
            mVfs.buildIndex();

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 2u);
            EXPECT_EQ(overrides[0].mPath.value(), "p.txt");
            EXPECT_FALSE(overrides[0].mIdentical);
            EXPECT_EQ(overrides[1].mPath.value(), "q.txt");
            EXPECT_TRUE(overrides[1].mIdentical);

            const std::vector<Override> conflicts = mVfs.getConflicts();
            ASSERT_EQ(conflicts.size(), 1u);
            EXPECT_EQ(conflicts[0].mPath.value(), "p.txt");
        }

        TEST_F(VFSOverridesTest, hashesThatDifferInTheLastByteOnlyShouldBeAConflict)
        {
            VFS::ContentHash almost = makeHash(5);
            almost.back() ^= 1;
            layer("l0").add("p.txt", "x", makeHash(5));
            layer("l1").add("p.txt", "x", almost);
            mVfs.buildIndex();

            EXPECT_FALSE(mVfs.getOverrides()[0].mIdentical);
            EXPECT_EQ(mVfs.getConflicts().size(), 1u);
        }

        // ---- counts ----

        TEST_F(VFSOverridesTest, countsShouldBeGivenPerArchiveInRegistrationOrder)
        {
            buildThreeLayers();

            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 3u);
            for (std::size_t i = 0; i < counts.size(); ++i)
            {
                EXPECT_EQ(counts[i].mArchive, i);
                EXPECT_EQ(counts[i].mDescription, mVfs.getArchiveDescription(i));
                EXPECT_EQ(counts[i].mIdenticalDuplicates, 0u);
            }
            EXPECT_EQ(counts[0].mDescription, "layer0");

            // layer0 loses a, b and d; layer1 wins d and loses a; layer2 wins a and b.
            EXPECT_EQ(counts[0].mOverridden, 3u);
            EXPECT_EQ(counts[0].mOverriding, 0u);
            EXPECT_EQ(counts[1].mOverridden, 1u);
            EXPECT_EQ(counts[1].mOverriding, 1u);
            EXPECT_EQ(counts[2].mOverridden, 0u);
            EXPECT_EQ(counts[2].mOverriding, 2u);
        }

        TEST_F(VFSOverridesTest, identicalDuplicatesShouldBeCountedSeparatelyFromConflicts)
        {
            FakeArchive& l0 = layer("l0");
            FakeArchive& l1 = layer("l1");
            FakeArchive& l2 = layer("l2");
            l0.add("same.txt", "x", makeHash(3));
            l1.add("same.txt", "x", makeHash(3));
            l2.add("same.txt", "x", makeHash(3));
            l1.add("clash.txt", "1", makeHash(4));
            l2.add("clash.txt", "2", makeHash(5));
            mVfs.buildIndex();

            const std::vector<SourceOverrideCount> counts = mVfs.getOverrideCounts();
            ASSERT_EQ(counts.size(), 3u);
            EXPECT_EQ(counts[0].mIdenticalDuplicates, 1u);
            EXPECT_EQ(counts[1].mIdenticalDuplicates, 1u);
            EXPECT_EQ(counts[2].mIdenticalDuplicates, 1u);
            // Identical duplicates are neither overriding nor overridden.
            EXPECT_EQ(counts[0].mOverridden, 0u);
            EXPECT_EQ(counts[0].mOverriding, 0u);
            EXPECT_EQ(counts[1].mOverridden, 1u);
            EXPECT_EQ(counts[1].mOverriding, 0u);
            EXPECT_EQ(counts[2].mOverridden, 0u);
            EXPECT_EQ(counts[2].mOverriding, 1u);
        }

        TEST_F(VFSOverridesTest, totalsOfCountsShouldMatchTheConflictList)
        {
            buildThreeLayers();

            std::size_t overridden = 0;
            std::size_t overriding = 0;
            for (const SourceOverrideCount& count : mVfs.getOverrideCounts())
            {
                overridden += count.mOverridden;
                overriding += count.mOverriding;
            }
            std::size_t losers = 0;
            for (const Override& item : mVfs.getConflicts())
                losers += item.mLosers.size();
            EXPECT_EQ(overridden, losers);
            EXPECT_EQ(overriding, mVfs.getConflicts().size());
        }

        // ---- ordering and determinism ----

        TEST_F(VFSOverridesTest, reportShouldBeOrderedByPathWhateverTheInsertionOrder)
        {
            FakeArchive& l0 = layer("l0");
            FakeArchive& l1 = layer("l1");
            // Insert in a scrambled order.
            for (int i = 0; i < 300; ++i)
            {
                const int scrambled = (i * 37) % 300;
                const std::string name = std::format("dir{}/file{}.txt", scrambled % 7, scrambled);
                l1.add(name, "1");
                l0.add(name, "0");
            }
            mVfs.buildIndex();

            const std::vector<Override> overrides = mVfs.getOverrides();
            ASSERT_EQ(overrides.size(), 300u);
            for (std::size_t i = 1; i < overrides.size(); ++i)
            {
                EXPECT_LT(overrides[i - 1].mPath.value(), overrides[i].mPath.value());
            }
        }

        std::vector<Flat> buildScenario(bool reversed, std::vector<Flat>* conflictsOut = nullptr)
        {
            Manager vfs;
            auto make = [&](int which) {
                auto archive = std::make_unique<FakeArchive>(std::format("layer{}", which));
                for (int i = 0; i < 40; ++i)
                    if ((i + which) % 3 != 0)
                        archive->add(std::format("f{}.txt", i), std::format("{}-{}", which, i),
                            i % 5 == 0 ? std::optional<VFS::ContentHash>(makeHash(1)) : std::nullopt);
                return archive;
            };
            if (!reversed)
            {
                vfs.addArchive(make(0));
                vfs.addArchive(make(1));
                vfs.addArchive(make(2));
            }
            else
            {
                vfs.addArchive(make(2));
                vfs.addArchive(make(1));
                vfs.addArchive(make(0));
            }
            vfs.buildIndex();
            if (conflictsOut != nullptr)
                *conflictsOut = flatten(vfs.getConflicts());
            return flatten(vfs.getOverrides());
        }

        TEST_F(VFSOverridesTest, sameArchivesInTheSameOrderShouldGiveEqualReports)
        {
            std::vector<Flat> conflictsA;
            std::vector<Flat> conflictsB;
            const std::vector<Flat> a = buildScenario(false, &conflictsA);
            const std::vector<Flat> b = buildScenario(false, &conflictsB);

            ASSERT_FALSE(a.empty());
            EXPECT_EQ(a, b);
            EXPECT_EQ(conflictsA, conflictsB);
            EXPECT_LE(conflictsA.size(), a.size());
        }

        TEST_F(VFSOverridesTest, callingTheReportTwiceShouldGiveTheSameAnswer)
        {
            buildThreeLayers();
            EXPECT_EQ(flatten(mVfs.getOverrides()), flatten(mVfs.getOverrides()));
            EXPECT_EQ(flatten(mVfs.getConflicts()), flatten(mVfs.getConflicts()));
        }

        TEST_F(VFSOverridesTest, reversingTheRegistrationOrderShouldFlipTheWinner)
        {
            Manager forward;
            Manager backward;
            for (Manager* vfs : { &forward, &backward })
            {
                const bool reversed = vfs == &backward;
                for (int i = 0; i < 2; ++i)
                {
                    const int which = reversed ? 1 - i : i;
                    FakeArchive& archive = addFakeArchive(*vfs, std::format("l{}", which));
                    archive.add("x.txt", std::format("from{}", which));
                }
                vfs->buildIndex();
            }

            EXPECT_EQ(contentOf(forward, "x.txt"), "from1");
            EXPECT_EQ(contentOf(backward, "x.txt"), "from0");
            const std::vector<Override> f = forward.getOverrides();
            const std::vector<Override> b = backward.getOverrides();
            ASSERT_EQ(f.size(), 1u);
            ASSERT_EQ(b.size(), 1u);
            EXPECT_EQ(f[0].mWinner.mArchive, 1u);
            EXPECT_EQ(b[0].mWinner.mArchive, 1u); // the last registered always has the highest index
            EXPECT_EQ(forward.getArchiveDescription(f[0].mWinner.mArchive), "l1");
            EXPECT_EQ(backward.getArchiveDescription(b[0].mWinner.mArchive), "l0");
        }

        TEST_F(VFSOverridesTest, reversedOrderOfAScenarioShouldMirrorTheArchiveIndices)
        {
            const std::vector<Flat> forward = buildScenario(false);
            const std::vector<Flat> backward = buildScenario(true);

            // The same paths conflict in both orders; the provider sets are the same, with indices mirrored.
            ASSERT_EQ(forward.size(), backward.size());
            for (std::size_t i = 0; i < forward.size(); ++i)
            {
                EXPECT_EQ(forward[i].mPath, backward[i].mPath);
                std::vector<std::size_t> f = forward[i].mLosers;
                f.insert(f.begin(), forward[i].mWinner);
                std::vector<std::size_t> b = backward[i].mLosers;
                b.insert(b.begin(), backward[i].mWinner);
                // Providers in priority order: forward's first is the highest index; mirrored, the lowest.
                for (std::size_t& index : b)
                    index = 2 - index;
                std::sort(f.begin(), f.end());
                std::sort(b.begin(), b.end());
                EXPECT_EQ(f, b) << forward[i].mPath;
                EXPECT_GT(forward[i].mWinner, forward[i].mLosers.front());
                EXPECT_GT(backward[i].mWinner, backward[i].mLosers.front());
            }
        }

        TEST_F(VFSOverridesTest, losersShouldAlwaysHaveDecreasingArchiveIndices)
        {
            std::vector<Flat> flat = buildScenario(false);
            ASSERT_FALSE(flat.empty());
            for (const Flat& item : flat)
            {
                std::size_t previous = item.mWinner;
                for (std::size_t loser : item.mLosers)
                {
                    EXPECT_LT(loser, previous) << item.mPath;
                    previous = loser;
                }
            }
        }

        // ---- the report must not change lookups ----

        TEST_F(VFSOverridesTest, lookupsShouldNotChangeWhenTheReportIsBuilt)
        {
            buildThreeLayers();
            const std::vector<std::string> names = listAllNames(mVfs);
            std::vector<std::string> before;
            for (const std::string& name : names)
                before.push_back(contentOf(mVfs, name));
            const std::string archiveBefore = mVfs.getArchive(Path::Normalized("a.txt"));

            (void)mVfs.getOverrides();
            (void)mVfs.getConflicts();
            (void)mVfs.getOverrideCounts();

            EXPECT_EQ(listAllNames(mVfs), names);
            std::vector<std::string> after;
            for (const std::string& name : names)
                after.push_back(contentOf(mVfs, name));
            EXPECT_EQ(after, before);
            EXPECT_EQ(mVfs.getArchive(Path::Normalized("a.txt")), archiveBefore);
            EXPECT_THAT(before, ElementsAre("a2", "b2", "c1", "d1"));
        }

        // ---- reset ----

        TEST_F(VFSOverridesTest, resetShouldEmptyTheReportAndRebuildingShouldRestoreIt)
        {
            buildThreeLayers();
            const std::vector<Flat> before = flatten(mVfs.getOverrides());
            ASSERT_EQ(before.size(), 3u);

            mVfs.reset();
            EXPECT_THAT(mVfs.getOverrides(), IsEmpty());
            EXPECT_THAT(mVfs.getConflicts(), IsEmpty());
            EXPECT_THAT(mVfs.getOverrideCounts(), IsEmpty());
            EXPECT_EQ(mVfs.getArchiveCount(), 0u);

            mArchives.clear();
            buildThreeLayers();
            EXPECT_EQ(flatten(mVfs.getOverrides()), before);
            EXPECT_EQ(mVfs.getArchiveCount(), 3u);
        }

        TEST_F(VFSOverridesTest, buildingTheIndexAgainShouldNotDuplicateProviders)
        {
            buildThreeLayers();
            const std::vector<Flat> before = flatten(mVfs.getOverrides());

            mVfs.buildIndex();

            EXPECT_EQ(flatten(mVfs.getOverrides()), before);
        }

        // ---- real archives ----

        TEST(VFSOverridesRealArchivesTest, foldersAndBsaShouldBeReportedLikeAnyOtherSource)
        {
            ScratchDir temp;
            const auto first = temp.makeDir("first");
            const auto second = temp.makeDir("second");
            writeBsa(first / "base.bsa", { { "Meshes\\A.nif", "bsa" }, { "only.txt", "o" } });
            writeFile(second / "meshes" / "a.nif", "loose");

            Manager vfs;
            registerAll(vfs, { first, second }, { "base.bsa" });

            // registerArchives adds the BSA (index 0), then the two folders (1 and 2).
            ASSERT_EQ(vfs.getArchiveCount(), 3u);
            EXPECT_TRUE(vfs.getArchiveDescription(0).starts_with("BSA: "));
            EXPECT_TRUE(vfs.getArchiveDescription(1).starts_with("DIR: "));
            EXPECT_TRUE(vfs.getArchiveDescription(2).starts_with("DIR: "));

            const std::vector<Override> overrides = vfs.getOverrides();
            ASSERT_EQ(overrides.size(), 1u);
            EXPECT_EQ(overrides[0].mPath.value(), "meshes/a.nif");
            EXPECT_EQ(overrides[0].mWinner.mArchive, 2u);
            ASSERT_EQ(overrides[0].mLosers.size(), 1u);
            EXPECT_EQ(overrides[0].mLosers[0].mArchive, 0u);
            // Legacy sources have no content hash, so this is a conflict, never "identical".
            EXPECT_FALSE(overrides[0].mIdentical);
            EXPECT_FALSE(overrides[0].mWinner.mFile->getContentHash().has_value());
            EXPECT_FALSE(overrides[0].mLosers[0].mFile->getContentHash().has_value());
            EXPECT_EQ(vfs.getConflicts().size(), 1u);
        }
    }
}
