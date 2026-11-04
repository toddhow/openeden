// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage b): tests of the "openeden.skeletonmap" v1 format
// (components/body/skeletonmap.*): the reader, the canonical writer, the validator and the digest. Written from the
// spec before the implementation, on SYNTHETIC skeletons only: no game or mod data is read here.

#include <gtest/gtest.h>

#include "skeletonfixtures.hpp"

#include <components/body/skeletonmap.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace Body;
    using namespace SkeletonFixtures;

    SkeletonMap readOrFail(const std::string& text)
    {
        const SkeletonMapResult result = readSkeletonMap(text, "test.skeletonmap.json");
        EXPECT_TRUE(result.mValue.has_value()) << result.mError.toString();
        return result.mValue ? *result.mValue : SkeletonMap{};
    }

    std::size_t errorCount(const MapReport& report)
    {
        return static_cast<std::size_t>(std::count_if(report.mFindings.begin(), report.mFindings.end(),
            [](const MapFinding& f) { return f.mSeverity == MapSeverity::Error; }));
    }

    bool hasFinding(const MapReport& report, std::string_view id, std::string_view pointer = {})
    {
        for (const MapFinding& f : report.mFindings)
        {
            if (f.mId == id && (pointer.empty() || f.mPointer == pointer))
            {
                return true;
            }
        }
        return false;
    }

    std::string describe(const MapReport& report)
    {
        std::string text;
        for (const MapFinding& f : report.mFindings)
        {
            text += f.mId + " " + f.mPointer + ": " + f.mMessage + "\n";
        }
        return text;
    }

    const std::string sDigestA(64, 'a');

    SkeletonMap tinyMap()
    {
        SkeletonMap map;
        map.mName = "tiny";
        map.mSource.mBoneCount = 2;
        map.mSource.mDigest = sDigestA;
        map.mProfile = "humanoid-to-eden";
        map.mRoles.push_back({ "Pelvis", "Hips", {} });
        MapExtra helper = extra("Helper", MapAction::Drop);
        helper.mReason = "none";
        map.mExtra.push_back(helper);
        return map;
    }

    // ---- the canonical text ------------------------------------------------------------------------------------

    TEST(SkeletonMapFormat, the_canonical_text_of_a_tiny_map_is_pinned)
    {
        const std::string expected = "{\n"
                                     "  \"format\": \"openeden.skeletonmap\",\n"
                                     "  \"version\": 1,\n"
                                     "  \"name\": \"tiny\",\n"
                                     "  \"source\": {\"boneCount\": 2, \"digest\": \""
            + sDigestA
            + "\"},\n"
              "  \"profile\": \"humanoid-to-eden\",\n"
              "  \"roles\": [\n"
              "    {\"role\": \"Pelvis\", \"bone\": \"Hips\"}\n"
              "  ],\n"
              "  \"extra\": [\n"
              "    {\"bone\": \"Helper\", \"action\": \"drop\", \"reason\": \"none\", \"accepted\": true}\n"
              "  ],\n"
              "  \"proportions\": {\"mode\": \"tier1\"}\n"
              "}\n";
        EXPECT_EQ(writeSkeletonMap(tinyMap()), expected);
    }

    TEST(SkeletonMapFormat, aliases_and_suggestions_have_their_canonical_places)
    {
        SkeletonMap map = tinyMap();
        map.mRoles[0].mAliases = { "Pelvis_01", "Hips_Alt" };
        map.mExtra[0].mSuggested = MapSuggestion{ "name.helper", MapConfidence::Medium };
        map.mExtra[0].mAccepted = false;
        const std::string text = writeSkeletonMap(map);
        EXPECT_NE(text.find("{\"role\": \"Pelvis\", \"bone\": \"Hips\", \"aliases\": [\"Pelvis_01\", \"Hips_Alt\"]}"),
            std::string::npos)
            << text;
        EXPECT_NE(text.find("      \"suggested\": {\"heuristic\": \"name.helper\", \"confidence\": \"medium\"},\n"
                            "      \"accepted\": false\n"),
            std::string::npos)
            << text;
    }

    TEST(SkeletonMapFormat, empty_lists_are_written_as_empty_arrays)
    {
        SkeletonMap map = tinyMap();
        map.mRoles.clear();
        map.mExtra.clear();
        const std::string text = writeSkeletonMap(map);
        EXPECT_NE(text.find("\"roles\": [],"), std::string::npos) << text;
        EXPECT_NE(text.find("\"extra\": [],"), std::string::npos) << text;
    }

    TEST(SkeletonMapFormat, a_proportions_note_and_the_inverse_bind_mode_are_written)
    {
        SkeletonMap map = tinyMap();
        map.mProportions = ProportionsMode::InverseBind;
        map.mProportionsNote = "derived, never authored";
        EXPECT_NE(writeSkeletonMap(map).find("\"proportions\": {\"mode\": \"inverse-bind\", \"note\": \"derived, never "
                                             "authored\"}"),
            std::string::npos);
    }

    TEST(SkeletonMapFormat, a_valid_map_round_trips_byte_for_byte)
    {
        const SkeletonMap map = extendedMap();
        const std::string text = writeSkeletonMap(map);
        const SkeletonMap back = readOrFail(text);
        EXPECT_EQ(back, map);
        EXPECT_EQ(writeSkeletonMap(back), text);
    }

    TEST(SkeletonMapFormat, thirty_generated_maps_round_trip_byte_for_byte)
    {
        const std::vector<RoleInfo>& roles = canonicalRoles();
        ASSERT_FALSE(roles.empty());
        Lcg random(1234);
        for (int trial = 0; trial < 30; ++trial)
        {
            SCOPED_TRACE("trial " + std::to_string(trial));
            SkeletonMap map;
            map.mName = "generated-" + std::to_string(trial);
            map.mProfile = "humanoid-to-eden";
            map.mProportions = random.below(2) == 0 ? ProportionsMode::Tier1 : ProportionsMode::InverseBind;
            if (random.below(2) == 0)
            {
                map.mProportionsNote = "note " + std::to_string(random.next());
            }
            std::string digest;
            for (int i = 0; i < 64; ++i)
            {
                digest += "0123456789abcdef"[random.below(16)];
            }
            map.mSource = { random.below(900) + 1, digest };
            int serial = 0;
            for (const RoleInfo& role : roles)
            {
                if (role.mName == "Pelvis" || random.below(3) == 0)
                {
                    MapRole entry{ std::string(role.mName), "Role_" + std::to_string(serial++), {} };
                    const std::uint32_t aliases = random.below(4);
                    for (std::uint32_t a = 0; a < aliases; ++a)
                    {
                        entry.mAliases.push_back("Alias_" + std::to_string(serial++));
                    }
                    map.mRoles.push_back(entry);
                }
            }
            const std::uint32_t extras = random.below(40);
            for (std::uint32_t e = 0; e < extras; ++e)
            {
                MapExtra x;
                x.mBone = "Extra_" + std::to_string(serial++);
                x.mAccepted = random.below(2) == 0;
                switch (random.below(5))
                {
                    case 0:
                        x.mAction = MapAction::Secondary;
                        x.mParent = "Chest";
                        x.mKind = static_cast<SecondaryKind>(random.below(8));
                        x.mUseClipTrack = random.below(2) == 0;
                        break;
                    case 1:
                        x.mAction = MapAction::Fold;
                        x.mRule = static_cast<FoldRule>(random.below(3));
                        if (*x.mRule != FoldRule::NearestMappedAncestor)
                        {
                            x.mTo = "Head";
                        }
                        if (*x.mRule == FoldRule::Distribute)
                        {
                            x.mToSecond = "Neck";
                        }
                        break;
                    case 2:
                        x.mAction = MapAction::Attach;
                        x.mParent = "Chest";
                        x.mAttachPoint = "weapon.back";
                        break;
                    case 3:
                        x.mAction = MapAction::Drop;
                        x.mReason = "reason " + std::to_string(random.next());
                        break;
                    default:
                        x.mAction = MapAction::Ask;
                        break;
                }
                if (random.below(2) == 0)
                {
                    x.mSuggested = MapSuggestion{ "heuristic." + std::to_string(random.below(9)),
                        static_cast<MapConfidence>(random.below(3)) };
                }
                map.mExtra.push_back(x);
            }
            const std::string text = writeSkeletonMap(map);
            const SkeletonMapResult result = readSkeletonMap(text, "generated");
            ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
            EXPECT_EQ(*result.mValue, map);
            EXPECT_EQ(writeSkeletonMap(*result.mValue), text);
        }
    }

    // ---- reading: located errors -----------------------------------------------------------------------------------

    TEST(SkeletonMapReader, an_unknown_top_level_key_is_a_located_error)
    {
        std::string text = writeSkeletonMap(tinyMap());
        text.replace(text.find("\"profile\""), 9, "\"profil\"");
        const SkeletonMapResult result = readSkeletonMap(text, "t.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.file, "t.json");
        EXPECT_EQ(result.mError.field, "/profil");
        EXPECT_GT(result.mError.line, 1u);
    }

    TEST(SkeletonMapReader, the_wrong_format_and_version_are_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string wrongFormat = text;
        wrongFormat.replace(wrongFormat.find("openeden.skeletonmap"), 20, "openeden.somethingels");
        EXPECT_EQ(readSkeletonMap(wrongFormat, "f").mError.field, "/format");
        std::string wrongVersion = text;
        wrongVersion.replace(wrongVersion.find("\"version\": 1"), 12, "\"version\": 2");
        const SkeletonMapResult result = readSkeletonMap(wrongVersion, "f");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/version");
    }

    TEST(SkeletonMapReader, a_missing_accepted_field_names_its_pointer)
    {
        std::string text = writeSkeletonMap(tinyMap());
        text.replace(text.find(", \"accepted\": true"), 18, "");
        const SkeletonMapResult result = readSkeletonMap(text, "f");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/extra/0/accepted");
        EXPECT_NE(result.mError.message.find("missing"), std::string::npos);
    }

    TEST(SkeletonMapReader, a_bad_action_and_a_bad_confidence_are_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string badAction = text;
        badAction.replace(badAction.find("\"drop\""), 6, "\"bogus\"");
        const SkeletonMapResult a = readSkeletonMap(badAction, "f");
        ASSERT_FALSE(a.mValue.has_value());
        EXPECT_EQ(a.mError.field, "/extra/0/action");

        SkeletonMap map = tinyMap();
        map.mExtra[0].mSuggested = MapSuggestion{ "x", MapConfidence::High };
        std::string badConfidence = writeSkeletonMap(map);
        badConfidence.replace(badConfidence.find("\"high\""), 6, "\"certain\"");
        const SkeletonMapResult b = readSkeletonMap(badConfidence, "f");
        ASSERT_FALSE(b.mValue.has_value());
        EXPECT_EQ(b.mError.field, "/extra/0/suggested/confidence");
    }

    TEST(SkeletonMapReader, wrong_types_are_refused_with_their_pointer)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string badCount = text;
        badCount.replace(badCount.find("\"boneCount\": 2"), 14, "\"boneCount\": \"2\"");
        EXPECT_EQ(readSkeletonMap(badCount, "f").mError.field, "/source/boneCount");
        std::string badAccepted = text;
        badAccepted.replace(badAccepted.find("\"accepted\": true"), 16, "\"accepted\": 1");
        EXPECT_EQ(readSkeletonMap(badAccepted, "f").mError.field, "/extra/0/accepted");
        std::string notObject = "[]";
        EXPECT_FALSE(readSkeletonMap(notObject, "f").mValue.has_value());
        EXPECT_FALSE(readSkeletonMap("", "f").mValue.has_value());
        EXPECT_FALSE(readSkeletonMap("{", "f").mValue.has_value());
    }

    TEST(SkeletonMapReader, a_negative_or_huge_bone_count_is_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string negative = text;
        negative.replace(negative.find("\"boneCount\": 2"), 14, "\"boneCount\": -1");
        EXPECT_FALSE(readSkeletonMap(negative, "f").mValue.has_value());
        std::string huge = text;
        huge.replace(huge.find("\"boneCount\": 2"), 14, "\"boneCount\": 99999");
        const SkeletonMapResult result = readSkeletonMap(huge, "f");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/source/boneCount");
    }

    TEST(SkeletonMapReader, a_bad_digest_is_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string upper = text;
        upper.replace(upper.find(sDigestA), 1, "A");
        EXPECT_EQ(readSkeletonMap(upper, "f").mError.field, "/source/digest");
        std::string shortDigest = text;
        shortDigest.replace(shortDigest.find(sDigestA), 4, "");
        EXPECT_EQ(readSkeletonMap(shortDigest, "f").mError.field, "/source/digest");
    }

    TEST(SkeletonMapReader, control_characters_in_a_name_are_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        text.replace(text.find("\"Helper\""), 8, "\"Hel\\u0001per\"");
        const SkeletonMapResult result = readSkeletonMap(text, "f");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/extra/0/bone");
    }

    TEST(SkeletonMapReader, an_empty_and_a_too_long_bone_name_are_refused)
    {
        std::string text = writeSkeletonMap(tinyMap());
        std::string empty = text;
        empty.replace(empty.find("\"Helper\""), 8, "\"\"");
        EXPECT_EQ(readSkeletonMap(empty, "f").mError.field, "/extra/0/bone");
        std::string longName = text;
        longName.replace(
            longName.find("\"Helper\""), 8, "\"" + std::string(SkeletonMapLimits::sMaxNameBytes + 1, 'x') + "\"");
        EXPECT_EQ(readSkeletonMap(longName, "f").mError.field, "/extra/0/bone");
        std::string exact = text;
        exact.replace(exact.find("\"Helper\""), 8, "\"" + std::string(SkeletonMapLimits::sMaxNameBytes, 'x') + "\"");
        EXPECT_TRUE(readSkeletonMap(exact, "f").mValue.has_value());
    }

    // ---- reading: hostile input -------------------------------------------------------------------------------

    TEST(SkeletonMapHostile, deep_nesting_is_refused_before_any_parsing)
    {
        const std::string deep(1000, '[');
        const SkeletonMapResult result = readSkeletonMap(deep, "deep.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_NE(result.mError.message.find("nesting"), std::string::npos) << result.mError.toString();
    }

    TEST(SkeletonMapHostile, a_two_mebibyte_file_is_refused_by_size)
    {
        const std::string big(2u << 20, ' ');
        const SkeletonMapResult result = readSkeletonMap(big, "big.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_NE(result.mError.message.find("larger"), std::string::npos) << result.mError.toString();
    }

    TEST(SkeletonMapHostile, a_hundred_thousand_entries_are_refused_by_the_prescan)
    {
        std::string text = "{\"format\": \"openeden.skeletonmap\", \"version\": 1, \"extra\": [";
        for (int i = 0; i < 100000; ++i)
        {
            text += i == 0 ? "{}" : ",{}";
        }
        text += "]}";
        ASSERT_LT(text.size(), SkeletonMapLimits::sMaxFileBytes) << "the case must be refused by count, not by size";
        const SkeletonMapResult result = readSkeletonMap(text, "many.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_NE(result.mError.message.find("too many"), std::string::npos) << result.mError.toString();
    }

    TEST(SkeletonMapHostile, more_extra_entries_than_the_cap_are_refused_by_the_reader)
    {
        SkeletonMap map = tinyMap();
        map.mExtra.clear();
        for (std::size_t i = 0; i < SkeletonMapLimits::sMaxBones + 1; ++i)
        {
            MapExtra x = extra("b" + std::to_string(i), MapAction::Drop);
            map.mExtra.push_back(x);
        }
        const std::string text = writeSkeletonMap(map);
        ASSERT_LT(text.size(), SkeletonMapLimits::sMaxFileBytes);
        const SkeletonMapResult result = readSkeletonMap(text, "cap.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/extra");
    }

    TEST(SkeletonMapHostile, too_many_aliases_for_one_role_are_refused_by_the_reader)
    {
        SkeletonMap map = tinyMap();
        for (std::size_t i = 0; i <= SkeletonMapLimits::sMaxAliasesPerRole; ++i)
        {
            map.mRoles[0].mAliases.push_back("a" + std::to_string(i));
        }
        const SkeletonMapResult result = readSkeletonMap(writeSkeletonMap(map), "alias.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.field, "/roles/0/aliases");
    }

    TEST(SkeletonMapHostile, a_missing_file_is_a_line_zero_error)
    {
        const SkeletonMapResult result = readSkeletonMapFile("this/file/does/not/exist.skeletonmap.json");
        ASSERT_FALSE(result.mValue.has_value());
        EXPECT_EQ(result.mError.line, 0u);
    }

    // ---- the vocabulary -----------------------------------------------------------------------------------------

    TEST(SkeletonMapNames, normalisation_follows_the_stated_rule)
    {
        EXPECT_EQ(normalisedBoneName("Bip01 L Hand"), "bip01_l_hand");
        EXPECT_EQ(normalisedBoneName("Bip01_L__Hand"), "bip01_l_hand");
        EXPECT_EQ(normalisedBoneName("  Foo.Bar-Baz:Q "), "foo_bar_baz_q");
        EXPECT_EQ(normalisedBoneName("___"), "");
        EXPECT_EQ(normalisedBoneName(""), "");
        EXPECT_EQ(normalisedBoneName("NPC Spine1 [Spn1]"), "npc_spine1_[spn1]");
    }

    TEST(SkeletonMapRoles, the_core_roles_come_first_in_the_profiles_order)
    {
        const std::vector<RoleInfo>& roles = canonicalRoles();
        const std::vector<std::string> core = { "Pelvis", "Spine", "Chest", "Neck", "Head", "LeftUpperArm",
            "LeftForearm", "LeftHand", "RightUpperArm", "RightForearm", "RightHand", "LeftThigh", "LeftShin",
            "LeftFoot", "RightThigh", "RightShin", "RightFoot" };
        ASSERT_GE(roles.size(), core.size());
        for (std::size_t i = 0; i < core.size(); ++i)
        {
            EXPECT_EQ(roles[i].mName, core[i]);
            EXPECT_FALSE(roles[i].mOptional) << core[i];
        }
    }

    TEST(SkeletonMapRoles, the_optional_roles_of_toddhows_answer_five_exist)
    {
        for (const char* name : { "LeftClavicle", "RightClavicle", "Spine2", "LeftToes", "RightToes", "LeftThumb1",
                 "LeftThumb3", "LeftIndex2", "LeftMiddle1", "LeftRing3", "LeftPinky2", "RightThumb1", "RightPinky3",
                 "Tail1", "Tail8" })
        {
            const RoleInfo* role = findRole(name);
            ASSERT_NE(role, nullptr) << name;
            EXPECT_TRUE(role->mOptional) << name;
        }
        EXPECT_EQ(findRole("Tail9"), nullptr);
        EXPECT_EQ(findRole("LeftThumb4"), nullptr);
        EXPECT_EQ(findRole("NotARole"), nullptr);
    }

    TEST(SkeletonMapRoles, there_are_fifteen_fingers_per_hand_and_every_role_is_unique_with_a_known_parent)
    {
        const std::vector<RoleInfo>& roles = canonicalRoles();
        std::set<std::string> names;
        std::size_t leftFingers = 0;
        std::size_t rightFingers = 0;
        for (const RoleInfo& role : roles)
        {
            EXPECT_TRUE(names.insert(std::string(role.mName)).second) << role.mName;
            if (role.mName.rfind("LeftThumb", 0) == 0 || role.mName.rfind("LeftIndex", 0) == 0
                || role.mName.rfind("LeftMiddle", 0) == 0 || role.mName.rfind("LeftRing", 0) == 0
                || role.mName.rfind("LeftPinky", 0) == 0)
            {
                ++leftFingers;
            }
            if (role.mName.rfind("RightThumb", 0) == 0 || role.mName.rfind("RightIndex", 0) == 0
                || role.mName.rfind("RightMiddle", 0) == 0 || role.mName.rfind("RightRing", 0) == 0
                || role.mName.rfind("RightPinky", 0) == 0)
            {
                ++rightFingers;
            }
        }
        EXPECT_EQ(leftFingers, 15u);
        EXPECT_EQ(rightFingers, 15u);
        for (const RoleInfo& role : roles)
        {
            if (!role.mParent.empty())
            {
                EXPECT_NE(findRole(role.mParent), nullptr) << role.mName << " -> " << role.mParent;
            }
        }
        EXPECT_TRUE(findRole("Pelvis")->mParent.empty());
        EXPECT_EQ(findRole("LeftIndex2")->mParent, "LeftIndex1");
        EXPECT_EQ(findRole("LeftIndex1")->mParent, "LeftHand");
    }

    // ---- the digest ---------------------------------------------------------------------------------------------

    TEST(SkeletonMapDigest, it_is_sixty_four_lower_case_hex_digits)
    {
        const std::string digest = skeletonDigest(baseSkeleton());
        ASSERT_EQ(digest.size(), 64u);
        for (const char c : digest)
        {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << digest;
        }
    }

    TEST(SkeletonMapDigest, it_does_not_depend_on_the_order_of_the_bones)
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        const std::string expected = skeletonDigest(skeleton);
        Lcg random(77);
        for (int i = 0; i < 20; ++i)
        {
            EXPECT_EQ(skeletonDigest(shuffledTopologically(skeleton, random)), expected);
        }
    }

    TEST(SkeletonMapDigest, a_changed_name_parent_translation_or_rotation_changes_it)
    {
        const std::vector<RestBone> skeleton = baseSkeleton();
        const std::string base = skeletonDigest(skeleton);
        std::vector<RestBone> renamed = skeleton;
        renamed[4].mName = "Bip01 Head2";
        EXPECT_NE(skeletonDigest(renamed), base);
        std::vector<RestBone> reparented = skeleton;
        reparented[4].mParent = 2;
        EXPECT_NE(skeletonDigest(reparented), base);
        std::vector<RestBone> moved = skeleton;
        moved[4].mTranslation[1] += 0.01;
        EXPECT_NE(skeletonDigest(moved), base);
        std::vector<RestBone> rotated = skeleton;
        rotated[4].mRotation = { 0, 0.0998334, 0, 0.9950042 };
        EXPECT_NE(skeletonDigest(rotated), base);
        std::vector<RestBone> extraBone = skeleton;
        extraBone.push_back(makeBone("Extra", 0, 0, 0, 0));
        EXPECT_NE(skeletonDigest(extraBone), base);
    }

    TEST(SkeletonMapDigest, a_change_below_float_precision_does_not_change_it)
    {
        std::vector<RestBone> skeleton = baseSkeleton();
        const std::string base = skeletonDigest(skeleton);
        skeleton[4].mTranslation[1] += 1e-12;
        EXPECT_EQ(skeletonDigest(skeleton), base);
    }

    TEST(SkeletonMapDigest, a_name_is_not_confused_with_its_parents_name)
    {
        // Length-prefixed fields: "ab" under "c" is not "a" under "bc".
        std::vector<RestBone> first = { makeBone("c", -1, 0, 0, 0), makeBone("ab", 0, 0, 0, 0) };
        std::vector<RestBone> second = { makeBone("bc", -1, 0, 0, 0), makeBone("a", 0, 0, 0, 0) };
        EXPECT_NE(skeletonDigest(first), skeletonDigest(second));
    }

    // ---- the validator: a valid map passes --------------------------------------------------------------------

    TEST(SkeletonMapValidate, the_extended_test_map_has_no_findings_alone_or_against_its_source)
    {
        const SkeletonMap map = extendedMap();
        const MapReport alone = validateSkeletonMap(map);
        EXPECT_TRUE(alone.mFindings.empty()) << describe(alone);
        const std::vector<RestBone> source = extendedSkeleton();
        const MapReport withSource = validateSkeletonMap(map, &source);
        EXPECT_TRUE(withSource.mFindings.empty()) << describe(withSource);
        EXPECT_FALSE(withSource.hasErrors());
    }

    TEST(SkeletonMapValidate, a_clean_source_skeleton_has_no_findings)
    {
        EXPECT_TRUE(validateSourceSkeleton(extendedSkeleton()).mFindings.empty());
    }

    TEST(SkeletonMapValidate, findings_are_sorted_errors_first_then_pointer_id_message)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[0].mAccepted = false; // error 007
        map.mExtra[4].mReason.clear(); // warning 018
        const MapReport report = validateSkeletonMap(map);
        ASSERT_GE(report.mFindings.size(), 2u);
        for (std::size_t i = 1; i < report.mFindings.size(); ++i)
        {
            const MapFinding& a = report.mFindings[i - 1];
            const MapFinding& b = report.mFindings[i];
            if (a.mSeverity != b.mSeverity)
            {
                EXPECT_EQ(a.mSeverity, MapSeverity::Error);
            }
        }
    }

    // ---- the validator: one failing fixture per rule ---------------------------------------------------------

    TEST(SkeletonMapValidate, 001_a_role_that_is_not_canonical)
    {
        SkeletonMap map = extendedMap();
        map.mRoles[1].mRole = "Bogus";
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-001", "/roles/1/role")) << describe(report);
    }

    TEST(SkeletonMapValidate, 002_a_role_listed_twice)
    {
        SkeletonMap map = extendedMap();
        map.mRoles.push_back({ "Head", "Another Head", {} });
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-002")) << describe(report);
    }

    TEST(SkeletonMapValidate, 003_a_bone_used_twice)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[0].mBone = "Bip01 Head"; // also a role bone
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-003")) << describe(report);
        SkeletonMap twice = extendedMap();
        twice.mExtra[1].mBone = twice.mExtra[0].mBone;
        EXPECT_TRUE(hasFinding(validateSkeletonMap(twice), "BODY-MAP-003"));
    }

    TEST(SkeletonMapValidate, 004_two_names_that_normalise_equal)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[0].mBone = "Bip01_L_Hand"; // "Bip01 L Hand" is a role bone
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-004")) << describe(report);
        EXPECT_FALSE(hasFinding(report, "BODY-MAP-003"));
    }

    TEST(SkeletonMapValidate, 005_a_bone_that_the_source_does_not_have)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[0].mBone = "Not In Source";
        const std::vector<RestBone> source = extendedSkeleton();
        const MapReport report = validateSkeletonMap(map, &source);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-005", "/extra/0/bone")) << describe(report);
    }

    TEST(SkeletonMapValidate, 006_a_source_bone_the_map_does_not_cover)
    {
        SkeletonMap map = extendedMap();
        map.mExtra.pop_back(); // Cape_01
        map.mSource.mBoneCount = 0; // not what this case tests
        const std::vector<RestBone> source = extendedSkeleton();
        const MapReport report = validateSkeletonMap(map, &source);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-006")) << describe(report);
    }

    TEST(SkeletonMapValidate, 007_an_entry_that_is_not_accepted)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[2].mAccepted = false;
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-007", "/extra/2/accepted")) << describe(report);
    }

    TEST(SkeletonMapValidate, 008_an_ask_entry_even_when_marked_accepted)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[4].mAction = MapAction::Ask;
        map.mExtra[4].mAccepted = true;
        const MapReport report = validateSkeletonMap(map);
        EXPECT_TRUE(hasFinding(report, "BODY-MAP-008", "/extra/4/action")) << describe(report);
    }

    TEST(SkeletonMapValidate, 009_fields_that_the_action_does_not_allow_or_requires)
    {
        {
            SkeletonMap map = extendedMap();
            map.mExtra[2].mKind = SecondaryKind::Breast; // a fold with a kind
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/2/kind"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mKind.reset(); // a secondary without a kind
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/0/kind"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mParent.clear(); // a secondary without a parent
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/0/parent"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[5].mTo.clear(); // a named fold without a target
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/5/to"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[3].mAttachPoint.clear(); // an attach without an attach point
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/3/attachPoint"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[2].mUseClipTrack = true; // clip track on a fold
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/2/useClipTrack"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[5].mRule = FoldRule::Distribute; // distribute needs a second target
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/5/toSecond"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[2].mTo = "Head"; // nearest-mapped-ancestor with a target
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-009", "/extra/2/to"));
        }
    }

    TEST(SkeletonMapValidate, 010_a_parent_or_a_fold_target_that_is_not_mapped)
    {
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mParent = "Nowhere";
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-010", "/extra/0/parent"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[5].mTo = "IK_Helper_01"; // a dropped bone is not a target
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-010", "/extra/5/to"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[5].mTo = "Bip01 Head"; // the role's bone name works as well as the role name
            EXPECT_FALSE(hasFinding(validateSkeletonMap(map), "BODY-MAP-010"));
        }
    }

    TEST(SkeletonMapValidate, 011_a_distribute_fold_with_one_target_twice)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[5].mRule = FoldRule::Distribute;
        map.mExtra[5].mTo = "Head";
        map.mExtra[5].mToSecond = "Head";
        EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-011", "/extra/5/toSecond"));
    }

    TEST(SkeletonMapValidate, 012_cycles_and_deep_chains_of_parents)
    {
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mParent = "Breast_R_01";
            map.mExtra[1].mParent = "Breast_L_01";
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-012"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mParent = "Breast_L_01"; // its own parent
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-012"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra.clear();
            std::string parent = "Chest";
            for (std::size_t i = 0; i < SkeletonMapLimits::sMaxDepth + 6; ++i)
            {
                MapExtra x = extra("Chain_" + std::to_string(i), MapAction::Secondary);
                x.mParent = parent;
                x.mKind = SecondaryKind::Other;
                map.mExtra.push_back(x);
                parent = x.mBone;
            }
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-012"));
        }
    }

    TEST(SkeletonMapValidate, 013_a_digest_or_bone_count_that_does_not_match_the_source)
    {
        const std::vector<RestBone> source = extendedSkeleton();
        {
            SkeletonMap map = extendedMap();
            map.mSource.mDigest = sDigestA;
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map, &source), "BODY-MAP-013", "/source/digest"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mSource.mBoneCount += 1;
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map, &source), "BODY-MAP-013", "/source/boneCount"));
        }
        {
            // A changed rest pose is a different skeleton: the map is stale.
            std::vector<RestBone> moved = source;
            moved[4].mTranslation[1] += 0.05;
            EXPECT_TRUE(hasFinding(validateSkeletonMap(extendedMap(), &moved), "BODY-MAP-013", "/source/digest"));
        }
    }

    TEST(SkeletonMapValidate, 014_more_entries_than_the_caps_allow)
    {
        SkeletonMap map = extendedMap();
        for (std::size_t i = 0; i < SkeletonMapLimits::sMaxBones; ++i)
        {
            map.mExtra.push_back(extra("Many_" + std::to_string(i), MapAction::Drop));
        }
        EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-014", "/extra"));
        SkeletonMap aliases = extendedMap();
        for (std::size_t i = 0; i <= SkeletonMapLimits::sMaxAliasesPerRole; ++i)
        {
            aliases.mRoles[0].mAliases.push_back("Alias_" + std::to_string(i));
        }
        EXPECT_TRUE(hasFinding(validateSkeletonMap(aliases), "BODY-MAP-014", "/roles/0/aliases"));
    }

    TEST(SkeletonMapValidate, 015_a_warning_when_a_drop_loses_weight)
    {
        const SkeletonMap map = extendedMap();
        const std::vector<RestBone> source = extendedSkeleton();
        const std::vector<BoneWeightMass> none = { { "IK_Helper_01", 0.0 } };
        EXPECT_FALSE(hasFinding(validateSkeletonMap(map, &source, &none), "BODY-MAP-015"));
        const std::vector<BoneWeightMass> heavy = { { "IK_Helper_01", 0.02 } };
        const MapReport report = validateSkeletonMap(map, &source, &heavy);
        ASSERT_TRUE(hasFinding(report, "BODY-MAP-015", "/extra/4")) << describe(report);
        for (const MapFinding& f : report.mFindings)
        {
            if (f.mId == "BODY-MAP-015")
            {
                EXPECT_EQ(f.mSeverity, MapSeverity::Warning);
            }
        }
        EXPECT_FALSE(report.hasErrors());
    }

    TEST(SkeletonMapValidate, 016_empty_long_or_control_character_text)
    {
        {
            SkeletonMap map = extendedMap();
            map.mName.clear();
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-016", "/name"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mProfile = std::string(SkeletonMapLimits::sMaxTextBytes + 1, 'p');
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-016", "/profile"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mExtra[0].mBone = "Bad\tName";
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-016", "/extra/0/bone"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mRoles[0].mBone.clear();
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-016", "/roles/0/bone"));
        }
    }

    TEST(SkeletonMapValidate, 017_an_alias_that_is_a_bone_or_listed_twice)
    {
        {
            SkeletonMap map = extendedMap();
            map.mRoles[0].mAliases = { "Jaw" }; // an extra bone
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-017"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mRoles[0].mAliases = { "Same", "Same" };
            EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-017"));
        }
        {
            SkeletonMap map = extendedMap();
            map.mRoles[0].mAliases = { "Not In Source" }; // an alias need not exist in the source
            const std::vector<RestBone> source = extendedSkeleton();
            EXPECT_FALSE(validateSkeletonMap(map, &source).hasErrors());
        }
    }

    TEST(SkeletonMapValidate, 018_a_warning_for_a_drop_without_a_reason)
    {
        SkeletonMap map = extendedMap();
        map.mExtra[4].mReason.clear();
        const MapReport report = validateSkeletonMap(map);
        ASSERT_TRUE(hasFinding(report, "BODY-MAP-018", "/extra/4/reason")) << describe(report);
        EXPECT_FALSE(report.hasErrors());
    }

    TEST(SkeletonMapValidate, 019_a_map_without_a_pelvis)
    {
        SkeletonMap map = extendedMap();
        map.mRoles.erase(map.mRoles.begin());
        EXPECT_TRUE(hasFinding(validateSkeletonMap(map), "BODY-MAP-019"));
    }

    // ---- the validator: the source skeleton ------------------------------------------------------------------------

    TEST(SkeletonMapValidate, 020_021_022_023_source_skeleton_rules)
    {
        {
            std::vector<RestBone> s = baseSkeleton();
            s[3].mName.clear();
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-020"));
            s[3].mName = std::string(SkeletonMapLimits::sMaxNameBytes + 1, 'n');
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-020"));
            s[3].mName = "Bad\nName";
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-020"));
        }
        {
            std::vector<RestBone> s;
            for (std::size_t i = 0; i < SkeletonMapLimits::sMaxBones + 1; ++i)
            {
                s.push_back(makeBone("b" + std::to_string(i), i == 0 ? -1 : 0, 0, 0, 0));
            }
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-020"));
        }
        {
            std::vector<RestBone> s = baseSkeleton();
            s[4].mParent = 9; // a parent that comes later
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-021"));
            s[4].mParent = 4; // itself
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-021"));
            s[4].mParent = -7;
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-021"));
        }
        {
            std::vector<RestBone> s = baseSkeleton();
            s[4].mName = s[3].mName;
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-022"));
            s = baseSkeleton();
            s[4].mName = "Bip01_Neck"; // normalises equal to "Bip01 Neck"
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-022"));
        }
        {
            std::vector<RestBone> s = { makeBone("root", -1, 0, 0, 0) };
            for (std::size_t i = 1; i < SkeletonMapLimits::sMaxDepth + 3; ++i)
            {
                s.push_back(makeBone("c" + std::to_string(i), static_cast<int>(i - 1), 0, 0, 0));
            }
            EXPECT_TRUE(hasFinding(validateSourceSkeleton(s), "BODY-MAP-023"));
            std::vector<RestBone> exact = { makeBone("root", -1, 0, 0, 0) };
            for (std::size_t i = 1; i < SkeletonMapLimits::sMaxDepth; ++i)
            {
                exact.push_back(makeBone("c" + std::to_string(i), static_cast<int>(i - 1), 0, 0, 0));
            }
            EXPECT_FALSE(hasFinding(validateSourceSkeleton(exact), "BODY-MAP-023"));
        }
    }

    TEST(SkeletonMapValidate, errors_are_counted_and_a_clean_map_has_none)
    {
        EXPECT_EQ(errorCount(validateSkeletonMap(extendedMap())), 0u);
        SkeletonMap map = extendedMap();
        map.mExtra[0].mAccepted = false;
        map.mExtra[1].mAccepted = false;
        const MapReport report = validateSkeletonMap(map);
        EXPECT_EQ(errorCount(report), 2u);
        EXPECT_EQ(report.count("BODY-MAP-007"), 2u);
        EXPECT_TRUE(report.hasErrors());
    }
}
