// Task 0203: the cell source's answers to the engine about lock picking (PlayerItemSource 0.2). Written before the
// setter existed (a compile failure was the red run).
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <openeden/game/player_item_source.hpp>

namespace
{
    using namespace game_skyrimse;

    TEST(LockpickSource, theVersionIsZeroTwo)
    {
        EXPECT_EQ(openeden::sdk::game::kPlayerItemSourceVersion.major, 0);
        EXPECT_EQ(openeden::sdk::game::kPlayerItemSourceVersion.minor, 2);
    }

    TEST(LockpickSource, withoutTheLookupsLockpickingIsOffAndNoLockIsPicked)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        auto* items = dynamic_cast<openeden::sdk::game::PlayerItemSource*>(&source);
        ASSERT_NE(items, nullptr);
        EXPECT_FALSE(items->lockpickingOn());
        EXPECT_FALSE(items->lockPicked(185905));
    }

    TEST(LockpickSource, theLookupTurnsItOnAndAnswersForTheLocksItKnows)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        auto* items = dynamic_cast<openeden::sdk::game::PlayerItemSource*>(&source);
        ASSERT_NE(items, nullptr);
        source.setLockpickLookups([](std::uint64_t placement) { return placement == 185905; });
        EXPECT_TRUE(items->lockpickingOn());
        EXPECT_TRUE(items->lockPicked(185905));
        EXPECT_FALSE(items->lockPicked(185906));
        source.setLockpickLookups({});
        EXPECT_FALSE(items->lockpickingOn());
        EXPECT_FALSE(items->lockPicked(185905));
    }

    // A source of the old interface (version 0.1: only playerHasItem) still compiles and answers "no" to the new calls.
    struct OldSource final : openeden::sdk::game::PlayerItemSource
    {
        bool playerHasItem(openeden::sdk::game::WorldId) const override { return true; }
    };

    TEST(LockpickSource, anInterfaceThatKnowsNothingOfLockpickingAnswersNo)
    {
        OldSource old;
        EXPECT_TRUE(old.playerHasItem(1));
        EXPECT_FALSE(old.lockpickingOn());
        EXPECT_FALSE(old.lockPicked(1));
    }
}
