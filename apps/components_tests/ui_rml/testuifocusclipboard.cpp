// Task 0111 follow-ups (decision 0015, UI system U2): the gamepad-only way into the overlay's panel (a configurable
// focus key or controller button) and the clipboard bridge between RmlUi (GUI thread) and SDL (main thread). Written
// from the decisions of the follow-up before the code (red against stubs first). No window, no device.
#include <components/ui_rml/clipboard.hpp>
#include <components/ui_rml/inputmap.hpp>
#include <components/ui_rml/inputrouter.hpp>

#include <gtest/gtest.h>

#include <SDL_gamecontroller.h>
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace UiRml;

    UiState state(bool documents, bool navDocument, bool navCaptured = false, bool textFocus = false)
    {
        UiState s;
        s.mHasDocuments = documents;
        s.mHasNavDocument = navDocument;
        s.mNavCaptured = navCaptured;
        s.mTextFocus = textFocus;
        return s;
    }

    // ---- focus entry: controller button ----

    TEST(UiRmlFocusEntry, nothing_is_an_entry_while_no_button_or_key_is_configured)
    {
        InputRouter router;
        const Routing down = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true));
        EXPECT_FALSE(down.mFocusEntry);
        EXPECT_FALSE(down.mToUi);
        EXPECT_TRUE(down.mToGame);
        const Routing key = router.keyDown(SDL_SCANCODE_F9, state(true, true));
        EXPECT_FALSE(key.mFocusEntry);
        EXPECT_TRUE(key.mToGame);
    }

    TEST(UiRmlFocusEntry, the_button_enters_a_nav_capture_document_and_the_game_does_not_see_it)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, -1);
        const Routing down = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true));
        EXPECT_TRUE(down.mFocusEntry);
        EXPECT_FALSE(down.mToUi);
        EXPECT_FALSE(down.mToGame);
    }

    TEST(UiRmlFocusEntry, the_release_of_the_entry_button_is_swallowed_by_both)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, -1);
        router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true));
        const Routing up = router.controllerButtonUp(SDL_CONTROLLER_BUTTON_BACK);
        EXPECT_FALSE(up.mToUi);
        EXPECT_FALSE(up.mToGame);
        EXPECT_FALSE(up.mFocusEntry);
        // and the next release of that button is the game's again
        const Routing again = router.controllerButtonUp(SDL_CONTROLLER_BUTTON_BACK);
        EXPECT_TRUE(again.mToGame);
    }

    TEST(UiRmlFocusEntry, the_button_is_the_games_when_no_document_asks_for_the_controller)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, -1);
        const Routing noNav = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, false));
        EXPECT_FALSE(noNav.mFocusEntry);
        EXPECT_TRUE(noNav.mToGame);
        router.controllerButtonUp(SDL_CONTROLLER_BUTTON_BACK);
        const Routing noDocs = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(false, true));
        EXPECT_FALSE(noDocs.mFocusEntry);
        EXPECT_TRUE(noDocs.mToGame);
    }

    TEST(UiRmlFocusEntry, pressing_it_again_while_the_controller_is_captured_is_still_the_entry_so_it_can_leave)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, -1);
        const Routing down = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true, true));
        EXPECT_TRUE(down.mFocusEntry);
        EXPECT_FALSE(down.mToUi);
        EXPECT_FALSE(down.mToGame);
    }

    TEST(UiRmlFocusEntry, other_buttons_keep_their_rules)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, -1);
        const Routing a = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, state(true, true, true));
        EXPECT_TRUE(a.mToUi);
        EXPECT_FALSE(a.mFocusEntry);
        const Routing b = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_B, state(true, true, false));
        EXPECT_TRUE(b.mToGame);
        EXPECT_FALSE(b.mFocusEntry);
    }

    TEST(UiRmlFocusEntry, a_button_the_overlay_already_holds_does_not_become_an_entry_on_its_repeat)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_A, -1);
        // pressed while the controller is captured: it is an entry press and swallowed; the repeat is too
        const Routing first = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, state(true, true, true));
        const Routing repeat = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, state(true, true, false));
        EXPECT_TRUE(first.mFocusEntry);
        EXPECT_FALSE(repeat.mFocusEntry);
        EXPECT_FALSE(repeat.mToUi);
        EXPECT_FALSE(repeat.mToGame);
    }

    // ---- focus entry: keyboard ----

    TEST(UiRmlFocusEntry, the_key_enters_the_document_even_while_a_text_field_has_the_focus)
    {
        InputRouter router;
        router.setFocusEntry(-1, SDL_SCANCODE_F9);
        const Routing down = router.keyDown(SDL_SCANCODE_F9, state(true, true, true, true));
        EXPECT_TRUE(down.mFocusEntry);
        EXPECT_FALSE(down.mToUi);
        EXPECT_FALSE(down.mToGame);
        const Routing up = router.keyUp(SDL_SCANCODE_F9);
        EXPECT_FALSE(up.mToUi);
        EXPECT_FALSE(up.mToGame);
    }

    TEST(UiRmlFocusEntry, the_key_is_the_games_when_no_document_asks_for_the_controller)
    {
        InputRouter router;
        router.setFocusEntry(-1, SDL_SCANCODE_F9);
        const Routing down = router.keyDown(SDL_SCANCODE_F9, state(true, false));
        EXPECT_FALSE(down.mFocusEntry);
        EXPECT_TRUE(down.mToGame);
        EXPECT_FALSE(down.mToUi);
    }

    TEST(UiRmlFocusEntry, a_key_repeat_of_the_entry_key_does_not_enter_again)
    {
        InputRouter router;
        router.setFocusEntry(-1, SDL_SCANCODE_F9);
        const Routing first = router.keyDown(SDL_SCANCODE_F9, state(true, true));
        const Routing repeat = router.keyDown(SDL_SCANCODE_F9, state(true, true));
        EXPECT_TRUE(first.mFocusEntry);
        EXPECT_FALSE(repeat.mFocusEntry);
        EXPECT_FALSE(repeat.mToGame);
        EXPECT_FALSE(repeat.mToUi);
    }

    TEST(UiRmlFocusEntry, reset_forgets_a_swallowed_press)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, SDL_SCANCODE_F9);
        router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true));
        router.keyDown(SDL_SCANCODE_F9, state(true, true));
        router.reset();
        EXPECT_TRUE(router.controllerButtonUp(SDL_CONTROLLER_BUTTON_BACK).mToGame);
        EXPECT_TRUE(router.keyUp(SDL_SCANCODE_F9).mToGame);
    }

    TEST(UiRmlFocusEntry, unbinding_with_minus_one_turns_the_entry_off)
    {
        InputRouter router;
        router.setFocusEntry(SDL_CONTROLLER_BUTTON_BACK, SDL_SCANCODE_F9);
        router.setFocusEntry(-1, -1);
        EXPECT_FALSE(router.controllerButtonDown(SDL_CONTROLLER_BUTTON_BACK, state(true, true)).mFocusEntry);
        EXPECT_FALSE(router.keyDown(SDL_SCANCODE_F9, state(true, true)).mFocusEntry);
    }

    // ---- configuration names ----

    TEST(UiRmlFocusNames, a_controller_button_name_is_sdls)
    {
        EXPECT_EQ(controllerButtonFromName("back"), static_cast<int>(SDL_CONTROLLER_BUTTON_BACK));
        EXPECT_EQ(controllerButtonFromName("x"), static_cast<int>(SDL_CONTROLLER_BUTTON_X));
        EXPECT_EQ(controllerButtonFromName("leftshoulder"), static_cast<int>(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    }

    TEST(UiRmlFocusNames, an_empty_or_unknown_controller_button_is_unbound)
    {
        EXPECT_EQ(controllerButtonFromName(""), -1);
        EXPECT_EQ(controllerButtonFromName("nonsense"), -1);
        EXPECT_EQ(controllerButtonFromName("   "), -1);
    }

    TEST(UiRmlFocusNames, a_key_name_is_sdls_scancode_name)
    {
        EXPECT_EQ(scancodeFromName("F9"), static_cast<int>(SDL_SCANCODE_F9));
        EXPECT_EQ(scancodeFromName("Tab"), static_cast<int>(SDL_SCANCODE_TAB));
    }

    TEST(UiRmlFocusNames, an_empty_or_unknown_key_is_unbound)
    {
        EXPECT_EQ(scancodeFromName(""), -1);
        EXPECT_EQ(scancodeFromName("no such key"), -1);
    }

    // ---- paste chord ----

    TEST(UiRmlPasteChord, control_v_and_command_v_and_shift_insert_are_paste)
    {
        EXPECT_TRUE(isPasteChord(SDLK_v, KMOD_LCTRL));
        EXPECT_TRUE(isPasteChord(SDLK_v, KMOD_RCTRL | KMOD_NUM));
        EXPECT_TRUE(isPasteChord(SDLK_v, KMOD_LGUI));
        EXPECT_TRUE(isPasteChord(SDLK_INSERT, KMOD_LSHIFT));
    }

    TEST(UiRmlPasteChord, other_keys_are_not)
    {
        EXPECT_FALSE(isPasteChord(SDLK_v, KMOD_NONE));
        EXPECT_FALSE(isPasteChord(SDLK_v, KMOD_LSHIFT));
        EXPECT_FALSE(isPasteChord(SDLK_c, KMOD_LCTRL));
        EXPECT_FALSE(isPasteChord(SDLK_INSERT, KMOD_NONE));
        EXPECT_FALSE(isPasteChord(SDLK_INSERT, KMOD_LCTRL));
    }

    // ---- clipboard bridge ----

    struct FakeClipboard final : ClipboardBackend
    {
        std::string mText;
        int mGets = 0;
        int mSets = 0;
        std::string get() override
        {
            ++mGets;
            return mText;
        }
        void set(const std::string& text) override
        {
            ++mSets;
            mText = text;
        }
    };

    TEST(UiRmlClipboard, the_ui_has_nothing_before_the_main_thread_has_read_the_clipboard)
    {
        FakeClipboard backend;
        backend.mText = "outside";
        ClipboardBridge bridge(backend);
        EXPECT_EQ(bridge.textForUi(), "");
        EXPECT_EQ(backend.mGets, 0);
    }

    TEST(UiRmlClipboard, a_refresh_on_the_main_thread_gives_the_ui_the_current_text)
    {
        FakeClipboard backend;
        backend.mText = "outside";
        ClipboardBridge bridge(backend);
        bridge.refresh();
        EXPECT_EQ(bridge.textForUi(), "outside");
        EXPECT_EQ(backend.mGets, 1);
    }

    TEST(UiRmlClipboard, text_copied_by_the_ui_reaches_the_backend_only_on_the_main_thread)
    {
        FakeClipboard backend;
        ClipboardBridge bridge(backend);
        bridge.setFromUi("copied");
        EXPECT_EQ(backend.mSets, 0);
        // the ui sees its own copy at once
        EXPECT_EQ(bridge.textForUi(), "copied");
        bridge.pump();
        EXPECT_EQ(backend.mSets, 1);
        EXPECT_EQ(backend.mText, "copied");
        // nothing pending: no second write
        bridge.pump();
        EXPECT_EQ(backend.mSets, 1);
    }

    TEST(UiRmlClipboard, only_the_last_copy_before_a_pump_is_written)
    {
        FakeClipboard backend;
        ClipboardBridge bridge(backend);
        bridge.setFromUi("one");
        bridge.setFromUi("two");
        bridge.pump();
        EXPECT_EQ(backend.mSets, 1);
        EXPECT_EQ(backend.mText, "two");
    }

    TEST(UiRmlClipboard, a_refresh_does_not_lose_a_copy_that_was_not_written_yet)
    {
        FakeClipboard backend;
        backend.mText = "old outside text";
        ClipboardBridge bridge(backend);
        bridge.setFromUi("fresh copy");
        // Ctrl+V arrives on the main thread before the next pump
        bridge.refresh();
        EXPECT_EQ(bridge.textForUi(), "fresh copy");
        EXPECT_EQ(backend.mText, "fresh copy");
    }

    TEST(UiRmlClipboard, an_empty_copy_clears_the_clipboard)
    {
        FakeClipboard backend;
        backend.mText = "something";
        ClipboardBridge bridge(backend);
        bridge.setFromUi("");
        bridge.pump();
        EXPECT_EQ(backend.mSets, 1);
        EXPECT_EQ(backend.mText, "");
    }

    TEST(UiRmlClipboard, text_beyond_the_limit_is_cut_not_stored_whole)
    {
        FakeClipboard backend;
        ClipboardBridge bridge(backend);
        bridge.setFromUi(std::string(ClipboardBridge::kMaxBytes + 1000, 'x'));
        EXPECT_LE(bridge.textForUi().size(), ClipboardBridge::kMaxBytes);
        bridge.pump();
        EXPECT_LE(backend.mText.size(), ClipboardBridge::kMaxBytes);
        backend.mText.assign(ClipboardBridge::kMaxBytes + 5, 'y');
        bridge.refresh();
        EXPECT_LE(bridge.textForUi().size(), ClipboardBridge::kMaxBytes);
    }

    TEST(UiRmlClipboard, a_cut_never_splits_a_utf8_sequence)
    {
        FakeClipboard backend;
        ClipboardBridge bridge(backend);
        // 2-byte code points up to the limit and one past it
        std::string text;
        while (text.size() <= ClipboardBridge::kMaxBytes)
            text += "\xC3\xA9";
        bridge.setFromUi(text);
        const std::string stored = bridge.textForUi();
        ASSERT_FALSE(stored.empty());
        EXPECT_EQ(stored.size() % 2, 0u);
        EXPECT_EQ(static_cast<unsigned char>(stored.back()), 0xA9u);
    }

    TEST(UiRmlClipboard, the_two_threads_can_use_it_at_once)
    {
        FakeClipboard backend;
        backend.mText = "start";
        ClipboardBridge bridge(backend);
        std::atomic<bool> stop{ false };
        std::thread ui([&] {
            for (int i = 0; i < 2000; ++i)
            {
                bridge.setFromUi("copy " + std::to_string(i));
                (void)bridge.textForUi();
            }
            stop = true;
        });
        // the main thread pumps and refreshes; the backend is only ever touched here
        while (!stop)
        {
            bridge.pump();
            bridge.refresh();
        }
        ui.join();
        bridge.pump();
        EXPECT_EQ(backend.mText, "copy 1999");
    }
}
