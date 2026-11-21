// Task 0301: the creatures that kept the rest pose (task 0297 item 11) have profile rows. Each row's clip names are
// files of the creature's own animations folder (the listing of Skyrim - Animations.bsa and the meshes archives); the
// strides come from the sidecar's blocks where one exists and are named ESTIMATES (foot excursion over a stance share)
// where none does; heights and radii from the skin models (docs/tasks/0301-skyrim-creatures-rest-pose.md).
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
        const char* mName;
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
        { "mudcrab", "Actors/Mudcrab/Character Assets/skeleton.nif", "mudcrabproject", "mainidle", "forward_walk",
            "forward_run", 72.7391f, 1.f, 145.865f, 1.f, 75.f, 34.8f, "attack_leftchop", "attack_combochop",
            "recoillchop", "stagger_small", "stagger_back" },
        { "hare", "Actors/Ambient/Hare/Character Assets/skeleton.nif", "hareproject", "mt_idle", "walkforward",
            "runforward", 52.2566f, 0.833333f, 100.194f, 0.5f, 34.4f, 16.f, "", "", "", "", "" },
        { "chicken", "Actors/Ambient/Chicken/Character Assets/skeleton.nif", "chickenproject", "mt_idle", "walkforward",
            "runforward", 34.7115f, 1.f, 251.938f, 1.f, 37.2f, 16.f, "", "", "", "", "" },
        { "cow", "Actors/Cow/Character Assets/skeleton.nif", "highlandcowproject", "mt_idle", "walkforward",
            "runforward", 129.727f, 2.f, 350.164f, 0.666667f, 112.8f, 29.1f, "attack1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "deathpose" },
        { "horker", "Actors/Horker/Character Assets/skeleton.nif", "horkerproject", "mt_idle", "walkforward",
            "runforward", 49.1507f, 1.5f, 125.112f, 1.5f, 74.5f, 44.5f, "attackleft1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "death" },
        { "slaughterfish", "Actors/Slaughterfish/Character Assets/skeleton.nif", "slaughterfishproject", "mainidle",
            "swim_forward", "swim_fastforward", 600.f, 3.33333f, 600.1f, 1.66667f, 23.5f, 16.f, "attacklslash",
            "attackruppercut", "recoilright", "stagger", "stagger" },
        { "icewraith", "Actors/IceWraith/Character Assets/skeleton.nif", "icewraithproject", "idle", "runf", "runf",
            341.f, 1.06667f, 341.f, 1.06667f, 136.f, 18.2f, "attack_power", "attack_grounddive", "recoil", "stagger",
            "staggerlarge" },
        { "riekling", "Actors/DLC02/Riekling/Character Assets/skeleton.nif", "rieklingproject", "mt_idle", "mt_walkf",
            "mt_runf", 99.23f, 1.f, 177.92f, 0.666667f, 95.6f, 16.f, "mt_attack1", "spear_standingpowerattack",
            "mt_attack1_recoil", "mt_staggerb_small", "mt_staggerb_large" },
        { "scrib", "Actors/DLC02/Scrib/Character Assets/skeleton.nif", "scribproject", "mt_idle", "walkforward",
            "runforward", 204.42f, 1.f, 367.99f, 1.f, 76.9f, 50.3f, "attack1", "attackpowerstanding", "recoil",
            "staggerbacksmall", "death" },
        { "netch", "Actors/DLC02/Netch/CharacterAssets/skeleton.nif", "netchproject", "netch_idle", "netch_runforward",
            "netch_sprintforward", 200.f, 2.f, 265.f, 1.f, 430.4f, 50.6f, "netch_attackright",
            "netch_attackpowerstanding", "netch_recoilright", "netch_staggersmall", "netch_staggerlarge" },
        { "dragonpriest", "Actors/DragonPriest/Character Assets/skeleton.nif", "dragon_priest", "mt_idle", "mt_walk_f",
            "mt_run_f", 160.f, 2.f, 600.f, 2.f, 162.8f, 20.4f, "", "", "mt_stagger_b_small", "mt_stagger_b_medium",
            "deathpose" },
        { "vampirelord", "Actors/VampireLord/Character Assets/skeleton.nif", "vampirelord", "mt_idle", "mt_walkforward",
            "mt_runforward", 98.36f, 1.13333f, 230.68f, 0.633333f, 153.9f, 16.f, "mt_attackright",
            "mt_powerattackright", "mt_staggerbacksmall", "mt_staggerbacklarge", "mt_staggerbacklarge" },
        { "vampirebrute", "Actors/DLC01/VampireBrute/Character Assets/skeleton.nif", "vampirebruteproject", "idle",
            "walkforward", "runforward", 161.79f, 1.43333f, 268.83f, 0.6f, 182.4f, 33.5f, "attackr",
            "attack_standingpower", "recoilr", "stagger_small", "deathpose" },
        { "dwarvenspider", "Actors/DwarvenSpider/Character Assets/skeleton.nif", "dwarvenspidercenturionproject",
            "mainidle", "forward_walk", "forward_run", 100.037f, 1.f, 200.074f, 1.f, 56.7f, 54.6f, "attack_combolow",
            "attackcombohigh", "recoil_jump", "stagger_back", "stagger_back" },
    };
}

