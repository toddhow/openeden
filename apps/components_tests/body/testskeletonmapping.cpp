// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage d): tests of the consumer API, the weight remap and the
// rest maths (components/body/skeletonmapping.*, bodyinfluences.*). Written from the spec before the implementation, on
// SYNTHETIC skeletons and meshes only.

#include <gtest/gtest.h>

#include "skeletonfixtures.hpp"

#include <components/body/bodyinfluences.hpp>
#include <components/body/skeletonmapping.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Body;
    using namespace SkeletonFixtures;

    constexpr double pi = 3.14159265358979323846;

    bool contains(const std::vector<std::string>& lines, const std::string& needle)
    {
        return std::any_of(lines.begin(), lines.end(),
            [&](const std::string& line) { return line.find(needle) != std::string::npos; });
    }

    MappingResult buildExtended(const BuildOptions& options = {})
    {
        return buildMapping(extendedMap(), extendedSkeleton(), nativeRoles(), options);
    }

    std::uint32_t targetIndex(const SkeletonMapping& mapping, const std::string& name)
    {
        const auto& names = mapping.targetBones();
        const auto it = std::find(names.begin(), names.end(), name);
        EXPECT_NE(it, names.end()) << name;
        return static_cast<std::uint32_t>(it - names.begin());
    }

    // ---- bodyinfluences: the shared weight rules (moved from the merger, so these characterise existing behaviour) --

    TEST(BodyInfluences, duplicates_add_zeros_drop_and_the_sum_is_one)
    {
        const WeightList result = normalisedWeights({ { 3, 0.5 }, { 1, 0.25 }, { 3, 0.5 }, { 2, 0.0 } });
        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[0].first, 1u);
        EXPECT_NEAR(result[0].second, 0.2, 1e-12);
        EXPECT_EQ(result[1].first, 3u);
        EXPECT_NEAR(result[1].second, 0.8, 1e-12);
    }

    TEST(BodyInfluences, an_empty_or_all_zero_input_gives_an_empty_result)
    {
        EXPECT_TRUE(normalisedWeights({}).empty());
        EXPECT_TRUE(normalisedWeights({ { 1, 0.0 }, { 2, 0.0 } }).empty());
    }

    TEST(BodyInfluences, the_four_largest_are_kept_largest_first_and_ties_go_to_the_lower_bone)
    {
        const WeightList weights = { { 1, 0.1 }, { 2, 0.3 }, { 3, 0.1 }, { 4, 0.3 }, { 5, 0.1 }, { 6, 0.1 } };
        const CutWeights cut = cutToFourWeights(normalisedWeights(weights));
        EXPECT_EQ(cut.mBone[0], 2u);
        EXPECT_EQ(cut.mBone[1], 4u);
        EXPECT_EQ(cut.mBone[2], 1u); // the 0.1 ties: the lowest bones win
        EXPECT_EQ(cut.mBone[3], 3u);
        float sum = 0;
        for (const float w : cut.mWeight)
        {
            sum += w;
        }
        EXPECT_NEAR(sum, 1.0f, 1e-6f);
    }

    TEST(BodyInfluences, fewer_than_four_leave_unused_entries_at_bone_zero_and_weight_zero)
    {
        const CutWeights cut = cutToFourWeights({ { 7, 1.0 } });
        EXPECT_EQ(cut.mBone[0], 7u);
        EXPECT_EQ(cut.mWeight[0], 1.0f);
        EXPECT_EQ(cut.mBone[1], 0u);
        EXPECT_EQ(cut.mWeight[1], 0.0f);
        EXPECT_EQ(cut.mWeight[3], 0.0f);
    }

    // ---- the consumer API refuses --------------------------------------------------------------------------

    TEST(SkeletonMappingBuild, a_valid_accepted_map_builds)
    {
        const MappingResult result = buildExtended();
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        EXPECT_TRUE(result.mErrors.empty());
        const SkeletonMapping& mapping = *result.mValue;
        EXPECT_EQ(mapping.roleBoneCount(), 17u);
        EXPECT_EQ(mapping.targetBones().size(), 21u);
        EXPECT_EQ(mapping.targetBones()[0], "Bip01 Pelvis");
        EXPECT_EQ(mapping.targetBones()[2], "Bip01 Spine1");
        EXPECT_EQ(mapping.sourceDigest(), skeletonDigest(extendedSkeleton()));
    }

    TEST(SkeletonMappingBuild, virtual_bones_follow_the_roles_sorted_by_depth_then_name)
    {
        const MappingResult result = buildExtended();
        ASSERT_TRUE(result.mValue.has_value());
        const auto& names = result.mValue->targetBones();
        EXPECT_EQ(names[17], "Breast_L_01");
        EXPECT_EQ(names[18], "Breast_R_01");
        EXPECT_EQ(names[19], "Cape_01");
        EXPECT_EQ(names[20], "WeaponBack");
        const auto& virtualBones = result.mValue->virtualBones();
        ASSERT_EQ(virtualBones.size(), 4u);
        for (const VirtualBone& bone : virtualBones)
        {
            EXPECT_EQ(bone.mParent, 2u) << bone.mName; // all hang from the chest
        }
        EXPECT_EQ(virtualBones[0].mAction, MapAction::Secondary);
        EXPECT_EQ(virtualBones[0].mKind, SecondaryKind::Breast);
        EXPECT_FALSE(virtualBones[0].mUseClipTrack);
        EXPECT_TRUE(virtualBones[2].mUseClipTrack); // the cape
        EXPECT_EQ(virtualBones[3].mAction, MapAction::Attach);
        EXPECT_EQ(virtualBones[3].mAttachPoint, "weapon.back");
    }

    TEST(SkeletonMappingBuild, an_unaccepted_entry_is_refused_and_named)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[0].mAccepted = false;
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles());
        EXPECT_FALSE(result.mValue.has_value());
        ASSERT_FALSE(result.mErrors.empty());
        EXPECT_TRUE(contains(result.mErrors, "Breast_L_01")) << result.mErrors[0];
        EXPECT_TRUE(contains(result.mErrors, "not accepted")) << result.mErrors[0];
    }

    TEST(SkeletonMappingBuild, an_ask_entry_is_refused_even_with_accept_all)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[4].mAction = MapAction::Ask;
        map.mExtra[4].mReason.clear();
        BuildOptions options;
        options.mAcceptAllSuggestions = true;
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles(), options);
        EXPECT_FALSE(result.mValue.has_value());
        EXPECT_TRUE(contains(result.mErrors, "IK_Helper_01"));
    }

    TEST(SkeletonMappingBuild, accept_all_accepts_the_unaccepted_entries_that_have_an_action)
    {
        SkeletonMap map = extendedMap();
        for (MapExtra& e : map.mExtra)
        {
            e.mAccepted = false;
        }
        EXPECT_FALSE(buildMapping(map, extendedSkeleton(), nativeRoles()).mValue.has_value());
        BuildOptions options;
        options.mAcceptAllSuggestions = true;
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles(), options);
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        ASSERT_TRUE(result.mAcceptedMap.has_value());
        for (const MapExtra& e : result.mAcceptedMap->mExtra)
        {
            EXPECT_TRUE(e.mAccepted) << e.mBone;
        }
    }

    TEST(SkeletonMappingBuild, a_digest_that_does_not_match_the_source_is_refused)
    {
        std::vector<RestBone> moved = extendedSkeleton();
        moved[4].mTranslation[1] += 0.05;
        const MappingResult result = buildMapping(extendedMap(), moved, nativeRoles());
        EXPECT_FALSE(result.mValue.has_value());
        EXPECT_TRUE(contains(result.mErrors, "digest"));
    }

    TEST(SkeletonMappingBuild, a_map_with_validation_errors_is_refused)
    {
        SkeletonMap map = extendedMap();
        map.mRoles[1].mRole = "Bogus";
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles());
        EXPECT_FALSE(result.mValue.has_value());
        EXPECT_TRUE(contains(result.mErrors, "BODY-MAP-001"));
    }

    TEST(SkeletonMappingBuild, a_role_the_actor_does_not_have_is_refused)
    {
        std::vector<RoleBinding> roles = nativeRoles();
        roles.erase(std::remove_if(roles.begin(), roles.end(), [](const RoleBinding& r) { return r.mRole == "Neck"; }),
            roles.end());
        const MappingResult result = buildMapping(extendedMap(), extendedSkeleton(), roles);
        EXPECT_FALSE(result.mValue.has_value());
        EXPECT_TRUE(contains(result.mErrors, "Neck"));
    }

    TEST(SkeletonMappingBuild, the_same_input_gives_the_same_mapping_twice)
    {
        const MappingResult a = buildExtended();
        const MappingResult b = buildExtended();
        ASSERT_TRUE(a.mValue && b.mValue);
        EXPECT_EQ(a.mValue->targetBones(), b.mValue->targetBones());
        ASSERT_EQ(a.mValue->sourceRules().size(), b.mValue->sourceRules().size());
        for (std::size_t i = 0; i < a.mValue->sourceRules().size(); ++i)
        {
            EXPECT_EQ(a.mValue->sourceRules()[i].mBone, b.mValue->sourceRules()[i].mBone);
            EXPECT_EQ(a.mValue->sourceRules()[i].mEntry, b.mValue->sourceRules()[i].mEntry);
        }
    }

    TEST(SkeletonMappingBuild, the_rest_transform_of_a_virtual_bone_is_in_its_mapped_parents_frame)
    {
        const MappingResult result = buildExtended();
        ASSERT_TRUE(result.mValue.has_value());
        const VirtualBone& breast = result.mValue->virtualBones()[0];
        EXPECT_EQ(breast.mName, "Breast_L_01");
        EXPECT_NEAR(breast.mRestTranslation[0], 0.06, 1e-9);
        EXPECT_NEAR(breast.mRestTranslation[1], 0.05, 1e-9);
        EXPECT_NEAR(breast.mRestTranslation[2], 0.08, 1e-9);
        EXPECT_NEAR(breast.mRestRotation[3], 1.0, 1e-9);
    }

    TEST(SkeletonMappingBuild, a_virtual_bone_whose_source_parent_was_dropped_gets_the_whole_chain_in_its_rest)
    {
        // Chest -> Hub (dropped, rotated 90 degrees about z, 0.1 up) -> Breast_Z (0.1 along the hub's y).
        std::vector<RestBone> skeleton = baseSkeleton();
        RestBone hub = makeBone("Hub", indexOf(skeleton, "Bip01 Spine1"), 0, 0.1, 0);
        const double h = std::sqrt(0.5);
        hub.mRotation = { 0, 0, h, h };
        skeleton.push_back(hub);
        skeleton.push_back(makeBone("Breast_Z", static_cast<int>(skeleton.size() - 1), 0, 0.1, 0));

        SkeletonMap map;
        map.mName = "hub";
        map.mSource = { static_cast<std::uint32_t>(skeleton.size()), skeletonDigest(skeleton) };
        map.mProfile = "humanoid-to-eden";
        for (const RoleBinding& role : nativeRoles())
        {
            map.mRoles.push_back({ role.mRole, role.mBone, {} });
        }
        MapExtra drop = extra("Hub", MapAction::Drop);
        drop.mReason = "a transform without weights";
        map.mExtra.push_back(drop);
        MapExtra breast = extra("Breast_Z", MapAction::Secondary);
        breast.mParent = "Chest";
        breast.mKind = SecondaryKind::Breast;
        map.mExtra.push_back(breast);

        const MappingResult result = buildMapping(map, skeleton, nativeRoles());
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        const VirtualBone& bone = result.mValue->virtualBones()[0];
        // The hub turns (0, 0.1, 0) to (-0.1, 0, 0); plus the hub's own offset (0, 0.1, 0).
        EXPECT_NEAR(bone.mRestTranslation[0], -0.1, 1e-9);
        EXPECT_NEAR(bone.mRestTranslation[1], 0.1, 1e-9);
        EXPECT_NEAR(bone.mRestTranslation[2], 0.0, 1e-9);
        EXPECT_NEAR(bone.mRestRotation[2], h, 1e-9);
        EXPECT_NEAR(bone.mRestRotation[3], h, 1e-9);
    }

    // ---- the remap table -------------------------------------------------------------------------------------

    TEST(SkeletonMappingTable, role_bones_secondary_bones_and_folds_resolve_to_the_expected_targets)
    {
        const MappingResult result = buildExtended();
        ASSERT_TRUE(result.mValue.has_value());
        const SkeletonMapping& mapping = *result.mValue;
        const SkeletonMapping::TableResult table = mapping.remapTable(
            { "Bip01 L Hand", "L Forearm Twist", "Jaw", "Breast_L_01", "WeaponBack", "IK_Helper_01" });
        ASSERT_TRUE(table.mValue.has_value());
        const auto& entries = table.mValue->mEntries;
        ASSERT_EQ(entries.size(), 6u);
        EXPECT_EQ(entries[0].mKind, RemapEntry::Kind::Direct);
        EXPECT_EQ(entries[0].mTarget, targetIndex(mapping, "Bip01 L Hand"));
        EXPECT_EQ(entries[1].mKind, RemapEntry::Kind::Direct); // nearest mapped ancestor: the forearm
        EXPECT_EQ(entries[1].mTarget, targetIndex(mapping, "Bip01 L Forearm"));
        EXPECT_EQ(entries[2].mKind, RemapEntry::Kind::Direct); // named: the head
        EXPECT_EQ(entries[2].mTarget, targetIndex(mapping, "Bip01 Head"));
        EXPECT_EQ(entries[3].mKind, RemapEntry::Kind::Direct); // a secondary is its own bone
        EXPECT_EQ(entries[3].mTarget, targetIndex(mapping, "Breast_L_01"));
        EXPECT_EQ(entries[4].mTarget, targetIndex(mapping, "WeaponBack"));
        EXPECT_EQ(entries[5].mKind, RemapEntry::Kind::Dropped);
        EXPECT_EQ(entries[5].mFallback, targetIndex(mapping, "Bip01 Pelvis")); // the helper hangs from the pelvis
    }

    TEST(SkeletonMappingTable, a_skin_bone_without_a_rule_is_an_error_naming_it)
    {
        const MappingResult result = buildExtended();
        ASSERT_TRUE(result.mValue.has_value());
        const SkeletonMapping::TableResult table
            = result.mValue->remapTable({ "Bip01 L Hand", "Nowhere_01", "Nowhere_02" });
        EXPECT_FALSE(table.mValue.has_value());
        EXPECT_TRUE(contains(table.mErrors, "Nowhere_01"));
        EXPECT_TRUE(contains(table.mErrors, "Nowhere_02"));
        EXPECT_FALSE(contains(table.mErrors, "Bip01 L Hand"));
    }

    TEST(SkeletonMappingTable, a_distribute_fold_splits_by_rest_distance_and_the_closer_bone_gets_more)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[5].mRule = FoldRule::Distribute; // Jaw
        map.mExtra[5].mTo = "Head";
        map.mExtra[5].mToSecond = "Neck";
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles());
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        const SkeletonMapping::TableResult table = result.mValue->remapTable({ "Jaw" });
        ASSERT_TRUE(table.mValue.has_value());
        const RemapEntry& entry = table.mValue->mEntries[0];
        EXPECT_EQ(entry.mKind, RemapEntry::Kind::Split);
        EXPECT_EQ(entry.mTarget, targetIndex(*result.mValue, "Bip01 Head"));
        EXPECT_EQ(entry.mTarget2, targetIndex(*result.mValue, "Bip01 Neck"));
        // The jaw is (0, -0.04, 0.05) from the head and (0, 0.06, 0.05) from the neck: d1 = 0.064031, d2 = 0.078102.
        const double d1 = std::sqrt(0.04 * 0.04 + 0.05 * 0.05);
        const double d2 = std::sqrt(0.06 * 0.06 + 0.05 * 0.05);
        EXPECT_NEAR(entry.mFraction, d2 / (d1 + d2), 1e-9);
        EXPECT_GT(entry.mFraction, 0.5);
    }

    TEST(SkeletonMappingTable, a_named_fold_to_the_role_bones_native_name_works_like_the_role_name)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[5].mTo = "Bip01 Head";
        const MappingResult result = buildMapping(map, extendedSkeleton(), nativeRoles());
        ASSERT_TRUE(result.mValue.has_value());
        const SkeletonMapping::TableResult table = result.mValue->remapTable({ "Jaw" });
        ASSERT_TRUE(table.mValue.has_value());
        EXPECT_EQ(table.mValue->mEntries[0].mTarget, targetIndex(*result.mValue, "Bip01 Head"));
    }

    TEST(SkeletonMappingTable, the_nearest_mapped_ancestor_skips_folded_and_dropped_bones)
    {
        std::vector<RestBone> skeleton = extendedSkeleton();
        skeleton = withBone(skeleton, "Twist_Child", "L Forearm Twist");
        SkeletonMap map = extendedMap();
        map.mSource = { static_cast<std::uint32_t>(skeleton.size()), skeletonDigest(skeleton) };
        MapExtra child = extra("Twist_Child", MapAction::Fold);
        child.mRule = FoldRule::NearestMappedAncestor;
        map.mExtra.push_back(child);
        const MappingResult result = buildMapping(map, skeleton, nativeRoles());
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        const SkeletonMapping::TableResult table = result.mValue->remapTable({ "Twist_Child" });
        ASSERT_TRUE(table.mValue.has_value());
        EXPECT_EQ(table.mValue->mEntries[0].mTarget, targetIndex(*result.mValue, "Bip01 L Forearm"));
    }

    // ---- the weight remap: properties ---------------------------------------------------------------------------

    RemapTable tableFor(const SkeletonMapping& mapping, const std::vector<std::string>& skinBones)
    {
        const SkeletonMapping::TableResult table = mapping.remapTable(skinBones);
        EXPECT_TRUE(table.mValue.has_value());
        return table.mValue ? *table.mValue : RemapTable{};
    }

    float weightOf(const CutWeights& cut, std::uint32_t bone)
    {
        float total = 0;
        for (std::size_t i = 0; i < maxSkinInfluences; ++i)
        {
            if (cut.mWeight[i] > 0 && cut.mBone[i] == bone)
            {
                total += cut.mWeight[i];
            }
        }
        return total;
    }

    float weightSum(const CutWeights& cut)
    {
        float sum = 0;
        for (const float w : cut.mWeight)
        {
            sum += w;
        }
        return sum;
    }

    TEST(SkeletonMappingRemap, a_twist_bones_weight_joins_its_forearm)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "Bip01 L Forearm", "L Forearm Twist", "Bip01 L Hand" });
        const CutWeights cut = remapVertex(table, { { 0, 0.3f }, { 1, 0.5f }, { 2, 0.2f } });
        const std::uint32_t forearm = targetIndex(*built.mValue, "Bip01 L Forearm");
        const std::uint32_t hand = targetIndex(*built.mValue, "Bip01 L Hand");
        EXPECT_NEAR(weightOf(cut, forearm), 0.8f, 1e-6f);
        EXPECT_NEAR(weightOf(cut, hand), 0.2f, 1e-6f);
        EXPECT_NEAR(weightSum(cut), 1.0f, 1e-6f);
        EXPECT_EQ(cut.mBone[0], forearm); // largest first
    }

    TEST(SkeletonMappingRemap, a_split_gives_the_fraction_to_the_first_target_and_the_rest_to_the_second)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[5].mRule = FoldRule::Distribute;
        map.mExtra[5].mTo = "Head";
        map.mExtra[5].mToSecond = "Neck";
        const MappingResult built = buildMapping(map, extendedSkeleton(), nativeRoles());
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "Jaw" });
        const CutWeights cut = remapVertex(table, { { 0, 1.0f } });
        const double fraction = table.mEntries[0].mFraction;
        EXPECT_NEAR(weightOf(cut, targetIndex(*built.mValue, "Bip01 Head")), fraction, 1e-6);
        EXPECT_NEAR(weightOf(cut, targetIndex(*built.mValue, "Bip01 Neck")), 1.0 - fraction, 1e-6);
    }

    TEST(SkeletonMappingRemap, dropped_weight_is_discarded_and_the_rest_renormalised)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "Bip01 L Hand", "IK_Helper_01" });
        const CutWeights cut = remapVertex(table, { { 0, 0.6f }, { 1, 0.4f } });
        EXPECT_NEAR(weightOf(cut, targetIndex(*built.mValue, "Bip01 L Hand")), 1.0f, 1e-6f);
        EXPECT_NEAR(weightSum(cut), 1.0f, 1e-6f);
    }

    TEST(SkeletonMappingRemap, a_vertex_left_with_nothing_takes_the_dropped_bones_nearest_mapped_ancestor)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "IK_Helper_01" });
        const CutWeights cut = remapVertex(table, { { 0, 1.0f } });
        EXPECT_EQ(cut.mBone[0], targetIndex(*built.mValue, "Bip01 Pelvis"));
        EXPECT_NEAR(cut.mWeight[0], 1.0f, 1e-6f);
    }

    TEST(SkeletonMappingRemap, more_than_four_targets_are_cut_to_the_four_largest_with_ties_to_the_lower_index)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const std::vector<std::string> skin
            = { "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head", "Bip01 L Hand" };
        const RemapTable table = tableFor(*built.mValue, skin);
        const CutWeights cut
            = remapVertex(table, { { 0, 0.1f }, { 1, 0.1f }, { 2, 0.2f }, { 3, 0.2f }, { 4, 0.2f }, { 5, 0.2f } });
        EXPECT_NEAR(weightSum(cut), 1.0f, 1e-6f);
        // Targets in canonical order: Pelvis 0, Spine 1, Chest 2, Neck 3, Head 4, LeftHand 7. Four 0.2 weights win.
        std::vector<std::uint32_t> kept(cut.mBone.begin(), cut.mBone.end());
        std::sort(kept.begin(), kept.end());
        EXPECT_EQ(kept, (std::vector<std::uint32_t>{ 2, 3, 4, 7 }));
        EXPECT_NEAR(cut.mWeight[0], 0.25f, 1e-6f);
    }

    TEST(SkeletonMappingRemap, non_finite_negative_and_zero_weights_count_as_nothing)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "Bip01 L Hand", "Bip01 R Hand", "Bip01 Head" });
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const CutWeights cut = remapVertex(table, { { 0, 0.5f }, { 1, nan }, { 2, -3.0f }, { 2, inf }, { 5, 1.0f } });
        EXPECT_NEAR(weightOf(cut, targetIndex(*built.mValue, "Bip01 L Hand")), 1.0f, 1e-6f);
        for (const float w : cut.mWeight)
        {
            EXPECT_TRUE(std::isfinite(w));
        }
    }

    TEST(SkeletonMappingRemap, an_empty_influence_list_gives_all_zero_without_nan)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const RemapTable table = tableFor(*built.mValue, { "Bip01 L Hand" });
        const CutWeights cut = remapVertex(table, {});
        for (const float w : cut.mWeight)
        {
            EXPECT_EQ(w, 0.0f);
        }
    }

    std::vector<std::string> allSkinBones()
    {
        std::vector<std::string> names;
        for (const RestBone& bone : extendedSkeleton())
        {
            names.push_back(bone.mName);
        }
        return names;
    }

    TEST(SkeletonMappingRemap, random_vertices_always_sum_to_one_with_valid_finite_bones_and_at_most_four_influences)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const std::vector<std::string> skin = allSkinBones();
        const RemapTable table = tableFor(*built.mValue, skin);
        Lcg random(2024);
        for (int vertex = 0; vertex < 2000; ++vertex)
        {
            std::vector<std::pair<std::uint32_t, float>> influences;
            const std::uint32_t count = 1 + random.below(8);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                influences.emplace_back(
                    random.below(static_cast<std::uint32_t>(skin.size())), static_cast<float>(random.unit()));
            }
            const CutWeights cut = remapVertex(table, influences);
            float total = 0;
            for (std::size_t i = 0; i < maxSkinInfluences; ++i)
            {
                ASSERT_TRUE(std::isfinite(cut.mWeight[i]));
                ASSERT_GE(cut.mWeight[i], 0.0f);
                ASSERT_LT(cut.mBone[i], built.mValue->targetBones().size());
                total += cut.mWeight[i];
            }
            // A vertex whose weights were all (numerically) zero is the only exception.
            float inputSum = 0;
            for (const auto& [bone, w] : influences)
            {
                inputSum += w;
            }
            if (inputSum > 1e-6f)
            {
                ASSERT_NEAR(total, 1.0f, 1e-3f) << "vertex " << vertex;
            }
        }
    }

    TEST(SkeletonMappingRemap, the_result_does_not_depend_on_the_order_of_the_input_influences)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const std::vector<std::string> skin = allSkinBones();
        const RemapTable table = tableFor(*built.mValue, skin);
        Lcg random(555);
        for (int vertex = 0; vertex < 500; ++vertex)
        {
            std::vector<std::pair<std::uint32_t, float>> influences;
            const std::uint32_t count = 1 + random.below(8);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                influences.emplace_back(random.below(static_cast<std::uint32_t>(skin.size())),
                    static_cast<float>(random.below(5)) * 0.1f + 0.1f); // coarse values: many ties
            }
            const CutWeights expected = remapVertex(table, influences);
            for (int shuffle = 0; shuffle < 5; ++shuffle)
            {
                for (std::size_t i = influences.size(); i > 1; --i)
                {
                    std::swap(influences[i - 1], influences[random.below(static_cast<std::uint32_t>(i))]);
                }
                const CutWeights actual = remapVertex(table, influences);
                ASSERT_EQ(actual.mBone, expected.mBone) << "vertex " << vertex;
                ASSERT_EQ(actual.mWeight, expected.mWeight) << "vertex " << vertex;
            }
        }
    }

    TEST(SkeletonMappingRemap, applying_an_identity_table_to_the_output_changes_nothing)
    {
        const MappingResult built = buildExtended();
        ASSERT_TRUE(built.mValue.has_value());
        const std::vector<std::string> skin = allSkinBones();
        const RemapTable table = tableFor(*built.mValue, skin);
        RemapTable identity;
        for (std::uint32_t i = 0; i < built.mValue->targetBones().size(); ++i)
        {
            RemapEntry entry;
            entry.mKind = RemapEntry::Kind::Direct;
            entry.mTarget = i;
            identity.mEntries.push_back(entry);
        }
        Lcg random(31);
        for (int vertex = 0; vertex < 500; ++vertex)
        {
            std::vector<std::pair<std::uint32_t, float>> influences;
            const std::uint32_t count = 1 + random.below(8);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                influences.emplace_back(
                    random.below(static_cast<std::uint32_t>(skin.size())), static_cast<float>(random.unit()) + 0.01f);
            }
            const CutWeights once = remapVertex(table, influences);
            std::vector<std::pair<std::uint32_t, float>> again;
            for (std::size_t i = 0; i < maxSkinInfluences; ++i)
            {
                if (once.mWeight[i] > 0)
                {
                    again.emplace_back(once.mBone[i], once.mWeight[i]);
                }
            }
            const CutWeights twice = remapVertex(identity, again);
            ASSERT_EQ(twice.mBone, once.mBone) << vertex;
            for (std::size_t i = 0; i < maxSkinInfluences; ++i)
            {
                ASSERT_NEAR(twice.mWeight[i], once.mWeight[i], 1e-6f) << vertex;
            }
        }
    }

    // ---- the rest maths ----------------------------------------------------------------------------------------

    TEST(SkeletonMappingRest, a_rest_matrix_turns_then_moves_a_point)
    {
        const double h = std::sqrt(0.5);
        const Mat4 m = restMatrix({ 1, 2, 3 }, { 0, 0, h, h }); // 90 degrees about z, then +(1, 2, 3)
        const std::array<double, 3> p = transformPoint({ 1, 0, 0 }, m);
        EXPECT_NEAR(p[0], 1.0, 1e-12);
        EXPECT_NEAR(p[1], 3.0, 1e-12);
        EXPECT_NEAR(p[2], 3.0, 1e-12);
    }

    TEST(SkeletonMappingRest, multiply_applies_the_left_matrix_first)
    {
        const Mat4 move = restMatrix({ 1, 0, 0 }, { 0, 0, 0, 1 });
        const double h = std::sqrt(0.5);
        const Mat4 turn = restMatrix({ 0, 0, 0 }, { 0, 0, h, h });
        const std::array<double, 3> p = transformPoint({ 0, 0, 0 }, multiply(move, turn)); // move, then turn
        EXPECT_NEAR(p[0], 0.0, 1e-12);
        EXPECT_NEAR(p[1], 1.0, 1e-12);
    }

    TEST(SkeletonMappingRest, the_inverse_of_a_rest_matrix_undoes_it)
    {
        const double a = 0.3;
        const Mat4 m
            = restMatrix({ 0.4, -1.2, 2.5 }, { std::sin(a / 2) * 0.6, std::sin(a / 2) * 0.8, 0, std::cos(a / 2) });
        const std::optional<Mat4> inv = inverse(m);
        ASSERT_TRUE(inv.has_value());
        const Mat4 product = multiply(m, *inv);
        const Mat4 identity;
        for (std::size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(product.m[i], identity.m[i], 1e-12) << i;
        }
    }

    TEST(SkeletonMappingRest, a_singular_or_non_finite_matrix_has_no_inverse)
    {
        Mat4 singular;
        singular.m[0] = 0;
        EXPECT_FALSE(inverse(singular).has_value());
        Mat4 bad;
        bad.m[12] = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(inverse(bad).has_value());
    }

    TEST(SkeletonMappingRest, world_rest_matrices_compose_the_chain)
    {
        const std::vector<RestBone> skeleton = baseSkeleton();
        const std::vector<Mat4> world = worldRestMatrices(skeleton);
        ASSERT_EQ(world.size(), skeleton.size());
        const std::array<double, 3> head = transformPoint({ 0, 0, 0 }, world[4]);
        EXPECT_NEAR(head[0], 0.0, 1e-12);
        EXPECT_NEAR(head[1], 1.55, 1e-12);
        const std::array<double, 3> hand = transformPoint({ 0, 0, 0 }, world[7]);
        EXPECT_NEAR(hand[0], 0.15 + 0.28 + 0.25, 1e-12);
        EXPECT_NEAR(hand[1], 1.35, 1e-12); // 1.0 + 0.10 + 0.15 + the upper arm's 0.10
    }

    TEST(SkeletonMappingRest, an_invalid_parent_gives_no_matrices)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[2].mParent = 40;
        EXPECT_TRUE(worldRestMatrices(skeleton).empty());
    }

    // A small skinned mesh: points bound to bones of a chain; skin matrix = inverseBind * boneWorld (row vectors).
    struct SkinnedPoint
    {
        std::array<double, 3> mPosition;
        std::size_t mBone;
    };

    std::vector<RestBone> chain(double elbowAngle)
    {
        std::vector<RestBone> bones
            = { makeBone("root", -1, 0, 0, 0), makeBone("upper", 0, 0, 0.5, 0), makeBone("lower", 1, 0, 0.5, 0) };
        bones[2].mRotation = { 0, 0, std::sin(elbowAngle / 2), std::cos(elbowAngle / 2) };
        return bones;
    }

    double worstRestError(const std::vector<SkinnedPoint>& points, const std::vector<Mat4>& inverseBind,
        const std::vector<Mat4>& poseWorld)
    {
        double worst = 0;
        for (const SkinnedPoint& point : points)
        {
            const Mat4 skin = multiply(inverseBind[point.mBone], poseWorld[point.mBone]);
            const std::array<double, 3> moved = transformPoint(point.mPosition, skin);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                worst = std::max(worst, std::abs(moved[axis] - point.mPosition[axis]));
            }
        }
        return worst;
    }

    TEST(SkeletonMappingRest, the_inverse_bind_correction_keeps_rest_positions_and_without_it_they_move)
    {
        // The mesh was made for a skeleton whose elbow is bent 20 degrees; the actor's rest has it straight.
        const std::vector<RestBone> source = chain(0.35);
        const std::vector<RestBone> actor = chain(0.0);
        const std::vector<Mat4> sourceWorld = worldRestMatrices(source);
        const std::vector<Mat4> actorWorld = worldRestMatrices(actor);
        ASSERT_EQ(sourceWorld.size(), 3u);
        std::vector<Mat4> inverseBind;
        for (const Mat4& m : sourceWorld)
        {
            inverseBind.push_back(*inverse(m));
        }
        std::vector<SkinnedPoint> points;
        Lcg random(9);
        for (int i = 0; i < 60; ++i)
        {
            const std::size_t bone = random.below(3);
            const std::array<double, 3> local = { random.unit() - 0.5, random.unit() - 0.5, random.unit() - 0.5 };
            points.push_back({ transformPoint(local, sourceWorld[bone]), bone }); // a world point at the source's rest
        }
        // Without the correction the actor's rest pose moves the lower arm's points.
        EXPECT_GT(worstRestError(points, inverseBind, actorWorld), 1e-2);
        // With it, every point stays where the mesh put it.
        std::vector<Mat4> corrected;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::optional<Mat4> c = correctedInverseBind(inverseBind[i], sourceWorld[i], actorWorld[i]);
            ASSERT_TRUE(c.has_value());
            corrected.push_back(*c);
        }
        EXPECT_LT(worstRestError(points, corrected, actorWorld), 1e-4);
    }

    TEST(SkeletonMappingRest, a_singular_actor_matrix_gives_no_correction)
    {
        Mat4 singular;
        singular.m[5] = 0;
        EXPECT_FALSE(correctedInverseBind(Mat4{}, Mat4{}, singular).has_value());
    }

    TEST(SkeletonMappingRest, matching_rests_need_no_correction_and_the_correction_is_then_the_identity_effect)
    {
        const std::vector<RestBone> source = chain(0.35);
        const std::vector<Mat4> world = worldRestMatrices(source);
        const Mat4 ib = *inverse(world[2]);
        const std::optional<Mat4> c = correctedInverseBind(ib, world[2], world[2]);
        ASSERT_TRUE(c.has_value());
        for (std::size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(c->m[i], ib.m[i], 1e-12) << i;
        }
    }

    // ---- comparing rests -----------------------------------------------------------------------------------------

    TEST(SkeletonMappingRest, compare_rest_reports_translation_length_and_rotation_per_role)
    {
        std::vector<RestBone> source = extendedSkeleton();
        source[7].mTranslation = { 0.30, 0, 0 }; // the left hand: 0.25 -> 0.30 along the same direction
        const double angle = 0.1;
        source[4].mRotation = { 0, 0, std::sin(angle / 2), std::cos(angle / 2) }; // the head turned 0.1 rad
        SkeletonMap map = extendedMap();
        map.mSource.mDigest = skeletonDigest(source);
        const std::vector<RestBone> actor = baseSkeleton();
        const std::vector<RoleRestDifference> differences = compareRest(map, source, actor, nativeRoles());
        const RoleRestDifference* hand = nullptr;
        const RoleRestDifference* head = nullptr;
        const RoleRestDifference* neck = nullptr;
        for (const RoleRestDifference& d : differences)
        {
            if (d.mRole == "LeftHand")
            {
                hand = &d;
            }
            if (d.mRole == "Head")
            {
                head = &d;
            }
            if (d.mRole == "Neck")
            {
                neck = &d;
            }
        }
        ASSERT_NE(hand, nullptr);
        ASSERT_NE(head, nullptr);
        ASSERT_NE(neck, nullptr);
        EXPECT_NEAR(hand->mTranslationDelta, 0.05, 1e-9);
        EXPECT_NEAR(hand->mLengthRatio, 1.2, 1e-9);
        EXPECT_NEAR(hand->mRotationAngle, 0.0, 1e-9);
        EXPECT_NEAR(head->mRotationAngle, 0.1, 1e-9);
        EXPECT_NEAR(head->mTranslationDelta, 0.0, 1e-9);
        EXPECT_NEAR(neck->mTranslationDelta, 0.0, 1e-9);
        EXPECT_NEAR(neck->mRotationAngle, 0.0, 1e-9);
    }

    TEST(SkeletonMappingRest, compare_rest_skips_the_root_role_and_lists_roles_in_canonical_order)
    {
        const SkeletonMap map = extendedMap();
        const std::vector<RoleRestDifference> differences
            = compareRest(map, extendedSkeleton(), baseSkeleton(), nativeRoles());
        ASSERT_FALSE(differences.empty());
        for (const RoleRestDifference& d : differences)
        {
            EXPECT_NE(d.mRole, "Pelvis");
        }
        std::vector<std::string> order;
        for (const RoleInfo& role : canonicalRoles())
        {
            order.emplace_back(role.mName);
        }
        std::size_t last = 0;
        for (const RoleRestDifference& d : differences)
        {
            const std::size_t position
                = static_cast<std::size_t>(std::find(order.begin(), order.end(), d.mRole) - order.begin());
            EXPECT_GE(position, last);
            last = position;
        }
    }

    TEST(SkeletonMappingRest, compare_rest_measures_across_bones_that_the_map_does_not_use_as_roles)
    {
        // A mod skeleton with an extra bone between the chest and the neck: the neck's difference is measured
        // against the chest, through the extra bone.
        std::vector<RestBone> source = baseSkeleton();
        source.insert(source.begin() + 3, makeBone("Spine_Extra", 2, 0, 0.05, 0));
        for (std::size_t i = 4; i < source.size(); ++i)
        {
            if (source[i].mParent >= 3)
            {
                source[i].mParent += 1;
            }
        }
        source[4].mParent = 3; // the neck now hangs from the extra bone
        source[4].mTranslation = { 0, 0.15, 0 }; // the neck: 0.05 + 0.15 = 0.20 from the chest, as in the actor
        SkeletonMap map;
        map.mName = "extra-spine";
        map.mSource = { static_cast<std::uint32_t>(source.size()), skeletonDigest(source) };
        map.mProfile = "humanoid-to-eden";
        for (const RoleBinding& role : nativeRoles())
        {
            map.mRoles.push_back({ role.mRole, role.mBone, {} });
        }
        MapExtra spine = extra("Spine_Extra", MapAction::Fold);
        spine.mRule = FoldRule::NearestMappedAncestor;
        map.mExtra.push_back(spine);
        const std::vector<RoleRestDifference> differences = compareRest(map, source, baseSkeleton(), nativeRoles());
        for (const RoleRestDifference& d : differences)
        {
            if (d.mRole == "Neck")
            {
                EXPECT_NEAR(d.mTranslationDelta, 0.0, 1e-9);
                return;
            }
        }
        FAIL() << "no difference reported for the neck";
    }

    TEST(SkeletonMappingRest, the_plan_needs_the_correction_only_when_a_rotation_is_above_the_tolerance)
    {
        std::vector<RoleRestDifference> differences = { { "Head", 0.0, 1.0, 0.0 }, { "LeftHand", 0.05, 1.2, 0.0 } };
        EXPECT_FALSE(planRest(differences).mNeedsInverseBindCorrection);
        differences.push_back({ "Neck", 0.0, 1.0, sRestRotationTolerance * 0.99 });
        EXPECT_FALSE(planRest(differences).mNeedsInverseBindCorrection);
        differences.push_back({ "Chest", 0.0, 1.0, sRestRotationTolerance * 1.01 });
        const RestPlan plan = planRest(differences);
        EXPECT_TRUE(plan.mNeedsInverseBindCorrection);
        EXPECT_EQ(plan.mDifferences.size(), differences.size());
        EXPECT_FALSE(planRest(differences, 1.0).mNeedsInverseBindCorrection);
        EXPECT_FALSE(planRest({}).mNeedsInverseBindCorrection);
    }
}
