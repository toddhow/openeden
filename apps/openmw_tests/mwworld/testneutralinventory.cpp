// Task 0182: when a neutral container's activation opens the window, and what the event tells the scripts. Written
// before the code and run red against a stub first.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using openeden::sdk::game::ObjectKind;
    using namespace MWWorld;

    TEST(NeutralInventoryDecision, onlyThePlayerOpeningAContainerWithTheSettingOnOpensTheWindow)
    {
        EXPECT_EQ(decideNeutralInventory(ObjectKind::Container, true, true), NeutralInventoryDecision::Open);
        EXPECT_EQ(decideNeutralInventory(ObjectKind::Container, true, false), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralInventory(ObjectKind::Container, false, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralInventory(ObjectKind::Door, true, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralInventory(ObjectKind::Actor, true, true), NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralInventoryEvent, idsAreHexTextOfAtLeastEightDigits)
    {
        EXPECT_EQ(neutralHexId(0x1A2B), "00001a2b");
        EXPECT_EQ(neutralHexId(0xFE000801), "fe000801");
        EXPECT_EQ(neutralHexId(0x1FE000801ull), "1fe000801");
        EXPECT_EQ(neutralHexId(0), "00000000");
    }

    TEST(NeutralInventoryEvent, theFieldsAreThePlacementTheBaseTheKindAndTheNameInThatOrder)
    {
        EXPECT_EQ(kNeutralInventoryEvent, "openeden_skyrimse_container_opened");
        const auto fields = neutralInventoryEventFields(0x000A1B2C, 0x00039F29, "container", "Chest");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[0].mName, "placement");
        EXPECT_EQ(fields[0].mText, "000a1b2c");
        EXPECT_EQ(fields[1].mName, "base");
        EXPECT_EQ(fields[1].mText, "00039f29");
        EXPECT_EQ(fields[2].mName, "kind");
        EXPECT_EQ(fields[2].mText, "container");
        EXPECT_EQ(fields[3].mName, "name");
        EXPECT_EQ(fields[3].mText, "Chest");
    }
}
