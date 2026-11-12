// Condition function names from xEdit's table (survey_names_xedit.cpp), checked against pins that were read from the
// source table and against the older upstream OpenMW names. No game data.
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace game_skyrimse::survey;

    TEST(SkyrimConditionNames, holds_a_name_for_every_one_of_the_255_surveyed_indices)
    {
        EXPECT_EQ(xeditFunctionNameCount(), 255u);
    }

    TEST(SkyrimConditionNames, the_nine_biggest_functions_have_their_xedit_names)
    {
        EXPECT_EQ(xeditFunctionName(72), "GetIsID");
        EXPECT_EQ(xeditFunctionName(566), "GetIsAliasRef");
        EXPECT_EQ(xeditFunctionName(58), "GetStage");
        EXPECT_EQ(xeditFunctionName(426), "GetIsVoiceType");
        EXPECT_EQ(xeditFunctionName(71), "GetInFaction");
        EXPECT_EQ(xeditFunctionName(629), "GetVMQuestVariable");
        EXPECT_EQ(xeditFunctionName(59), "GetStageDone");
        EXPECT_EQ(xeditFunctionName(74), "GetGlobalValue");
        EXPECT_EQ(xeditFunctionName(560), "HasKeyword");
        EXPECT_EQ(xeditFunctionName(359), "GetInCurrentLoc");
        EXPECT_EQ(xeditFunctionName(0), "GetWantBlocking");
    }

    TEST(SkyrimConditionNames, an_index_the_survey_did_not_meet_has_no_name)
    {
        EXPECT_TRUE(xeditFunctionName(2).empty()); // in xEdit's table, not in the five masters
        EXPECT_TRUE(xeditFunctionName(65535).empty());
    }

    // Where the older upstream OpenMW table (Oblivion/FO3/FNV numbering) and xEdit's differ below 331, the data
    // decides (note 0055 section 2.5): these have parameters the upstream names do not take.
    TEST(SkyrimConditionNames, the_names_that_differ_from_the_upstream_table_follow_the_data)
    {
        EXPECT_EQ(upstreamFunctionName(264), "GetBarterGold"); // takes no parameter; the data has one in 128 of 128
        EXPECT_EQ(xeditFunctionName(264), "HasSpell");
        EXPECT_EQ(upstreamFunctionName(258), "GetUsedItemLevel");
        EXPECT_EQ(xeditFunctionName(258), "HasAssociationType");
        EXPECT_EQ(xeditFunctionName(101), "IsWeaponMagicOut");
        EXPECT_EQ(xeditFunctionName(327), "IsRidingMount");
    }

    TEST(SkyrimConditionNames, the_older_upstream_names_are_kept_unchanged)
    {
        EXPECT_EQ(upstreamFunctionName(72), "GetIsID");
        EXPECT_TRUE(upstreamFunctionName(426).empty());
    }
}
