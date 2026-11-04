// Task 0069 stage f: hostile input to the advancedbodies_editor model (bodyview.hpp), written from the design alone
// (the section "Stage f, design" of docs/tasks/0069-advancedbodies-merged-legacy-body.md). Every case must be
// refused with one non-empty located message and no view (or, for a bad payload, the entry's message and nothing
// else), with no crash, no exception and no hang. Each refusal is paired with a positive control or a check that the
// message names the thing, so a model that refuses everything does not pass.
//
// Assumptions where the design left room (see the report):
//  - "Located" messages name the file they are about: the pack file name for a pack that does not read or holds
//    neither body.json nor manifest.txt, "body.json" for a package whose body.json is missing or does not read,
//    "current" for a pointer that is unsafe or missing, the pack name for a pointer to a missing pack, the path's
//    file or folder name for a source that does not exist or has another name.
//  - A body.json refused by the reader gives the reader's located error (file, line, pointer): "/version" or line 3
//    for a version 2 file, "/regionMap" for one over the region range cap.
//  - A merged cache with no entries is refused (the header says a merged cache has at least one body).
//  - A folder is not a source for openBodySource: only a body.json, a `current` or a *.edenpak are.
//  - The stage a reader's refusals name the field for a wrong format ("format"), version ("version"), too many
//    units ("units") or seams ("seams") and a field of the wrong type (its name); it ignores fields it does not read,
//    so nesting up to its cap inside an unread field is accepted.
//  - Exactly Body::Limits::sMaxRegionRanges ranges are accepted (the reader refuses only more), and each triangle of
//    such a map goes to the first region; a map with more cannot be read from a file at all, so the model's own
//    "more than the cap colours nothing" rule is not reachable through the public open functions.
#include "editor_modules/advancedbodies_editor/bodyview.hpp"

#include "abtestsupport.hpp"