TEST(SkyrimCreaturesFifth, the_table_has_the_rows_and_the_older_tables_keep_their_size)
{
    EXPECT_EQ(creatureProfilesFifth().size(), std::size(kRows));
    EXPECT_EQ(creatureProfiles().size(), 10u);
    EXPECT_EQ(creatureProfilesAll().size(), 18u);
    EXPECT_EQ(creatureProfilesFourth().size(), 3u);
}

TEST(SkyrimCreaturesFifth, every_row_is_found_by_its_skeleton_and_carries_the_surveyed_numbers)
{
    for (const Row& row : kRows)
    {
        const CreatureProfile* p = creatureProfileFor(row.mSkeleton);
        ASSERT_NE(p, nullptr) << row.mSkeleton;
        EXPECT_EQ(p->mName, row.mName);
        EXPECT_EQ(p->mProject, row.mProject) << row.mName;
        EXPECT_EQ(p->mIdle, row.mIdle) << row.mName;
        EXPECT_EQ(p->mWalk, row.mWalk) << row.mName;
        EXPECT_EQ(p->mRun, row.mRun) << row.mName;
        EXPECT_NEAR(p->mWalkStride, row.mWalkStride, 0.02f) << row.mName;
        EXPECT_NEAR(p->mRunStride, row.mRunStride, 0.02f) << row.mName;
        EXPECT_NEAR(p->mWalkSpeed, row.mWalkStride / row.mWalkSeconds, 0.1f) << row.mName;
        EXPECT_NEAR(p->mRunSpeed, row.mRunStride / row.mRunSeconds, 0.1f) << row.mName;
        EXPECT_NEAR(p->mCapsuleHeight, row.mHeight, 0.01f) << row.mName;
        EXPECT_NEAR(p->mCapsuleRadius, row.mRadius, 0.01f) << row.mName;
        EXPECT_EQ(p->mAttack, row.mAttack) << row.mName;
        EXPECT_EQ(p->mPower, row.mPower) << row.mName;
        EXPECT_EQ(p->mRecoil, row.mRecoil) << row.mName;
        EXPECT_EQ(p->mStagger, row.mStagger) << row.mName;
        EXPECT_EQ(p->mDeath, row.mDeath) << row.mName;
    }
}

TEST(SkyrimCreaturesFifth, the_rows_are_in_the_fifth_table_only)
{
    std::set<const CreatureProfile*> others;
    for (const CreatureProfile& p : creatureProfilesAll())
        others.insert(&p);
    for (const CreatureProfile& p : creatureProfilesFourth())
        others.insert(&p);
    for (const Row& row : kRows)
    {
        const CreatureProfile* p = creatureProfileFor(row.mSkeleton);
        ASSERT_NE(p, nullptr) << row.mSkeleton;
        EXPECT_EQ(others.count(p), 0u) << row.mName;
        bool inFifth = false;
        for (const CreatureProfile& q : creatureProfilesFifth())
            inFifth = inFifth || &q == p;
        EXPECT_TRUE(inFifth) << row.mName;
    }
}

TEST(SkyrimCreaturesFifth, siblings_with_a_similar_folder_are_not_taken_and_the_boar_keeps_its_own_row)
{
    // The boar riekling's folder holds the ordinary riekling's name inside its own: its row stays the boar's.
    const CreatureProfile* boar = creatureProfileFor("actors/dlc02/boarriekling/character assets/skeletonboar.nif");
    ASSERT_NE(boar, nullptr);
    EXPECT_EQ(boar->mName, "boarriekling");
    EXPECT_EQ(creatureProfileFor("actors/dlc02/mudcrabash/character assets/skeleton.nif"), nullptr);
    EXPECT_EQ(creatureProfileFor("actors/dwarvenspidercenturion/character assets/skeleton.nif"), nullptr);
    EXPECT_EQ(creatureProfileFor("actors/cowbell/character assets/skeleton.nif"), nullptr);
    EXPECT_EQ(creatureProfileFor("actors/dragonpriestmask/character assets/skeleton.nif"), nullptr);
    EXPECT_EQ(creatureProfileFor("mods/actors/horker/character assets/skeleton.nif"), nullptr);
}

