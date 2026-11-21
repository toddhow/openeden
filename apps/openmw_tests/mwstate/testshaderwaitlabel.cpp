// Task 0363, stage f: the loading screen says "Compiling shaders" while any thread waits for the Slang runtime's
// worker. The label is chosen where the screen is drawn (the loading work rewrites its own description every frame, so
// a label put there would be gone at once).
#include <gtest/gtest.h>

#include <apps/openmw/mwgui/menureplacement.hpp>
#include <apps/openmw/mwstate/shaderwaitlabel.hpp>

#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace MWState;

    TEST(ShaderWaitLabel, the_label_is_the_localisation_key_of_the_message)
    {
        EXPECT_EQ(std::string(shaderWaitLabel), "#{OMWEngine:CompilingShaders}");
    }

    TEST(ShaderWaitLabel, the_screen_says_so_only_while_a_thread_waits)
    {
        EXPECT_EQ(loadingScreenText("#{OMWEngine:LoadingInterior}", 0), "#{OMWEngine:LoadingInterior}");
        EXPECT_EQ(loadingScreenText("#{OMWEngine:LoadingInterior}", 1), shaderWaitLabel);
        EXPECT_EQ(loadingScreenText("#{OMWEngine:LoadingInterior}", 5), shaderWaitLabel);
        EXPECT_EQ(loadingScreenText("", 1), shaderWaitLabel) << "a screen with no text of its own is drawn for it";
        EXPECT_EQ(loadingScreenText("", 0), "") << "and with no waiter stays as it was (not drawn)";
    }

    TEST(ShaderWaitLabel, a_pack_loading_screen_with_a_tip_still_shows_it_instead_of_loading)
    {
        EXPECT_EQ(MWGui::MenuReplacement::loadingLabel(shaderWaitLabel, true), shaderWaitLabel);
        EXPECT_EQ(MWGui::MenuReplacement::loadingLabel(shaderWaitLabel, false), shaderWaitLabel);
    }

    TEST(ShaderWaitLabel, waiters_are_counted_and_a_stop_without_a_start_is_ignored)
    {
        ASSERT_EQ(shaderWaiters(), 0);
        shaderWait(true);
        shaderWait(true);
        EXPECT_EQ(shaderWaiters(), 2);
        shaderWait(false);
        EXPECT_EQ(shaderWaiters(), 1);
        shaderWait(false);
        shaderWait(false);
        EXPECT_EQ(shaderWaiters(), 0) << "never below zero";
    }

    TEST(ShaderWaitLabel, threads_that_wait_at_once_leave_the_count_at_zero)
    {
        ASSERT_EQ(shaderWaiters(), 0);
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([] {
                for (int n = 0; n < 500; ++n)
                {
                    shaderWait(true);
                    shaderWait(false);
                }
            });
        for (auto& thread : threads)
            thread.join();
        EXPECT_EQ(shaderWaiters(), 0);
    }

    TEST(ShaderWaitLabel, the_frame_that_showed_the_label_is_remembered_for_the_screenshot_aid)
    {
        EXPECT_FALSE(shaderLabelShown());
        setShaderLabelShown(true);
        EXPECT_TRUE(shaderLabelShown());
        setShaderLabelShown(false);
        EXPECT_FALSE(shaderLabelShown());
    }
}
