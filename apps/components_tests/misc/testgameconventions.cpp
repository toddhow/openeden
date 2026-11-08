// Task 0085, move 1: the game's data conventions (components/misc/gameconventions.hpp), the `openeden.game` contract
// 1.1 that supplies them, and the pure helpers the generic sites call. Written before the sites moved.

#include <gtest/gtest.h>

#include <compat_morrowind/morrowind_compat_extension.hpp>

#include <components/extension/game_conventions.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>

#include <array>
#include <cstring>
#include <string>

namespace
{
    // Puts the defaults back whatever a test did.
    struct ConventionsGuard
    {
        ConventionsGuard() { Misc::resetGameConventions(); }
        ~ConventionsGuard() { Misc::resetGameConventions(); }
    };

    // A contract of a game that is not Morrowind: every value differs.
    // A pack written for contract 1.0: it answers only the original functions.
    class OldStyleContract : public openeden::contracts::GameContract
    {
    public:
        [[nodiscard]] std::string_view getGameId() const noexcept override { return "old"; }
        [[nodiscard]] std::string_view getGameDisplayName() const noexcept override { return "Old"; }
        [[nodiscard]] float getUnitsPerMeter() const noexcept override { return 0.0f; }
        [[nodiscard]] std::string_view getDefaultHumanoidSkeleton() const noexcept override { return "x.nif"; }
    };

    class OtherGameContract : public openeden::contracts::GameContract
    {
    public:
        [[nodiscard]] std::string_view getGameId() const noexcept override { return "other"; }
        [[nodiscard]] std::string_view getGameDisplayName() const noexcept override { return "Other"; }
        [[nodiscard]] float getUnitsPerMeter() const noexcept override { return 100.0f; }
        [[nodiscard]] std::string_view getDefaultHumanoidSkeleton() const noexcept override { return "x.nif"; }
        [[nodiscard]] HumanoidBoneNames getHumanoidBoneNames() const noexcept override
        {
            return { "pelvis_root", "pelvis_alt", "head_b", "spine_a", "spine_b" };
        }
        [[nodiscard]] std::span<const std::string_view> getSkeletonModelNamePrefixes() const noexcept override
        {
            static constexpr std::array<std::string_view, 1> sPrefixes{ "rig_" };
            return sPrefixes;
        }
        [[nodiscard]] std::string_view getFirstPersonBodyPartSuffix() const noexcept override { return "_fp"; }
        [[nodiscard]] std::string_view getFirstPersonBaseAnimationFile() const noexcept override { return "fp.nif"; }
    };
}

TEST(GameConventions, defaults_are_the_values_morrowind_had_before_the_move)
{
    const ConventionsGuard guard;
    const Misc::GameConventions& c = Misc::gameConventions();
    // Bit equality, not near: a differing float would change every converted length.
    EXPECT_EQ(std::memcmp(&c.mUnitsPerMeter, &Constants::UnitsPerMeter, sizeof(float)), 0);
    EXPECT_EQ(c.mRootBone, "bip01");
    EXPECT_EQ(c.mRootBoneFallback, "root bone");
    EXPECT_EQ(c.mHeadBone, "bip01 head");
    EXPECT_EQ(c.mSpine1Bone, "bip01 spine1");
    EXPECT_EQ(c.mSpine2Bone, "bip01 spine2");
    ASSERT_EQ(c.mSkeletonModelPrefixes.size(), 2u);
    EXPECT_EQ(c.mSkeletonModelPrefixes[0], "base_anim");
    EXPECT_EQ(c.mSkeletonModelPrefixes[1], "skin");
    EXPECT_EQ(c.mFirstPersonBodyPartSuffix, ".1st");
    EXPECT_EQ(c.mFirstPersonBaseAnimationFile, "xbase_anim.1st.nif");
}

TEST(GameConventions, set_replaces_and_reset_restores_and_old_references_stay_valid)
{
    const ConventionsGuard guard;
    const Misc::GameConventions& before = Misc::gameConventions();
    Misc::GameConventions other;
    other.mUnitsPerMeter = 12.5f;
    other.mRootBone = "r";
    Misc::setGameConventions(other);
    EXPECT_EQ(Misc::gameConventions().mUnitsPerMeter, 12.5f);
    EXPECT_EQ(Misc::gameConventions().mRootBone, "r");
    // A reader that took the reference earlier keeps a valid, unchanged snapshot.
    EXPECT_EQ(before.mRootBone, "bip01");
    Misc::resetGameConventions();
    EXPECT_EQ(Misc::gameConventions().mRootBone, "bip01");
}

