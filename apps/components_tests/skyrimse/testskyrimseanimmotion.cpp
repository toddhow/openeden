// Task 0135 stage S6f: the root motion of Skyrim character clips from the animation data text files, and the package
// decoding the stage added (PTDA targets, activities, XLKR links). Generated text and records only; the installed data
// is checked by the gated test in testskyrimsenpcactivitiesreal.cpp.
#include <game_skyrimse/anim_motion.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    constexpr double kPi = 3.14159265358979323846;

    // The shape of defaultmale.txt around two clips (names, numbers, the "1 0 0" and an event list), plus noise lines.
    const std::string kProject = "Chair_FrontEnterVar1\n469\n1\n0\n0\n2\nSoundPlay:0\nChairIdle:2.63333\n\n"
                                 "Chair_LeftEnter\n478\n1\n0\n0\n2\nSoundPlay:0\nChairIdle:1.56667\n\n"
                                 "42\n43\n1\n0\n0\n";

    // The shape of anims_defaultmale.txt: number, duration, translation rows (t x y z), rotation rows (t qx qy qz qw).
    const std::string kBound = "2\n"
                               "469\n2.83333\n3\n0.5 -9 10 0\n1.5 -3 40 0\n2.83333 0 63.5 0\n"
                               "2\n0 0 0 0 1\n2.83333 0 0 -1 0.000746564\n"
                               "478\n1.76667\n2\n0.8 0 20 0\n1.76667 0 52 0\n"
                               "2\n0 0 0 0 1\n1.76667 0 0 0.707107 0.707107\n";

    TEST(SkyrimAnimMotion, clipNumbersComeFromTheNameLineBeforeTheNumber)
    {
        const auto numbers = parseClipNumbers(kProject);
        ASSERT_EQ(numbers.count("chair_frontentervar1"), 1u);
        EXPECT_EQ(numbers.at("chair_frontentervar1"), 469);
        EXPECT_EQ(numbers.at("chair_leftenter"), 478);
        EXPECT_EQ(numbers.size(), 2u); // "42 / 43" is a number pair, not a name
    }

    TEST(SkyrimAnimMotion, aMotionBlockHasItsDurationRowsAndTotalTurn)
    {
        const auto blocks = parseMotionBlocks(kBound);
        ASSERT_EQ(blocks.count(469), 1u);
        const ClipMotion& m = blocks.at(469);
        EXPECT_NEAR(m.mDuration, 2.83333, 1e-4);
        ASSERT_EQ(m.mTranslation.size(), 3u);
        EXPECT_NEAR(m.mTranslation[2].mY, 63.5, 1e-4);
        ASSERT_EQ(m.mRotation.size(), 2u);
        EXPECT_NEAR(std::abs(m.mRotation[1].mYaw), kPi, 2e-3); // a half turn (the sign of an exact half turn is ambiguous)
        ASSERT_EQ(blocks.count(478), 1u);
        EXPECT_NEAR(blocks.at(478).mRotation[1].mYaw, kPi / 2, 1e-4);
    }

    TEST(SkyrimAnimMotion, sampleInterpolatesFromTheOriginAndHoldsTheEnd)
    {
        const auto blocks = parseMotionBlocks(kBound);
        const ClipMotion& m = blocks.at(478);
        double x = 9, y = 9, yaw = 9;
        m.sample(0.f, x, y, yaw);
        EXPECT_DOUBLE_EQ(x, 0);
        EXPECT_DOUBLE_EQ(y, 0);
        EXPECT_DOUBLE_EQ(yaw, 0);
        m.sample(0.4f, x, y, yaw); // half way to the first row (0.8 s, 20 units)
        EXPECT_NEAR(y, 10, 1e-4);
        m.sample(1.28333f, x, y, yaw); // half way between the rows
        EXPECT_NEAR(y, 36, 1e-3);
        EXPECT_NEAR(yaw, (kPi / 2) * (1.28333 / 1.76667), 1e-3); // the turn rows are 0 at t = 0 and a quarter circle at the end
        m.sample(100.f, x, y, yaw);
        EXPECT_NEAR(y, 52, 1e-4);
        EXPECT_NEAR(yaw, kPi / 2, 1e-4);
        m.total(x, y, yaw);
        EXPECT_NEAR(y, 52, 1e-4);
        m.sample(-1.f, x, y, yaw);
        EXPECT_DOUBLE_EQ(y, 0);
        m.sample(std::nanf(""), x, y, yaw);
        EXPECT_DOUBLE_EQ(y, 0);
    }

    TEST(SkyrimAnimMotion, theTurnIsUnwrappedAlongTheRows)
    {
        // 170 degrees, then 190 written as -170: the turn keeps counting up, it does not jump back.
        const double a = 170 * kPi / 180, b = 190 * kPi / 180;
        auto q = [](double yaw, double& z, double& w) {
            z = std::sin(yaw / 2);
            w = std::cos(yaw / 2);
        };
        double z1, w1, z2, w2;
        q(a, z1, w1);
        q(b, z2, w2);
        const std::string text = "7\n1\n1\n1 0 5 0\n3\n0 0 0 0 1\n1 0 0 " + std::to_string(z1) + " " + std::to_string(w1) + "\n2 0 0 "
            + std::to_string(z2) + " " + std::to_string(w2) + "\n";
        const auto blocks = parseMotionBlocks(text);
        ASSERT_EQ(blocks.count(7), 1u);
        const ClipMotion& m = blocks.at(7);
        ASSERT_EQ(m.mRotation.size(), 3u);
        EXPECT_NEAR(m.mRotation[1].mYaw, a, 1e-3);
        EXPECT_NEAR(m.mRotation[2].mYaw, b, 1e-3);
    }

    TEST(SkyrimAnimMotion, theTableJoinsNamesAndMotionCaseInsensitivelyAndKeepsTheFirstReading)
    {
        MotionTable table;
        EXPECT_EQ(table.add(kProject, kBound), 2u);
        EXPECT_EQ(table.size(), 2u);
        const ClipMotion* m = table.find("CHAIR_LEFTENTER");
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mDuration, 1.76667, 1e-4);
        EXPECT_EQ(table.find("chair_nothing"), nullptr);
        // A second reading of the same names changes nothing and adds none.
        EXPECT_EQ(table.add(kProject, "469\n9\n1\n1 0 1 0\n1\n0 0 0 0 1\n"), 0u);
        EXPECT_NEAR(table.find("chair_frontentervar1")->mDuration, 2.83333, 1e-4);
    }

    TEST(SkyrimAnimMotion, hostileTextGivesAnEmptyTableNotAnError)
    {
        MotionTable table;
        EXPECT_EQ(table.add("", ""), 0u);
        EXPECT_EQ(table.add("garbage\n\x01\x02\n\n\n", "9999999999\n1e999\n-5\nx y z\n"), 0u);
        // A row count far beyond the text: the block is skipped, the scan goes on and finds the real one after it.
        const auto blocks = parseMotionBlocks("5\n1\n999999999\n1 2 3 4\n" + kBound);
        EXPECT_EQ(blocks.count(5), 0u);
        EXPECT_EQ(blocks.count(469), 1u);
        // A non-finite number never reaches a row.
        const auto bad = parseMotionBlocks("5\n1\n1\n1 nan 3 0\n1\n0 0 0 0 1\n");
        EXPECT_EQ(bad.count(5), 0u);
    }

    // ---- package decoding added in S6f -----------------------------------------------------------------------------

    TEST(SkyrimPackageActivity, patrolFollowAndEscortAreRecognisedAndSandboxWins)
    {
        auto act = [](std::vector<std::string> names) { return classifyActivity(names); };
        EXPECT_EQ(act({ "Patrol" }), PackageActivity::Patrol);
        EXPECT_EQ(act({ "Guard", "Patrol", "Patrol", "Travel", "Follow" }), PackageActivity::Patrol);
        EXPECT_EQ(act({ "Follow" }), PackageActivity::Follow);
        EXPECT_EQ(act({ "FollowTo", "Travel" }), PackageActivity::Follow);
        EXPECT_EQ(act({ "Escort", "Travel" }), PackageActivity::Escort);
        EXPECT_EQ(act({ "Travel", "UnlockDoors", "Sandbox" }), PackageActivity::None);
        EXPECT_EQ(act({ "Patrol", "Follow", "Sandbox", "Guard" }), PackageActivity::None); // a sandbox package sandboxes
        EXPECT_EQ(act({ "Patrol", "Sleep" }), PackageActivity::None);
        EXPECT_EQ(act({ "HoldPosition" }), PackageActivity::None);
        EXPECT_EQ(act({}), PackageActivity::None);
        EXPECT_EQ(act({ "PATROL" }), PackageActivity::Patrol);
    }

    TEST(SkyrimPackageActivity, theIndexFollowsATemplateForTheActivityAndTheTarget)
    {
        PackageIndex index;
        PackageRecords records;
        PackageRecord tmpl;
        tmpl.mCommon.mId = 0x100;
        tmpl.mProcedures = { "Patrol" };
        tmpl.mTargets = { { PackageTargetType::SpecificReference, 0x77, 3 } };
        PackageRecord derived;
        derived.mCommon.mId = 0x101;
        derived.mTemplate = 0x100;
        PackageRecord own;
        own.mCommon.mId = 0x102;
        own.mTemplate = 0x100;
        own.mProcedures = { "Follow" };
        own.mTargets = { { PackageTargetType::ReferenceAlias, 4, 0 } };
        records.mPackages = { tmpl, derived, own };
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("test.esp", false, err));
        const Content::Header header;
        index.add(std::move(records), FormIdResolver(header, "test.esp", order));
        const PackageRecord* d = index.package(0x101);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(index.activityOf(*d), PackageActivity::Patrol);
        PackageTarget target;
        ASSERT_TRUE(index.targetOf(*d, target));
        EXPECT_EQ(target.mType, PackageTargetType::SpecificReference);
        EXPECT_EQ(target.mValue, 0x77u);
        const PackageRecord* o = index.package(0x102);
        ASSERT_NE(o, nullptr);
        EXPECT_EQ(index.activityOf(*o), PackageActivity::Follow);
        ASSERT_TRUE(index.targetOf(*o, target));
        EXPECT_EQ(target.mType, PackageTargetType::ReferenceAlias);
    }

    TEST(SkyrimPlacedRefLinks, aLinkIsAKeywordAndAReferenceAndAnIdleIsAFloat)
    {
        PlacedRef ref;
        EXPECT_TRUE(ref.mLinks.empty());
        EXPECT_LT(ref.mPatrolIdle, 0.f);
        RefLink link;
        EXPECT_EQ(link.mKeyword, kNullFormId);
        EXPECT_EQ(link.mRef, kNullFormId);
        EXPECT_EQ(kMaxRefLinks, 16u);
    }
}
