// Task 0219: when a dialogue service opens a window, and what the event tells the scripts. Written before the code and
// run red against a stub first.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace MWWorld;

    TEST(NeutralServiceDecision, theThreeOpenedServicesNeedTheSettingAndGiveTheirBareName)
    {
        EXPECT_EQ(decideNeutralService("open:training", true), "training");
        EXPECT_EQ(decideNeutralService("open:rent", true), "rent");
        EXPECT_EQ(decideNeutralService("open:gift", true), "gift");
        EXPECT_EQ(decideNeutralService("open:carry", true), "carry");
        EXPECT_EQ(decideNeutralService("open:training", false), "");
        EXPECT_EQ(decideNeutralService("open:gift", false), "");
    }

    TEST(NeutralServiceDecision, anythingElseIsLeftToTheOldPath)
    {
        EXPECT_EQ(decideNeutralService("barter", true), "");
        EXPECT_EQ(decideNeutralService("training", true), ""); // not opened by the pack: "service not implemented"
        EXPECT_EQ(decideNeutralService("rent a room", true), "");
        EXPECT_EQ(decideNeutralService("", true), "");
        EXPECT_EQ(decideNeutralService("open:", true), "");
        EXPECT_EQ(decideNeutralService("open:barter", true), "");
        EXPECT_EQ(decideNeutralService("Open:training", true), "");
    }

    TEST(NeutralServiceEvent, theNameIsPinnedAndDiffersFromTheOtherEvents)
    {
        EXPECT_EQ(kNeutralServiceEvent, "openeden_skyrimse_service_opened");
        EXPECT_NE(kNeutralServiceEvent, kNeutralBarterEvent);
        EXPECT_NE(kNeutralServiceEvent, kNeutralInventoryEvent);
    }

    TEST(NeutralServiceEvent, theFieldsAreTheServiceThePlacementTheBaseAndTheNameInThatOrder)
    {
        const auto fields = neutralServiceEventFields("training", 0x0001A6D9, 0x0001A6D9, "Njada Stonearm");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[0].mName, "service");
        EXPECT_EQ(fields[0].mText, "training");
        EXPECT_EQ(fields[1].mName, "placement");
        EXPECT_EQ(fields[1].mText, "0001a6d9");
        EXPECT_EQ(fields[2].mName, "base");
        EXPECT_EQ(fields[2].mText, "0001a6d9");
        EXPECT_EQ(fields[3].mName, "name");
        EXPECT_EQ(fields[3].mText, "Njada Stonearm");
    }
}