TEST(SkyrimCreaturesFifth, the_skeletons_of_one_folder_share_its_row)
{
    // The dragon priest's second skeleton, the necromancer's, lies in the same folder.
    const CreatureProfile* a = creatureProfileFor("actors/dragonpriest/character assets/skeleton.nif");
    const CreatureProfile* b = creatureProfileFor("actors/dragonpriest/character assets/skeletonnecropriest.nif");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
}

TEST(SkyrimCreaturesFifth, every_name_is_distinct_and_no_row_repeats_an_older_name)
{
    std::set<std::string_view> names;
    for (const CreatureProfile& p : creatureProfilesFifth())
        names.insert(p.mName);
    EXPECT_EQ(names.size(), creatureProfilesFifth().size());
    for (const CreatureProfile& p : creatureProfilesAll())
        EXPECT_EQ(names.count(p.mName), 0u) << p.mName;
    for (const CreatureProfile& p : creatureProfilesFourth())
        EXPECT_EQ(names.count(p.mName), 0u) << p.mName;
}

TEST(SkyrimCreaturesFifth, a_row_with_no_sidecar_still_names_a_project_so_it_is_not_taken_for_a_person)
{
    for (const CreatureProfile& p : creatureProfilesFifth())
        EXPECT_FALSE(p.mProject.empty()) << p.mName;
}

TEST(SkyrimCreaturesFifth, the_speeds_are_the_strides_over_the_clip_durations_and_positive)
{
    for (const CreatureProfile& p : creatureProfilesFifth())
    {
        EXPECT_GT(p.mWalkSpeed, 0.f) << p.mName;
        EXPECT_GT(p.mRunSpeed, 0.f) << p.mName;
        EXPECT_GT(p.mWalkStride, 0.f) << p.mName;
        EXPECT_GT(p.mRunStride, 0.f) << p.mName;
        EXPECT_GT(p.mCapsuleHeight, 0.f) << p.mName;
        EXPECT_GE(p.mCapsuleRadius, kCreatureMinRadius) << p.mName;
    }
}

// ---- the cast clips -----------------------------------------------------------------------------------------------

TEST(SkyrimCreaturesFifthCast, a_creature_that_casts_names_its_own_clip)
{
    const CreatureProfile* priest = creatureProfileFor("actors/dragonpriest/character assets/skeleton.nif");
    const CreatureProfile* spider = creatureProfileFor("actors/dwarvenspider/character assets/skeleton.nif");
    const CreatureProfile* chaurus = creatureProfileFor("actors/chaurus/character assets/skeleton.nif");
    ASSERT_NE(priest, nullptr);
    ASSERT_NE(spider, nullptr);
    ASSERT_NE(chaurus, nullptr);
    EXPECT_EQ(creatureCastClip(*priest, false), "mag_rh_ff_r_release");
    EXPECT_EQ(creatureCastClip(*priest, true), "mag_rh_ff_s_release");
    EXPECT_EQ(creatureCastClip(*spider, false), "attack_castinga");
    EXPECT_EQ(creatureCastClip(*spider, true), "attack_castinga");
    EXPECT_EQ(creatureCastClip(*chaurus, false), "attack_releasespit");
    EXPECT_EQ(creatureCastClip(*chaurus, true), "attack_releasespit");
}

TEST(SkyrimCreaturesFifthCast, a_creature_that_does_not_cast_has_none_and_the_person_rules_stay)
{
    const CreatureProfile* wolf = creatureProfileFor("actors/canine/character assets wolf/skeleton.nif");
    const CreatureProfile* cow = creatureProfileFor("actors/cow/character assets/skeleton.nif");
    ASSERT_NE(wolf, nullptr);
    ASSERT_NE(cow, nullptr);
    EXPECT_TRUE(creatureCastClip(*wolf, false).empty());
    EXPECT_TRUE(creatureCastClip(*cow, true).empty());
}
