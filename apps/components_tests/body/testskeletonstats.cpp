// Task 0070 (stage a): the aggregate measurement of extended skeletons (components/body/skeletonstats.hpp). Synthetic
// skeletons only (skeletonfixtures.hpp and small ones built here); no game or mod data.
#include "skeletonfixtures.hpp"

#include <components/body/skeletonstats.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Body;
    using namespace SkeletonFixtures;

    constexpr double pi = 3.14159265358979323846;

    SkeletonStatInput extendedInput()
    {
        SkeletonStatInput input;
        input.mLabel = "test.ext";
        input.mBaseLabel = "test.base";
        input.mSkeleton = extendedSkeleton();
        input.mBase = baseSkeleton();
        input.mBaseRoles = nativeRoles();
        return input;
    }

    BodySetInput massesOf(const std::string& label, std::vector<BoneWeightMass> masses)
    {
        BodySetInput set;
        set.mLabel = label;
        set.mMasses = std::move(masses);
        return set;
    }

    const HeuristicCount* findHeuristic(const SuggestionStats& stats, const std::string& heuristic, MapAction action)
    {
        for (const HeuristicCount& h : stats.mHeuristics)
        {
            if (h.mHeuristic == heuristic && h.mAction == action)
            {
                return &h;
            }
        }
        return nullptr;
    }

    // ---- the normalisations --------------------------------------------------------------------------------------

    TEST(SkeletonStatsKey, the_same_role_in_two_naming_schemes_gives_the_same_key)
    {
        EXPECT_EQ(boneComparisonKey("Bip01 L UpperArm"), "l_upper_arm");
        EXPECT_EQ(boneComparisonKey("NPC L UpperArm [LUar]"), "l_upper_arm");
        EXPECT_EQ(boneComparisonKey("Bip01 Spine1"), "spine1");
        EXPECT_EQ(boneComparisonKey("NPC Spine1 [Spn1]"), "spine1");
        EXPECT_EQ(boneComparisonKey("Bip01 R Forearm"), "r_forearm");
        EXPECT_EQ(boneComparisonKey("RightHand"), "r_hand");
        EXPECT_EQ(boneComparisonKey("Left Thigh"), "l_thigh");
    }

    TEST(SkeletonStatsKey, a_name_of_noise_tokens_only_has_an_empty_key)
    {
        EXPECT_EQ(boneComparisonKey("Bip01"), "");
        EXPECT_EQ(boneComparisonKey("NPC [Root]"), "");
    }

    TEST(SkeletonStatsKey, an_empty_key_is_never_in_the_base_set)
    {
        SkeletonStatInput input = extendedInput();
        input.mBase.push_back(makeBone("Bip01", -1, 0, 0, 0));
        input.mSkeleton.push_back(makeBone("NPC", 0, 0, 0, 0.01));
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mInBase, 17u);
        EXPECT_EQ(stats.mNotInBase, 8u);
    }

    TEST(SkeletonStatsStems, stems_drop_noise_fold_sides_and_strip_trailing_digits)
    {
        EXPECT_EQ(boneTokenStems("NPC L Breast01 [LBr1]"), (std::vector<std::string>{ "l", "breast" }));
        EXPECT_EQ(boneTokenStems("Breast_L_01"), (std::vector<std::string>{ "breast", "l", "#" }));
        EXPECT_EQ(
            boneTokenStems("NPC L UpperarmTwist1 [LUt1]"), (std::vector<std::string>{ "l", "upperarm", "twist" }));
        EXPECT_EQ(boneTokenStems("Right Cape"), (std::vector<std::string>{ "r", "cape" }));
        EXPECT_EQ(boneTokenStems("Twist Twist2"), (std::vector<std::string>{ "twist" }));
    }

    TEST(SkeletonStatsQuantiles, nearest_rank)
    {
        const Quantiles none = quantiles({});
        EXPECT_EQ(none.mCount, 0u);
        EXPECT_EQ(none.mMedian, 0.0);
        const Quantiles one = quantiles({ 5.0 });
        EXPECT_EQ(one.mMin, 5.0);
        EXPECT_EQ(one.mP25, 5.0);
        EXPECT_EQ(one.mMax, 5.0);
        const Quantiles four = quantiles({ 4.0, 1.0, 3.0, 2.0 });
        EXPECT_EQ(four.mCount, 4u);
        EXPECT_EQ(four.mMin, 1.0);
        EXPECT_EQ(four.mP25, 1.0);
        EXPECT_EQ(four.mMedian, 2.0);
        EXPECT_EQ(four.mP75, 3.0);
        EXPECT_EQ(four.mMax, 4.0);
        const Quantiles three = quantiles({ 3.0, 1.0, 2.0 });
        EXPECT_EQ(three.mP25, 1.0);
        EXPECT_EQ(three.mMedian, 2.0);
        EXPECT_EQ(three.mP75, 3.0);
    }

    // ---- one skeleton --------------------------------------------------------------------------------------------

    TEST(SkeletonStatsSkeleton, counts_bones_depth_leaves_base_set_and_extra_bones)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        EXPECT_EQ(stats.mLabel, "test.ext");
        EXPECT_EQ(stats.mBones, 24u);
        EXPECT_EQ(stats.mMaxDepth, 5u); // Pelvis 0, Spine, Spine1, UpperArm, Forearm, then Hand and the twist at 5
        EXPECT_EQ(stats.mBaseBones, 17u);
        EXPECT_EQ(stats.mInBase, 17u);
        EXPECT_EQ(stats.mNotInBase, 7u);
        EXPECT_EQ(stats.mExtra, 7u);
        EXPECT_EQ(stats.mSuggestion.mCoreRoles, 17u);
        EXPECT_TRUE(stats.mSuggestion.mMissingCoreRoles.empty());
        EXPECT_TRUE(stats.mSuggestion.mValid);
        EXPECT_EQ(stats.mSuggestion.mExtra, 7u);
        // Leaves: both hands, both feet and the seven extras (the head carries the jaw).
        EXPECT_EQ(stats.mLeaves, 11u);
    }

    TEST(SkeletonStatsSkeleton, the_token_histogram_counts_each_stem_once_per_extra_bone)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        ASSERT_GE(stats.mTokens.size(), 3u);
        EXPECT_EQ(stats.mTokens[0], (std::pair<std::string, std::size_t>{ "#", 4 }));
        EXPECT_EQ(stats.mTokens[1], (std::pair<std::string, std::size_t>{ "breast", 2 }));
        EXPECT_EQ(stats.mTokens[2], (std::pair<std::string, std::size_t>{ "l", 2 }));
        // back, cape, forearm, helper, ik, jaw, r, twist, weapon once each.
        EXPECT_EQ(stats.mTokens.size(), 12u);
        EXPECT_EQ(stats.mTokens.back(), (std::pair<std::string, std::size_t>{ "weapon", 1 }));
    }

    TEST(SkeletonStatsSkeleton, rest_offsets_of_the_extra_bones_are_quantiles_in_metres)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        EXPECT_EQ(stats.mParentOffset.mCount, 7u);
        EXPECT_NEAR(stats.mParentOffset.mMin, std::sqrt(0.04 * 0.04 + 0.05 * 0.05), 1e-9); // the jaw
        EXPECT_NEAR(stats.mParentOffset.mP25, 0.10, 1e-9);
        EXPECT_NEAR(stats.mParentOffset.mMedian, std::sqrt(0.0125), 1e-9); // a breast bone
        EXPECT_NEAR(stats.mParentOffset.mP75, std::sqrt(0.10 * 0.10 + 0.08 * 0.08), 1e-9); // the cape
        EXPECT_NEAR(stats.mParentOffset.mMax, 0.3, 1e-9); // the helper
        // Every extra bone hangs from a role bone here, so the distance to the nearest role ancestor is the same.
        EXPECT_NEAR(stats.mAncestorDistance.mMedian, stats.mParentOffset.mMedian, 1e-9);
    }

    TEST(SkeletonStatsSkeleton, the_height_is_the_z_extent_of_the_world_rest_positions)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton
            = { makeBone("Root", -1, 0, 0, 0), makeBone("Up", 0, 0, 0, 1.25), makeBone("Down", 0, 0, 0, -0.5) };
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_NEAR(stats.mHeight, 1.75, 1e-12);
    }

    TEST(SkeletonStatsSkeleton, the_suggestion_counts_by_heuristic_skeleton_only)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        const SuggestionStats& s = stats.mSuggestion;
        const HeuristicCount* breast = findHeuristic(s, "name.soft-tissue", MapAction::Secondary);
        ASSERT_NE(breast, nullptr);
        EXPECT_EQ(breast->mCount, 2u);
        EXPECT_EQ(breast->mConfidence, MapConfidence::High);
        ASSERT_TRUE(breast->mKind.has_value());
        EXPECT_EQ(*breast->mKind, SecondaryKind::Breast);
        // Without weights the helper is a question (review fix 5 of the suggestion engine).
        const HeuristicCount* helper = findHeuristic(s, "name.helper.weight-unknown", MapAction::Ask);
        ASSERT_NE(helper, nullptr);
        EXPECT_EQ(helper->mCount, 1u);
        EXPECT_EQ(s.mAsk, 1u);
        EXPECT_EQ(s.mMediumOrHigher, 6u);
    }

    TEST(SkeletonStatsSkeleton, an_ambiguous_role_is_reported_by_role_and_missing)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton = withBone(input.mSkeleton, "CME L Forearm", "Bip01 L UpperArm", 0.28, 0, 0);
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mSuggestion.mAmbiguousRoles, (std::vector<std::string>{ "LeftForearm" }));
        EXPECT_EQ(stats.mSuggestion.mMissingCoreRoles, (std::vector<std::string>{ "LeftForearm" }));
        EXPECT_EQ(stats.mSuggestion.mCoreRoles, 16u);
    }

    TEST(SkeletonStatsSkeleton, an_invalid_source_skeleton_is_reported_by_rule_id)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton.push_back(makeBone("Jaw", 4, 0, 0, 0.01)); // the name is used twice
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_FALSE(stats.mSuggestion.mValid);
        EXPECT_EQ(stats.mSuggestion.mInvalidRules, (std::vector<std::string>{ "BODY-MAP-022" }));
    }

    TEST(SkeletonStatsSkeleton, the_same_rest_pose_has_no_rest_differences)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        EXPECT_TRUE(stats.mRestValid);
        EXPECT_EQ(stats.mRestErrors, 0u);
        EXPECT_FALSE(stats.mNeedsInverseBind);
        EXPECT_EQ(stats.mRest.size(), 16u); // every core role but the root
        for (const RoleRestDifference& d : stats.mRest)
        {
            EXPECT_NEAR(d.mTranslationDelta, 0.0, 1e-12) << d.mRole;
            EXPECT_NEAR(d.mRotationAngle, 0.0, 1e-9) << d.mRole;
            EXPECT_NEAR(d.mLengthRatio, 1.0, 1e-12) << d.mRole;
        }
    }

    TEST(SkeletonStatsSkeleton, a_rotated_role_bone_needs_the_inverse_bind_correction)
    {
        SkeletonStatInput input = extendedInput();
        const int forearm = indexOf(input.mSkeleton, "Bip01 L Forearm");
        input.mSkeleton[static_cast<std::size_t>(forearm)].mRotation = { 0, 0, std::sqrt(0.5), std::sqrt(0.5) };
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_TRUE(stats.mRestValid);
        EXPECT_TRUE(stats.mNeedsInverseBind);
        for (const RoleRestDifference& d : stats.mRest)
        {
            if (d.mRole == "LeftForearm")
            {
                EXPECT_NEAR(d.mRotationAngle, pi / 2, 1e-9);
            }
            else
            {
                EXPECT_NEAR(d.mRotationAngle, 0.0, 1e-9) << d.mRole;
            }
        }
    }

    // ---- body sets -----------------------------------------------------------------------------------------------

    TEST(SkeletonStatsMasses, shares_on_role_extra_and_missing_bones)
    {
        SkeletonStatInput input = extendedInput();
        input.mBodySets.push_back(massesOf("body.a",
            { { "Bip01 Spine1", 1.0 }, { "bip01 spine1", 1.0 }, { "Breast_L_01", 1.0 }, { "Breast_R_01", 0.5 },
                { "Not In Skeleton", 0.5 } }));
        const SkeletonStats stats = computeSkeletonStats(input);
        ASSERT_EQ(stats.mBodySets.size(), 1u);
        const MassStats& m = stats.mBodySets[0];
        EXPECT_EQ(m.mLabel, "body.a");
        EXPECT_DOUBLE_EQ(m.mTotal, 4.0);
        EXPECT_EQ(m.mSkinBones, 4u); // the two spellings of the chest are one bone
        EXPECT_EQ(m.mSkinBonesMissing, 1u);
        EXPECT_DOUBLE_EQ(m.mMissingShare, 0.125);
        EXPECT_DOUBLE_EQ(m.mRoleShare, 0.5);
        EXPECT_DOUBLE_EQ(m.mExtraShare, 0.375);
        EXPECT_EQ(m.mExtraWithAnyMass, 2u);
        EXPECT_EQ(m.mExtraWithMass, 2u);
        EXPECT_EQ(m.mExtraMassBins, (std::array<std::size_t, 5>{ 5, 0, 0, 0, 2 }));
        EXPECT_DOUBLE_EQ(m.mExtraMass.mMin, 0.125);
        EXPECT_DOUBLE_EQ(m.mExtraMass.mMax, 0.25);
    }

    TEST(SkeletonStatsMasses, known_weights_change_the_helper_from_a_question_to_a_drop)
    {
        SkeletonStatInput input = extendedInput();
        input.mBodySets.push_back(massesOf("body.a", { { "Bip01 Spine1", 1.0 }, { "Breast_L_01", 1.0 } }));
        const SkeletonStats stats = computeSkeletonStats(input);
        ASSERT_EQ(stats.mBodySets.size(), 1u);
        const SuggestionStats& s = stats.mBodySets[0].mSuggestion;
        EXPECT_EQ(findHeuristic(s, "name.helper.weight-unknown", MapAction::Ask), nullptr);
        const HeuristicCount* drop = findHeuristic(s, "name.helper", MapAction::Drop);
        ASSERT_NE(drop, nullptr);
        EXPECT_EQ(drop->mCount, 1u);
        const HeuristicCount* breast = findHeuristic(s, "name.soft-tissue", MapAction::Secondary);
        ASSERT_NE(breast, nullptr);
        EXPECT_DOUBLE_EQ(breast->mShare, 0.5);
    }

    // ---- determinism and what the reports may contain ------------------------------------------------------------

    TEST(SkeletonStatsReport, the_report_does_not_depend_on_the_order_of_the_bones_or_the_masses)
    {
        SkeletonStatInput input = extendedInput();
        input.mBodySets.push_back(massesOf("body.a",
            { { "Bip01 Spine1", 2.0 }, { "Breast_L_01", 1.0 }, { "Bip01 L Forearm", 0.25 }, { "Jaw", 0.5 } }));
        const std::string expected = writeSkeletonStatsReport({ computeSkeletonStats(input) }, {});
        Lcg random(7);
        for (int run = 0; run < 10; ++run)
        {
            SkeletonStatInput shuffled = input;
            shuffled.mSkeleton = shuffledTopologically(input.mSkeleton, random);
            std::vector<BoneWeightMass>& masses = shuffled.mBodySets[0].mMasses;
            std::swap(masses[0], masses[random.below(static_cast<std::uint32_t>(masses.size()))]);
            EXPECT_EQ(writeSkeletonStatsReport({ computeSkeletonStats(shuffled) }, {}), expected) << "run " << run;
        }
    }

    TEST(SkeletonStatsReport, the_aggregate_report_never_names_a_bone_and_the_detail_does)
    {
        SkeletonStatInput input = extendedInput();
        input.mBodySets.push_back(massesOf("body.a", { { "Bip01 Spine1", 1.0 }, { "Breast_L_01", 1.0 } }));
        const SkeletonStats stats = computeSkeletonStats(input);
        const std::string report = writeSkeletonStatsReport({ stats }, {});
        for (const RestBone& bone : input.mSkeleton)
        {
            EXPECT_EQ(report.find(bone.mName), std::string::npos) << bone.mName;
        }
        EXPECT_NE(report.find("test.ext"), std::string::npos);
        EXPECT_NE(report.find("body.a"), std::string::npos);
        const std::string detail = writeSkeletonStatsDetail({ stats });
        EXPECT_NE(detail.find("Breast_L_01"), std::string::npos);
        EXPECT_NE(detail.find("Bip01 L Forearm"), std::string::npos);
    }

    TEST(SkeletonStatsReport, reader_facts_are_printed_as_given)
    {
        SkeletonStatInput input = extendedInput();
        input.mFacts = { { "nif version", "20.2.0.7" }, { "geometry", "0" } };
        const std::string report = writeSkeletonStatsReport({ computeSkeletonStats(input) }, {});
        EXPECT_NE(report.find("nif version 20.2.0.7"), std::string::npos);
        EXPECT_NE(report.find("geometry 0"), std::string::npos);
    }

    TEST(SkeletonStatsPairs, the_same_skeleton_twice_has_no_differences)
    {
        const RestPairStats pair = compareSkeletonRests("a", extendedSkeleton(), "b", extendedSkeleton());
        EXPECT_EQ(pair.mFirst, "a");
        EXPECT_EQ(pair.mSecond, "b");
        EXPECT_TRUE(pair.mValid);
        EXPECT_FALSE(pair.mNeedsInverseBind);
        EXPECT_EQ(pair.mRest.size(), 16u);
        const std::string report = writeSkeletonStatsReport({}, { pair });
        EXPECT_NE(report.find("== pair a / b"), std::string::npos);
    }

    // ---- second round (after the first run on real skeletons) ---------------------------------------------------

    TEST(SkeletonStatsStems, a_stem_that_is_a_noise_token_after_its_digits_are_removed_is_dropped)
    {
        EXPECT_EQ(boneTokenStems("Tail Bone01"), (std::vector<std::string>{ "tail" }));
    }

    // Two names that only normalisedBoneName makes equal: the strict verdict stays (BODY-MAP-022) and the suggestion
    // is measured on a relaxed copy.
    TEST(SkeletonStatsRelaxed, names_that_collide_after_normalisation_are_measured_on_a_relaxed_copy)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton = withBone(input.mSkeleton, "Bolt Quiver", "Bip01 Spine1", 0, 0.1, -0.1);
        input.mSkeleton = withBone(input.mSkeleton, "BOLT_QUIVER", "Bip01 Pelvis", 0, 0.1, -0.1);
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_FALSE(stats.mSuggestion.mValid);
        EXPECT_EQ(stats.mSuggestion.mInvalidRules, (std::vector<std::string>{ "BODY-MAP-022" }));
        EXPECT_TRUE(stats.mSuggestion.mRelaxed);
        EXPECT_EQ(stats.mSuggestion.mRenamed, 1u);
        EXPECT_EQ(stats.mExactKeyDuplicates, 0u);
        EXPECT_EQ(stats.mNormalisedOnlyCollisions, 1u);
        EXPECT_EQ(stats.mSuggestion.mCoreRoles, 17u);
        EXPECT_EQ(stats.mSuggestion.mExtra, 9u);
        EXPECT_EQ(stats.mExtra, 9u);
        const HeuristicCount* quiver = findHeuristic(stats.mSuggestion, "name.attach", MapAction::Attach);
        ASSERT_NE(quiver, nullptr);
        EXPECT_EQ(quiver->mCount, 3u); // the weapon and both quivers
        EXPECT_TRUE(stats.mRestValid);
        const std::string report = writeSkeletonStatsReport({ stats }, {});
        EXPECT_NE(report.find("relaxed"), std::string::npos);
    }

    TEST(SkeletonStatsRelaxed, an_exact_duplicate_is_counted_apart_from_a_normalisation_collision)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton.push_back(makeBone("JAW", 4, 0, 0, 0.01));
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mExactKeyDuplicates, 1u);
        EXPECT_EQ(stats.mNormalisedOnlyCollisions, 0u);
        EXPECT_TRUE(stats.mSuggestion.mRelaxed);
        EXPECT_EQ(stats.mSuggestion.mRenamed, 1u);
    }

    TEST(SkeletonStatsRelaxed, a_skeleton_invalid_for_another_reason_is_not_measured)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton.push_back(makeBone("Late", 30, 0, 0, 0.01)); // a parent that does not come first
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_FALSE(stats.mSuggestion.mValid);
        EXPECT_FALSE(stats.mSuggestion.mRelaxed);
        EXPECT_EQ(stats.mSuggestion.mExtra, 0u);
    }

    // A pack-style role table finds a role bone that tokens miss; the table's map then defines the extra bones and
    // the rest comparison.
    TEST(SkeletonStatsTable, a_role_table_resolves_what_tokens_cannot_and_defines_the_extras)
    {
        SkeletonStatInput input = extendedInput();
        const int forearm = indexOf(input.mSkeleton, "Bip01 L Forearm");
        input.mSkeleton[static_cast<std::size_t>(forearm)].mName = "LArm_ForeArm1";
        input.mRoles = nativeRoles();
        for (RoleBinding& role : input.mRoles)
        {
            if (role.mRole == "LeftForearm")
            {
                role.mBone = "LArm_ForeArm1";
            }
        }
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mSuggestion.mCoreRoles, 16u);
        EXPECT_EQ(stats.mSuggestion.mMissingCoreRoles, (std::vector<std::string>{ "LeftForearm" }));
        EXPECT_TRUE(stats.mHasRoleTable);
        EXPECT_EQ(stats.mSuggestionWithTable.mCoreRoles, 17u);
        EXPECT_TRUE(stats.mSuggestionWithTable.mMissingCoreRoles.empty());
        EXPECT_EQ(stats.mExtra, 7u);
        EXPECT_EQ(stats.mRest.size(), 16u);
        const std::string report = writeSkeletonStatsReport({ stats }, {});
        EXPECT_NE(report.find("roles (pack table)"), std::string::npos);
        EXPECT_EQ(report.find("LArm_ForeArm1"), std::string::npos);
    }

    TEST(SkeletonStatsTable, without_a_table_the_tokens_define_the_extras)
    {
        const SkeletonStats stats = computeSkeletonStats(extendedInput());
        EXPECT_FALSE(stats.mHasRoleTable);
        EXPECT_EQ(stats.mSuggestionWithTable.mExtra, 0u);
    }

    // Weight that sits on extra bones directly below role bones, at the role bone's own position, moves exactly as the
    // role bone would: such a fold loses nothing at rest.
    TEST(SkeletonStatsMasses, weight_on_extra_children_of_role_bones_at_their_position_is_measured)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton = withBone(input.mSkeleton, "L Forearm Skin", "Bip01 L Forearm", 0, 0, 0);
        input.mBodySets.push_back(massesOf("body.a", { { "L Forearm Skin", 1.0 }, { "Bip01 Spine1", 1.0 } }));
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mExtraUnderRole, 8u);
        EXPECT_EQ(stats.mExtraZeroOffset, 1u);
        ASSERT_EQ(stats.mBodySets.size(), 1u);
        EXPECT_DOUBLE_EQ(stats.mBodySets[0].mShareUnderRole, 0.5);
        EXPECT_DOUBLE_EQ(stats.mBodySets[0].mShareUnderRoleZeroOffset, 0.5);
    }

    // How far from its nearest role ancestor the weight of the extra bones sits: a fold to that ancestor is exact at
    // rest and loses only the bone's own motion; the distance says how much motion there is to lose.
    TEST(SkeletonStatsMasses, the_weight_of_extra_bones_is_binned_by_the_distance_to_the_nearest_role_ancestor)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton = withBone(input.mSkeleton, "L Forearm Skin", "Bip01 L Forearm", 0.005, 0, 0);
        input.mBodySets.push_back(massesOf("body.a",
            { { "L Forearm Skin", 1.0 }, { "L Forearm Twist", 1.0 }, { "Jaw", 1.0 }, { "IK_Helper_01", 1.0 },
                { "Bip01 Spine1", 6.0 } }));
        const SkeletonStats stats = computeSkeletonStats(input);
        ASSERT_EQ(stats.mBodySets.size(), 1u);
        const std::array<double, 5>& bins = stats.mBodySets[0].mShareByAncestorDistance;
        EXPECT_DOUBLE_EQ(bins[0], 0.1); // the skin bone, 5 mm away
        EXPECT_DOUBLE_EQ(bins[1], 0.0);
        EXPECT_DOUBLE_EQ(bins[2], 0.2); // the twist (0.10 m) and the jaw (0.064 m)
        EXPECT_DOUBLE_EQ(bins[3], 0.1); // the helper, 0.3 m away
        EXPECT_DOUBLE_EQ(bins[4], 0.0); // no extra bone without a role ancestor
    }

    // A node between two role bones (a wrapper the skeleton puts around each bone) is an extra bone on a role chain:
    // none of the map's actions passes a chain through it.
    TEST(SkeletonStatsSkeleton, extra_bones_between_role_bones_are_counted)
    {
        SkeletonStatInput input = extendedInput();
        input.mSkeleton = { makeBone("Bip01 Pelvis", -1, 0, 1.0, 0), makeBone("Pelvis Wrapper", 0, 0, 0, 0),
            makeBone("Bip01 Spine", 1, 0, 0.1, 0), makeBone("Bip01 Spine1", 2, 0, 0.15, 0),
            makeBone("Loose Leaf", 3, 0, 0.05, 0) };
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mExtra, 2u);
        EXPECT_EQ(stats.mExtraOnRoleChain, 1u);
    }

    TEST(SkeletonStatsSkeleton, untrusted_rest_values_on_a_role_path_are_counted)
    {
        SkeletonStatInput input = extendedInput();
        input.mUntrusted.assign(input.mSkeleton.size(), false);
        input.mUntrusted[static_cast<std::size_t>(indexOf(input.mSkeleton, "Bip01 Spine"))] = true; // a role bone
        input.mUntrusted[static_cast<std::size_t>(indexOf(input.mSkeleton, "Cape_01"))] = true; // an extra leaf
        const SkeletonStats stats = computeSkeletonStats(input);
        EXPECT_EQ(stats.mUntrustedOnRolePath, 1u);
    }

    TEST(SkeletonStatsManifest, role_lines_make_a_role_table_per_skeleton)
    {
        const std::string text
            = "base\tmw\tx\n"
              "skeleton\ts\tmw\ty\n"
              "role\ts\tPelvis\tNPC Pelvis [Pelv]\n"
              "role\ts\tLeftHand\tLArm_Hand\n";
        const SkelStatManifestResult result = parseSkelStatManifest(text);
        ASSERT_TRUE(result.mValue.has_value()) << result.mError;
        ASSERT_EQ(result.mValue->mRoles.size(), 2u);
        EXPECT_EQ(result.mValue->mRoles[0].mSkeleton, "s");
        EXPECT_EQ(result.mValue->mRoles[0].mRole, "Pelvis");
        EXPECT_EQ(result.mValue->mRoles[0].mBone, "NPC Pelvis [Pelv]");
        const auto errorOf = [](const std::string& manifest) {
            const SkelStatManifestResult r = parseSkelStatManifest(manifest);
            EXPECT_FALSE(r.mValue.has_value());
            return r.mError;
        };
        EXPECT_EQ(errorOf("base\tmw\tx\nskeleton\ts\tmw\ty\nrole\ts\tTail99\tb\n").rfind("line 3:", 0), 0u);
        EXPECT_EQ(
            errorOf("base\tmw\tx\nskeleton\ts\tmw\ty\nrole\ts\tPelvis\ta\nrole\ts\tPelvis\tb\n").rfind("line 4:", 0),
            0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nrole\ts\tPelvis\ta\n").rfind("line 2:", 0), 0u);
    }

    // ---- the manifest --------------------------------------------------------------------------------------------

    TEST(SkeletonStatsManifest, a_valid_manifest_keeps_its_order_and_merges_body_set_lines)
    {
        const std::string text
            = "# a comment\n"
              "\n"
              "base\tmw\tC:/x/base_anim.nif\r\n"
              "skeleton\tsk.male\tmw\tC:/y/skeleton.nif\n"
              "skeleton\tsk.female\tmw\tC:/y/skeleton female.nif\n"
              "bodyset\tsk.male.body\tsk.male\tC:/z/a.nif\n"
              "bodyset\tsk.male.body\tsk.male\tC:/z/b.nif\n"
              "compare\tsk.male\tsk.female\n";
        const SkelStatManifestResult result = parseSkelStatManifest(text);
        ASSERT_TRUE(result.mValue.has_value()) << result.mError;
        const SkelStatManifest& m = *result.mValue;
        ASSERT_EQ(m.mBases.size(), 1u);
        EXPECT_EQ(m.mBases[0].mLabel, "mw");
        EXPECT_EQ(m.mBases[0].mPath, "C:/x/base_anim.nif");
        ASSERT_EQ(m.mSkeletons.size(), 2u);
        EXPECT_EQ(m.mSkeletons[1].mLabel, "sk.female");
        EXPECT_EQ(m.mSkeletons[1].mPath, "C:/y/skeleton female.nif");
        ASSERT_EQ(m.mBodySets.size(), 1u);
        EXPECT_EQ(m.mBodySets[0].mPaths, (std::vector<std::string>{ "C:/z/a.nif", "C:/z/b.nif" }));
        ASSERT_EQ(m.mCompares.size(), 1u);
        EXPECT_EQ(m.mCompares[0], (std::pair<std::string, std::string>{ "sk.male", "sk.female" }));
    }

    TEST(SkeletonStatsManifest, errors_are_located_by_line)
    {
        const auto errorOf = [](const std::string& text) {
            const SkelStatManifestResult result = parseSkelStatManifest(text);
            EXPECT_FALSE(result.mValue.has_value());
            return result.mError;
        };
        EXPECT_EQ(errorOf("base\tmw\tx\nwhat\ta\tb\n").rfind("line 2:", 0), 0u);
        EXPECT_EQ(errorOf("base\tmw\n").rfind("line 1:", 0), 0u);
        EXPECT_EQ(errorOf("base\tm w\tx\n").rfind("line 1:", 0), 0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nbase\tmw\ty\n").rfind("line 2:", 0), 0u);
        EXPECT_EQ(errorOf("skeleton\ts\tnone\tx\n").rfind("line 1:", 0), 0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nskeleton\ts\tmw\tx\nbodyset\tb\tother\ty\n").rfind("line 3:", 0), 0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nskeleton\ts\tmw\tx\nskeleton\tt\tmw\tx\nbodyset\tb\ts\ty\nbodyset\tb\tt\tz\n")
                      .rfind("line 5:", 0),
            0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nskeleton\ts\tmw\tx\ncompare\ts\tnope\n").rfind("line 3:", 0), 0u);
        EXPECT_EQ(errorOf("base\tmw\tx\nskeleton\tmw\tmw\tx\n").rfind("line 2:", 0), 0u);
    }
}
