#include <components/havokpack/idle.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>

// Task 0290: a skeleton model with no skeleton file of its own name takes the actor folder's shared skeleton.hkx.
// Expected paths are the installed Skyrim SE listing (Skyrim - Meshes0.bsa): `actors/character/character assets/` holds
// skeleton.hkx and `skeletonbeast.nif`; `... character assets female/` holds skeleton_female.hkx and
// `skeletonbeast_female.nif`; `actors/dlc02/boarriekling/character assets/` holds skeleton.hkx and skeletonboar.nif.
namespace
{
    using namespace HavokPack;
}

TEST(HavokSkeletonFallback, a_beast_skeleton_falls_back_to_the_shared_male_skeleton)
{
    EXPECT_EQ(skeletonHkxFallbackPath("Actors\\Character\\Character Assets\\skeletonBeast.nif"),
        "meshes/actors/character/character assets/skeleton.hkx");
}

TEST(HavokSkeletonFallback, a_female_folder_falls_back_to_skeleton_female)
{
    EXPECT_EQ(skeletonHkxFallbackPath("meshes/actors/character/character assets female/skeletonbeast_female.nif"),
        "meshes/actors/character/character assets female/skeleton_female.hkx");
}

TEST(HavokSkeletonFallback, a_dlc_creature_model_falls_back_in_its_own_folder)
{
    EXPECT_EQ(skeletonHkxFallbackPath("actors/dlc02/boarriekling/character assets/skeletonboar.nif"),
        "meshes/actors/dlc02/boarriekling/character assets/skeleton.hkx");
}

TEST(HavokSkeletonFallback, there_is_no_fallback_when_it_is_the_derived_path_or_the_input_is_no_nif)
{
    EXPECT_EQ(skeletonHkxFallbackPath("actors/character/character assets/skeleton.nif"), "");
    EXPECT_EQ(skeletonHkxFallbackPath("actors/character/character assets female/skeleton_female.nif"), "");
    EXPECT_EQ(skeletonHkxFallbackPath("actors/a/skeleton.hkx"), "");
    EXPECT_EQ(skeletonHkxFallbackPath(""), "");
    EXPECT_EQ(skeletonHkxFallbackPath("skeletonbeast.nif"), ""); // no folder
}

TEST(HavokSkeletonFallback, the_derived_file_wins_when_it_exists)
{
    const std::set<std::string> files{ "meshes/actors/a/character assets/skeletonx.hkx",
        "meshes/actors/a/character assets/skeleton.hkx" };
    const auto exists = [&](const std::string& p) { return files.count(p) != 0; };
    EXPECT_EQ(chooseSkeletonHkx("actors/a/character assets/skeletonx.nif", exists),
        "meshes/actors/a/character assets/skeletonx.hkx");
}

TEST(HavokSkeletonFallback, the_fallback_is_taken_when_only_it_exists)
{
    const std::set<std::string> files{ "meshes/actors/character/character assets/skeleton.hkx" };
    const auto exists = [&](const std::string& p) { return files.count(p) != 0; };
    EXPECT_EQ(chooseSkeletonHkx("actors/character/character assets/skeletonbeast.nif", exists),
        "meshes/actors/character/character assets/skeleton.hkx");
}

TEST(HavokSkeletonFallback, neither_existing_gives_the_derived_path_so_the_error_names_it)
{
    const auto none = [](const std::string&) { return false; };
    EXPECT_EQ(chooseSkeletonHkx("actors/character/character assets/skeletonbeast.nif", none),
        "meshes/actors/character/character assets/skeletonbeast.hkx");
    EXPECT_EQ(chooseSkeletonHkx("not a nif", none), "");
}
