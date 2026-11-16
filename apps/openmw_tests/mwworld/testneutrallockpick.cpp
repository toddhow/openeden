// Task 0203 (lock picking and books): when a locked door or container offers the lock picking minigame instead of the
// refusal, the event it sends, and how a lock the player picked counts as open. Written before the code and run red
// against stubs first.
#include <gtest/gtest.h>

#include <functional>

#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using namespace MWWorld;

    TEST(NeutralLockpickDecision, aLockedDoorOrContainerOffersTheMinigameWhenEverythingIsOn)
    {
        EXPECT_EQ(
            decideNeutralLockpick(ObjectKind::Door, true, true, true, true, true), NeutralInventoryDecision::Open);
        EXPECT_EQ(
            decideNeutralLockpick(ObjectKind::Container, true, true, true, true, true), NeutralInventoryDecision::Open);
    }

    TEST(NeutralLockpickDecision, anyMissingConditionKeepsTheRefusal)
    {
        // kind, byPlayer, inventory setting, rmlui, the pack's lockpicking, the lock is locked
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Actor, true, true, true, true, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Static, true, true, true, true, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Door, false, true, true, true, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Door, true, false, true, true, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Door, true, true, false, true, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Door, true, true, true, false, true),
            NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLockpick(ObjectKind::Door, true, true, true, true, false),
            NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralLockpickEvent, theEventFieldsCarryThePlaceKindLevelAndName)
    {
        EXPECT_EQ(kNeutralLockpickEvent, "openeden_skyrimse_lock_activated");
        const auto fields = neutralLockpickEventFields(0x2D631, 0x00013BBF, "door", 50, "Door");
        ASSERT_EQ(fields.size(), 5u);
        EXPECT_EQ(fields[0].mName, "placement");
        EXPECT_EQ(fields[0].mText, "0002d631");
        EXPECT_EQ(fields[1].mName, "base");
        EXPECT_EQ(fields[1].mText, "00013bbf");
        EXPECT_EQ(fields[2].mName, "kind");
        EXPECT_EQ(fields[2].mText, "door");
        EXPECT_EQ(fields[3].mName, "level");
        EXPECT_EQ(fields[3].mText, "50");
        EXPECT_EQ(fields[4].mName, "name");
        EXPECT_EQ(fields[4].mText, "Door");
        EXPECT_EQ(neutralLockpickEventFields(1, 2, "container", -1, "")[3].mText, "-1");
    }

    TEST(NeutralLockOpen, theSessionsStateOrAPickedLockMakesItOpen)
    {
        const auto picked = [](WorldId placement) { return placement == 0x77; };
        EXPECT_TRUE(neutralLockAlreadyOpen(true, picked, 0x11));
        EXPECT_TRUE(neutralLockAlreadyOpen(false, picked, 0x77));
        EXPECT_FALSE(neutralLockAlreadyOpen(false, picked, 0x11));
        EXPECT_FALSE(neutralLockAlreadyOpen(false, {}, 0x77)); // no pack lookup: only the session's state
        EXPECT_TRUE(neutralLockAlreadyOpen(true, {}, 0x77));
    }

    TEST(NeutralLockOpen, thePackIsNotAskedWhenTheSessionAlreadyKnows)
    {
        int asked = 0;
        const auto picked = [&](WorldId) {
            ++asked;
            return false;
        };
        EXPECT_TRUE(neutralLockAlreadyOpen(true, picked, 5));
        EXPECT_EQ(asked, 0);
        EXPECT_FALSE(neutralLockAlreadyOpen(false, picked, 5));
        EXPECT_EQ(asked, 1);
    }
}
