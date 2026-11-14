// Task 0111 (decision 0015, UI system U2): the pure parts of the overlay's input: the scale mapping, the SDL to RmlUi
// tables, the routing rules and the IME composition. Written from the task spec before the code (red against stubs
// first). No window, no device.
#include <components/ui_rml/bitmapfont.hpp>
#include <components/ui_rml/ime.hpp>
#include <components/ui_rml/inputmap.hpp>
#include <components/ui_rml/inputrouter.hpp>
#include <components/ui_rml/uiscale.hpp>

#include <gtest/gtest.h>

#include <SDL_gamecontroller.h>
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <SDL_mouse.h>
#include <SDL_scancode.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace UiRml;

    // ---- scale ----

    TEST(UiRmlScale, the_factor_is_the_ratio_inside_the_range)
    {
        EXPECT_FLOAT_EQ(dpRatioFor(1.f), 1.f);
        EXPECT_FLOAT_EQ(dpRatioFor(1.5f), 1.5f);
        EXPECT_FLOAT_EQ(dpRatioFor(2.f), 2.f);
        EXPECT_FLOAT_EQ(dpRatioFor(kMinGuiScale), kMinGuiScale);
        EXPECT_FLOAT_EQ(dpRatioFor(kMaxGuiScale), kMaxGuiScale);
    }

    TEST(UiRmlScale, the_factor_is_held_to_the_range)
    {
        EXPECT_FLOAT_EQ(dpRatioFor(0.01f), kMinGuiScale);
        EXPECT_FLOAT_EQ(dpRatioFor(100.f), kMaxGuiScale);
    }

    TEST(UiRmlScale, a_bad_factor_becomes_one)
    {
        EXPECT_FLOAT_EQ(dpRatioFor(0.f), 1.f);
        EXPECT_FLOAT_EQ(dpRatioFor(-2.f), 1.f);
        EXPECT_FLOAT_EQ(dpRatioFor(std::numeric_limits<float>::quiet_NaN()), 1.f);
        EXPECT_FLOAT_EQ(dpRatioFor(std::numeric_limits<float>::infinity()), 1.f);
        EXPECT_FLOAT_EQ(dpRatioFor(-std::numeric_limits<float>::infinity()), 1.f);
    }

    // The bitmap font engine is asked for round(sizeDp * ratio) pixels (RmlUi's own computation of a dp length); the
    // sampling then follows the ratio to the font's design size: nearest at whole scales, linear in between.
    TEST(UiRmlScale, whole_scales_of_a_bitmap_font_draw_nearest_and_the_rest_linear)
    {
        const float design = 16.f;
        for (const float scale : { 1.f, 2.f, 3.f })
        {
            const int requested = static_cast<int>(std::floor(design * dpRatioFor(scale) + 0.5f));
            EXPECT_TRUE(chooseSize(design, requested).mTextureNearest) << scale;
        }
        for (const float scale : { 1.25f, 1.5f, 2.5f })
        {
            const int requested = static_cast<int>(std::floor(design * dpRatioFor(scale) + 0.5f));
            EXPECT_FALSE(chooseSize(design, requested).mTextureNearest) << scale;
        }
    }

    // ---- tables ----

    TEST(UiRmlInputMap, letters_digits_and_the_named_keys)
    {
        EXPECT_EQ(keyFromSdl(SDLK_a), Rml::Input::KI_A);
        EXPECT_EQ(keyFromSdl(SDLK_z), Rml::Input::KI_Z);
        EXPECT_EQ(keyFromSdl(SDLK_0), Rml::Input::KI_0);
        EXPECT_EQ(keyFromSdl(SDLK_9), Rml::Input::KI_9);
        EXPECT_EQ(keyFromSdl(SDLK_RETURN), Rml::Input::KI_RETURN);
        EXPECT_EQ(keyFromSdl(SDLK_KP_ENTER), Rml::Input::KI_NUMPADENTER);
        EXPECT_EQ(keyFromSdl(SDLK_ESCAPE), Rml::Input::KI_ESCAPE);
        EXPECT_EQ(keyFromSdl(SDLK_BACKSPACE), Rml::Input::KI_BACK);
        EXPECT_EQ(keyFromSdl(SDLK_DELETE), Rml::Input::KI_DELETE);
        EXPECT_EQ(keyFromSdl(SDLK_TAB), Rml::Input::KI_TAB);
        EXPECT_EQ(keyFromSdl(SDLK_SPACE), Rml::Input::KI_SPACE);
        EXPECT_EQ(keyFromSdl(SDLK_HOME), Rml::Input::KI_HOME);
        EXPECT_EQ(keyFromSdl(SDLK_END), Rml::Input::KI_END);
        EXPECT_EQ(keyFromSdl(SDLK_PAGEUP), Rml::Input::KI_PRIOR);
        EXPECT_EQ(keyFromSdl(SDLK_PAGEDOWN), Rml::Input::KI_NEXT);
        EXPECT_EQ(keyFromSdl(SDLK_LEFT), Rml::Input::KI_LEFT);
        EXPECT_EQ(keyFromSdl(SDLK_RIGHT), Rml::Input::KI_RIGHT);
        EXPECT_EQ(keyFromSdl(SDLK_UP), Rml::Input::KI_UP);
        EXPECT_EQ(keyFromSdl(SDLK_DOWN), Rml::Input::KI_DOWN);
        EXPECT_EQ(keyFromSdl(SDLK_F1), Rml::Input::KI_F1);
        EXPECT_EQ(keyFromSdl(SDLK_F12), Rml::Input::KI_F12);
        EXPECT_EQ(keyFromSdl(SDLK_KP_0), Rml::Input::KI_NUMPAD0);
        EXPECT_EQ(keyFromSdl(SDLK_KP_9), Rml::Input::KI_NUMPAD9);
        EXPECT_EQ(keyFromSdl(SDLK_LSHIFT), Rml::Input::KI_LSHIFT);
        EXPECT_EQ(keyFromSdl(SDLK_RCTRL), Rml::Input::KI_RCONTROL);
    }

    TEST(UiRmlInputMap, every_letter_and_digit_is_distinct)
    {
        for (int i = 0; i < 26; ++i)
            EXPECT_EQ(keyFromSdl(SDLK_a + i), static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + i)) << i;
        for (int i = 0; i < 10; ++i)
            EXPECT_EQ(keyFromSdl(SDLK_0 + i), static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + i)) << i;
    }

    TEST(UiRmlInputMap, a_key_rmlui_has_no_identifier_for_stays_unknown)
    {
        EXPECT_EQ(keyFromSdl(SDLK_UNKNOWN), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromSdl(SDLK_AUDIOMUTE), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromSdl(0x7fffffff), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromSdl(-5), Rml::Input::KI_UNKNOWN);
    }

    TEST(UiRmlInputMap, mouse_buttons)
    {
        EXPECT_EQ(buttonFromSdl(SDL_BUTTON_LEFT), 0);
        EXPECT_EQ(buttonFromSdl(SDL_BUTTON_RIGHT), 1);
        EXPECT_EQ(buttonFromSdl(SDL_BUTTON_MIDDLE), 2);
        EXPECT_EQ(buttonFromSdl(SDL_BUTTON_X1), 3);
        EXPECT_EQ(buttonFromSdl(SDL_BUTTON_X2), 4);
        EXPECT_EQ(buttonFromSdl(0), -1);
        EXPECT_EQ(buttonFromSdl(200), -1);
    }

    TEST(UiRmlInputMap, modifiers)
    {
        EXPECT_EQ(modifiersFromSdl(KMOD_NONE), 0);
        EXPECT_EQ(modifiersFromSdl(KMOD_LSHIFT), Rml::Input::KM_SHIFT);
        EXPECT_EQ(modifiersFromSdl(KMOD_RSHIFT), Rml::Input::KM_SHIFT);
        EXPECT_EQ(modifiersFromSdl(KMOD_LCTRL), Rml::Input::KM_CTRL);
        EXPECT_EQ(modifiersFromSdl(KMOD_RALT), Rml::Input::KM_ALT);
        EXPECT_EQ(modifiersFromSdl(KMOD_NUM), Rml::Input::KM_NUMLOCK);
        EXPECT_EQ(modifiersFromSdl(KMOD_CAPS), Rml::Input::KM_CAPSLOCK);
        EXPECT_EQ(modifiersFromSdl(KMOD_LCTRL | KMOD_LSHIFT | KMOD_CAPS),
            Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT | Rml::Input::KM_CAPSLOCK);
        EXPECT_EQ(modifiersFromSdl(KMOD_LGUI), Rml::Input::KM_META);
    }

    TEST(UiRmlInputMap, controller_buttons)
    {
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_DPAD_UP), Rml::Input::KI_UP);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN), Rml::Input::KI_DOWN);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT), Rml::Input::KI_LEFT);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), Rml::Input::KI_RIGHT);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_A), Rml::Input::KI_RETURN);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_B), Rml::Input::KI_ESCAPE);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_X), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromControllerButton(SDL_CONTROLLER_BUTTON_START), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromControllerButton(-1), Rml::Input::KI_UNKNOWN);
        EXPECT_EQ(keyFromControllerButton(1000), Rml::Input::KI_UNKNOWN);
    }

    TEST(UiRmlInputMap, text_entry_controls)
    {
        EXPECT_TRUE(isTextEntry("textarea", ""));
        EXPECT_TRUE(isTextEntry("input", ""));
        EXPECT_TRUE(isTextEntry("input", "text"));
        EXPECT_TRUE(isTextEntry("input", "TEXT"));
        EXPECT_TRUE(isTextEntry("input", "password"));
        EXPECT_TRUE(isTextEntry("input", "search"));
        EXPECT_TRUE(isTextEntry("input", "number"));
        EXPECT_FALSE(isTextEntry("input", "checkbox"));
        EXPECT_FALSE(isTextEntry("input", "radio"));
        EXPECT_FALSE(isTextEntry("input", "range"));
        EXPECT_FALSE(isTextEntry("input", "submit"));
        EXPECT_FALSE(isTextEntry("button", ""));
        EXPECT_FALSE(isTextEntry("div", "text"));
        EXPECT_FALSE(isTextEntry("", ""));
    }

    TEST(UiRmlInputMap, the_stick_has_a_dead_zone_and_a_direction)
    {
        EXPECT_FLOAT_EQ(stickWheelTicks(0, 1.f), 0.f);
        EXPECT_FLOAT_EQ(stickWheelTicks(8000, 1.f), 0.f); // below a quarter of 32767
        EXPECT_FLOAT_EQ(stickWheelTicks(-8000, 1.f), 0.f);
        EXPECT_NEAR(stickWheelTicks(32767, 1.f), kStickTicksPerSecond, 1e-3f);
        EXPECT_NEAR(stickWheelTicks(-32768, 1.f), -kStickTicksPerSecond, 1e-3f);
        EXPECT_NEAR(stickWheelTicks(32767, 0.25f), kStickTicksPerSecond * 0.25f, 1e-3f);
        const float half = stickWheelTicks(20000, 1.f);
        EXPECT_GT(half, 0.f);
        EXPECT_LT(half, kStickTicksPerSecond);
        EXPECT_FLOAT_EQ(stickWheelTicks(32767, 0.f), 0.f);
        EXPECT_FLOAT_EQ(stickWheelTicks(32767, -1.f), 0.f);
    }

    // ---- routing ----

    UiState state(bool docs, bool over, bool text, bool nav)
    {
        UiState s;
        s.mHasDocuments = docs;
        s.mOverDocument = over;
        s.mTextFocus = text;
        s.mNavCaptured = nav;
        return s;
    }

    const UiState kIdle = state(true, false, false, false);
    const UiState kOver = state(true, true, false, false);
    const UiState kText = state(true, false, true, false);
    const UiState kNav = state(true, false, false, true);
    const UiState kNoDocs = state(false, true, true, true); // everything set, but nothing to receive it

    TEST(UiRmlRouter, moves_go_to_both_and_only_to_the_game_without_documents)
    {
        InputRouter router;
        auto r = router.mouseMoved(kIdle);
        EXPECT_TRUE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.mouseMoved(kNoDocs);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, a_press_over_a_document_is_the_overlays_alone)
    {
        InputRouter router;
        auto r = router.mouseButtonDown(SDL_BUTTON_LEFT, kOver);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
    }

    TEST(UiRmlRouter, a_press_elsewhere_is_the_games_alone)
    {
        InputRouter router;
        auto r = router.mouseButtonDown(SDL_BUTTON_LEFT, kIdle);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.mouseButtonDown(SDL_BUTTON_LEFT, kNoDocs);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, the_release_follows_the_press_even_when_the_cursor_has_moved)
    {
        InputRouter router;
        router.mouseButtonDown(SDL_BUTTON_LEFT, kOver);
        // The release is decided by the capture, not by where the cursor is now.
        auto r = router.mouseButtonUp(SDL_BUTTON_LEFT);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        // A second release is not captured any more.
        r = router.mouseButtonUp(SDL_BUTTON_LEFT);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, a_press_the_game_took_is_released_to_the_game_even_over_a_document)
    {
        InputRouter router;
        router.mouseButtonDown(SDL_BUTTON_LEFT, kIdle);
        auto r = router.mouseButtonUp(SDL_BUTTON_LEFT);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, buttons_are_captured_independently)
    {
        InputRouter router;
        router.mouseButtonDown(SDL_BUTTON_LEFT, kOver);
        router.mouseButtonDown(SDL_BUTTON_RIGHT, kIdle);
        EXPECT_TRUE(router.mouseButtonUp(SDL_BUTTON_LEFT).mToUi);
        EXPECT_TRUE(router.mouseButtonUp(SDL_BUTTON_RIGHT).mToGame);
    }

    TEST(UiRmlRouter, a_button_number_out_of_range_goes_to_the_game)
    {
        InputRouter router;
        auto r = router.mouseButtonDown(200, kOver);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.mouseButtonDown(-1, kOver);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        EXPECT_TRUE(router.mouseButtonUp(200).mToGame);
    }

    TEST(UiRmlRouter, the_wheel_goes_where_the_cursor_is)
    {
        InputRouter router;
        auto r = router.mouseWheel(kOver);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        r = router.mouseWheel(kIdle);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.mouseWheel(kNoDocs);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, keys_are_the_overlays_only_with_a_text_focus)
    {
        InputRouter router;
        auto r = router.keyDown(SDL_SCANCODE_A, kText);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        r = router.keyDown(SDL_SCANCODE_B, kIdle);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.keyDown(SDL_SCANCODE_C, kNav); // controller capture is not a keyboard capture
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.keyDown(SDL_SCANCODE_D, kNoDocs);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, a_focused_button_does_not_take_game_keys)
    {
        InputRouter router;
        // A control with the focus that is not a text input is state (false, false, false, false) here.
        auto r = router.keyDown(SDL_SCANCODE_W, kOver);
        EXPECT_TRUE(r.mToGame);
        EXPECT_FALSE(r.mToUi);
    }

    TEST(UiRmlRouter, a_key_release_follows_its_press_when_focus_changes)
    {
        InputRouter router;
        router.keyDown(SDL_SCANCODE_A, kText); // the overlay took it
        router.keyDown(SDL_SCANCODE_W, kIdle); // the game took it
        // Focus has moved away from the text input: the releases still go where the presses went.
        auto r = router.keyUp(SDL_SCANCODE_A);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        r = router.keyUp(SDL_SCANCODE_W);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        // And a release nobody saw the press of is the game's.
        r = router.keyUp(SDL_SCANCODE_Q);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, a_repeat_follows_the_press)
    {
        InputRouter router;
        router.keyDown(SDL_SCANCODE_A, kText);
        // The same key down again (a repeat) while the focus has moved: still the overlay's.
        auto r = router.keyDown(SDL_SCANCODE_A, kIdle);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
    }

    TEST(UiRmlRouter, a_scancode_out_of_range_goes_to_the_game)
    {
        InputRouter router;
        EXPECT_TRUE(router.keyDown(100000, kText).mToGame);
        EXPECT_TRUE(router.keyDown(-1, kText).mToGame);
        EXPECT_TRUE(router.keyUp(100000).mToGame);
    }

    TEST(UiRmlRouter, text_follows_the_text_focus)
    {
        InputRouter router;
        EXPECT_TRUE(router.text(kText).mToUi);
        EXPECT_FALSE(router.text(kText).mToGame);
        EXPECT_FALSE(router.text(kIdle).mToUi);
        EXPECT_TRUE(router.text(kIdle).mToGame);
        EXPECT_FALSE(router.text(kNoDocs).mToUi);
    }

    TEST(UiRmlRouter, controller_buttons_need_a_captured_document)
    {
        InputRouter router;
        auto r = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, kNav);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        r = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_B, kIdle);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        r = router.controllerButtonDown(SDL_CONTROLLER_BUTTON_X, kNoDocs);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, a_controller_release_follows_its_press)
    {
        InputRouter router;
        router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, kNav);
        router.controllerButtonDown(SDL_CONTROLLER_BUTTON_B, kIdle);
        EXPECT_TRUE(router.controllerButtonUp(SDL_CONTROLLER_BUTTON_A).mToUi);
        EXPECT_FALSE(router.controllerButtonUp(SDL_CONTROLLER_BUTTON_A).mToUi);
        EXPECT_TRUE(router.controllerButtonUp(SDL_CONTROLLER_BUTTON_B).mToGame);
        EXPECT_TRUE(router.controllerButtonDown(500, kNav).mToGame);
    }

    TEST(UiRmlRouter, the_stick_goes_to_the_overlay_only_when_captured)
    {
        InputRouter router;
        auto r = router.controllerAxis(SDL_CONTROLLER_AXIS_RIGHTY, kNav);
        EXPECT_TRUE(r.mToUi);
        EXPECT_FALSE(r.mToGame);
        r = router.controllerAxis(SDL_CONTROLLER_AXIS_RIGHTY, kIdle);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
        // The other axes (movement, camera) stay the game's even when captured.
        r = router.controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, kNav);
        EXPECT_FALSE(r.mToUi);
        EXPECT_TRUE(r.mToGame);
    }

    TEST(UiRmlRouter, reset_forgets_the_captures)
    {
        InputRouter router;
        router.mouseButtonDown(SDL_BUTTON_LEFT, kOver);
        router.keyDown(SDL_SCANCODE_A, kText);
        router.controllerButtonDown(SDL_CONTROLLER_BUTTON_A, kNav);
        router.reset();
        EXPECT_TRUE(router.mouseButtonUp(SDL_BUTTON_LEFT).mToGame);
        EXPECT_TRUE(router.keyUp(SDL_SCANCODE_A).mToGame);
        EXPECT_TRUE(router.controllerButtonUp(SDL_CONTROLLER_BUTTON_A).mToGame);
    }

    TEST(UiRmlRouter, nothing_is_ever_dropped)
    {
        // Property: for every state and event, at least one destination is chosen.
        InputRouter router;
        for (int bits = 0; bits < 16; ++bits)
        {
            const UiState s = state(bits & 1, bits & 2, bits & 4, bits & 8);
            const Routing all[] = { router.mouseMoved(s), router.mouseButtonDown(1, s), router.mouseWheel(s),
                router.keyDown(10, s), router.text(s), router.controllerButtonDown(0, s), router.controllerAxis(3, s) };
            for (const Routing& r : all)
                EXPECT_TRUE(r.mToUi || r.mToGame) << bits;
            router.mouseButtonUp(1);
            router.keyUp(10);
            router.controllerButtonUp(0);
        }
    }

    TEST(UiRmlRouter, nothing_goes_to_the_overlay_without_documents)
    {
        InputRouter router;
        const UiState s = kNoDocs;
        EXPECT_FALSE(router.mouseMoved(s).mToUi);
        EXPECT_FALSE(router.mouseButtonDown(1, s).mToUi);
        EXPECT_FALSE(router.mouseWheel(s).mToUi);
        EXPECT_FALSE(router.keyDown(10, s).mToUi);
        EXPECT_FALSE(router.text(s).mToUi);
        EXPECT_FALSE(router.controllerButtonDown(0, s).mToUi);
        EXPECT_FALSE(router.controllerAxis(3, s).mToUi);
    }

    // ---- IME ----

    // A text input that records what it is asked, over a string of characters (ASCII or one byte per "character" in
    // these tests; offsets are character offsets as the interface says).
    class FakeInput final : public Rml::TextInputContext
    {
    public:
        std::u32string mText;
        int mSelStart = 0;
        int mSelEnd = 0;
        int mCompStart = 0;
        int mCompEnd = 0;
        int mCommits = 0;
        std::vector<std::string> mCalls;

        bool GetBoundingBox(Rml::Rectanglef&) const override { return false; }
        void GetSelectionRange(int& start, int& end) const override
        {
            start = mSelStart;
            end = mSelEnd;
        }
        void SetSelectionRange(int start, int end) override
        {
            mSelStart = start;
            mSelEnd = end;
            mCalls.push_back("select");
        }
        void SetCursorPosition(int position) override
        {
            mSelStart = mSelEnd = position;
            mCalls.push_back("cursor");
        }
        void SetText(Rml::StringView text, int start, int end) override
        {
            std::u32string replacement;
            for (const char c : std::string(text.begin(), text.end()))
                replacement.push_back(static_cast<unsigned char>(c));
            mText.replace(start, end - start, replacement);
            mCalls.push_back("text");
        }
        void SetCompositionRange(int start, int end) override
        {
            mCompStart = start;
            mCompEnd = end;
            mCalls.push_back("range");
        }
        void CommitComposition(Rml::StringView) override
        {
            ++mCommits;
            mCompStart = mCompEnd = 0;
            mCalls.push_back("commit");
        }
    };

    TEST(UiRmlIme, a_composition_replaces_the_selection_and_is_marked)
    {
        FakeInput input;
        input.mText = U"ab";
        input.mSelStart = input.mSelEnd = 2; // the caret at the end
        ImeComposition ime;
        ime.edit(input, "ni", 2, 0);
        EXPECT_EQ(input.mText, U"abni");
        EXPECT_EQ(input.mCompStart, 2);
        EXPECT_EQ(input.mCompEnd, 4);
        EXPECT_TRUE(ime.composing());
        // The input method put the caret after the second character of the composition
        EXPECT_EQ(input.mSelStart, 4);
    }

    TEST(UiRmlIme, the_next_edit_replaces_the_previous_composition)
    {
        FakeInput input;
        input.mText = U"ab";
        input.mSelStart = input.mSelEnd = 2;
        ImeComposition ime;
        ime.edit(input, "ni", 2, 0);
        ime.edit(input, "nihao", 5, 0);
        EXPECT_EQ(input.mText, U"abnihao");
        EXPECT_EQ(input.mCompStart, 2);
        EXPECT_EQ(input.mCompEnd, 7);
    }

    TEST(UiRmlIme, an_empty_edit_after_a_composition_removes_it_and_commits)
    {
        FakeInput input;
        input.mText = U"ab";
        input.mSelStart = input.mSelEnd = 2;
        ImeComposition ime;
        ime.edit(input, "ni", 2, 0);
        ime.edit(input, "", 0, 0);
        EXPECT_EQ(input.mText, U"ab");
        EXPECT_EQ(input.mCommits, 1);
        EXPECT_FALSE(ime.composing());
    }

    TEST(UiRmlIme, an_empty_edit_with_no_composition_does_nothing)
    {
        FakeInput input;
        input.mText = U"ab";
        input.mSelStart = input.mSelEnd = 1;
        ImeComposition ime;
        ime.edit(input, "", 0, 0);
        EXPECT_EQ(input.mText, U"ab");
        EXPECT_EQ(input.mCommits, 0);
        EXPECT_FALSE(ime.composing());
    }

    TEST(UiRmlIme, a_new_composition_starts_at_the_selection_after_an_ended_one)
    {
        FakeInput input;
        input.mText = U"ab";
        input.mSelStart = input.mSelEnd = 2;
        ImeComposition ime;
        ime.edit(input, "x", 1, 0);
        ime.edit(input, "", 0, 0);
        input.mSelStart = input.mSelEnd = 0; // the caret moved to the front
        ime.edit(input, "y", 1, 0);
        EXPECT_EQ(input.mText, U"yab");
        EXPECT_EQ(input.mCompStart, 0);
        EXPECT_EQ(input.mCompEnd, 1);
    }

    TEST(UiRmlIme, the_input_methods_selection_is_applied_inside_the_composition)
    {
        FakeInput input;
        input.mSelStart = input.mSelEnd = 0;
        ImeComposition ime;
        ime.edit(input, "abc", 1, 2);
        EXPECT_EQ(input.mSelStart, 1);
        EXPECT_EQ(input.mSelEnd, 3);
    }

    TEST(UiRmlIme, utf8_composition_counts_characters_not_bytes)
    {
        FakeInput input;
        input.mSelStart = input.mSelEnd = 0;
        ImeComposition ime;
        // Two characters, six bytes: the range must be two characters long.
        ime.edit(input, "\xE4\xBD\xA0\xE5\xA5\xBD", 2, 0);
        EXPECT_EQ(input.mCompEnd - input.mCompStart, 2);
    }

    TEST(UiRmlIme, reset_forgets_the_composition)
    {
        FakeInput input;
        ImeComposition ime;
        ime.edit(input, "ni", 2, 0);
        ime.reset();
        EXPECT_FALSE(ime.composing());
    }

    TEST(UiRmlIme, the_tracker_follows_activation)
    {
        TextInputTracker tracker;
        FakeInput a, b;
        EXPECT_FALSE(tracker.active());
        EXPECT_EQ(tracker.context(), nullptr);
        tracker.OnActivate(&a);
        EXPECT_TRUE(tracker.active());
        EXPECT_EQ(tracker.context(), &a);
        // Another input takes over, then the first one is deactivated late: the second stays.
        tracker.OnActivate(&b);
        tracker.OnDeactivate(&a);
        EXPECT_TRUE(tracker.active());
        EXPECT_EQ(tracker.context(), &b);
        tracker.OnDestroy(&b);
        EXPECT_FALSE(tracker.active());
        EXPECT_EQ(tracker.context(), nullptr);
    }

    TEST(UiRmlIme, the_tracker_edits_only_the_active_input)
    {
        TextInputTracker tracker;
        FakeInput input;
        EXPECT_FALSE(tracker.edit("ni", 2, 0));
        tracker.OnActivate(&input);
        EXPECT_TRUE(tracker.edit("ni", 2, 0));
        EXPECT_EQ(input.mText, U"ni");
        tracker.OnDeactivate(&input);
        EXPECT_FALSE(tracker.edit("ni", 2, 0));
    }

    TEST(UiRmlIme, losing_the_active_input_ends_the_composition)
    {
        TextInputTracker tracker;
        FakeInput first, second;
        tracker.OnActivate(&first);
        tracker.edit("ni", 2, 0);
        tracker.OnDeactivate(&first);
        tracker.OnActivate(&second);
        second.mSelStart = second.mSelEnd = 0;
        tracker.edit("x", 1, 0);
        EXPECT_EQ(second.mCompStart, 0);
        EXPECT_EQ(second.mCompEnd, 1);
    }
}
