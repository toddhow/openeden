// Task 0208 (pickup and pickpocket): what the activation of a placed item or a living actor does, and the events it
// sends. Written before the code and run red against stubs first.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using namespace MWWorld;

    TEST(NeutralPickupDecision, onlyThePlayerActivatingAnItemWithTheSettingOnPicksItUp)
    {
        EXPECT_EQ(decideNeutralPickup(ObjectKind::Item, true, true), NeutralInventoryDecision::Open);
        EXPECT_EQ(decideNeutralPickup(ObjectKind::Item, true, false), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralPickup(ObjectKind::Item, false, true), NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralPickupDecision, nothingButAnItemIsPickedUp)
    {
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::MovableStatic,
                 ObjectKind::Furniture, ObjectKind::Door, ObjectKind::Activator, ObjectKind::Container,
                 ObjectKind::Flora, ObjectKind::Tree, ObjectKind::Light, ObjectKind::Actor, ObjectKind::LeveledList,
                 ObjectKind::Other })
            EXPECT_EQ(decideNeutralPickup(kind, true, true), NeutralInventoryDecision::NotApplicable)
                << static_cast<int>(kind);
    }

    TEST(NeutralPickpocketDecision, aSneakingPlayerActivatingALivingActorPicksItsPockets)
    {
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Actor, true, true, false, true), NeutralInventoryDecision::Open);
    }

    TEST(NeutralPickpocketDecision, everyOtherCombinationIsLeftToTheOldPaths)
    {
        // kind, byPlayer, settingOn, dead, sneaking
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Actor, true, true, false, false),
            NeutralInventoryDecision::NotApplicable); // not sneaking: a conversation
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Actor, true, true, true, true),
            NeutralInventoryDecision::NotApplicable); // dead: the loot window
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Actor, true, false, false, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Actor, false, true, false, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Container, true, true, false, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralPickpocket(ObjectKind::Item, true, true, false, true),
            NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralPickupEvents, haveTheirOwnNames)
    {
        EXPECT_EQ(kNeutralPickupEvent, "openeden_skyrimse_item_activated");
        EXPECT_EQ(kNeutralPickpocketEvent, "openeden_skyrimse_pickpocket_opened");
        EXPECT_NE(kNeutralPickupEvent, kNeutralInventoryEvent);
        EXPECT_NE(kNeutralPickpocketEvent, kNeutralInventoryEvent);
    }

    TEST(NeutralPickupEvents, carryThePlacementTheBaseAndTheName)
    {
        const auto fields = neutralInventoryEventFields(0x0009cafb, 0x00012eb7, "item", "Iron Sword");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[0].mText, "0009cafb");
        EXPECT_EQ(fields[1].mText, "00012eb7");
        EXPECT_EQ(fields[2].mText, "item");
        EXPECT_EQ(fields[3].mText, "Iron Sword");
    }
}