TEST(GameConventions, skeleton_model_names_follow_the_prefixes)
{
    const ConventionsGuard guard;
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("base_anim.nif"));
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("base_animkna.nif"));
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("base_anim_female.nif"));
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("skin.nif"));
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("skinned_thing.nif")); // a prefix test, as before
    EXPECT_FALSE(Misc::isSkeletonModelBaseName("base_ani"));
    EXPECT_FALSE(Misc::isSkeletonModelBaseName("xbase_anim.nif"));
    EXPECT_FALSE(Misc::isSkeletonModelBaseName(""));
    EXPECT_FALSE(Misc::isSkeletonModelBaseName("rig_a.nif"));

    Misc::GameConventions other;
    other.mSkeletonModelPrefixes = { "rig_" };
    Misc::setGameConventions(other);
    EXPECT_TRUE(Misc::isSkeletonModelBaseName("rig_a.nif"));
    EXPECT_FALSE(Misc::isSkeletonModelBaseName("base_anim.nif"));
    EXPECT_FALSE(Misc::isSkeletonModelBaseName("skin.nif"));
}

TEST(GameConventions, first_person_body_part_ids_follow_the_suffix)
{
    const ConventionsGuard guard;
    EXPECT_EQ(Misc::bodyPartIdFor("b_n_nord_m_hand", false), "b_n_nord_m_hand");
    EXPECT_EQ(Misc::bodyPartIdFor("b_n_nord_m_hand", true), "b_n_nord_m_hand.1st");
    Misc::GameConventions other;
    other.mFirstPersonBodyPartSuffix = "_fp";
    Misc::setGameConventions(other);
    EXPECT_EQ(Misc::bodyPartIdFor("hand", true), "hand_fp");
    EXPECT_EQ(Misc::bodyPartIdFor("hand", false), "hand");
}

TEST(GameConventionsContract, the_morrowind_pack_provides_exactly_the_engine_defaults)
{
    const ConventionsGuard guard;
    EXPECT_EQ(openeden::contracts::GameContract::kVersion, (openeden::sdk::Version{ 1, 1 }));
    const compat_morrowind::MorrowindGameContract contract;
    const Misc::GameConventions fromPack = openeden::extension::gameConventionsFrom(contract);
    const Misc::GameConventions defaults;
    EXPECT_EQ(std::memcmp(&fromPack.mUnitsPerMeter, &defaults.mUnitsPerMeter, sizeof(float)), 0);
    EXPECT_EQ(fromPack.mRootBone, defaults.mRootBone);
    EXPECT_EQ(fromPack.mRootBoneFallback, defaults.mRootBoneFallback);
    EXPECT_EQ(fromPack.mHeadBone, defaults.mHeadBone);
    EXPECT_EQ(fromPack.mSpine1Bone, defaults.mSpine1Bone);
    EXPECT_EQ(fromPack.mSpine2Bone, defaults.mSpine2Bone);
    EXPECT_EQ(fromPack.mSkeletonModelPrefixes, defaults.mSkeletonModelPrefixes);
    EXPECT_EQ(fromPack.mFirstPersonBodyPartSuffix, defaults.mFirstPersonBodyPartSuffix);
    EXPECT_EQ(fromPack.mFirstPersonBaseAnimationFile, defaults.mFirstPersonBaseAnimationFile);
}

TEST(GameConventionsContract, another_games_contract_changes_every_value)
{
    const ConventionsGuard guard;
    const OtherGameContract contract;
    Misc::setGameConventions(openeden::extension::gameConventionsFrom(contract));
    const Misc::GameConventions& c = Misc::gameConventions();
    EXPECT_EQ(c.mUnitsPerMeter, 100.0f);
    EXPECT_EQ(c.mRootBone, "pelvis_root");
    EXPECT_EQ(c.mRootBoneFallback, "pelvis_alt");
    EXPECT_EQ(c.mHeadBone, "head_b");
    EXPECT_EQ(c.mSpine1Bone, "spine_a");
    EXPECT_EQ(c.mSpine2Bone, "spine_b");
    ASSERT_EQ(c.mSkeletonModelPrefixes.size(), 1u);
    EXPECT_EQ(c.mSkeletonModelPrefixes[0], "rig_");
    EXPECT_EQ(c.mFirstPersonBodyPartSuffix, "_fp");
    EXPECT_EQ(c.mFirstPersonBaseAnimationFile, "fp.nif");
}

TEST(GameConventionsContract, a_pack_that_specifies_nothing_leaves_the_engine_defaults)
{
    const ConventionsGuard guard;
    const OldStyleContract contract;
    const Misc::GameConventions fromPack = openeden::extension::gameConventionsFrom(contract);
    const Misc::GameConventions defaults;
    EXPECT_EQ(std::memcmp(&fromPack.mUnitsPerMeter, &defaults.mUnitsPerMeter, sizeof(float)), 0);
    EXPECT_EQ(fromPack.mRootBone, defaults.mRootBone);
    EXPECT_EQ(fromPack.mRootBoneFallback, defaults.mRootBoneFallback);
    EXPECT_EQ(fromPack.mSkeletonModelPrefixes, defaults.mSkeletonModelPrefixes);
    EXPECT_EQ(fromPack.mFirstPersonBodyPartSuffix, defaults.mFirstPersonBodyPartSuffix);
    EXPECT_EQ(fromPack.mFirstPersonBaseAnimationFile, defaults.mFirstPersonBaseAnimationFile);
}
