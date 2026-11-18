// Task 0290: the Dwarven steam and sphere centurions and the Solstheim boar riekling have profile rows (their clips are
// in the actor folder's animations folder with no sex folder, under names the person rules do not know). The numbers
// are those of docs/tasks/0290-skyrim-beast-and-machine-animations.md: strides and durations from the sidecars
// (animationdata/steamproject.txt, spherecenturion.txt and their boundanims files, linked as the survey
// SkyrimCreaturesFourthSurvey prints them), heights and radii from the skin models. The boar has no sidecar in the
// installed data: its strides are an ESTIMATE from the hoof excursion.
#include <game_skyrimse/creatures.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>

namespace
{
    using namespace game_skyrimse;

    struct Row
    {
        const char* mSkeleton;
        const char* mProject;
        const char* mIdle;
        const char* mWalk;
        const char* mRun;
        float mWalkStride;
        float mWalkSeconds;
        float mRunStride;
        float mRunSeconds;
        float mHeight;
        float mRadius;
        const char* mAttack;
        const char* mPower;
        const char* mRecoil;
        const char* mStagger;
        const char* mDeath;
    };

    const Row kRows[] = {
        { "Actors/DwarvenSteamCenturion/Character Assets/skeleton.nif", "steamproject", "mtidle", "mtforward",
            "mtfastforward", 128.f, 1.33333f, 256.f, 1.33333f, 305.f, 76.7f, "h2hattackleftslash",
            "h2hattackforwardpower", "h2hrecoilleft", "stagger_light", "death" },
        { "Actors/DwarvenSphereCenturion/Character Assets/skeleton.nif", "spherecenturion", "mt_idle", "mt_forward",
            "mt_fastforward", 192.f, 1.f, 384.f, 1.f, 143.f, 46.5f, "h2h_attackslash", "h2h_attackforwardpowerchop",
            "h2h_recoil", "combat_staggerlight", "combat_staggerheavy" },
        { "Actors/DLC02/BoarRiekling/Character Assets/skeletonboar.nif", "boarproject", "mt_idle", "walkforward",
            "runforward", 75.55f, 1.06667f, 240.4f, 0.533333f, 138.f, 40.5f, "attack1", "attackpowerstanding",
            "recoil", "staggerbacksmall", "death" },
    };
}

TEST(SkyrimCreaturesFourth, the_table_has_the_three_rows_and_the_older_tables_keep_their_size)
{
    EXPECT_EQ(creatureProfilesFourth().size(), std::size(kRows));
    EXPECT_EQ(creatureProfiles().size(), 10u);
    EXPECT_EQ(creatureProfilesAll().size(), 18u);
}

TEST(SkyrimCreaturesFourth, every_row_is_found_by_its_skeleton_and_carries_the_surveyed_numbers)
{
    for (const Row& row : kRows)
    {
        const CreatureProfile* p = creatureProfileFor(row.mSkeleton);
        ASSERT_NE(p, nullptr) << row.mSkeleton;
        EXPECT_EQ(p->mProject, row.mProject);
        EXPECT_EQ(p->mIdle, row.mIdle);
        EXPECT_EQ(p->mWalk, row.mWalk);
        EXPECT_EQ(p->mRun, row.mRun);
        EXPECT_NEAR(p->mWalkStride, row.mWalkStride, 0.01f);
        EXPECT_NEAR(p->mRunStride, row.mRunStride, 0.01f);
        EXPECT_NEAR(p->mWalkSpeed, row.mWalkStride / row.mWalkSeconds, 0.05f) << row.mSkeleton;
        EXPECT_NEAR(p->mRunSpeed, row.mRunStride / row.mRunSeconds, 0.05f) << row.mSkeleton;
        EXPECT_NEAR(p->mCapsuleHeight, row.mHeight, 0.01f);
        EXPECT_NEAR(p->mCapsuleRadius, row.mRadius, 0.01f);
        EXPECT_EQ(p->mAttack, row.mAttack);
        EXPECT_EQ(p->mPower, row.mPower);
        EXPECT_EQ(p->mRecoil, row.mRecoil);
        EXPECT_EQ(p->mStagger, row.mStagger);
        EXPECT_EQ(p->mDeath, row.mDeath);
    }
}

TEST(SkyrimCreaturesFourth, the_rows_are_in_the_fourth_table_not_the_older_ones)
{
    std::set<const CreatureProfile*> older;
    for (const CreatureProfile& p : creatureProfilesAll())
        older.insert(&p);
    for (const Row& row : kRows)
    {
        const CreatureProfile* p = creatureProfileFor(row.mSkeleton);
        ASSERT_NE(p, nullptr) << row.mSkeleton;
        EXPECT_EQ(older.count(p), 0u);
        bool inFourth = false;
        for (const CreatureProfile& q : creatureProfilesFourth())
            inFourth = inFourth || &q == p;
        EXPECT_TRUE(inFourth);
    }
}

TEST(SkyrimCreaturesFourth, a_sibling_folder_or_another_creature_is_not_taken)
{
    EXPECT_EQ(creatureProfileFor("actors/dwarvenspidercenturion/character assets/skeleton.nif"), nullptr);
    // The ordinary riekling has a row since task 0301 (toddhow approved this edit on 2026-11-18).
    EXPECT_EQ(creatureProfileFor("actors/character/character assets/skeletonbeast.nif"), nullptr);
    EXPECT_EQ(creatureProfileFor("mods/actors/dwarvenspherecenturion/character assets/skeleton.nif"), nullptr);
}

TEST(SkyrimCreaturesFourth, every_name_is_a_distinct_row)
{
    std::set<std::string_view> names;
    for (const CreatureProfile& p : creatureProfilesFourth())
        names.insert(p.mName);
    EXPECT_EQ(names.size(), creatureProfilesFourth().size());
    for (const CreatureProfile& p : creatureProfilesAll())
        EXPECT_EQ(names.count(p.mName), 0u) << p.mName;
}
