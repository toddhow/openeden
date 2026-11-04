// Task 0069 stage f: the advancedbodies_editor model (bodyview.hpp), written from the design alone (the section
// "Stage f, design" of docs/tasks/0069-advancedbodies-merged-legacy-body.md), not from the implementation.
//
// Every expected value is computed in abtestsupport.hpp from the design's rules: the payload summary, the region of
// each triangle (first region in map order, then range order), the region lines (distinct edges in first-use order),
// the weld points, the palette. Bodies are synthetic stacked quads and strips.
//
// Assumptions where the design left room (see the report):
//  - path() is the path given to the named open function; for openBodySource it is either the path given or the
//    target it dispatched to (the folder of a body.json, the folder of `current`). Both are accepted.
//  - A body's findings never stop it being drawn: the drawn mesh, regions, lines and weld points are worked out
//    whatever the report says (a package with a region gap or overlap is still drawn).
//  - mRegions lists the package's region map in its order even when nothing is coloured (every mTriangles is then 0),
//    also when no mesh is drawn and when the map names another mesh.
//  - A region line runs from the position of the smaller vertex index (mFrom) to the larger's (mTo).
//  - mergedEntryPackage: the provenance tool version is std::to_string(manifest.mMergerVersion) and the parameters
//    are "layout " + the layout version (0 in the manifest and in the payload here, so either source gives "layout
//    0"); the licence and author texts are not pinned; there is no rest skeleton and no optional part other than the
//    region map and the weld groups ("exactly what the cache knows").
//  - A weld group whose first member is inside the mesh draws its point there even when a later member is past it.
//  - readAnalysisUnit's mRace is the report's own text; a merged entry with no unit in an existing report has no
//    numbers and a non-empty note.
//  - PayloadMeshSource gives no summary for a file of the package that is not a payload (it does not read).
//  - body(i) for an index past the end gives the selected body (the header's comment).
#include "editor_modules/advancedbodies_editor/bodyview.hpp"

#include "abtestsupport.hpp"

