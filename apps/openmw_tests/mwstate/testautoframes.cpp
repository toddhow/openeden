// Task 0165: --auto-screenshot and --auto-quit count frames of a running game. When the player dies the state manager
// goes to State_Ended (MWMechanics::Actors::killDeadActors -> StateManager::endGame) and the main menu draws; the old
// rule stopped counting there, so a run whose player died before the quit frame never quit.
#include <gtest/gtest.h>

#include <apps/openmw/mwstate/autoframes.hpp>

namespace
{
    using MWBase::StateManager;
    using MWState::countsAsAutoFrame;

    TEST(AutoFrames, a_running_game_with_a_scene_counts)
    {
        EXPECT_TRUE(countsAsAutoFrame(StateManager::State_Running, true, true));
    }

    TEST(AutoFrames, a_game_the_player_died_in_still_counts)
    {
        // the main menu over a finished game is a frame like any other, or a harness never reaches --auto-quit
        EXPECT_TRUE(countsAsAutoFrame(StateManager::State_Ended, true, true));
    }

    TEST(AutoFrames, loading_screens_and_operations_do_not_count)
    {
        EXPECT_FALSE(countsAsAutoFrame(StateManager::State_Running, false, false));
        EXPECT_FALSE(countsAsAutoFrame(StateManager::State_Running, true, false));
        EXPECT_FALSE(countsAsAutoFrame(StateManager::State_Running, false, true));
        EXPECT_FALSE(countsAsAutoFrame(StateManager::State_Ended, true, false));
    }

    TEST(AutoFrames, no_game_yet_does_not_count)
    {
        // the menu before a new game starts: counting there would fire the screenshot before the game exists
        EXPECT_FALSE(countsAsAutoFrame(StateManager::State_NoGame, true, true));
    }
}
