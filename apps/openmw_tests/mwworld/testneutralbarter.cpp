// Task 0189: when a dialogue service opens the barter window, and what the event tells the scripts. Written before the
// code and run red against a stub first.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace MWWorld;

    TEST(NeutralBarterDecision, onlyTheBarterServiceWithTheSettingOnOpensTheWindow)
    {
        EXPECT_TRUE(decideNeutralBarter("barter", true));
        EXPECT_FALSE(decideNeutralBarter("barter", false));
        EXPECT_FALSE(decideNeutralBarter("training", true));
        EXPECT_FALSE(decideNeutralBarter("rent a room", true));
        EXPECT_FALSE(decideNeutralBarter("gift", true));
        EXPECT_FALSE(decideNeutralBarter("", true));
        EXPECT_FALSE(decideNeutralBarter("Barter", true)); // the pack's name is lower case
    }

    TEST(NeutralBarterEvent, theNameIsPinnedAndDiffersFromTheContainerEvent)
    {
        EXPECT_EQ(kNeutralBarterEvent, "openeden_skyrimse_barter_opened");
        EXPECT_NE(kNeutralBarterEvent, kNeutralInventoryEvent);
    }

    TEST(NeutralBarterEvent, theFieldsAreThePlacementTheBaseAndTheNameInThatOrder)
    {
        const auto fields = neutralBarterEventFields(0x0001A66E, 0x00013BA3, "Hulda");
        ASSERT_EQ(fields.size(), 3u);
        EXPECT_EQ(fields[0].mName, "placement");
        EXPECT_EQ(fields[0].mText, "0001a66e");
        EXPECT_EQ(fields[1].mName, "base");
        EXPECT_EQ(fields[1].mText, "00013ba3");
        EXPECT_EQ(fields[2].mName, "name");
        EXPECT_EQ(fields[2].mText, "Hulda");
    }
}
