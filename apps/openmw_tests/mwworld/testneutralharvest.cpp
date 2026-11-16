// Task 0221 (harvesting): what the activation of a placed flora or tree does, which kinds show a tooltip, and the event
// it sends. Written before the code and run red against stubs first.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using namespace MWWorld;

    TEST(NeutralHarvestDecision, onlyThePlayerActivatingAFloraOrATreeWithTheSettingOnHarvests)
    {
        for (const ObjectKind kind : { ObjectKind::Flora, ObjectKind::Tree })
        {
            EXPECT_EQ(decideNeutralHarvest(kind, true, true), NeutralInventoryDecision::Open) << static_cast<int>(kind);
            EXPECT_EQ(decideNeutralHarvest(kind, true, false), NeutralInventoryDecision::NotApplicable);
            EXPECT_EQ(decideNeutralHarvest(kind, false, true), NeutralInventoryDecision::NotApplicable);
        }
    }

    TEST(NeutralHarvestDecision, nothingElseIsHarvested)
    {
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::MovableStatic,
                 ObjectKind::Furniture, ObjectKind::Door, ObjectKind::Activator, ObjectKind::Container,
                 ObjectKind::Item, ObjectKind::Light, ObjectKind::Actor, ObjectKind::LeveledList, ObjectKind::Other })
            EXPECT_EQ(decideNeutralHarvest(kind, true, true), NeutralInventoryDecision::NotApplicable)
                << static_cast<int>(kind);
    }

    TEST(NeutralHarvestDecision, theEventNameIsTheOneTheScriptListensTo)
    {
        EXPECT_EQ(kNeutralHarvestEvent, "openeden_skyrimse_harvest_activated");
        EXPECT_NE(kNeutralHarvestEvent, kNeutralPickupEvent);
        EXPECT_NE(kNeutralHarvestEvent, kNeutralInventoryEvent);
    }

    TEST(NeutralHarvestToolTip, aFloraAndATreeShowTheirNameOnlyWithTheNeutralInventoryOn)
    {
        EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Flora, true));
        EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Tree, true));
        EXPECT_FALSE(neutralKindHasToolTip(ObjectKind::Flora, false));
        EXPECT_FALSE(neutralKindHasToolTip(ObjectKind::Tree, false));
    }

    TEST(NeutralHarvestToolTip, theKindsThatHadATooltipKeepIt)
    {
        for (const bool inventoryOn : { false, true })
        {
            EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Actor, inventoryOn));
            EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Door, inventoryOn));
            EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Container, inventoryOn));
        }
        EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Furniture, true));
        EXPECT_TRUE(neutralKindHasToolTip(ObjectKind::Item, true));
        EXPECT_FALSE(neutralKindHasToolTip(ObjectKind::Furniture, false));
        EXPECT_FALSE(neutralKindHasToolTip(ObjectKind::Item, false));
    }

    TEST(NeutralHarvestToolTip, everyOtherKindStillShowsNone)
    {
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::MovableStatic,
                 ObjectKind::Activator, ObjectKind::Light, ObjectKind::LeveledList, ObjectKind::Other })
            for (const bool inventoryOn : { false, true })
                EXPECT_FALSE(neutralKindHasToolTip(kind, inventoryOn)) << static_cast<int>(kind);
    }

    TEST(NeutralHarvestEvent, carriesThePlacementTheBaseAndTheKind)
    {
        const std::vector<NeutralInventoryEventField> fields = neutralInventoryEventFields(0x000a1001, 0x0010dcc0, "flora", "Canis Root");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[0].mName, "placement");
        EXPECT_EQ(fields[0].mText, "000a1001");
        EXPECT_EQ(fields[1].mName, "base");
        EXPECT_EQ(fields[1].mText, "0010dcc0");
        EXPECT_EQ(fields[2].mText, "flora");
    }
}
