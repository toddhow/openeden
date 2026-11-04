// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage c): tests of the suggestion engine
// (components/body/skeletonsuggest.*). Written from the spec before the implementation, on SYNTHETIC skeletons only.
// Every case pins the chosen action, kind, heuristic id and confidence; a property test pins the determinism under a
// reordering of the source bones.

#include <gtest/gtest.h>

#include "skeletonfixtures.hpp"

#include <components/body/skeletonsuggest.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace Body;
    using namespace SkeletonFixtures;

    const std::string chest = "Bip01 Spine1";

    SuggestInput inputFor(const std::vector<RestBone>& skeleton, const std::vector<BoneWeightMass>& masses = {},
        bool hasMasses = true, bool withRoleTable = true)
    {
        SuggestInput input;
        input.mName = "test";
        input.mProfile = "humanoid-to-eden";
        input.mSkeleton = skeleton;
        input.mMasses = masses;
        input.mHasMasses = hasMasses;
        if (withRoleTable)
        {
            input.mRoles = nativeRoles();
        }
        return input;
    }

    struct Case
    {
        const char* mName;
        const char* mParent;
        double mMass;
        bool mHasChild;
        MapAction mAction;
        std::optional<SecondaryKind> mKind;
        const char* mHeuristic;
        MapConfidence mConfidence;
        const char* mAttachPoint = "";
        const char* mTo = "";
    };

    using enum MapAction;
    using enum SecondaryKind;
    using enum MapConfidence;

    const std::vector<Case>& cases()
    {
        static const std::vector<Case> all = {
            // soft tissue
            { "Breast_L_01", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue", High },
            { "NPC L Breast", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue", High },
            { "R_Boob_02", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue", High },
            { "Bust_Root", "Bip01 Spine1", 0.0, true, Secondary, Breast, "name.soft-tissue", High },
            { "BREAST-L.01", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue", High },
            { "NPC L Breast [LBrst]", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue", High },
            { "Pectoral_L", "Bip01 Spine1", 0.01, false, Secondary, Breast, "name.soft-tissue-weak", Medium },
            { "Belly_01", "Bip01 Spine", 0.01, false, Secondary, Belly, "name.soft-tissue", High },
            { "Stomach", "Bip01 Spine", 0.01, false, Secondary, Belly, "name.soft-tissue", High },
            { "Tummy", "Bip01 Spine", 0.0, false, Secondary, Belly, "name.soft-tissue", High },
            { "Butt_L", "Bip01 Pelvis", 0.01, false, Secondary, Butt, "name.soft-tissue", High },
            { "NPC L Butt", "Bip01 Pelvis", 0.01, false, Secondary, Butt, "name.soft-tissue", High },
            { "Glute_R", "Bip01 Pelvis", 0.01, false, Secondary, Butt, "name.soft-tissue", High },
            { "Buttock_01", "Bip01 Pelvis", 0.01, false, Secondary, Butt, "name.soft-tissue", High },
            { "GenitalsBase", "Bip01 Pelvis", 0.01, true, Secondary, Other, "name.soft-tissue", High },
            { "NPC GenitalsScrotum [Gens]", "Bip01 Pelvis", 0.0, false, Secondary, Other, "name.soft-tissue", High },
            { "Penis_02", "Bip01 Pelvis", 0.01, false, Secondary, Other, "name.soft-tissue", High },
            // cloth, hair, tail
            { "Cape_01", "Bip01 Spine1", 0.01, false, Secondary, Cloth, "name.cloth", High },
            { "Skirt_F_03", "Bip01 Pelvis", 0.01, false, Secondary, Cloth, "name.cloth", High },
            { "Scarf", "Bip01 Neck", 0.01, false, Secondary, Cloth, "name.cloth", High },
            { "Cloak_Back", "Bip01 Spine1", 0.01, false, Secondary, Cloth, "name.cloth", High },
            { "Robe_Hem_L", "Bip01 Pelvis", 0.01, false, Secondary, Cloth, "name.cloth", High },
            { "Hair_Back_02", "Bip01 Head", 0.01, false, Secondary, Hair, "name.hair", High },
            { "Ponytail", "Bip01 Head", 0.01, false, Secondary, Hair, "name.hair", High },
            { "Braid_01", "Bip01 Head", 0.0, false, Secondary, Hair, "name.hair", High },
            { "Tail_03", "Bip01 Pelvis", 0.01, false, Secondary, Tail, "name.tail", High },
            // twist and face and fingers: folds
            { "L Forearm Twist", "Bip01 L Forearm", 0.01, false, Fold, std::nullopt, "name.twist", High },
            { "ForearmRoll_R", "Bip01 R Forearm", 0.01, false, Fold, std::nullopt, "name.twist", High },
            { "UpperArm_Twist_L", "Bip01 L UpperArm", 0.01, false, Fold, std::nullopt, "name.twist", High },
            { "Jaw", "Bip01 Head", 0.01, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "Eye_L", "Bip01 Head", 0.01, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "Brow_Inner_R", "Bip01 Head", 0.0, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "Lip_Upper", "Bip01 Head", 0.01, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "Tongue_02", "Bip01 Head", 0.01, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "Cheek_L", "Bip01 Head", 0.01, false, Fold, std::nullopt, "name.face", High, "", "Head" },
            { "L Thumb1", "Bip01 L Hand", 0.01, false, Fold, std::nullopt, "name.finger", High },
            { "RightIndex2", "Bip01 R Hand", 0.01, false, Fold, std::nullopt, "name.finger", High },
            { "Pinky3_L", "Bip01 L Hand", 0.01, false, Fold, std::nullopt, "name.finger", High },
            // attach points
            { "WeaponBack", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High, "weapon.back" },
            { "NPC WeaponSword", "Bip01 Pelvis", 0.0, false, Attach, std::nullopt, "name.attach", High,
                "weapon.sword" },
            { "WeaponBow", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High, "weapon.bow" },
            { "WeaponAxe", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High, "weapon.axe" },
            { "ShieldBack", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High, "shield.back" },
            { "Quiver", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High, "quiver" },
            { "NPC Torch", "Bip01 L Hand", 0.0, false, Attach, std::nullopt, "name.attach", High, "torch" },
            { "CameraControl", "Bip01 Head", 0.0, false, Attach, std::nullopt, "name.attach", High, "camera" },
            { "MagicEffectsNode", "Bip01 Spine1", 0.0, false, Attach, std::nullopt, "name.attach", High,
                "magic.effects" },
            // helpers
            { "IK_Helper_01", "Bip01 Pelvis", 0.0, false, Drop, std::nullopt, "name.helper", Medium },
            { "Pole_Target", "Bip01 L Calf", 0.0, false, Drop, std::nullopt, "name.helper", Medium },
            { "Dummy", "Bip01 Spine1", 0.0, true, Attach, std::nullopt, "name.helper", Medium, "node" },
            { "IK_Weighted_01", "Bip01 Pelvis", 0.05, false, Fold, std::nullopt, "name.helper", Medium },
            // unknown names: weight and hierarchy
            { "Unknown_01", "Bip01 Spine1", 0.02, false, Fold, std::nullopt, "weight.deform", Medium },
            { "Bone_17", "Bip01 L Calf", 0.5, false, Fold, std::nullopt, "weight.deform", Medium },
            { "Unknown_At_Threshold", "Bip01 Spine1", 1e-4, false, Fold, std::nullopt, "weight.deform", Medium },
            { "Mystery", "Bip01 Spine1", 1e-6, false, Drop, std::nullopt, "hierarchy.leaf", Medium },
            { "Unknown_leaf", "Bip01 Spine1", 0.0, false, Drop, std::nullopt, "hierarchy.leaf", Medium },
            { "Unknown_node", "Bip01 Spine1", 0.0, true, Attach, std::nullopt, "hierarchy.transform", Medium, "node" },
            // a high name vote beats a medium hierarchy vote that disagrees
            { "Breast_Helper", "Bip01 Spine1", 0.0, false, Secondary, Breast, "name.soft-tissue", High },
            { "Dummy_Cape", "Bip01 Spine1", 0.0, false, Secondary, Cloth, "name.cloth", High },
        };
        return all;
    }

    class SuggestCase : public ::testing::TestWithParam<Case>
    {
    };

    TEST_P(SuggestCase, the_extra_bone_is_classified_as_pinned)
    {
        const Case& c = GetParam();
        std::vector<RestBone> skeleton = withBone(baseSkeleton(), c.mName, c.mParent, 0.05, 0.05, 0.05);
        if (c.mHasChild)
        {
            skeleton = withBone(skeleton, std::string(c.mName) + "_Child", c.mName, 0.02, 0.02, 0.02);
        }
        std::vector<BoneWeightMass> masses;
        if (c.mMass > 0)
        {
            masses.push_back({ c.mName, c.mMass });
        }
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton, masses));
        const MapExtra* entry = findExtra(result.mMap, c.mName);
        ASSERT_NE(entry, nullptr) << c.mName;
        EXPECT_EQ(entry->mAction, c.mAction) << c.mName << " got " << toString(entry->mAction);
        EXPECT_EQ(entry->mKind, c.mKind) << c.mName;
        ASSERT_TRUE(entry->mSuggested.has_value()) << c.mName;
        EXPECT_EQ(entry->mSuggested->mHeuristic, c.mHeuristic) << c.mName;
        EXPECT_EQ(entry->mSuggested->mConfidence, c.mConfidence) << c.mName;
        EXPECT_FALSE(entry->mAccepted) << c.mName << ": a suggestion is never accepted";
        if (c.mAction == Attach)
        {
            EXPECT_EQ(entry->mAttachPoint, c.mAttachPoint) << c.mName;
        }
        if (c.mAction == Fold)
        {
            ASSERT_TRUE(entry->mRule.has_value()) << c.mName;
            if (std::string(c.mTo).empty())
            {
                EXPECT_EQ(*entry->mRule, FoldRule::NearestMappedAncestor) << c.mName;
                EXPECT_TRUE(entry->mTo.empty()) << c.mName;
            }
            else
            {
                EXPECT_EQ(*entry->mRule, FoldRule::Named) << c.mName;
                EXPECT_EQ(entry->mTo, c.mTo) << c.mName;
            }
        }
        if (c.mAction == Secondary || c.mAction == Attach)
        {
            EXPECT_FALSE(entry->mParent.empty()) << c.mName;
        }
        if (c.mAction == Drop)
        {
            EXPECT_FALSE(entry->mReason.empty()) << c.mName << ": a drop says why";
        }
    }

    INSTANTIATE_TEST_SUITE_P(
        Vocabulary, SuggestCase, ::testing::ValuesIn(cases()), [](const ::testing::TestParamInfo<Case>& info) {
            std::string name = info.param.mName;
            for (char& c : name)
            {
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                {
                    c = '_';
                }
            }
            return std::to_string(info.index) + "_" + name;
        });

    // ---- ties and ambiguity ------------------------------------------------------------------------------------

    TEST(SuggestAmbiguity, two_medium_votes_that_disagree_make_an_ask_entry)
    {
        // "Pec" (weak soft tissue, medium) against "Dummy" (helper, medium) on a bone with no weight and no children.
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Pec_Dummy")));
        const MapExtra* entry = findExtra(result.mMap, "Pec_Dummy");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mAction, MapAction::Ask);
        EXPECT_FALSE(entry->mAccepted);
    }

    TEST(SuggestAmbiguity, two_high_votes_that_disagree_make_an_ask_entry)
    {
        for (const char* name : { "Butt_Breast", "Cloth_Tail", "Belly_Hair" })
        {
            const SuggestResult result = suggestSkeletonMap(inputFor(baseWith(name)));
            const MapExtra* entry = findExtra(result.mMap, name);
            ASSERT_NE(entry, nullptr) << name;
            EXPECT_EQ(entry->mAction, MapAction::Ask) << name;
        }
    }

    TEST(SuggestAmbiguity, two_high_votes_that_agree_are_not_ambiguous)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Breast_Bust_01")));
        const MapExtra* entry = findExtra(result.mMap, "Breast_Bust_01");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mAction, MapAction::Secondary);
        EXPECT_EQ(entry->mKind, SecondaryKind::Breast);
    }

    TEST(SuggestAmbiguity, an_ask_entry_has_no_action_fields)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Butt_Breast")));
        const MapExtra* entry = findExtra(result.mMap, "Butt_Breast");
        ASSERT_NE(entry, nullptr);
        EXPECT_FALSE(entry->mKind.has_value());
        EXPECT_TRUE(entry->mParent.empty());
        EXPECT_TRUE(entry->mTo.empty());
        EXPECT_TRUE(entry->mAttachPoint.empty());
    }

    // ---- no weight information: only names and rest positions vote ------------------------------------------

    TEST(SuggestWithoutMasses, a_name_vote_still_works)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Breast_L_01"), {}, false));
        const MapExtra* entry = findExtra(result.mMap, "Breast_L_01");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mAction, MapAction::Secondary);
    }

    TEST(SuggestWithoutMasses, a_bone_at_its_ancestors_rest_position_folds_with_low_confidence)
    {
        const SuggestResult result
            = suggestSkeletonMap(inputFor(withBone(baseSkeleton(), "Unknown_X", chest, 0.0, 0.0, 0.0), {}, false));
        const MapExtra* entry = findExtra(result.mMap, "Unknown_X");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mAction, MapAction::Fold);
        ASSERT_TRUE(entry->mSuggested.has_value());
        EXPECT_EQ(entry->mSuggested->mHeuristic, "rest.coincident");
        EXPECT_EQ(entry->mSuggested->mConfidence, MapConfidence::Low);
    }

    TEST(SuggestWithoutMasses, an_unknown_bone_far_from_everything_is_an_ask_entry)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Unknown_Y", chest, 0.4, 0.4), {}, false));
        const MapExtra* entry = findExtra(result.mMap, "Unknown_Y");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mAction, MapAction::Ask);
    }

    // ---- parents of secondary and attach entries ---------------------------------------------------------------

    TEST(SuggestParents, a_secondary_under_a_role_bone_names_the_role)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("Breast_L_01")));
        EXPECT_EQ(findExtra(result.mMap, "Breast_L_01")->mParent, "Chest");
    }

    TEST(SuggestParents, a_secondary_under_a_secondary_names_that_bone)
    {
        std::vector<RestBone> skeleton = withBone(baseSkeleton(), "Breast_L_01", chest);
        skeleton = withBone(skeleton, "Breast_L_02", "Breast_L_01");
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        EXPECT_EQ(findExtra(result.mMap, "Breast_L_02")->mParent, "Breast_L_01");
    }

    TEST(SuggestParents, a_parent_that_is_dropped_or_folded_is_skipped_for_the_next_mapped_ancestor)
    {
        std::vector<RestBone> skeleton = withBone(baseSkeleton(), "L Forearm Twist", "Bip01 L Forearm");
        skeleton = withBone(skeleton, "Breast_X_01", "L Forearm Twist"); // a secondary under a folded bone
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        const MapExtra* entry = findExtra(result.mMap, "Breast_X_01");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->mParent, "LeftForearm");
    }

    // ---- roles -------------------------------------------------------------------------------------------------

    TEST(SuggestRoles, the_native_names_of_the_pack_resolve_all_seventeen_roles)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseSkeleton()));
        EXPECT_EQ(result.mMap.mRoles.size(), 17u);
        EXPECT_TRUE(result.mMissingRoles.empty());
        EXPECT_TRUE(result.mMap.mExtra.empty());
        ASSERT_NE(findRoleEntry(result.mMap, "Chest"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "Chest")->mBone, "Bip01 Spine1");
    }

    TEST(SuggestRoles, the_roles_are_in_canonical_order)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseSkeleton()));
        const std::vector<RoleInfo>& canonical = canonicalRoles();
        ASSERT_GE(result.mMap.mRoles.size(), 2u);
        for (std::size_t i = 0; i < result.mMap.mRoles.size(); ++i)
        {
            EXPECT_EQ(result.mMap.mRoles[i].mRole, canonical[i].mName);
        }
    }

    TEST(SuggestRoles, an_alias_of_the_pack_resolves_a_renamed_bone)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[0].mName = "Pelvis_01";
        SuggestInput input = inputFor(skeleton);
        input.mRoles[0].mAliases = { "Pelvis_01" };
        const SuggestResult result = suggestSkeletonMap(input);
        ASSERT_NE(findRoleEntry(result.mMap, "Pelvis"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "Pelvis")->mBone, "Pelvis_01");
        EXPECT_TRUE(result.mMissingRoles.empty());
    }

    TEST(SuggestRoles, a_native_name_that_differs_only_by_separators_still_resolves)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[5].mName = "Bip01_L_UpperArm";
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        ASSERT_NE(findRoleEntry(result.mMap, "LeftUpperArm"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "LeftUpperArm")->mBone, "Bip01_L_UpperArm");
    }

    std::vector<RestBone> renamed(const std::vector<std::string>& names)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            skeleton[i].mName = names[i];
        }
        return skeleton;
    }

    TEST(SuggestRoles, skyrim_style_names_resolve_by_tokens_without_a_role_table)
    {
        const std::vector<RestBone> skeleton = renamed({ "NPC Pelvis [Pelv]", "NPC Spine [Spn0]", "NPC Spine1 [Spn1]",
            "NPC Neck [Neck]", "NPC Head [Head]", "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]",
            "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]", "NPC L Thigh [LThg]",
            "NPC L Calf [LClf]", "NPC L Foot [Lft ]", "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]" });
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton, {}, true, false));
        EXPECT_EQ(result.mMap.mRoles.size(), 17u) << "missing: " << result.mMissingRoles.size();
        ASSERT_NE(findRoleEntry(result.mMap, "Chest"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "Chest")->mBone, "NPC Spine1 [Spn1]");
        ASSERT_NE(findRoleEntry(result.mMap, "RightShin"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "RightShin")->mBone, "NPC R Calf [RClf]");
    }

    TEST(SuggestRoles, mixamo_style_names_resolve_by_tokens_without_a_role_table)
    {
        const std::vector<RestBone> skeleton
            = renamed({ "mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Neck", "mixamorig:Head",
                "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand", "mixamorig:RightArm",
                "mixamorig:RightForeArm", "mixamorig:RightHand", "mixamorig:LeftUpLeg", "mixamorig:LeftLeg",
                "mixamorig:LeftFoot", "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot" });
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton, {}, true, false));
        EXPECT_EQ(result.mMap.mRoles.size(), 17u);
        ASSERT_NE(findRoleEntry(result.mMap, "LeftThigh"), nullptr);
        EXPECT_EQ(findRoleEntry(result.mMap, "LeftThigh")->mBone, "mixamorig:LeftUpLeg");
        EXPECT_EQ(findRoleEntry(result.mMap, "LeftShin")->mBone, "mixamorig:LeftLeg");
    }

    TEST(SuggestRoles, a_missing_core_role_is_reported_and_its_bone_becomes_an_extra)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[3].mName = "Bip01 Gizmo"; // the neck, renamed to nothing the pack knows
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        EXPECT_EQ(result.mMap.mRoles.size(), 16u);
        ASSERT_EQ(result.mMissingRoles.size(), 1u);
        EXPECT_EQ(result.mMissingRoles[0], "Neck");
        EXPECT_NE(findExtra(result.mMap, "Bip01 Gizmo"), nullptr);
    }

    TEST(SuggestRoles, two_bones_for_one_role_leave_the_role_unresolved_and_are_noted)
    {
        const std::vector<RestBone> skeleton = withBone(baseSkeleton(), "Head", "Bip01 Neck");
        SuggestInput input = inputFor(skeleton, {}, true, false);
        input.mSkeleton[4].mName = "NPC Head [Head]"; // two bones whose tokens are exactly "head"
        const SuggestResult result = suggestSkeletonMap(input);
        EXPECT_EQ(findRoleEntry(result.mMap, "Head"), nullptr);
        EXPECT_FALSE(result.mNotes.empty());
        EXPECT_NE(
            std::find(result.mMissingRoles.begin(), result.mMissingRoles.end(), "Head"), result.mMissingRoles.end());
    }

    TEST(SuggestRoles, fingers_are_extras_when_the_role_table_has_no_finger_roles)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(baseWith("L Thumb1", "Bip01 L Hand")));
        EXPECT_EQ(findRoleEntry(result.mMap, "LeftThumb1"), nullptr);
        EXPECT_EQ(findExtra(result.mMap, "L Thumb1")->mAction, MapAction::Fold);
    }

    TEST(SuggestRoles, fingers_are_roles_when_the_role_table_lists_them)
    {
        SuggestInput input = inputFor(baseWith("Finger_Thumb_Base_L", "Bip01 L Hand"));
        input.mRoles.push_back({ "LeftThumb1", "Finger_Thumb_Base_L", {} });
        const SuggestResult result = suggestSkeletonMap(input);
        ASSERT_NE(findRoleEntry(result.mMap, "LeftThumb1"), nullptr);
        EXPECT_EQ(findExtra(result.mMap, "Finger_Thumb_Base_L"), nullptr);
    }

    // ---- the whole map ---------------------------------------------------------------------------------------

    TEST(SuggestMap, the_map_carries_the_digest_and_count_of_the_source_and_the_names)
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        SuggestInput input = inputFor(skeleton);
        input.mName = "my-map";
        const SuggestResult result = suggestSkeletonMap(input);
        EXPECT_EQ(result.mMap.mName, "my-map");
        EXPECT_EQ(result.mMap.mProfile, "humanoid-to-eden");
        EXPECT_EQ(result.mMap.mSource.mBoneCount, skeleton.size());
        EXPECT_EQ(result.mMap.mSource.mDigest, skeletonDigest(skeleton));
        EXPECT_EQ(result.mMap.mProportions, ProportionsMode::Tier1);
    }

    TEST(SuggestMap, a_suggested_map_is_complete_and_only_lacks_acceptance)
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        const MapReport report = validateSkeletonMap(result.mMap, &skeleton);
        EXPECT_EQ(report.count("BODY-MAP-007"), 7u);
        for (const MapFinding& f : report.mFindings)
        {
            EXPECT_EQ(f.mId, "BODY-MAP-007") << f.mId << " " << f.mPointer << ": " << f.mMessage;
        }
    }

    TEST(SuggestMap, accept_all_makes_a_decisive_map_valid)
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        const AcceptResult accepted = acceptAllSuggestions(result.mMap);
        EXPECT_EQ(accepted.mAccepted, 7u);
        EXPECT_EQ(accepted.mRemainingAsks, 0u);
        const MapReport report = validateSkeletonMap(result.mMap, &skeleton);
        EXPECT_FALSE(report.hasErrors());
    }

    TEST(SuggestMap, accept_all_leaves_ask_entries_unaccepted)
    {
        const std::vector<RestBone> skeleton = baseWith("Butt_Breast");
        SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        const AcceptResult accepted = acceptAllSuggestions(result.mMap);
        EXPECT_EQ(accepted.mAccepted, 0u);
        EXPECT_EQ(accepted.mRemainingAsks, 1u);
        const MapReport report = validateSkeletonMap(result.mMap, &skeleton);
        EXPECT_GT(report.count("BODY-MAP-008"), 0u); // an "ask" entry is 008 (review: 007 alone was accepted too)
        EXPECT_FALSE(findExtra(result.mMap, "Butt_Breast")->mAccepted);
    }

    TEST(SuggestMap, extra_entries_are_sorted_by_depth_then_name)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton = withBone(skeleton, "Zeta_Cape", chest);
        skeleton = withBone(skeleton, "Alpha_Cape_Deep", "Zeta_Cape");
        skeleton = withBone(skeleton, "Beta_Cape", chest);
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        ASSERT_EQ(result.mMap.mExtra.size(), 3u);
        EXPECT_EQ(result.mMap.mExtra[0].mBone, "Beta_Cape");
        EXPECT_EQ(result.mMap.mExtra[1].mBone, "Zeta_Cape");
        EXPECT_EQ(result.mMap.mExtra[2].mBone, "Alpha_Cape_Deep");
    }

    TEST(SuggestMap, an_invalid_source_skeleton_gives_an_empty_map_and_a_note)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        skeleton[4].mParent = 9; // a parent that comes later
        const SuggestResult result = suggestSkeletonMap(inputFor(skeleton));
        EXPECT_TRUE(result.mMap.mRoles.empty());
        EXPECT_TRUE(result.mMap.mExtra.empty());
        EXPECT_FALSE(result.mNotes.empty());
    }

    TEST(SuggestMap, the_suggestion_is_deterministic_under_a_reordering_of_the_source_bones)
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        const std::vector<BoneWeightMass> masses = { { "Breast_L_01", 0.01 }, { "L Forearm Twist", 0.02 } };
        const SuggestResult expected = suggestSkeletonMap(inputFor(skeleton, masses));
        const std::string expectedText = writeSkeletonMap(expected.mMap);
        Lcg random(4242);
        for (int i = 0; i < 25; ++i)
        {
            SCOPED_TRACE("shuffle " + std::to_string(i));
            const std::vector<RestBone> shuffled = shuffledTopologically(skeleton, random);
            const SuggestResult result = suggestSkeletonMap(inputFor(shuffled, masses));
            EXPECT_EQ(writeSkeletonMap(result.mMap), expectedText);
            EXPECT_EQ(result.mMissingRoles, expected.mMissingRoles);
            EXPECT_EQ(result.mNotes, expected.mNotes); // review: the notes must not follow the input order either
        }
    }

    TEST(SuggestMap, the_same_input_gives_the_same_bytes_twice)
    {
        const SuggestInput input = inputFor(extendedSkeleton());
        EXPECT_EQ(writeSkeletonMap(suggestSkeletonMap(input).mMap), writeSkeletonMap(suggestSkeletonMap(input).mMap));
    }

    TEST(SuggestMap, a_suggestion_survives_a_round_trip_through_the_file_format)
    {
        const SuggestResult result = suggestSkeletonMap(inputFor(extendedSkeleton()));
        const std::string text = writeSkeletonMap(result.mMap);
        const SkeletonMapResult back = readSkeletonMap(text, "t");
        ASSERT_TRUE(back.mValue.has_value()) << back.mError.toString();
        EXPECT_EQ(*back.mValue, result.mMap);
    }

    // ---- the name tokens ----------------------------------------------------------------------------------------

    TEST(SuggestTokens, separators_brackets_and_camel_case_split_a_name)
    {
        EXPECT_EQ(boneNameTokens("NPC L Breast [LBrst]"), (std::vector<std::string>{ "npc", "l", "breast" }));
        EXPECT_EQ(boneNameTokens("BREAST-L.01"), (std::vector<std::string>{ "breast", "l", "01" }));
        EXPECT_EQ(boneNameTokens("ForearmRoll_R"), (std::vector<std::string>{ "forearm", "roll", "r" }));
        EXPECT_EQ(
            boneNameTokens("mixamorig:LeftUpLeg"), (std::vector<std::string>{ "mixamorig", "left", "up", "leg" }));
        EXPECT_EQ(boneNameTokens("Bip01 Spine1"), (std::vector<std::string>{ "bip01", "spine1" }));
        EXPECT_EQ(boneNameTokens("MagicEffectsNode"), (std::vector<std::string>{ "magic", "effects", "node" }));
        EXPECT_TRUE(boneNameTokens("").empty());
        EXPECT_TRUE(boneNameTokens("[only brackets]").empty());
    }
}