#include <components/body/bodycache.hpp>
#include <components/body/bodymerge.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodypackage.hpp>
#include <components/body/bodyvalidate.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ABTest
{
    namespace
    {
        using ab::BodyData;
        using ab::BodyView;
        using ab::OpenResult;
        using ab::SourceKind;

        std::size_t indexOf(const BodyView& view, const std::string& race, const std::string& sex)
        {
            const std::vector<Body::CacheEntry>& entries = view.entries();
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].mRace == race && entries[i].mSex == sex)
                {
                    return i;
                }
            }
            ADD_FAILURE() << "no entry for " << race << " " << sex;
            return 0;
        }

        const Body::ProvenanceRecord* provenanceOf(const Body::BodyPackage& package, std::string_view part)
        {
            for (const Body::ProvenanceRecord& record : package.mProvenance)
            {
                if (record.mPart == part)
                {
                    return &record;
                }
            }
            return nullptr;
        }

        Body::CacheEntry entryFor(const std::string& race, const std::string& sex, char keyDigit)
        {
            return Body::CacheEntry{ Body::bodyEntryName(race, sex, kView), race, sex, kView,
                std::string(64, keyDigit) };
        }

        Body::CacheManifest manifestWith(const Body::CacheEntry& entry, std::uint32_t mergerVersion)
        {
            Body::CacheManifest manifest;
            manifest.mMergerVersion = mergerVersion;
            manifest.mLayoutVersion = 0;
            manifest.mEntries = { entry };
            return manifest;
        }

        // ---- the palette and the caps ------------------------------------------------------------------------

        TEST(AbPalette, regionColourIsTheDesignsPaletteByIndexModuloTwelve)
        {
            EXPECT_EQ(ab::kRegionPaletteSize, 12u);
            for (std::size_t i = 0; i < 36; ++i)
            {
                EXPECT_TRUE(sameColour(ab::regionColour(i), designPalette()[i % 12])) << "region " << i;
            }
            const std::size_t large = 1000003;
            EXPECT_TRUE(sameColour(ab::regionColour(large), designPalette()[large % 12]));
        }

        TEST(AbPalette, theUnassignedAndWeldColoursAreTheDesigns)
        {
            EXPECT_TRUE(sameColour(ab::kUnassignedColour, kDesignUnassigned));
            EXPECT_TRUE(sameColour(ab::kWeldColour, kDesignWeld));
        }

        TEST(AbPalette, theCapsAndTheUnitFactorAreTheDesigns)
        {
            EXPECT_EQ(ab::kMaxRegionLines, 28000u);
            EXPECT_EQ(ab::kMaxWeldPoints, 1000u);
            EXPECT_EQ(ab::kMaxAnalysisBytes, std::uintmax_t{ 64 } << 20);
            EXPECT_EQ(ab::kMaxAnalysisNesting, 32u);
            EXPECT_EQ(ab::kMaxAnalysisUnits, 1024u);
            EXPECT_EQ(ab::kMaxAnalysisSeams, 256u);
            EXPECT_EQ(ab::kGameUnitsPerMetre, static_cast<double>(Constants::UnitsPerMeter));
        }

        // ---- summaries -----------------------------------------------------------------------------------------

        TEST(AbSummary, summarizeMergedBodyDividesThePositionsAndKeepsCountsBonesSkinAndTriangles)
        {
            const Body::MergedBody body = roundTrip(makeStackedBody(128.f, 3, 16.f));
            expectSameSummary(
                ab::summarizeMergedBody(body, ab::kGameUnitsPerMetre), referenceSummary(body, ab::kGameUnitsPerMetre));
            expectSameSummary(ab::summarizeMergedBody(body, 1.0), referenceSummary(body, 1.0));

            // The reference itself: 128 units tall, and the top corners keep the payload's influence order (the
            // larger weight on the higher bone first), not an order by bone.
            const Body::MeshSummary reference = referenceSummary(body, ab::kGameUnitsPerMetre);
            EXPECT_NEAR(reference.mBoundsMax[2], 128.0 / ab::kGameUnitsPerMetre, 1e-9);
            ASSERT_GE(reference.mInfluences.size(), 4u);
            EXPECT_EQ(reference.mInfluences[2].mBone, 1u);
            EXPECT_EQ(reference.mInfluences[2].mWeight, 0.75);
            EXPECT_EQ(reference.mInfluences[3].mBone, 0u);
            EXPECT_EQ(reference.mUvSetCount, 1u);
        }

        TEST(AbSummary, aBodyWithoutUvsHasNoUvSet)
        {
            const Body::MergedBody body = roundTrip(makeStripBody(3, 120.f, 10.f));
            const Body::MeshSummary summary = ab::summarizeMergedBody(body, 2.0);
            EXPECT_EQ(summary.mUvSetCount, 0u);
            expectSameSummary(summary, referenceSummary(body, 2.0));
        }

        TEST(AbPayloadSource, summarizesABodymeshFileOfThePackageAsStored)
        {
            const PackageFixture fixture = makePackageFixture();
            const ab::PayloadMeshSource source(fixture.mFiles);
            const std::optional<Body::MeshSummary> summary = source.summarize(kPayloadPath, "bodymesh");
            ASSERT_TRUE(summary.has_value());
            expectSameSummary(*summary, referenceSummary(fixture.mPayload, 1.0));
        }

        TEST(AbPayloadSource, givesNothingForAnotherFormatOrAPathThatIsNotAPayloadOfThePackage)
        {
            const PackageFixture fixture = makePackageFixture();
            const ab::PayloadMeshSource source(fixture.mFiles);
            EXPECT_FALSE(source.summarize(kPayloadPath, "gltf").has_value());
            EXPECT_FALSE(source.summarize(kPayloadPath, "nif").has_value());
            EXPECT_FALSE(source.summarize(kPayloadPath, "summary").has_value());
            EXPECT_FALSE(source.summarize("mesh/other.bodymesh", "bodymesh").has_value());
            EXPECT_FALSE(source.summarize("body.json", "bodymesh").has_value());
        }

        // ---- the package a merged entry stands for --------------------------------------------------------------

        TEST(AbMergedEntryPackage, holdsExactlyWhatTheCacheKnows)
        {
            const Body::MergedBody payload = roundTrip(makeStackedBody(128.f, 3, 16.f));
            const Body::CacheEntry entry = entryFor("Ash Folk", "female", 'd');
            const Body::CacheManifest manifest = manifestWith(entry, 7);
            const Body::BodyPackage package = ab::mergedEntryPackage(entry, payload, manifest);

            EXPECT_EQ(package.mIdentity.mId, "merged-body");
            EXPECT_EQ(package.mIdentity.mName, "Ash Folk female third");
            EXPECT_EQ(package.mIdentity.mVersion, "0.0.0");
            EXPECT_FALSE(package.mIdentity.mLicense.empty());

            ASSERT_EQ(package.mTargets.size(), 1u);
            ASSERT_TRUE(package.mTargets[0].mRace.has_value());
            ASSERT_TRUE(package.mTargets[0].mSex.has_value());
            EXPECT_EQ(*package.mTargets[0].mRace, "Ash Folk");
            EXPECT_EQ(*package.mTargets[0].mSex, "female");

            // No skeleton binding: the cache records none.
            EXPECT_FALSE(package.mBinding.mSkeletonPath.has_value());
            EXPECT_FALSE(package.mBinding.mSkeletonProfile.has_value());
            EXPECT_FALSE(package.mBinding.mRestSkeleton.has_value());

            ASSERT_EQ(package.mMeshes.size(), 1u);
            const Body::MeshEntry& mesh = package.mMeshes[0];
            EXPECT_EQ(mesh.mId, "body");
            EXPECT_EQ(mesh.mPath, entry.mName);
            EXPECT_EQ(mesh.mFormat, Body::MeshFormat::BodyMesh);
            EXPECT_EQ(mesh.mLod, 0);
            EXPECT_EQ(mesh.mRole, Body::MeshRole::Skin);
            ASSERT_TRUE(mesh.mSummary.has_value());
            expectSameSummary(*mesh.mSummary, referenceSummary(payload, ab::kGameUnitsPerMetre));

            ASSERT_TRUE(package.mRegionMap.has_value());
            EXPECT_EQ(package.mRegionMap->mOrigin, Body::Origin::Derived);
            EXPECT_EQ(package.mRegionMap->mMesh, "body");
            ASSERT_EQ(package.mRegionMap->mRegions.size(), payload.mRegions.size());
            for (std::size_t k = 0; k < payload.mRegions.size(); ++k)
            {
                const Body::RegionRange& range = payload.mRegions[k];
                const Body::Region& region = package.mRegionMap->mRegions[k];
                EXPECT_EQ(region.mId, "slot-" + std::to_string(range.mSlot));
                std::vector<Body::TriangleRange> expectedRanges;
                expectedRanges.push_back(
                    Body::TriangleRange{ range.mFirstTriangle, range.mFirstTriangle + range.mTriangleCount });
                EXPECT_TRUE(region.mRanges == expectedRanges) << region.mId;
            }
            // The slot number, not the region's index: the stacked body's slots are 1, 3 and 5.
            EXPECT_EQ(package.mRegionMap->mRegions[1].mId, "slot-3");

            ASSERT_TRUE(package.mWeldGroups.has_value());
            EXPECT_EQ(package.mWeldGroups->mOrigin, Body::Origin::Derived);
            EXPECT_EQ(package.mWeldGroups->mMesh, "body");
            EXPECT_EQ(package.mWeldGroups->mGroups, payload.mWeldGroups);
            EXPECT_EQ(package.mWeldGroups->mToleranceM, 0.0);

            EXPECT_EQ(package.mProvenance.size(), 2u);
            for (const auto& part : { "regionMap", "weldGroups" })
            {
                const Body::ProvenanceRecord* record = provenanceOf(package, part);
                ASSERT_NE(record, nullptr) << part;
                EXPECT_EQ(record->mSourceDigest, entry.mKey) << part;
                EXPECT_EQ(record->mToolId, "openeden.bodymerge") << part;
                EXPECT_EQ(record->mToolVersion, "7") << part;
                EXPECT_EQ(record->mParameters, "layout 0") << part;
            }

            EXPECT_FALSE(package.mMorphSet.has_value());
            EXPECT_FALSE(package.mProxy.has_value());
            EXPECT_FALSE(package.mPhysicsProfile.has_value());
            EXPECT_FALSE(package.mMeasurements.has_value());
            EXPECT_FALSE(package.mLandmarks.has_value());
            EXPECT_FALSE(package.mSoftTissue.has_value());
        }

        TEST(AbMergedEntryPackage, theWeldToleranceIsTheLargestMemberDistanceInMetres)
        {
            Body::MergedBody body = makeStackedBody(126.f, 3, 14.f);
            body.mPositions[5][0] += 2.f; // group [2, 5]: 2 units apart
            body.mPositions[8][2] += 3.f; // group [7, 8]: 3 units apart, the largest
            const Body::MergedBody payload = roundTrip(body);
            const Body::CacheEntry entry = entryFor("Bog Kin", "male", 'e');
            const Body::BodyPackage package = ab::mergedEntryPackage(entry, payload, manifestWith(entry, 1));
            ASSERT_TRUE(package.mWeldGroups.has_value());
            EXPECT_NEAR(package.mWeldGroups->mToleranceM, 3.0 / ab::kGameUnitsPerMetre, 1e-9);
        }

        TEST(AbMergedEntryPackage, aGoodEntryValidatesWithOnlyTheTwoBindingFindings)
        {
            const Body::MergedBody payload = roundTrip(makeStackedBody(128.f, 2, 16.f));
            const Body::CacheEntry entry = entryFor("Ash Folk", "male", 'f');
            const Body::Report report
                = Body::validateBodyPackage(ab::mergedEntryPackage(entry, payload, manifestWith(entry, 1)));
            ASSERT_EQ(report.mFindings.size(), 2u) << describeReport(report);
            EXPECT_EQ(report.mFindings[0].mCode, "BODY-BIND-001");
            EXPECT_EQ(report.mFindings[0].mSeverity, Body::Severity::Error);
            EXPECT_EQ(report.mFindings[0].mPath, "/binding/skeleton");
            EXPECT_EQ(report.mFindings[1].mCode, "BODY-BIND-014");
            EXPECT_EQ(report.mFindings[1].mSeverity, Body::Severity::Warning);
            EXPECT_EQ(report.mFindings[1].mPath, "/binding/restSkeleton");
        }

        // ---- the stage a report ---------------------------------------------------------------------------------

        TEST(AbAnalysis, readsTheUnitOfTheRaceIgnoringCaseAndOfTheSex)
        {
            const SeamText wrist{};
            SeamText knee;
            knee.mLabel = "knee-ankle";
            knee.mSlotA = "rknee";
            knee.mSlotB = "rankle";
            knee.mTouches = false;
            knee.mCount = 36;
            knee.mMin = 0.0001;
            knee.mMedian = 0.0002;
            knee.mMax = 0.0009;
            knee.mLadder = { 1, 20, 36, 36, 36, 36 };
            knee.mOverlap = 2;
            knee.mGap = 3;
            knee.mMeeting = 31;
            knee.mWeightMax = 0.5;
            UnitText male = unitText("ash folk", false, 1483, { wrist, knee });
            male.mSkeleton = "meshes/skeleton_ash.nif";
            UnitText female = unitText("Ash Folk", true, 1922);
            female.mBeast = true;
            female.mTriangles = 3100;
            const std::string text = reportJson({ unitText("Bog Kin", false, 77), male, female });

            const ab::AnalysisResult maleResult = ab::readAnalysisUnit(text, "ASH FOLK", false);
            ASSERT_TRUE(maleResult.mUnit.has_value()) << maleResult.mError;
            const ab::AnalysisUnit& unit = *maleResult.mUnit;
            EXPECT_EQ(unit.mRace, "ash folk");
            EXPECT_FALSE(unit.mFemale);
            EXPECT_FALSE(unit.mBeast);
            EXPECT_EQ(unit.mSkeleton, "meshes/skeleton_ash.nif");
            EXPECT_EQ(unit.mVertices, 1483u);
            EXPECT_EQ(unit.mTriangles, 2200u);
            EXPECT_EQ(unit.mBones, 30u);
            EXPECT_EQ(unit.mMaterials, 4u);
            EXPECT_EQ(unit.mFiles, 12u);
            EXPECT_EQ(unit.mFileBytes, 456789u);
            EXPECT_EQ(unit.mSkinnedParts, 3u);
            EXPECT_EQ(unit.mRigidParts, 16u);
            ASSERT_EQ(unit.mSeams.size(), 2u);

            const ab::AnalysisSeam& first = unit.mSeams[0];
            EXPECT_EQ(first.mLabel, "forearm-wrist");
            EXPECT_EQ(first.mSlotA, "rforearm");
            EXPECT_EQ(first.mSlotB, "rwrist");
            EXPECT_TRUE(first.mTouches);
            EXPECT_EQ(first.mPairs, 40u);
            EXPECT_EQ(first.mMinDistance, 0.0);
            EXPECT_EQ(first.mMedianDistance, 0.0003);
            EXPECT_EQ(first.mMaxDistance, 0.0011);
            EXPECT_TRUE((first.mWithin == std::array<std::uint64_t, 6>{ 12, 30, 40, 40, 40, 40 }));
            EXPECT_EQ(first.mOverlapPairs, 0u);
            EXPECT_EQ(first.mGapPairs, 0u);
            EXPECT_EQ(first.mMeetingPairs, 40u);
            EXPECT_EQ(first.mMaxWeightDisagreement, 1.0);

            const ab::AnalysisSeam& second = unit.mSeams[1];
            EXPECT_EQ(second.mLabel, "knee-ankle");
            EXPECT_EQ(second.mSlotA, "rknee");
            EXPECT_EQ(second.mSlotB, "rankle");
            EXPECT_FALSE(second.mTouches);
            EXPECT_EQ(second.mPairs, 36u);
            EXPECT_EQ(second.mMinDistance, 0.0001);
            EXPECT_EQ(second.mMedianDistance, 0.0002);
            EXPECT_EQ(second.mMaxDistance, 0.0009);
            EXPECT_TRUE((second.mWithin == std::array<std::uint64_t, 6>{ 1, 20, 36, 36, 36, 36 }));
            EXPECT_EQ(second.mOverlapPairs, 2u);
            EXPECT_EQ(second.mGapPairs, 3u);
            EXPECT_EQ(second.mMeetingPairs, 31u);
            EXPECT_EQ(second.mMaxWeightDisagreement, 0.5);

            const ab::AnalysisResult femaleResult = ab::readAnalysisUnit(text, "Ash Folk", true);
            ASSERT_TRUE(femaleResult.mUnit.has_value()) << femaleResult.mError;
            EXPECT_EQ(femaleResult.mUnit->mRace, "Ash Folk");
            EXPECT_TRUE(femaleResult.mUnit->mFemale);
            EXPECT_TRUE(femaleResult.mUnit->mBeast);
            EXPECT_EQ(femaleResult.mUnit->mVertices, 1922u);
            EXPECT_EQ(femaleResult.mUnit->mTriangles, 3100u);
            EXPECT_TRUE(femaleResult.mUnit->mSeams.empty());
        }

        TEST(AbAnalysis, noUnitOfThatRaceAndSexIsAnError)
        {
            const std::string text = reportJson({ unitText("Ash Folk", false, 1483) });
            ASSERT_TRUE(ab::readAnalysisUnit(text, "Ash Folk", false).mUnit.has_value());

            const ab::AnalysisResult otherSex = ab::readAnalysisUnit(text, "Ash Folk", true);
            EXPECT_FALSE(otherSex.mUnit.has_value());
            EXPECT_FALSE(otherSex.mError.empty());
            const ab::AnalysisResult prefix = ab::readAnalysisUnit(text, "Ash", false);
            EXPECT_FALSE(prefix.mUnit.has_value());
            EXPECT_FALSE(prefix.mError.empty());
            const ab::AnalysisResult otherRace = ab::readAnalysisUnit(text, "Bog Kin", false);
            EXPECT_FALSE(otherRace.mUnit.has_value());
            EXPECT_FALSE(otherRace.mError.empty());
        }

        TEST(AbAnalysis, theReportOfACacheIsTwoFoldersUpInAnalysis)
        {
            const std::filesystem::path relative = std::filesystem::path("user") / "openeden" / "bodies";
            EXPECT_EQ(ab::analysisReportFor(relative).lexically_normal().generic_string(),
                (std::filesystem::path("user") / "analysis" / "bodies.json").lexically_normal().generic_string());

            const std::filesystem::path root = std::filesystem::temp_directory_path() / "ab-user";
            EXPECT_EQ(ab::analysisReportFor(root / "openeden" / "bodies").lexically_normal().generic_string(),
                (root / "analysis" / "bodies.json").lexically_normal().generic_string());
        }

        // ---- opening the three sources --------------------------------------------------------------------------

        class AbViewTest : public AbTempTest
        {
        protected:
            /// Opens the standard package with `map` as its region map and checks what is drawn against the rule's
            /// result written out by the test (and the test's own reference of the rule).
            void expectDrawnRegions(const std::string& name, const Body::RegionMap& map,
                const std::vector<int>& expectedTriangleRegion, const std::vector<std::uint32_t>& expectedCounts)
            {
                PackageFixture fixture = makePackageFixture();
                fixture.mPackage.mRegionMap = map;
                fixture.refresh();
                writeFolder(mDir / name, fixture.mFiles);
                const OpenResult result = ab::openPackageFolder(mDir / name);
                ASSERT_NE(result.mView, nullptr) << result.mError;
                const BodyData& data = result.mView->current();
                ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;

                EXPECT_EQ(referenceTriangleRegions(map, 4), expectedTriangleRegion) << "the case as written";
                EXPECT_EQ(data.mMesh->mTriangleRegion, expectedTriangleRegion);
                ASSERT_EQ(data.mRegions.size(), map.mRegions.size());
                for (std::size_t r = 0; r < map.mRegions.size(); ++r)
                {
                    EXPECT_EQ(data.mRegions[r].mId, map.mRegions[r].mId);
                    EXPECT_EQ(data.mRegions[r].mTriangles, expectedCounts[r]) << data.mRegions[r].mId;
                    EXPECT_TRUE(sameColour(data.mRegions[r].mColour, designColour(static_cast<int>(r))));
                }
                const std::vector<ab::OverlayLine> lines = referenceRegionLines(
                    positionsInMetres(fixture.mPayload, 1.0), fixture.mPayload.mTriangles, expectedTriangleRegion);
                expectSameLines(data.mRegionLines, lines);
                EXPECT_EQ(data.mRegionLinesTotal, lines.size());
            }
        };

        TEST_F(AbViewTest, aPackageFolderOpensFromItsBodyJsonAndByName)
        {
            const PackageFixture fixture = makePackageFixture();
            const std::filesystem::path folder = mDir / "package";
            const std::filesystem::path bodyJson = writeFolder(folder, fixture.mFiles);

            const OpenResult byName = ab::openPackageFolder(folder);
            ASSERT_NE(byName.mView, nullptr) << byName.mError;
            const OpenResult bySource = ab::openBodySource(bodyJson);
            ASSERT_NE(bySource.mView, nullptr) << bySource.mError;

            EXPECT_TRUE(samePath(byName.mView->path(), folder)) << byName.mView->path();
            EXPECT_TRUE(samePath(bySource.mView->path(), bodyJson) || samePath(bySource.mView->path(), folder))
                << bySource.mView->path();
            const Body::BodyPackage expectedPackage = readBack(fixture.mFiles);
            for (const BodyView* view : { byName.mView.get(), bySource.mView.get() })
            {
                EXPECT_EQ(view->kind(), SourceKind::PackageFolder);
                EXPECT_EQ(view->bodyCount(), 1u);
                EXPECT_TRUE(view->entries().empty());
                EXPECT_TRUE(view->failures().empty());
                EXPECT_EQ(view->selected(), 0u);
                EXPECT_EQ(view->current().mTitle, kPackageId);
                EXPECT_TRUE(view->current().mPayloadError.empty()) << view->current().mPayloadError;
                ASSERT_TRUE(view->current().mPackage.has_value());
                EXPECT_TRUE(*view->current().mPackage == expectedPackage);
            }
        }

        TEST_F(AbViewTest, aPackedPackageOpensFromItsPackAndByName)
        {
            const PackageFixture fixture = makePackageFixture();
            const std::filesystem::path pack = writePack(mDir / "synthetic.edenpak", fixture.mFiles);

            const OpenResult byName = ab::openPackageFile(pack);
            ASSERT_NE(byName.mView, nullptr) << byName.mError;
            const OpenResult bySource = ab::openBodySource(pack);
            ASSERT_NE(bySource.mView, nullptr) << bySource.mError;

            const Body::Report expected = expectedPackageReport(fixture.mFiles, { { kPayloadPath, fixture.mPayload } });
            for (const BodyView* view : { byName.mView.get(), bySource.mView.get() })
            {
                EXPECT_EQ(view->kind(), SourceKind::PackageFile);
                EXPECT_TRUE(samePath(view->path(), pack)) << view->path();
                EXPECT_EQ(view->bodyCount(), 1u);
                EXPECT_TRUE(view->entries().empty());
                EXPECT_EQ(view->current().mTitle, kPackageId);
                expectSameReport(view->current().mReport, expected);
                EXPECT_TRUE(view->current().mMesh.has_value()) << view->current().mMeshNote;
            }
        }

        TEST_F(AbViewTest, aMergedCacheOpensThroughCurrentAndTakesThePackItNames)
        {
            const std::filesystem::path folder = mDir / "user" / "openeden" / "bodies";
            std::vector<CacheBody> first;
            first.push_back(CacheBody{ "Cliff Born", "male", roundTrip(makeStackedBody(110.f, 1, 12.f)), 'e' });
            const std::vector<CacheBody> second = standardCacheBodies();
            const std::vector<Body::CacheFailure> failures{ Body::CacheFailure{
                "Dune Walker", "female", kView, "a part does not read" } };
            const std::string firstPack = publishBodies(folder, first);
            const std::string secondPack = publishBodies(folder, second, failures);
            ASSERT_NE(firstPack, secondPack);
            ASSERT_TRUE(std::filesystem::exists(folder / firstPack));

            const Body::CacheManifest expected = manifestOf(second, failures);
            const OpenResult byName = ab::openMergedCache(folder);
            ASSERT_NE(byName.mView, nullptr) << byName.mError;
            const OpenResult bySource = ab::openBodySource(folder / "current");
            ASSERT_NE(bySource.mView, nullptr) << bySource.mError;

            EXPECT_TRUE(samePath(byName.mView->path(), folder)) << byName.mView->path();
            EXPECT_TRUE(
                samePath(bySource.mView->path(), folder / "current") || samePath(bySource.mView->path(), folder))
                << bySource.mView->path();
            for (const BodyView* view : { byName.mView.get(), bySource.mView.get() })
            {
                EXPECT_EQ(view->kind(), SourceKind::MergedCache);
                EXPECT_EQ(view->bodyCount(), expected.mEntries.size());
                EXPECT_TRUE(view->entries() == expected.mEntries);
                EXPECT_TRUE(view->failures() == expected.mFailures);
                EXPECT_EQ(view->selected(), 0u);
            }
        }

        TEST_F(AbViewTest, aMergedCachePackOpensDirectly)
        {
            const std::filesystem::path folder = mDir / "cache";
            std::vector<CacheBody> first;
            first.push_back(CacheBody{ "Cliff Born", "male", roundTrip(makeStackedBody(110.f, 1, 12.f)), 'e' });
            const std::string firstPack = publishBodies(folder, first);
            publishBodies(folder, standardCacheBodies());

            const std::filesystem::path pack = folder / firstPack;
            const OpenResult byName = ab::openPackageFile(pack);
            ASSERT_NE(byName.mView, nullptr) << byName.mError;
            const OpenResult bySource = ab::openBodySource(pack);
            ASSERT_NE(bySource.mView, nullptr) << bySource.mError;
            for (const BodyView* view : { byName.mView.get(), bySource.mView.get() })
            {
                EXPECT_EQ(view->kind(), SourceKind::MergedCache);
                EXPECT_TRUE(samePath(view->path(), pack)) << view->path();
                EXPECT_EQ(view->bodyCount(), 1u);
                EXPECT_TRUE(view->entries() == manifestOf(first).mEntries);
                EXPECT_EQ(view->current().mTitle, "Cliff Born male third");
            }
        }

        TEST_F(AbViewTest, selectChangesTheBodyWithinRangeAndNothingBeyondIt)
        {
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, standardCacheBodies());
            const OpenResult result = ab::openMergedCache(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            BodyView& view = *result.mView;
            ASSERT_EQ(view.bodyCount(), 3u);
            EXPECT_EQ(view.selected(), 0u);
            EXPECT_EQ(view.current().mTitle, titleOf(view.entries()[0]));

            EXPECT_TRUE(view.select(2));
            EXPECT_EQ(view.selected(), 2u);
            EXPECT_EQ(view.current().mTitle, titleOf(view.entries()[2]));
            EXPECT_FALSE(view.select(3));
            EXPECT_FALSE(view.select(1000));
            EXPECT_EQ(view.selected(), 2u);
            EXPECT_EQ(view.current().mTitle, titleOf(view.entries()[2]));
            EXPECT_TRUE(view.select(1));
            EXPECT_EQ(view.selected(), 1u);
            EXPECT_EQ(view.current().mTitle, titleOf(view.entries()[1]));

            // A package is one body.
            writeFolder(mDir / "package", makePackageFixture().mFiles);
            const OpenResult package = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(package.mView, nullptr) << package.mError;
            EXPECT_TRUE(package.mView->select(0));
            EXPECT_FALSE(package.mView->select(1));
            EXPECT_EQ(package.mView->selected(), 0u);
        }

        TEST_F(AbViewTest, everyMergedEntryIsTitledByRaceSexAndView)
        {
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, standardCacheBodies());
            const OpenResult result = ab::openMergedCache(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyView& view = *result.mView;
            std::vector<std::string> titles;
            for (std::size_t i = 0; i < view.bodyCount(); ++i)
            {
                EXPECT_EQ(view.body(i).mTitle, titleOf(view.entries()[i]));
                titles.push_back(view.body(i).mTitle);
            }
            EXPECT_NE(std::find(titles.begin(), titles.end(), "Ash Folk female third"), titles.end());
            EXPECT_NE(std::find(titles.begin(), titles.end(), "Bog Kin male third"), titles.end());
            // A bad index gives the selected body.
            EXPECT_EQ(view.body(99).mTitle, view.current().mTitle);
        }

        // ---- findings --------------------------------------------------------------------------------------------

        TEST_F(AbViewTest, aPackagesFindingsAreTheValidatorsWithItsOwnFilesAndItsPayloadsSummary)
        {
            const PackageFixture fixture = makePackageFixture();
            const Body::Report expected = expectedPackageReport(fixture.mFiles, { { kPayloadPath, fixture.mPayload } });
            // The fixture is a clean package (checked with the test's own mesh source).
            EXPECT_TRUE(expected.mFindings.empty()) << describeReport(expected);

            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            expectSameReport(result.mView->current().mReport, expected);
            // Without the payload's summary the validator would warn BODY-MESH-001.
            EXPECT_EQ(findCode(result.mView->current().mReport, "BODY-MESH-001"), nullptr);
        }

        TEST_F(AbViewTest, aRegionMapWithAGapListsGeom005AtTheRegions)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mRegionMap->mRegions[0].mRanges = { Body::TriangleRange{ 0, 1 } };
            fixture.mPackage.mRegionMap->mRegions[1].mRanges = { Body::TriangleRange{ 2, 4 } };
            fixture.refresh();
            const Body::Report expected = expectedPackageReport(fixture.mFiles, { { kPayloadPath, fixture.mPayload } });

            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const Body::Report& report = result.mView->current().mReport;
            expectSameReport(report, expected);
            const Body::Finding* gap = findCode(report, "BODY-GEOM-005");
            ASSERT_NE(gap, nullptr) << describeReport(report);
            EXPECT_EQ(gap->mPath, "/regionMap/regions");
            EXPECT_EQ(gap->mSeverity, Body::Severity::Error);
        }

        TEST_F(AbViewTest, aWeldGroupNamingAVertexPastTheMeshListsWeld005AndItsPointStaysAtItsFirstMember)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mWeldGroups->mGroups = { { 2, 5 }, { 3, 99 } };
            fixture.refresh();
            const Body::Report expected = expectedPackageReport(fixture.mFiles, { { kPayloadPath, fixture.mPayload } });

            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            expectSameReport(data.mReport, expected);
            const Body::Finding* weld = findCode(data.mReport, "BODY-WELD-005");
            ASSERT_NE(weld, nullptr) << describeReport(data.mReport);
            EXPECT_EQ(weld->mPath, "/weldGroups/groups/1");

            const std::vector<Point> positions = positionsInMetres(fixture.mPayload, 1.0);
            expectNearPoints(data.mWeldPoints, { positions[2], positions[3] });
            EXPECT_EQ(data.mWeldGroups, 2u);
        }

        TEST_F(AbViewTest, aMeshThatIsNotAPayloadOfThePackageWarnsMesh001AndNothingIsDrawn)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mMeshes[0].mPath = "mesh/missing.bodymesh";
            fixture.refresh();
            const Body::Report expected = expectedPackageReport(fixture.mFiles, {});
            ASSERT_NE(findCode(expected, "BODY-MESH-001"), nullptr) << describeReport(expected);

            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            expectSameReport(data.mReport, expected);
            EXPECT_FALSE(data.mMesh.has_value());
            EXPECT_FALSE(data.mMeshNote.empty());
            EXPECT_TRUE(data.mRegionLines.empty());
            EXPECT_EQ(data.mRegionLinesTotal, 0u);
            EXPECT_TRUE(data.mWeldPoints.empty());
            ASSERT_EQ(data.mRegions.size(), 2u);
            for (const ab::RegionInfo& region : data.mRegions)
            {
                EXPECT_EQ(region.mTriangles, 0u) << region.mId;
            }
        }

        TEST_F(AbViewTest, aMergedEntryIsValidatedAsThePackageDerivedFromTheCache)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            const Body::CacheManifest manifest = manifestOf(bodies);
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, bodies);
            const OpenResult result = ab::openMergedCache(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyView& view = *result.mView;
            ASSERT_EQ(view.bodyCount(), bodies.size());
            for (std::size_t i = 0; i < view.bodyCount(); ++i)
            {
                const Body::CacheEntry& entry = view.entries()[i];
                const CacheBody* body = bodyNamed(bodies, entry.mName);
                ASSERT_NE(body, nullptr) << entry.mName;
                const BodyData& data = view.body(i);
                EXPECT_TRUE(data.mPayloadError.empty()) << data.mPayloadError;
                ASSERT_TRUE(data.mPackage.has_value()) << entry.mName;
                EXPECT_TRUE(*data.mPackage == ab::mergedEntryPackage(entry, body->mBody, manifest)) << entry.mName;
                expectSameReport(data.mReport, Body::validateBodyPackage(*data.mPackage));
                ASSERT_EQ(data.mReport.mFindings.size(), 2u) << describeReport(data.mReport);
                EXPECT_EQ(data.mReport.mFindings[0].mCode, "BODY-BIND-001");
                EXPECT_EQ(data.mReport.mFindings[1].mCode, "BODY-BIND-014");
            }
        }

        // ---- the drawn mesh, its regions, lines and weld points --------------------------------------------------

        TEST_F(AbViewTest, theDrawnPackageMeshIsItsPayloadAsStoredWithItsRegionsLinesAndWelds)
        {
            const PackageFixture fixture = makePackageFixture();
            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
            const ab::DrawnMesh& mesh = *data.mMesh;

            const std::vector<Point> positions = positionsInMetres(fixture.mPayload, 1.0);
            EXPECT_EQ(mesh.mMeshId, "body");
            expectNearPoints(mesh.mPositions, positions);
            EXPECT_EQ(mesh.mTriangles, fixture.mPayload.mTriangles);
            const std::vector<int> regions = referenceTriangleRegions(*fixture.mPackage.mRegionMap, 4);
            EXPECT_EQ(regions, (std::vector<int>{ 0, 0, 1, 1 })) << "the reference";
            EXPECT_EQ(mesh.mTriangleRegion, regions);
            EXPECT_EQ(mesh.mWeldGroups, fixture.mPayload.mWeldGroups);

            ASSERT_EQ(data.mRegions.size(), 2u);
            for (std::size_t k = 0; k < 2; ++k)
            {
                EXPECT_EQ(data.mRegions[k].mId, "region-" + std::to_string(k));
                EXPECT_EQ(data.mRegions[k].mTriangles, 2u);
                EXPECT_TRUE(sameColour(data.mRegions[k].mColour, designColour(static_cast<int>(k))));
            }

            const std::vector<ab::OverlayLine> lines
                = referenceRegionLines(positions, fixture.mPayload.mTriangles, regions);
            EXPECT_EQ(lines.size(), 10u) << "two quads of five distinct edges";
            expectSameLines(data.mRegionLines, lines);
            EXPECT_EQ(data.mRegionLinesTotal, lines.size());

            expectNearPoints(data.mWeldPoints, referenceWeldPoints(positions, fixture.mPayload.mWeldGroups));
            EXPECT_EQ(data.mWeldPoints.size(), 2u);
            EXPECT_EQ(data.mWeldGroups, 2u);
            EXPECT_EQ(data.mWeldMembers, 4u);
        }

        TEST_F(AbViewTest, theDrawnMeshIsTheFirstLod0SkinPayloadThatReads)
        {
            PackageFixture fixture = makePackageFixture();
            const Body::MergedBody distant = roundTrip(makeStackedBody(1.75f, 1, 0.25f));
            const Body::MeshEntry original = fixture.mPackage.mMeshes[0];
            Body::MeshEntry lod1 = original;
            lod1.mId = "body_lod1";
            lod1.mPath = "mesh/lod1.bodymesh";
            lod1.mLod = 1;
            Body::MeshEntry broken = original;
            broken.mId = "broken";
            broken.mPath = "mesh/broken.bodymesh";
            fixture.mPackage.mMeshes = { lod1, broken, original };
            fixture.mFiles.mFiles["mesh/lod1.bodymesh"] = payloadBytes(distant);
            fixture.mFiles.mFiles["mesh/broken.bodymesh"] = bytesOf("not a payload");
            fixture.refresh();
            const Body::Report expected = expectedPackageReport(
                fixture.mFiles, { { kPayloadPath, fixture.mPayload }, { "mesh/lod1.bodymesh", distant } });

            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            expectSameReport(data.mReport, expected);
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
            EXPECT_EQ(data.mMesh->mMeshId, "body");
            expectNearPoints(data.mMesh->mPositions, positionsInMetres(fixture.mPayload, 1.0));
            EXPECT_EQ(data.mMesh->mTriangles, fixture.mPayload.mTriangles);
        }

        TEST_F(AbViewTest, theDrawnMergedMeshIsThePayloadInMetresWithASlotRegionEach)
        {
            const std::vector<CacheBody> bodies = standardCacheBodies();
            const std::filesystem::path folder = mDir / "cache";
            publishBodies(folder, bodies);
            const OpenResult result = ab::openMergedCache(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyView& view = *result.mView;
            for (std::size_t i = 0; i < view.bodyCount(); ++i)
            {
                const CacheBody* body = bodyNamed(bodies, view.entries()[i].mName);
                ASSERT_NE(body, nullptr);
                const Body::MergedBody& payload = body->mBody;
                const BodyData& data = view.body(i);
                ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
                const ab::DrawnMesh& mesh = *data.mMesh;

                const std::vector<Point> positions = positionsInMetres(payload, ab::kGameUnitsPerMetre);
                EXPECT_EQ(mesh.mMeshId, "body");
                expectNearPoints(mesh.mPositions, positions);
                EXPECT_EQ(mesh.mTriangles, payload.mTriangles);
                EXPECT_EQ(mesh.mWeldGroups, payload.mWeldGroups);

                const std::size_t triangles = payload.mTriangles.size() / 3;
                std::vector<int> regions;
                for (std::size_t t = 0; t < triangles; ++t)
                {
                    regions.push_back(static_cast<int>(t / 2));
                }
                EXPECT_EQ(mesh.mTriangleRegion, regions);

                ASSERT_EQ(data.mRegions.size(), payload.mRegions.size());
                for (std::size_t k = 0; k < payload.mRegions.size(); ++k)
                {
                    EXPECT_EQ(data.mRegions[k].mId, "slot-" + std::to_string(payload.mRegions[k].mSlot));
                    EXPECT_EQ(data.mRegions[k].mTriangles, 2u);
                    EXPECT_TRUE(sameColour(data.mRegions[k].mColour, designColour(static_cast<int>(k))));
                }

                const std::vector<ab::OverlayLine> lines = referenceRegionLines(positions, payload.mTriangles, regions);
                expectSameLines(data.mRegionLines, lines);
                EXPECT_EQ(data.mRegionLinesTotal, lines.size());

                expectNearPoints(data.mWeldPoints, referenceWeldPoints(positions, payload.mWeldGroups));
                EXPECT_EQ(data.mWeldGroups, payload.mWeldGroups.size());
                EXPECT_EQ(data.mWeldMembers, 2 * payload.mWeldGroups.size());
            }
        }

        TEST_F(AbViewTest, overlappingRangesGoToTheFirstRegion)
        {
            Body::RegionMap map;
            map.mMesh = "body";
            map.mRegions = { Body::Region{ "front", { Body::TriangleRange{ 0, 3 } } },
                Body::Region{ "back", { Body::TriangleRange{ 1, 4 } } } };
            expectDrawnRegions("overlap", map, { 0, 0, 0, 1 }, { 3, 1 });
        }

        TEST_F(AbViewTest, mapOrderDecidesBeforeRangeOrder)
        {
            // Triangle 2 is in both regions; the first region of the map wins although its range starts later.
            Body::RegionMap map;
            map.mMesh = "body";
            map.mRegions = { Body::Region{ "late", { Body::TriangleRange{ 2, 4 } } },
                Body::Region{ "early", { Body::TriangleRange{ 0, 3 } } } };
            expectDrawnRegions("maporder", map, { 1, 1, 0, 0 }, { 2, 2 });

            // A region's second range counts as much as its first.
            Body::RegionMap split;
            split.mMesh = "body";
            split.mRegions = { Body::Region{ "ends", { Body::TriangleRange{ 3, 4 }, Body::TriangleRange{ 0, 1 } } },
                Body::Region{ "all", { Body::TriangleRange{ 0, 4 } } } };
            expectDrawnRegions("split", split, { 0, 1, 1, 0 }, { 2, 2 });
        }

        TEST_F(AbViewTest, aGapLeavesTrianglesUnassignedAndTheirNewEdgesGrey)
        {
            Body::RegionMap map;
            map.mMesh = "body";
            map.mRegions = { Body::Region{ "lower", { Body::TriangleRange{ 0, 1 } } },
                Body::Region{ "upper", { Body::TriangleRange{ 2, 4 } } } };
            expectDrawnRegions("gap", map, { 0, -1, 1, 1 }, { 1, 2 });
        }

        TEST_F(AbViewTest, rangesReachingPastTheMeshColourOnlyItsTriangles)
        {
            Body::RegionMap map;
            map.mMesh = "body";
            map.mRegions = { Body::Region{ "top", { Body::TriangleRange{ 2, 4294967295u } } },
                Body::Region{ "bottom", { Body::TriangleRange{ 0, 2 } } } };
            expectDrawnRegions("past", map, { 1, 1, 0, 0 }, { 2, 2 });
        }

        TEST_F(AbViewTest, aRegionMapOfAnotherMeshColoursNothing)
        {
            PackageFixture fixture = makePackageFixture();
            fixture.mPackage.mRegionMap->mMesh = "other";
            fixture.refresh();
            writeFolder(mDir / "package", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "package");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;
            EXPECT_EQ(data.mMesh->mTriangleRegion, (std::vector<int>{ -1, -1, -1, -1 }));
            ASSERT_EQ(data.mRegions.size(), 2u);
            for (const ab::RegionInfo& region : data.mRegions)
            {
                EXPECT_EQ(region.mTriangles, 0u) << region.mId;
            }
            const std::vector<int> none(4, -1);
            expectSameLines(data.mRegionLines,
                referenceRegionLines(positionsInMetres(fixture.mPayload, 1.0), fixture.mPayload.mTriangles, none));
            for (const ab::OverlayLine& line : data.mRegionLines)
            {
                EXPECT_TRUE(sameColour(line.mColour, kDesignUnassigned));
            }
        }

        TEST_F(AbViewTest, regionLinesAreCappedAndCountedBeforeTheCap)
        {
            const std::uint32_t quads = 7500;
            const PackageFixture fixture = makeStripPackageFixture(quads);
            writeFolder(mDir / "strip", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "strip");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;

            const std::vector<int> regions(2 * static_cast<std::size_t>(quads), 0);
            const std::vector<ab::OverlayLine> all
                = referenceRegionLines(positionsInMetres(fixture.mPayload, 1.0), fixture.mPayload.mTriangles, regions);
            ASSERT_EQ(all.size(), 4 * static_cast<std::size_t>(quads) + 1);
            ASSERT_GT(all.size(), ab::kMaxRegionLines);
            EXPECT_EQ(data.mRegionLinesTotal, all.size());
            const std::vector<ab::OverlayLine> capped(
                all.begin(), all.begin() + static_cast<std::ptrdiff_t>(ab::kMaxRegionLines));
            expectSameLines(data.mRegionLines, capped);
        }

        TEST_F(AbViewTest, weldPointsAreCapped)
        {
            const std::uint32_t groups = 1001;
            PackageFixture fixture = makeStripPackageFixture(groups);
            Body::WeldGroups welds;
            welds.mMesh = "body";
            for (std::uint32_t g = 0; g < groups; ++g)
            {
                welds.mGroups.push_back({ 2 * g, 2 * g + 1 });
            }
            fixture.mPackage.mWeldGroups = welds;
            fixture.refresh();
            writeFolder(mDir / "strip", fixture.mFiles);
            const OpenResult result = ab::openPackageFolder(mDir / "strip");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyData& data = result.mView->current();
            ASSERT_TRUE(data.mMesh.has_value()) << data.mMeshNote;

            ASSERT_GT(static_cast<std::size_t>(groups), ab::kMaxWeldPoints);
            EXPECT_EQ(data.mWeldGroups, static_cast<std::size_t>(groups));
            EXPECT_EQ(data.mWeldMembers, 2 * static_cast<std::size_t>(groups));
            std::vector<Point> expected = referenceWeldPoints(positionsInMetres(fixture.mPayload, 1.0), welds.mGroups);
            expected.resize(ab::kMaxWeldPoints);
            expectNearPoints(data.mWeldPoints, expected);
        }

        // ---- the stage a numbers ---------------------------------------------------------------------------------

        TEST_F(AbViewTest, aMergedEntryShowsTheStageANumbersOfItsRaceAndSexFromTheReportBesideTheCache)
        {
            const std::filesystem::path user = mDir / "user";
            const std::filesystem::path folder = user / "openeden" / "bodies";
            publishBodies(folder, standardCacheBodies());
            const std::string report
                = reportJson({ unitText("ash folk", false, 1483, { SeamText{} }), unitText("ASH FOLK", true, 1922) });
            writeText(user / "analysis" / "bodies.json", report);
            EXPECT_TRUE(samePath(ab::analysisReportFor(folder), user / "analysis" / "bodies.json"));

            const OpenResult result = ab::openBodySource(folder / "current");
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyView& view = *result.mView;

            const std::size_t male = indexOf(view, "Ash Folk", "male");
            const BodyData& maleData = view.body(male);
            ASSERT_TRUE(maleData.mAnalysis.has_value()) << maleData.mAnalysisNote;
            EXPECT_EQ(maleData.mAnalysis->mVertices, 1483u);
            EXPECT_FALSE(maleData.mAnalysis->mFemale);
            ASSERT_EQ(maleData.mAnalysis->mSeams.size(), 1u);
            EXPECT_EQ(maleData.mAnalysis->mSeams[0].mLabel, "forearm-wrist");
            EXPECT_EQ(maleData.mAnalysis->mSeams[0].mPairs, 40u);
            EXPECT_EQ(maleData.mAnalysis->mSeams[0].mMaxDistance, 0.0011);

            const std::size_t female = indexOf(view, "Ash Folk", "female");
            const BodyData& femaleData = view.body(female);
            ASSERT_TRUE(femaleData.mAnalysis.has_value()) << femaleData.mAnalysisNote;
            EXPECT_EQ(femaleData.mAnalysis->mVertices, 1922u);
            EXPECT_TRUE(femaleData.mAnalysis->mFemale);

            // No unit for this race in the report: no numbers, and a note saying so.
            const std::size_t other = indexOf(view, "Bog Kin", "male");
            const BodyData& otherData = view.body(other);
            EXPECT_FALSE(otherData.mAnalysis.has_value());
            EXPECT_FALSE(otherData.mAnalysisNote.empty());
        }

        TEST_F(AbViewTest, withoutAStageAReportAMergedEntryHasNoNumbersButANote)
        {
            const std::filesystem::path folder = mDir / "user" / "openeden" / "bodies";
            publishBodies(folder, standardCacheBodies());
            const OpenResult result = ab::openMergedCache(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            const BodyView& view = *result.mView;
            for (std::size_t i = 0; i < view.bodyCount(); ++i)
            {
                const BodyData& data = view.body(i);
                EXPECT_FALSE(data.mAnalysis.has_value());
                EXPECT_FALSE(data.mAnalysisNote.empty());
                EXPECT_TRUE(data.mMesh.has_value()) << "a missing report is a note, not an error";
            }
        }

        TEST_F(AbViewTest, aPackageHasNoStageANumbers)
        {
            const std::filesystem::path user = mDir / "user";
            const std::filesystem::path folder = user / "packages" / "package";
            writeFolder(folder, makePackageFixture().mFiles);
            // A report where a cache in this folder would find one.
            writeText(user / "analysis" / "bodies.json", reportJson({ unitText("synthetic_race", false, 1483) }));
            const OpenResult result = ab::openPackageFolder(folder);
            ASSERT_NE(result.mView, nullptr) << result.mError;
            EXPECT_FALSE(result.mView->current().mAnalysis.has_value());
        }
    }
}
