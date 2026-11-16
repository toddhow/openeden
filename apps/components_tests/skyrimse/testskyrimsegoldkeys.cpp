// Task 0184 (loot, equip and keys): the gold the speech and bribe checks read follows the inventory when a source is
// set, and the cell source answers "does the player hold this item" for the engine's lock decision. Written from the
// task spec before the code and run red against stubs first.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <openeden/game/player_item_source.hpp>

#include <limits>

namespace
{
    using namespace game_skyrimse;

    constexpr std::uint32_t kGold001 = 0x0000000F;

    struct GoldWorld : ::testing::Test
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlayerStandIn mPlayer;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        static constexpr conditions::Subject kPlayer = SkyrimConditionWorld::kPlayer;
    };

    TEST_F(GoldWorld, withoutASourceTheGoldIsTheStandInsAsBefore)
    {
        mPlayer.mGold = 77.f;
        mWorld.setPlayer(&mPlayer);
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 77.f);
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, kGold001), 77.f);
    }

    TEST_F(GoldWorld, aGoldSourceReplacesTheStandInsGoldInEveryCheck)
    {
        mPlayer.mGold = 77.f;
        mWorld.setPlayer(&mPlayer);
        float inventoryGold = 250.f;
        mWorld.setGoldSource([&] { return inventoryGold; });
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 250.f);
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, kGold001), 250.f);
        inventoryGold = 3.f; // read at the moment of the check, not cached
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 3.f);
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, 0x0000ABCD), 0.f); // another item is not gold
        mWorld.setGoldSource({});
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 77.f); // cleared: as before
    }

    TEST_F(GoldWorld, aNonFiniteOrNegativeSourceAnswerIsZero)
    {
        mWorld.setPlayer(&mPlayer);
        mWorld.setGoldSource([] { return -5.f; });
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 0.f);
        mWorld.setGoldSource([] { return std::numeric_limits<float>::quiet_NaN(); });
        EXPECT_FLOAT_EQ(mWorld.goldOf(kPlayer), 0.f);
    }

    // ---- the key source -------------------------------------------------------------------------------------------

    TEST(PlayerItems, theCellSourceAnswersFalseUntilTheInventoryGivesItsLookup)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        auto* items = dynamic_cast<openeden::sdk::game::PlayerItemSource*>(&source);
        ASSERT_NE(items, nullptr);
        EXPECT_FALSE(items->playerHasItem(0x1234));
        source.setPlayerItemLookup([](FormId id) { return id == 0x1234; });
        EXPECT_TRUE(items->playerHasItem(0x1234));
        EXPECT_FALSE(items->playerHasItem(0x1235));
        source.setPlayerItemLookup({});
        EXPECT_FALSE(items->playerHasItem(0x1234));
    }
}
