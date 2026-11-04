// Tests of the fixes of the fresh review of task 0070, stages b to d (written before the fixes, red first): bone names
// follow the engine's case-insensitive lookup, core roles are required, the actor's whole bone set is seen, the map has
// a digest of its own, helper bones of unknown weight are asked about, rest values are checked, comparisons fail
// closed, names are UTF-8, an optional role the actor lacks folds, duplicate JSON keys are refused, and one conversion
// helper ties the row-vector matrices here to the merger's and VSG's. Synthetic data only.

#include "skeletonfixtures.hpp"

#include <components/misc/strings/lower.hpp>

#include <gtest/gtest.h>

#include <vsg/maths/transform.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace SkeletonFixtures;

    bool hasErrorContaining(const std::vector<std::string>& errors, const std::string& text)
    {
        for (const std::string& e : errors)
        {
            if (e.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    const SkeletonMapping::SourceRule* ruleFor(const SkeletonMapping& mapping, const std::string& bone)
    {
        for (const SkeletonMapping::SourceRule& rule : mapping.sourceRules())
        {
            if (rule.mBone == bone)
            {
                return &rule;
            }
        }
        return nullptr;
    }

    // ---- (1) bone names as the engine compares them ----------------------------------------------------------------

    TEST(SkeletonMapReview, the_engine_key_is_the_engines_lower_case)
    {
        for (const std::string& name : { std::string("Bip01 L Hand"), std::string("BIP01 SPINE1"),
                 std::string("npc L Breast [LBr]"), std::string("caf\xc3\xa9 Bone"), std::string("A_b-C.d:E") })
        {
            EXPECT_EQ(engineBoneKey(name), Misc::StringUtils::lowerCase(name)) << name;
        }
    }

    TEST(SkeletonMapReview, a_skin_bone_matches_its_rule_whatever_its_case)
    {
        const MappingResult result = buildMapping(extendedMap(), extendedSkeleton(), nativeRoles());
        ASSERT_TRUE(result.mValue.has_value());
        const auto table = result.mValue->remapTable({ "BIP01 L HAND", "bip01 spine1", "Breast_l_01" });
        ASSERT_TRUE(table.mValue.has_value()) << (table.mErrors.empty() ? "" : table.mErrors.front());
        EXPECT_EQ(table.mValue->mEntries.size(), 3u);
    }

    TEST(SkeletonMapReview, the_validator_finds_source_bones_whatever_their_case)
    {
        SkeletonMap map = extendedMap();
        for (MapRole& role : map.mRoles)
        {
            if (role.mRole == "Pelvis")
            {
                role.mBone = "BIP01 PELVIS";
            }
        }
        const std::vector<RestBone> source = extendedSkeleton();
        const MapReport report = validateSkeletonMap(map, &source);
        EXPECT_EQ(report.count("BODY-MAP-005"), 0u);
        EXPECT_EQ(report.count("BODY-MAP-006"), 0u);
    }

    TEST(SkeletonMapReview, an_alias_that_normalises_to_an_extra_bone_is_refused)
    {
        SkeletonMap map = extendedMap();
        for (MapRole& role : map.mRoles)
        {
            if (role.mRole == "Chest")
            {
                role.mAliases = { "breast_l_01" };
            }
        }
        const MapReport report = validateSkeletonMap(map);
        EXPECT_GT(report.count("BODY-MAP-017"), 0u);
    }

    // ---- (2) core roles ------------------------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_map_without_a_core_role_is_refused)
    {
        SkeletonMap map = extendedMap();
        map.mRoles.erase(
            std::remove_if(map.mRoles.begin(), map.mRoles.end(), [](const MapRole& r) { return r.mRole == "Neck"; }),
            map.mRoles.end());
        MapExtra neck = extra("Bip01 Neck", MapAction::Fold);
        map.mExtra.push_back(neck);
        const std::vector<RestBone> source = extendedSkeleton();
        const MapReport report = validateSkeletonMap(map, &source);
        EXPECT_GT(report.count("BODY-MAP-024"), 0u);
        EXPECT_FALSE(buildMapping(map, source, nativeRoles()).mValue.has_value());
    }

    TEST(SkeletonMapReview, a_suggestion_writes_its_missing_core_roles_and_asks_about_weighted_unknown_bones)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[3].mName = "Bip01 Gizmo"; // the neck, under a name nothing recognises
        SuggestInput input;
        input.mName = "test";
        input.mProfile = "humanoid-to-eden";
        input.mSkeleton = skeleton;
        input.mHasMasses = true;
        input.mMasses = { { "Bip01 Gizmo", 0.02 } };
        input.mRoles = nativeRoles();
        input.mRoles[3].mBone = "Bip01 Neck"; // the table's name, not the skeleton's
        const SuggestResult result = suggestSkeletonMap(input);
        EXPECT_EQ(result.mMap.mMissingRoles, std::vector<std::string>{ "Neck" });
        const MapExtra* gizmo = findExtra(result.mMap, "Bip01 Gizmo");
        ASSERT_NE(gizmo, nullptr);
        EXPECT_EQ(gizmo->mAction, MapAction::Ask);
        const std::string text = writeSkeletonMap(result.mMap);
        EXPECT_NE(text.find("\"missingRoles\""), std::string::npos) << text;
        const SkeletonMapResult back = readSkeletonMap(text, "missing.skeletonmap.json");
        ASSERT_TRUE(back.mValue.has_value()) << back.mError.message;
        EXPECT_EQ(*back.mValue, result.mMap);
        EXPECT_EQ(writeSkeletonMap(*back.mValue), text);
    }

    // ---- (3) the actor's whole bone set ------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_source_bone_the_actor_also_has_maps_onto_the_actors_bone)
    {
        std::vector<RestBone> source = withBone(baseSkeleton(), "Bip01 L Clavicle", "Bip01 Spine1", 0.05, 0.1, 0);
        SkeletonMap map = extendedMap();
        map.mExtra.clear();
        map.mSource.mBoneCount = static_cast<std::uint32_t>(source.size());
        map.mSource.mDigest = skeletonDigest(source);
        map.mExtra.push_back(extra("Bip01 L Clavicle", MapAction::Fold));
        BuildOptions options;
        options.mActorBones = { "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 L Clavicle", "Bip01 Neck" };
        const MappingResult result = buildMapping(map, source, nativeRoles(), options);
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors.front());
        const SkeletonMapping::SourceRule* rule = ruleFor(*result.mValue, "Bip01 L Clavicle");
        ASSERT_NE(rule, nullptr);
        ASSERT_EQ(rule->mEntry.mKind, RemapEntry::Kind::Direct);
        EXPECT_EQ(result.mValue->targetBones().at(rule->mEntry.mTarget), "Bip01 L Clavicle");
        EXPECT_FALSE(result.mNotes.empty());
    }

    TEST(SkeletonMapReview, a_virtual_bone_with_the_name_of_an_actor_role_bone_is_refused)
    {
        std::vector<RoleBinding> roles = nativeRoles();
        roles[4].mBone = "Cape_01"; // the actor's head bone happens to be called like the source's cape
        BuildOptions options;
        for (const RoleBinding& r : roles)
        {
            options.mActorBones.push_back(r.mBone);
        }
        const MappingResult result = buildMapping(extendedMap(), extendedSkeleton(), roles, options);
        EXPECT_FALSE(result.mValue.has_value());
        EXPECT_TRUE(hasErrorContaining(result.mErrors, "Cape_01"));
    }

    // ---- (4) the map's own digest --------------------------------------------------------------------------------

    TEST(SkeletonMapReview, the_map_digest_changes_with_the_map_and_the_source_digest_does_not)
    {
        const SkeletonMap first = extendedMap();
        SkeletonMap second = extendedMap();
        for (MapExtra& e : second.mExtra)
        {
            if (e.mBone == "Cape_01")
            {
                e.mUseClipTrack = false;
            }
        }
        EXPECT_EQ(skeletonMapDigest(first), skeletonMapDigest(first));
        EXPECT_NE(skeletonMapDigest(first), skeletonMapDigest(second));
        EXPECT_EQ(skeletonMapDigest(first).size(), 64u);
        const MappingResult a = buildMapping(first, extendedSkeleton(), nativeRoles());
        const MappingResult b = buildMapping(second, extendedSkeleton(), nativeRoles());
        ASSERT_TRUE(a.mValue && b.mValue);
        EXPECT_EQ(a.mValue->sourceDigest(), b.mValue->sourceDigest());
        EXPECT_NE(a.mValue->mapDigest(), b.mValue->mapDigest());
        EXPECT_EQ(a.mValue->mapDigest(), skeletonMapDigest(*a.mAcceptedMap));
    }

    TEST(SkeletonMapReview, the_ambiguity_note_does_not_follow_the_input_order)
    {
        // Written after the fix (the order of the candidates in the note), not red first.
        std::vector<RestBone> skeleton = withBone(baseSkeleton(), "L Toe", "Bip01 L Foot");
        skeleton = withBone(skeleton, "Left Toes", "Bip01 L Foot");
        SuggestInput input;
        input.mName = "test";
        input.mProfile = "humanoid-to-eden";
        input.mSkeleton = skeleton;
        const SuggestResult forward = suggestSkeletonMap(input);
        std::swap(input.mSkeleton[17], input.mSkeleton[18]);
        const SuggestResult swapped = suggestSkeletonMap(input);
        ASSERT_FALSE(forward.mNotes.empty());
        EXPECT_EQ(forward.mNotes, swapped.mNotes);
    }

    // ---- (5) helper bones of unknown weight ------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_helper_bone_of_unknown_weight_is_asked_about)
    {
        const std::vector<RestBone> skeleton = baseWith("Handle_L", "Bip01 L Hand");
        SuggestInput input;
        input.mName = "test";
        input.mProfile = "humanoid-to-eden";
        input.mSkeleton = skeleton;
        input.mHasMasses = false;
        input.mRoles = nativeRoles();
        const SuggestResult result = suggestSkeletonMap(input);
        const MapExtra* handle = findExtra(result.mMap, "Handle_L");
        ASSERT_NE(handle, nullptr);
        EXPECT_EQ(handle->mAction, MapAction::Ask);
    }

    // ---- (6) rest values
    // ---------------------------------------------------------------------------------------------

    TEST(SkeletonMapReview, rest_values_that_are_not_finite_or_absurd_are_refused)
    {
        for (const double bad :
            { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1.0e40, -1.0e40 })
        {
            std::vector<RestBone> skeleton = baseSkeleton();
            skeleton[5].mTranslation[1] = bad;
            EXPECT_GT(validateSourceSkeleton(skeleton).count("BODY-MAP-025"), 0u) << bad;
            EXPECT_EQ(skeletonDigest(skeleton).size(), 64u);
        }
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[5].mRotation = { 0, 0, 0, std::numeric_limits<double>::quiet_NaN() };
        EXPECT_GT(validateSourceSkeleton(skeleton).count("BODY-MAP-025"), 0u);
        skeleton[5].mRotation = { 0, 0, 0, 0 };
        EXPECT_GT(validateSourceSkeleton(skeleton).count("BODY-MAP-025"), 0u);
    }

    // ---- (7) rest comparisons fail closed ------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_rest_comparison_with_an_invalid_skeleton_is_an_error_not_a_pass)
    {
        std::vector<RestBone> actor = baseSkeleton();
        actor[2].mParent = 7; // after itself
        const RestComparison comparison = compareRest(extendedMap(), extendedSkeleton(), actor, nativeRoles());
        EXPECT_FALSE(comparison.mErrors.empty());
        const RestPlan plan = planRest(comparison);
        EXPECT_FALSE(plan.mValid);
    }

    TEST(SkeletonMapReview, a_role_whose_actor_bone_is_missing_is_an_error)
    {
        std::vector<RoleBinding> roles = nativeRoles();
        roles[3].mBone = "No Such Bone";
        const RestComparison comparison = compareRest(extendedMap(), extendedSkeleton(), baseSkeleton(), roles);
        EXPECT_TRUE(hasErrorContaining(comparison.mErrors, "No Such Bone"));
        EXPECT_FALSE(planRest(comparison).mValid);
    }

    TEST(SkeletonMapReview, an_actor_role_bone_is_found_whatever_its_case)
    {
        std::vector<RoleBinding> roles = nativeRoles();
        roles[3].mBone = "BIP01 NECK";
        const RestComparison comparison = compareRest(extendedMap(), extendedSkeleton(), baseSkeleton(), roles);
        EXPECT_TRUE(comparison.mErrors.empty()) << comparison.mErrors.front();
        EXPECT_TRUE(planRest(comparison).mValid);
    }

    // ---- (8) UTF-8
    // ---------------------------------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_bone_name_that_is_not_utf8_is_refused)
    {
        std::vector<RestBone> skeleton = baseWith("Breast \xe9");
        EXPECT_GT(validateSourceSkeleton(skeleton).count("BODY-MAP-020"), 0u);
        SkeletonMap map = extendedMap();
        map.mExtra.front().mBone = "Breast \xe9";
        EXPECT_GT(validateSkeletonMap(map).count("BODY-MAP-016"), 0u);
    }

    TEST(SkeletonMapReview, a_utf8_bone_name_round_trips)
    {
        const std::vector<RestBone> skeleton = baseWith("Brust \xc3\xbc");
        SuggestInput input;
        input.mName = "test";
        input.mProfile = "humanoid-to-eden";
        input.mSkeleton = skeleton;
        input.mRoles = nativeRoles();
        const SuggestResult result = suggestSkeletonMap(input);
        const std::string text = writeSkeletonMap(result.mMap);
        const SkeletonMapResult back = readSkeletonMap(text, "utf8.skeletonmap.json");
        ASSERT_TRUE(back.mValue.has_value()) << back.mError.message;
        EXPECT_EQ(*back.mValue, result.mMap);
    }

    // ---- (9) an optional role the actor lacks --------------------------------------------------------------------

    TEST(SkeletonMapReview, an_optional_role_the_actor_lacks_folds_into_its_canonical_parent)
    {
        std::vector<RestBone> source = baseSkeleton();
        source.push_back(makeBone("Bip01 Spine2", 2, 0, 0.1, 0));
        SkeletonMap map = extendedMap();
        map.mExtra.clear();
        map.mSource.mBoneCount = static_cast<std::uint32_t>(source.size());
        map.mSource.mDigest = skeletonDigest(source);
        map.mRoles.push_back({ "Spine2", "Bip01 Spine2", {} });
        const MappingResult result = buildMapping(map, source, nativeRoles());
        ASSERT_TRUE(result.mValue.has_value()) << (result.mErrors.empty() ? "" : result.mErrors.front());
        const SkeletonMapping::SourceRule* rule = ruleFor(*result.mValue, "Bip01 Spine2");
        ASSERT_NE(rule, nullptr);
        ASSERT_EQ(rule->mEntry.mKind, RemapEntry::Kind::Direct);
        EXPECT_EQ(result.mValue->targetBones().at(rule->mEntry.mTarget), "Bip01 Spine1"); // the actor's Chest
        EXPECT_TRUE(hasErrorContaining(result.mNotes, "Spine2"));
    }

    // ---- (10) role names and bone names ----------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_bone_named_like_a_role_it_is_not_is_warned_about)
    {
        SkeletonMap map = extendedMap();
        map.mExtra.front().mBone = "Chest";
        const MapReport report = validateSkeletonMap(map);
        EXPECT_GT(report.count("BODY-MAP-026"), 0u);
    }

    // ---- (11) one conversion between the matrix conventions ------------------------------------------------------

    TEST(SkeletonMapReview, the_row_vector_matrix_converts_to_the_mergers_placement_and_to_vsg)
    {
        const std::array<double, 3> t{ 0.25, -1.5, 3.0 };
        const double half = 0.3;
        const std::array<double, 4> q{ std::sin(half) * 0.6, std::sin(half) * 0.0, std::sin(half) * 0.8,
            std::cos(half) };
        const Mat4 rest = restMatrix(t, q);
        const Mat34 placement = toPlacement(rest);
        const std::array<double, 3> p{ 0.7, -0.2, 1.1 };
        const std::array<double, 3> expected = transformPoint(p, rest);
        for (std::size_t r = 0; r < 3; ++r)
        {
            const double v = placement[r * 4] * p[0] + placement[r * 4 + 1] * p[1] + placement[r * 4 + 2] * p[2]
                + placement[r * 4 + 3];
            EXPECT_NEAR(v, expected[r], 1e-12) << r;
        }
        EXPECT_EQ(fromPlacement(placement), rest);
        const vsg::dmat4 m
            = vsg::translate(vsg::dvec3(t[0], t[1], t[2])) * vsg::rotate(vsg::dquat(q[0], q[1], q[2], q[3]));
        const std::array<double, 16> columns = toColumnMajor(rest);
        for (std::size_t c = 0; c < 4; ++c)
        {
            for (std::size_t r = 0; r < 4; ++r)
            {
                EXPECT_NEAR(columns[c * 4 + r], m[c][r], 1e-12) << c << "," << r;
            }
        }
    }

    // ---- (12) duplicate JSON keys --------------------------------------------------------------------------------

    TEST(SkeletonMapReview, a_duplicate_json_key_is_refused)
    {
        std::string text = writeSkeletonMap(extendedMap());
        const std::string accepted = "\"accepted\": true";
        const auto at = text.find(accepted);
        ASSERT_NE(at, std::string::npos);
        text.replace(at, accepted.size(), "\"accepted\": false, \"accepted\": true");
        EXPECT_FALSE(readSkeletonMap(text, "dup.skeletonmap.json").mValue.has_value());
        std::string escaped = writeSkeletonMap(extendedMap());
        const auto at2 = escaped.find(accepted);
        escaped.replace(at2, accepted.size(), "\"accepted\": false, \"acc\\u0065pted\": true");
        EXPECT_FALSE(readSkeletonMap(escaped, "dup2.skeletonmap.json").mValue.has_value());
    }
}
