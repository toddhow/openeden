#include <components/crashcatcher/freezeresponse.hpp>

#include <gtest/gtest.h>

namespace
{
    // The Windows crash monitor's answer to a frozen application (2026-11-04). Without OPENEDEN_QUIET_WINDOW it is the
    // inherited one: the "has frozen" box when the application has just frozen, the box taken down when it answers
    // again. With the variable set no box may appear, and the freeze is answered as the box's Abort button answers
    // it: a freeze dump, then the application is ended.
    using Crash::freezeResponse;
    using Crash::FreezeResponse;

    TEST(CrashFreezeResponseTest, a_fresh_freeze_shows_the_box_when_not_quiet)
    {
        EXPECT_EQ(freezeResponse(true, false, false), FreezeResponse::ShowBox);
    }

    TEST(CrashFreezeResponseTest, a_fresh_freeze_is_aborted_when_quiet)
    {
        EXPECT_EQ(freezeResponse(true, false, true), FreezeResponse::Abort);
    }

    TEST(CrashFreezeResponseTest, a_freeze_that_goes_on_changes_nothing)
    {
        EXPECT_EQ(freezeResponse(true, true, false), FreezeResponse::None);
        EXPECT_EQ(freezeResponse(true, true, true), FreezeResponse::None);
    }

    TEST(CrashFreezeResponseTest, an_application_that_answers_again_has_its_box_taken_down)
    {
        EXPECT_EQ(freezeResponse(false, true, false), FreezeResponse::HideBox);
        // Quiet, no box was shown; taking down none does nothing (CrashMonitor::hideFreezeMessageBox).
        EXPECT_EQ(freezeResponse(false, true, true), FreezeResponse::HideBox);
    }

    TEST(CrashFreezeResponseTest, an_application_that_answers_needs_nothing)
    {
        EXPECT_EQ(freezeResponse(false, false, false), FreezeResponse::None);
        EXPECT_EQ(freezeResponse(false, false, true), FreezeResponse::None);
    }

    TEST(CrashFreezeResponseTest, only_a_quiet_run_is_ever_aborted_without_a_click)
    {
        for (const bool frozen : { false, true })
            for (const bool wasFrozen : { false, true })
            {
                EXPECT_NE(freezeResponse(frozen, wasFrozen, false), FreezeResponse::Abort);
                EXPECT_NE(freezeResponse(frozen, wasFrozen, true), FreezeResponse::ShowBox);
            }
    }
}