#include <components/body/bodycache.hpp>
#include <components/body/bodyjson.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodypackage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ABTest
{
    namespace
    {
        using ab::OpenResult;

        ::testing::AssertionResult refused(const OpenResult& result, std::string_view mustName)
        {
            if (result.mView != nullptr)
            {
                return ::testing::AssertionFailure() << "the source opened, but it should be refused";
            }
            if (result.mError.empty())
            {
                return ::testing::AssertionFailure() << "refused without a message";
            }
            if (!mustName.empty() && !contains(result.mError, mustName))
            {
                return ::testing::AssertionFailure()
                    << "the message does not name '" << mustName << "': " << result.mError;
            }
            return ::testing::AssertionSuccess();
        }

        ::testing::AssertionResult analysisRefused(const ab::AnalysisResult& result, std::string_view mustName)
        {
            if (result.mUnit.has_value())
            {
                return ::testing::AssertionFailure() << "the report gave a unit, but it should be refused";
            }
            if (result.mError.empty())
            {
                return ::testing::AssertionFailure() << "refused without a message";
            }
            if (!mustName.empty() && !contains(result.mError, mustName))
            {
                return ::testing::AssertionFailure()
                    << "the message does not name '" << mustName << "': " << result.mError;
            }
            return ::testing::AssertionSuccess();
        }

        bool locatedAtTheVersion(const std::string& error)
        {
            return contains(error, "/version") || contains(error, ":3:");
        }

        /// A region map of `regions` regions ("r0", "r1", ...) each with `perRegion` ranges [0, 4294967295).
        Body::RegionMap hugeRegionMap(std::size_t regions, std::size_t perRegion)
        {
            Body::RegionMap map;
            map.mMesh = "body";
            for (std::size_t r = 0; r < regions; ++r)
            {
                Body::Region region;
                region.mId = "r" + std::to_string(r);
                region.mRanges.assign(perRegion, Body::TriangleRange{ 0, 4294967295u });
                map.mRegions.push_back(std::move(region));
            }
            return map;
        }

        std::string goodReport()
        {
            return reportJson({ unitText("Ash Folk", false, 1483, { SeamText{} }) });
        }

        class AbHostileTest : public AbTempTest
        {
        protected:
            /// Opens the merged cache in `folder` and checks that the entry `badName` holds only its payload message
            /// while every other entry reads and draws.
            void expectOnlyThisEntryFails(
                const std::filesystem::path& folder, const std::string& badName, std::size_t entries)
            {
                const OpenResult result = ab::openMergedCache(folder);
                ASSERT_NE(result.mView, nullptr) << result.mError;
                const ab::BodyView& view = *result.mView;
                ASSERT_EQ(view.bodyCount(), entries);
                bool seen = false;
                for (std::size_t i = 0; i < view.bodyCount(); ++i)
                {
                    const ab::BodyData& data = view.body(i);
                    if (view.entries()[i].mName == badName)
                    {
                        seen = true;
                        EXPECT_FALSE(data.mPayloadError.empty());
                        EXPECT_FALSE(data.mPackage.has_value());
                        EXPECT_FALSE(data.mMesh.has_value());
                        EXPECT_TRUE(data.mReport.mFindings.empty());
                        EXPECT_TRUE(data.mRegionLines.empty());
                        EXPECT_TRUE(data.mWeldPoints.empty());
                        EXPECT_EQ(data.mTitle, titleOf(view.entries()[i]));
                    }
                    else
                    {
                        EXPECT_TRUE(data.mPayloadError.empty()) << data.mPayloadError;
                        EXPECT_TRUE(data.mPackage.has_value()) << view.entries()[i].mName;
                        EXPECT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
                    }
                }
                EXPECT_TRUE(seen) << "no entry named " << badName;
            }
        };

        // ---- packs and packages ----------------------------------------------------------------------------------

        TEST_F(AbHostileTest, aTruncatedPackageFileIsRefusedAtEveryLength)
        {
            const PackageFixture fixture = makePackageFixture();
            const std::filesystem::path whole = writePack(mDir / "whole.edenpak", fixture.mFiles);
            const std::vector<std::uint8_t> bytes = readBytes(whole);
            ASSERT_GT(bytes.size(), 20u);
            const OpenResult intact = ab::openPackageFile(whole);
            ASSERT_NE(intact.mView, nullptr) << intact.mError;

            const std::vector<std::size_t> lengths{ bytes.size() / 2, std::min<std::size_t>(4096, bytes.size() - 1),
                10 };
            for (const std::size_t length : lengths)
            {
                const std::string name = "cut-" + std::to_string(length) + ".edenpak";
                const std::filesystem::path cut = mDir / name;
                writeBytes(
                    cut, std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)));
                EXPECT_TRUE(refused(ab::openPackageFile(cut), name)) << length << " bytes";
                EXPECT_TRUE(refused(ab::openBodySource(cut), name)) << length << " bytes";
            }
        }

        TEST_F(AbHostileTest, aPackHoldingNeitherBodyJsonNorAManifestIsRefused)
        {
            Body::PackageFiles files;
            files.mFiles["notes/readme.txt"] = bytesOf("nothing here\n");
            const std::filesystem::path pack = writePack(mDir / "other.edenpak", files);
            EXPECT_TRUE(refused(ab::openPackageFile(pack), "other.edenpak"));
            EXPECT_TRUE(refused(ab::openBodySource(pack), "other.edenpak"));
        }

        TEST_F(AbHostileTest, aBodyJsonOfAnotherVersionIsRefusedWithItsLocation)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mFiles.mFiles["body.json"]
                = bytesOf(replaceFirst(Body::writeBodyPackage(fixture.mPackage), "\"version\": 1", "\"version\": 2"));
            const std::filesystem::path bodyJson = writeFolder(mDir / "v2", fixture.mFiles);
            const std::filesystem::path pack = writePack(mDir / "v2.edenpak", fixture.mFiles);

            const OpenResult byFolder = ab::openPackageFolder(mDir / "v2");
            EXPECT_TRUE(refused(byFolder, "body.json"));
            EXPECT_TRUE(locatedAtTheVersion(byFolder.mError)) << byFolder.mError;
            const OpenResult bySource = ab::openBodySource(bodyJson);
            EXPECT_TRUE(refused(bySource, "body.json"));
            EXPECT_TRUE(locatedAtTheVersion(bySource.mError)) << bySource.mError;
            const OpenResult byPack = ab::openPackageFile(pack);
            EXPECT_TRUE(refused(byPack, "body.json"));
            EXPECT_TRUE(locatedAtTheVersion(byPack.mError)) << byPack.mError;
        }

        TEST_F(AbHostileTest, aBodyJsonThatIsNotJsonIsRefusedNamingIt)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mFiles.mFiles["body.json"] = bytesOf("this is { not json");
            const std::filesystem::path bodyJson = writeFolder(mDir / "broken", fixture.mFiles);
            const std::filesystem::path pack = writePack(mDir / "broken.edenpak", fixture.mFiles);
            EXPECT_TRUE(refused(ab::openPackageFolder(mDir / "broken"), "body.json"));
            EXPECT_TRUE(refused(ab::openBodySource(bodyJson), "body.json"));
            EXPECT_TRUE(refused(ab::openPackageFile(pack), "body.json"));
        }

        TEST_F(AbHostileTest, aFolderWithoutBodyJsonIsRefused)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mFiles.mFiles.erase("body.json");
            writeFolder(mDir / "nobody", fixture.mFiles);
            EXPECT_TRUE(refused(ab::openPackageFolder(mDir / "nobody"), "body.json"));
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "nobody" / "body.json"), "body.json"));
        }

        TEST_F(AbHostileTest, aRegionMapAtTheRangeCapOpensAndEveryTriangleGoesToTheFirstRegion)
        {
            const std::uint32_t quads = 5000;
            PackageFixture fixture = makeStripPackageFixture(quads);
            const std::size_t perRegion = Body::Limits::sMaxListEntries;
            const std::size_t regions = Body::Limits::sMaxRegionRanges / perRegion;
            ASSERT_EQ(regions * perRegion, Body::Limits::sMaxRegionRanges);
            fixture.mPackage.mRegionMap = hugeRegionMap(regions, perRegion);
            fixture.refresh();
            writeFolder(mDir / "huge", fixture.mFiles);

            const OpenResult result = ab::openPackageFolder(mDir / "huge");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const ab::BodyData& data = result.mView->current();
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
            const std::size_t triangles = 2 * static_cast<std::size_t>(quads);
            ASSERT_EQ(data.mMesh->mTriangleRegion.size(), triangles);
            EXPECT_EQ(std::count(data.mMesh->mTriangleRegion.begin(), data.mMesh->mTriangleRegion.end(), 0),
                static_cast<std::ptrdiff_t>(triangles));
            ASSERT_EQ(data.mRegions.size(), regions);
            EXPECT_EQ(data.mRegions[0].mTriangles, static_cast<std::uint32_t>(triangles));
            for (std::size_t r = 1; r < regions; ++r)
            {
                EXPECT_EQ(data.mRegions[r].mTriangles, 0u) << data.mRegions[r].mId;
            }
        }

        TEST_F(AbHostileTest, aRegionMapOverTheRangeCapIsRefusedByTheReaderWithItsLocation)
        {
            PackageFixture fixture = makePackageFixture();
            const std::size_t perRegion = Body::Limits::sMaxListEntries;
            Body::RegionMap map = hugeRegionMap(Body::Limits::sMaxRegionRanges / perRegion, perRegion);
            map.mRegions.push_back(Body::Region{ "extra", { Body::TriangleRange{ 0, 1 } } });
            fixture.mPackage.mRegionMap = map;
            fixture.refresh();
            writeFolder(mDir / "over", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "over");
            EXPECT_TRUE(refused(result, "body.json"));
            EXPECT_TRUE(contains(result.mError, "/regionMap")) << result.mError;
        }

        TEST_F(AbHostileTest, otherNamesAndMissingPathsAreRefused)
        {
            writeText(mDir / "notes.txt", "hello\n");
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "notes.txt"), "notes.txt"));
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "missing" / "body.json"), "missing"));
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "missing.edenpak"), "missing.edenpak"));
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "nowhere" / "current"), "current"));
            EXPECT_TRUE(refused(ab::openPackageFile(mDir / "missing.edenpak"), "missing.edenpak"));
            EXPECT_TRUE(refused(ab::openPackageFolder(mDir / "nowhere"), "nowhere"));
            EXPECT_TRUE(refused(ab::openMergedCache(mDir / "nowhere"), "current"));

            // A package folder itself is not a source (its body.json is), and the folder still opens by name.
            writeFolder(mDir / "package", makePackageFixture().mFiles);
            EXPECT_TRUE(refused(ab::openBodySource(mDir / "package"), ""));
            const OpenResult byName = ab::openPackageFolder(mDir / "package");
            EXPECT_NE(byName.mView, nullptr) << byName.mError;
        }

        // ---- merged caches -------------------------------------------------------------------------------------

        TEST_F(AbHostileTest, aPointerOutOfItsFolderIsRefusedEvenWhenAGoodPackIsThere)
        {
            const std::filesystem::path folder = mDir / "cache";
            const std::string pack = publishBodies(folder, standardCacheBodies());
            ASSERT_NE(ab::openMergedCache(folder).mView, nullptr);
            std::filesystem::copy_file(folder / pack, mDir / "evil.edenpak");
            writeText(folder / "current", "../evil.edenpak\n");
            EXPECT_TRUE(refused(ab::openMergedCache(folder), "current"));
            EXPECT_TRUE(refused(ab::openBodySource(folder / "current"), "current"));
        }

        TEST_F(AbHostileTest, aPointerThatNamesNoMergedPackIsRefused)
        {
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, standardCacheBodies());
            writeText(folder / "current", "not a pack name\n");
            EXPECT_TRUE(refused(ab::openMergedCache(folder), "current"));
            EXPECT_TRUE(refused(ab::openBodySource(folder / "current"), "current"));
        }

        TEST_F(AbHostileTest, aPointerToAMissingPackIsRefusedNamingThePack)
        {
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, standardCacheBodies());
            writeText(folder / "current", "merged-0123456789abcdef.edenpak\n");
            EXPECT_TRUE(refused(ab::openMergedCache(folder), "merged-0123456789abcdef.edenpak"));
            EXPECT_TRUE(refused(ab::openBodySource(folder / "current"), "merged-0123456789abcdef.edenpak"));
        }

        TEST_F(AbHostileTest, aCacheWhoseManifestDoesNotReadIsRefused)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            Body::PackageFiles files = cacheFiles(manifestOf(bodies), payloadsOf(bodies));
            files.mFiles["manifest.txt"] = bytesOf("not a manifest\n");
            const std::filesystem::path folder = mDir / "cache";
            const std::string pack = publishFiles(folder, files);
            EXPECT_TRUE(refused(ab::openMergedCache(folder), "manifest"));
            EXPECT_TRUE(refused(ab::openPackageFile(folder / pack), "manifest"));
        }

        TEST_F(AbHostileTest, aCacheWithNoEntriesIsRefused)
        {
            const std::vector<Body::CacheFailure> failures{ Body::CacheFailure{
                "Ash Folk", "male", kView, "every part failed" } };
            const std::filesystem::path folder = mDir / "cache";
            publishFiles(folder, cacheFiles(manifestOf({}, failures), {}));
            EXPECT_TRUE(refused(ab::openMergedCache(folder), ""));
        }

        TEST_F(AbHostileTest, aPayloadOfAnotherLayoutVersionLeavesThatEntryOnlyItsMessage)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            std::map<std::string, std::vector<std::uint8_t>> payloads = payloadsOf(bodies);
            const std::string bad = entryOf(bodies[1]).mName;
            patchU32(payloads[bad], 8, 1);
            const std::filesystem::path folder = mDir / "cache";
            publishFiles(folder, cacheFiles(manifestOf(bodies), payloads));
            expectOnlyThisEntryFails(folder, bad, bodies.size());
        }

        TEST_F(AbHostileTest, aPayloadWhoseCountsAreAllHugeLeavesThatEntryOnlyItsMessage)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            std::map<std::string, std::vector<std::uint8_t>> payloads = payloadsOf(bodies);
            const std::string bad = entryOf(bodies[0]).mName;
            for (std::size_t offset = 16; offset < 48; offset += 4)
            {
                patchU32(payloads[bad], offset, 0xFFFFFFFFu);
            }
            const std::filesystem::path folder = mDir / "cache";
            publishFiles(folder, cacheFiles(manifestOf(bodies), payloads));
            expectOnlyThisEntryFails(folder, bad, bodies.size());
        }

        TEST_F(AbHostileTest, anEntryWithoutItsPayloadFileLeavesThatEntryOnlyItsMessage)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            std::map<std::string, std::vector<std::uint8_t>> payloads = payloadsOf(bodies);
            const std::string bad = entryOf(bodies[2]).mName;
            payloads.erase(bad);
            const std::filesystem::path folder = mDir / "cache";
            publishFiles(folder, cacheFiles(manifestOf(bodies), payloads));
            expectOnlyThisEntryFails(folder, bad, bodies.size());
        }

        // ---- the stage a report ----------------------------------------------------------------------------------

        TEST(AbHostileAnalysis, theGoodReportReads)
        {
            const ab::AnalysisResult result = ab::readAnalysisUnit(goodReport(), "Ash Folk", false);
            ASSERT_TRUE(result.mUnit.has_value()) << result.mError;
            EXPECT_EQ(result.mUnit->mVertices, 1483u);
        }

        TEST(AbHostileAnalysis, deepNestingIsRefused)
        {
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(std::string(100000, '['), "Ash Folk", false), ""));
            EXPECT_TRUE(analysisRefused(
                ab::readAnalysisUnit(std::string(100000, '[') + std::string(100000, ']'), "Ash Folk", false), ""));
        }

        TEST(AbHostileAnalysis, nestingOverTheCapInsideAnUnreadFieldIsRefusedAndBelowItIsAccepted)
        {
            UnitText deep = unitText("Ash Folk", false, 1483);
            deep.mExtraField = "\"extra\": " + std::string(40, '[') + "1" + std::string(40, ']');
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(reportJson({ deep }), "Ash Folk", false), ""));

            UnitText shallow = unitText("Ash Folk", false, 1483);
            shallow.mExtraField = "\"extra\": " + std::string(16, '[') + "1" + std::string(16, ']');
            const ab::AnalysisResult accepted = ab::readAnalysisUnit(reportJson({ shallow }), "Ash Folk", false);
            EXPECT_TRUE(accepted.mUnit.has_value()) << accepted.mError;
        }

        TEST(AbHostileAnalysis, aTextOverTheSizeCapIsRefused)
        {
            const std::string text = goodReport() + std::string(static_cast<std::size_t>(ab::kMaxAnalysisBytes), ' ');
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(text, "Ash Folk", false), ""));
        }

        TEST(AbHostileAnalysis, textThatIsNotAReportObjectIsRefused)
        {
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit("", "Ash Folk", false), ""));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit("{ \"format\": ", "Ash Folk", false), ""));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit("not json at all", "Ash Folk", false), ""));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit("[]", "Ash Folk", false), ""));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit("{}", "Ash Folk", false), ""));
        }

        TEST(AbHostileAnalysis, anotherFormatOrVersionIsRefusedNamingIt)
        {
            const std::vector<UnitText> units{ unitText("Ash Folk", false, 1483) };
            EXPECT_TRUE(analysisRefused(
                ab::readAnalysisUnit(reportJson(units, "openeden.other"), "Ash Folk", false), "format"));
            EXPECT_TRUE(analysisRefused(
                ab::readAnalysisUnit(reportJson(units, "openeden.bodyanalysis", 2), "Ash Folk", false), "version"));
        }

        TEST(AbHostileAnalysis, tooManyUnitsAreRefusedAndTheCapIsAccepted)
        {
            const std::vector<UnitText> atCap(ab::kMaxAnalysisUnits, unitText("Ash Folk", false, 1483));
            const ab::AnalysisResult accepted = ab::readAnalysisUnit(reportJson(atCap), "Ash Folk", false);
            EXPECT_TRUE(accepted.mUnit.has_value()) << accepted.mError;

            const std::vector<UnitText> over(ab::kMaxAnalysisUnits + 1, unitText("Ash Folk", false, 1483));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(reportJson(over), "Ash Folk", false), "units"));
        }

        TEST(AbHostileAnalysis, tooManySeamsAreRefusedAndTheCapIsAccepted)
        {
            const UnitText atCap
                = unitText("Ash Folk", false, 1483, std::vector<SeamText>(ab::kMaxAnalysisSeams, SeamText{}));
            const ab::AnalysisResult accepted = ab::readAnalysisUnit(reportJson({ atCap }), "Ash Folk", false);
            ASSERT_TRUE(accepted.mUnit.has_value()) << accepted.mError;
            EXPECT_EQ(accepted.mUnit->mSeams.size(), ab::kMaxAnalysisSeams);

            const UnitText over
                = unitText("Ash Folk", false, 1483, std::vector<SeamText>(ab::kMaxAnalysisSeams + 1, SeamText{}));
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(reportJson({ over }), "Ash Folk", false), "seams"));
        }

        TEST(AbHostileAnalysis, aFieldOfTheWrongTypeIsRefusedNamingIt)
        {
            const std::string good = goodReport();
            const std::string vertices = replaceFirst(good, "\"vertices\": 1483", "\"vertices\": \"many\"");
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(vertices, "Ash Folk", false), "vertices"));
            const std::string female = replaceFirst(good, "\"female\": false", "\"female\": \"no\"");
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(female, "Ash Folk", false), "female"));
            const std::string touches = replaceFirst(good, "\"touches\": true", "\"touches\": 1");
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(touches, "Ash Folk", false), "touches"));
            const std::string ladder
                = replaceFirst(good, "\"withinLadder\": [12, 30, 40, 40, 40, 40]", "\"withinLadder\": \"none\"");
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(ladder, "Ash Folk", false), "withinLadder"));
            // "units" an object (holding the array) instead of an array: still valid JSON.
            const std::string unitsOpened = replaceFirst(good, "\"units\": [", "\"units\": {\"a\": [");
            const std::string unitsObject
                = replaceFirst(unitsOpened, "  ],\n  \"aggregate\"", "  ]},\n  \"aggregate\"");
            EXPECT_TRUE(analysisRefused(ab::readAnalysisUnit(unitsObject, "Ash Folk", false), "units"));
        }
    }
}
