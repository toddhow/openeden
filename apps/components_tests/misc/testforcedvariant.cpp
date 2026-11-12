#include <gtest/gtest.h>

#include <components/misc/forcedvariant.hpp>

// OPENEDEN_FORCE_ANIM_VARIANT (task 0124, stage C2): the developer knob that fixes the random animation variants.
TEST(ForcedAnimVariant, a_whole_number_from_1_to_99_is_taken)
{
    EXPECT_EQ(Misc::parseForcedAnimVariant("1"), 1);
    EXPECT_EQ(Misc::parseForcedAnimVariant("2"), 2);
    EXPECT_EQ(Misc::parseForcedAnimVariant("99"), 99);
}

TEST(ForcedAnimVariant, anything_else_means_no_forcing)
{
    EXPECT_EQ(Misc::parseForcedAnimVariant(nullptr), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant(""), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant("0"), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant("-3"), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant("100"), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant("two"), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant("2x"), 0);
    EXPECT_EQ(Misc::parseForcedAnimVariant(" 2"), 2) << "strtol skips leading blanks";
    EXPECT_EQ(Misc::parseForcedAnimVariant("99999999999999999999"), 0);
}

TEST(ForcedAnimVariant, the_knob_is_off_in_a_test_run)
{
    // The variable is not set for the unit tests; with it unset the random generators are used as before.
    EXPECT_EQ(Misc::forcedAnimVariant(), 0);
}
