// Task 0069 (stage a): the data model of the stock body analysis, its totals, its aggregate table and its canonical
// JSON and text. Synthetic reports only: no game data.
#include <gtest/gtest.h>

#include <components/bodynif/analysisreport.hpp>

#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace BodyNif;

    SlotReport slot(const std::string& name, const std::string& model, std::size_t vertices, std::size_t triangles,
        bool skinned, std::vector<std::string> materials, std::uint64_t bytes = 100)
    {
        SlotReport result;
        result.mSlot = name;
        result.mRecord = name + " record";
        result.mModel = model;
        result.mFileBytes = bytes;
        result.mRead = true;
        result.mStats.mVertices = vertices;
        result.mStats.mTriangles = triangles;
        result.mStats.mSkinned = skinned;
        result.mMaterials = std::move(materials);
        return result;
    }

    SeamReport seam(
        std::size_t candidatesA, std::size_t candidatesB, double minDistance, double medianDistance, double maxDistance)
    {
        SeamReport report;
        report.mTouches = candidatesA + candidatesB > 0;
        report.mCandidatesA = candidatesA;
        report.mCandidatesB = candidatesB;
        report.mDistance.mCount = candidatesA + candidatesB;
        report.mDistance.mMin = minDistance;
        report.mDistance.mMedian = medianDistance;
        report.mDistance.mMax = maxDistance;
        report.mSignedGap.mCount = candidatesA + candidatesB;
        report.mSignedGap.mMin = -minDistance;
        report.mSignedGap.mMedian = medianDistance;
        report.mSignedGap.mMax = maxDistance;
        return report;
    }

    // ---- totals --------------------------------------------------------------------------------------------------

    TEST(BodyNifReportTotals, SumsTheSlotsAndCountsFilesAndMaterialsOnce)
    {
        UnitReport unit;
        unit.mSlots.push_back(slot("rhand", "hand.nif", 100, 50, true, { "skin.tga|-" }, 1000));
        unit.mSlots.push_back(slot("lhand", "hand.nif", 90, 45, true, { "skin.tga|-" }, 1000)); // the same file
        unit.mSlots.push_back(slot("cuirass", "chest.nif", 400, 200, false, { "skin.tga|-", "cloth.tga|a" }, 5000));
        unit.mSlots[0].mBoneNames = { "b1", "b2", "b3" };
        unit.mSlots[1].mBoneNames = { "b1", "b4" };
        SlotReport missing;
        missing.mSlot = "neck"; // no record: not read
        unit.mSlots.push_back(missing);

        computeTotals(unit);

        EXPECT_EQ(unit.mVertices, 590u);
        EXPECT_EQ(unit.mTriangles, 295u);
        EXPECT_EQ(unit.mFiles, 2u);
        EXPECT_EQ(unit.mFileBytes, 6000u);
        EXPECT_EQ(unit.mMaterials, 2u);
        EXPECT_EQ(unit.mBones, 4u);
        EXPECT_EQ(unit.mSkinnedParts, 2u);
        EXPECT_EQ(unit.mRigidParts, 1u);
    }

    // ---- aggregate -----------------------------------------------------------------------------------------------

    AnalysisReport twoUnits()
    {
        AnalysisReport report;
        UnitReport human;
        human.mRace = "Imperial";
        SeamEntry right{ "chest-upperarm", "cuirass", "rupperarm", seam(3, 2, 0.0, 0.0, 0.01) };
        right.mReport.mWithin = { 5, 5, 5, 5, 5, 5 };
        right.mReport.mOverlapPairs = 1;
        right.mReport.mMeetingPairs = 4;
        right.mReport.mWeightDisagreement.mCount = 1;
        right.mReport.mWeightDisagreement.mMax = 0.25;
        right.mReport.mUvDiffers = 2;
        right.mReport.mNormalAngle.mCount = 1;
        right.mReport.mNormalAngle.mMax = 12.0;
        SeamEntry left{ "chest-upperarm", "cuirass", "lupperarm", seam(4, 4, 0.02, 0.05, 0.4) };
        left.mReport.mWithin = { 0, 0, 0, 0, 3, 8 };
        left.mReport.mGapPairs = 8;
        left.mReport.mWeightDisagreement.mCount = 1;
        left.mReport.mWeightDisagreement.mMax = 0.5;
        left.mReport.mBoneSetDiffers = 1;
        left.mReport.mNormalAngle.mCount = 1;
        left.mReport.mNormalAngle.mMax = 30.0;
        SeamEntry apart{ "neck-chest", "neck", "cuirass", seam(0, 0, 0, 0, 0) };
        human.mSeams = { right, left, apart };
        HeadEntry head;
        head.mRecord = "head 1";
        head.mRead = true;
        head.mNeckSeam = seam(2, 2, 0.1, 0.1, 0.1);
        head.mNeckSeam.mWithin = { 0, 0, 0, 0, 4, 4 };
        human.mHeads = { head };
        report.mUnits.push_back(human);

        UnitReport beast;
        beast.mRace = "Khajiit";
        beast.mBeast = true;
        beast.mSeams = { SeamEntry{ "chest-upperarm", "cuirass", "rupperarm", seam(9, 9, 5.0, 5.0, 5.0) } };
        report.mUnits.push_back(beast);
        return report;
    }

    TEST(BodyNifReportAggregate, PoolsTheSeamsOfOneGroupByLabel)
    {
        const std::vector<SeamAggregate> rows = aggregateSeams(twoUnits(), Group::Humanoid);

        ASSERT_EQ(rows.size(), 3u); // chest-upperarm, neck-chest, head-neck (the beast's seam is not here)
        const SeamAggregate& arm = rows[0];
        EXPECT_EQ(arm.mLabel, "chest-upperarm");
        EXPECT_EQ(arm.mSeams, 2u);
        EXPECT_EQ(arm.mTouching, 2u);
        EXPECT_EQ(arm.mPairs, 13u);
        EXPECT_EQ(arm.mWithin[0], 5u);
        EXPECT_EQ(arm.mWithin[4], 8u);
        EXPECT_EQ(arm.mWithin[5], 13u);
        EXPECT_DOUBLE_EQ(arm.mMinDistance, 0.0);
        EXPECT_DOUBLE_EQ(arm.mMedianDistance, 0.0); // the nearest-rank median of the seams' medians {0, 0.05}
        EXPECT_DOUBLE_EQ(arm.mMaxDistance, 0.4);
        EXPECT_DOUBLE_EQ(arm.mMinGap, -0.02);
        EXPECT_DOUBLE_EQ(arm.mMaxGap, 0.4);
        EXPECT_EQ(arm.mOverlapPairs, 1u);
        EXPECT_EQ(arm.mGapPairs, 8u);
        EXPECT_EQ(arm.mMeetingPairs, 4u);
        EXPECT_DOUBLE_EQ(arm.mMaxWeightDisagreement, 0.5);
        EXPECT_EQ(arm.mBoneSetDiffers, 1u);
        EXPECT_EQ(arm.mUvDiffers, 2u);
        EXPECT_DOUBLE_EQ(arm.mMaxNormalAngle, 30.0);

        const SeamAggregate& neck = rows[1];
        EXPECT_EQ(neck.mLabel, "neck-chest");
        EXPECT_EQ(neck.mSeams, 1u);
        EXPECT_EQ(neck.mTouching, 0u); // a seam that does not touch is counted but has no pairs
        EXPECT_EQ(neck.mPairs, 0u);

        EXPECT_EQ(rows[2].mLabel, "head-neck");
        EXPECT_EQ(rows[2].mSeams, 1u);
        EXPECT_EQ(rows[2].mPairs, 4u);
    }

    TEST(BodyNifReportAggregate, TheBeastGroupHasItsOwnRows)
    {
        const std::vector<SeamAggregate> rows = aggregateSeams(twoUnits(), Group::Beast);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].mLabel, "chest-upperarm");
        EXPECT_EQ(rows[0].mPairs, 18u);
        EXPECT_DOUBLE_EQ(rows[0].mMinDistance, 5.0);
    }

    TEST(BodyNifReportAggregate, NoUnitsGiveNoRows)
    {
        EXPECT_TRUE(aggregateSeams(AnalysisReport{}, Group::Humanoid).empty());
    }

    // ---- JSON and text -------------------------------------------------------------------------------------------

    TEST(BodyNifReportJson, IsDeterministicAndCanonical)
    {
        AnalysisReport report = twoUnits();
        report.mUnits[0].mSlots.push_back(slot("cuirass", "chest.nif", 400, 200, true, { "skin.tga|-" }));
        const std::string first = toJson(report);
        const std::string second = toJson(report);
        EXPECT_EQ(first, second);
        ASSERT_FALSE(first.empty());
        EXPECT_EQ(first.back(), '\n');
        EXPECT_NE(first.find("\"race\": \"Imperial\""), std::string::npos);
        EXPECT_NE(first.find("\"female\": false"), std::string::npos);
        EXPECT_NE(first.find("\"chest-upperarm\""), std::string::npos);
        // a number is written in its shortest form
        EXPECT_NE(first.find("0.05"), std::string::npos);
        EXPECT_EQ(first.find("0.050000"), std::string::npos);
    }

    TEST(BodyNifReportJson, EscapesHostileStrings)
    {
        AnalysisReport report;
        UnitReport unit;
        unit.mRace = "Bad\"Race\\x\n\t\x01";
        report.mUnits.push_back(unit);

        const std::string json = toJson(report);

        EXPECT_NE(json.find("Bad\\\"Race\\\\x\\n\\t\\u0001"), std::string::npos);
        EXPECT_EQ(json.find('\t'), std::string::npos); // the tab is escaped, none left raw
        EXPECT_EQ(json.find('\x01'), std::string::npos);
    }

    TEST(BodyNifReportJson, NonFiniteNumbersAreWrittenAsNull)
    {
        AnalysisReport report;
        UnitReport unit;
        unit.mSeams.push_back(SeamEntry{ "a-b", "a", "b", SeamReport{} });
        unit.mSeams[0].mReport.mDistance.mMedian = std::numeric_limits<double>::quiet_NaN();
        report.mUnits.push_back(unit);

        const std::string json = toJson(report);

        EXPECT_NE(json.find("null"), std::string::npos);
        EXPECT_EQ(json.find(": nan"), std::string::npos);
        EXPECT_EQ(json.find(": inf"), std::string::npos);
    }

    TEST(BodyNifReportText, HasAUnitSectionAndBothAggregateTables)
    {
        const std::string text = toText(twoUnits());
        EXPECT_NE(text.find("Imperial"), std::string::npos);
        EXPECT_NE(text.find("Khajiit"), std::string::npos);
        EXPECT_NE(text.find("Humanoid seams"), std::string::npos);
        EXPECT_NE(text.find("Beast seams"), std::string::npos);
        EXPECT_NE(text.find("chest-upperarm"), std::string::npos);
        EXPECT_EQ(text, toText(twoUnits()));
    }
}
