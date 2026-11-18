#include <game_skyrimse/dragonborn.hpp>
#include <game_skyrimse/triggers.hpp>

#include <gtest/gtest.h>

// Task 0320 (Raven Rock): the arrival box (DLC2RRESKickoffTriggerRef) is a box trigger, so `dragonborn` alone must let
// boxes fire, as `main quest start`, `college` and `daedric quests` do. Nothing on turns them off.

TEST(SkyrimRavenRock, DragonbornAloneAcceptsBoxTriggers)
{
    EXPECT_TRUE(game_skyrimse::acceptsBoxTriggers(false, false, false, true));
}

TEST(SkyrimRavenRock, NoFeatureNoBoxes)
{
    EXPECT_FALSE(game_skyrimse::acceptsBoxTriggers(false, false, false, false));
}

TEST(SkyrimRavenRock, TheOlderFeaturesStillAcceptBoxes)
{
    EXPECT_TRUE(game_skyrimse::acceptsBoxTriggers(true, false, false, false));
    EXPECT_TRUE(game_skyrimse::acceptsBoxTriggers(false, true, false, false));
    EXPECT_TRUE(game_skyrimse::acceptsBoxTriggers(false, false, true, false));
}

// The ride script disables the ship at Raven Rock's dock; with no boat animation the ship is the deck, so only it is kept.
TEST(SkyrimRavenRock, OnlyTheShipAtTheDockStaysEnabledDuringTheRide)
{
    EXPECT_TRUE(game_skyrimse::dragonborn::keepsEnabledDuringTheRide(0x016FE2));
    EXPECT_FALSE(game_skyrimse::dragonborn::keepsEnabledDuringTheRide(0x02582C)); // Gjalund's placement: disabled
    EXPECT_FALSE(game_skyrimse::dragonborn::keepsEnabledDuringTheRide(0x018B17)); // the stage 20 trigger: disabled
}
