#include "apps/opencs/model/prefs/quietwindow.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace CSMPrefs
{
    namespace
    {
        // OPENEDEN_QUIET_WINDOW in Eden Studio (2026-11-04): test and check runs must not show a window on the desktop,
        // as the game's quiet window (apps/openmw/vsgengine.cpp) does not.

        Qt::WindowType typeOf(Qt::WindowFlags flags)
        {
            return static_cast<Qt::WindowType>(flags.toInt() & Qt::WindowType_Mask);
        }

        int hintsOf(Qt::WindowFlags flags)
        {
            return flags.toInt() & ~int(Qt::WindowType_Mask);
        }

        TEST(CSMPrefsQuietWindowTest, unset_empty_and_zero_mean_off_as_in_the_game)
        {
            EXPECT_FALSE(isQuietWindowValue(nullptr));
            EXPECT_FALSE(isQuietWindowValue(""));
            EXPECT_FALSE(isQuietWindowValue("0"));
        }

        TEST(CSMPrefsQuietWindowTest, any_other_value_means_on_as_in_the_game)
        {
            EXPECT_TRUE(isQuietWindowValue("1"));
            EXPECT_TRUE(isQuietWindowValue("yes"));
            // The game compares with "0" exactly.
            EXPECT_TRUE(isQuietWindowValue("00"));
            EXPECT_TRUE(isQuietWindowValue(" 0"));
        }

        TEST(CSMPrefsQuietWindowTest, the_position_is_the_games_in_device_pixels)
        {
            EXPECT_EQ(quietWindowPosition(1.0), QPoint(-30000, -30000));
            EXPECT_EQ(quietWindowPosition(1.5), QPoint(-20000, -20000));
            EXPECT_EQ(quietWindowPosition(2.0), QPoint(-15000, -15000));
            EXPECT_EQ(quietWindowPosition(3.0), QPoint(-10000, -10000));
        }

        TEST(CSMPrefsQuietWindowTest, a_ratio_that_is_not_positive_counts_as_one)
        {
            EXPECT_EQ(quietWindowPosition(0.0), QPoint(-30000, -30000));
            EXPECT_EQ(quietWindowPosition(-2.0), QPoint(-30000, -30000));
        }

        TEST(CSMPrefsQuietWindowTest, the_quiet_position_is_on_no_screen_of_a_wide_layout)
        {
            // Three 4K monitors left of a primary one, and one above.
            const std::vector<QRect> screens{ QRect(0, 0, 3840, 2160), QRect(-11520, 0, 11520, 2160),
                QRect(0, -2160, 3840, 2160) };
            const QSize frame(1400, 900);
            EXPECT_FALSE(isOnAnyScreen(QRect(quietWindowPosition(1.0), frame), screens));
            EXPECT_FALSE(isOnAnyScreen(QRect(quietWindowPosition(2.0), frame), screens));
        }

        TEST(CSMPrefsQuietWindowTest, a_frame_overlapping_a_screen_is_on_it)
        {
            const std::vector<QRect> screens{ QRect(0, 0, 2560, 1440), QRect(-1920, 200, 1920, 1080) };
            EXPECT_TRUE(isOnAnyScreen(QRect(100, 100, 800, 600), screens));
            // Only one corner pixel on the primary screen.
            EXPECT_TRUE(isOnAnyScreen(QRect(-799, -599, 800, 600), screens));
            // On the second screen only.
            EXPECT_TRUE(isOnAnyScreen(QRect(-1000, 1000, 400, 400), screens));
        }

        TEST(CSMPrefsQuietWindowTest, a_frame_that_only_touches_an_edge_is_not_on_the_screen)
        {
            const std::vector<QRect> screens{ QRect(0, 0, 2560, 1440) };
            EXPECT_FALSE(isOnAnyScreen(QRect(-800, 100, 800, 600), screens));
            EXPECT_FALSE(isOnAnyScreen(QRect(100, -600, 800, 600), screens));
            EXPECT_FALSE(isOnAnyScreen(QRect(2560, 100, 800, 600), screens));
        }

        TEST(CSMPrefsQuietWindowTest, no_screens_means_on_no_screen)
        {
            EXPECT_FALSE(isOnAnyScreen(QRect(0, 0, 800, 600), {}));
        }

        TEST(CSMPrefsQuietWindowTest, an_unowned_window_becomes_a_tool_window_that_takes_no_focus)
        {
            const Qt::WindowFlags window = Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint;
            const Qt::WindowFlags quiet = quietWindowFlags(window, false);
            EXPECT_EQ(typeOf(quiet), Qt::Tool);
            EXPECT_TRUE(quiet.testFlag(Qt::WindowDoesNotAcceptFocus));
            // The hints are kept.
            EXPECT_EQ(hintsOf(quiet), hintsOf(window | Qt::WindowDoesNotAcceptFocus));
        }

        TEST(CSMPrefsQuietWindowTest, unowned_dialogs_and_sheets_become_tool_windows)
        {
            const Qt::WindowFlags dialog = Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint;
            const Qt::WindowFlags quietDialog = quietWindowFlags(dialog, false);
            EXPECT_EQ(typeOf(quietDialog), Qt::Tool);
            EXPECT_TRUE(quietDialog.testFlag(Qt::MSWindowsFixedSizeDialogHint));
            EXPECT_TRUE(quietDialog.testFlag(Qt::WindowDoesNotAcceptFocus));
            EXPECT_EQ(typeOf(quietWindowFlags(Qt::Sheet, false)), Qt::Tool);
        }

        TEST(CSMPrefsQuietWindowTest, an_owned_window_keeps_its_type_and_takes_no_focus)
        {
            // Windows gives an owned window no taskbar button, and Qt makes a transient parent the owner.
            for (const Qt::WindowType type : { Qt::Window, Qt::Dialog, Qt::Sheet })
            {
                const Qt::WindowFlags quiet = quietWindowFlags(type | Qt::WindowTitleHint, true);
                EXPECT_EQ(typeOf(quiet), type);
                EXPECT_TRUE(quiet.testFlag(Qt::WindowDoesNotAcceptFocus));
                EXPECT_TRUE(quiet.testFlag(Qt::WindowTitleHint));
            }
        }

        TEST(CSMPrefsQuietWindowTest, tools_drawers_and_splash_screens_keep_their_type_and_take_no_focus)
        {
            for (const Qt::WindowType type : { Qt::Tool, Qt::Drawer, Qt::SplashScreen })
                for (const bool owned : { false, true })
                {
                    const Qt::WindowFlags quiet = quietWindowFlags(type, owned);
                    EXPECT_EQ(typeOf(quiet), type);
                    EXPECT_TRUE(quiet.testFlag(Qt::WindowDoesNotAcceptFocus));
                }
        }

        TEST(CSMPrefsQuietWindowTest, popups_tooltips_and_child_widgets_are_left_alone)
        {
            for (const Qt::WindowType type : { Qt::Popup, Qt::ToolTip, Qt::Widget, Qt::SubWindow, Qt::Desktop })
                for (const bool owned : { false, true })
                {
                    const Qt::WindowFlags flags = type | Qt::FramelessWindowHint;
                    EXPECT_EQ(quietWindowFlags(flags, owned).toInt(), flags.toInt());
                }
        }

        TEST(CSMPrefsQuietWindowTest, quiet_flags_do_not_change_when_applied_twice)
        {
            for (const Qt::WindowType type : { Qt::Window, Qt::Dialog, Qt::Tool, Qt::Popup })
                for (const bool owned : { false, true })
                {
                    const Qt::WindowFlags once = quietWindowFlags(type | Qt::WindowTitleHint, owned);
                    EXPECT_EQ(quietWindowFlags(once, owned).toInt(), once.toInt());
                }
        }

        TEST(CSMPrefsQuietWindowTest, maximised_full_screen_and_minimised_are_taken_off)
        {
            EXPECT_EQ(quietWindowStates(Qt::WindowMaximized).toInt(), static_cast<int>(Qt::WindowNoState));
            EXPECT_EQ(quietWindowStates(Qt::WindowFullScreen).toInt(), static_cast<int>(Qt::WindowNoState));
            EXPECT_EQ(quietWindowStates(Qt::WindowMinimized).toInt(), static_cast<int>(Qt::WindowNoState));
            EXPECT_EQ(quietWindowStates(Qt::WindowMinimized | Qt::WindowMaximized).toInt(),
                static_cast<int>(Qt::WindowNoState));
        }

        TEST(CSMPrefsQuietWindowTest, other_states_are_kept)
        {
            EXPECT_EQ(quietWindowStates(Qt::WindowNoState).toInt(), static_cast<int>(Qt::WindowNoState));
            EXPECT_EQ(quietWindowStates(Qt::WindowActive).toInt(), static_cast<int>(Qt::WindowActive));
            EXPECT_EQ(
                quietWindowStates(Qt::WindowActive | Qt::WindowMaximized).toInt(), static_cast<int>(Qt::WindowActive));
        }
    }
}
