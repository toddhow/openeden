#ifndef OPENEDEN_COMPONENTS_UI_RML_INPUTMAP_H
#define OPENEDEN_COMPONENTS_UI_RML_INPUTMAP_H

#include <cstdint>
#include <string_view>

#include <RmlUi/Core/Input.h>

// Task 0111 (U2): the tables that turn SDL's input values into RmlUi's. Pure: no SDL call, only SDL's constants as
// plain integers (the values are SDL 2's, fixed by its ABI), so the tests need no window.
namespace UiRml
{
    /// RmlUi's identifier for an SDL key code (`SDL_Keycode`, the layout-aware value of a key event), or KI_UNKNOWN.
    Rml::Input::KeyIdentifier keyFromSdl(std::int32_t sdlKeycode);

    /// RmlUi's mouse button index (0 left, 1 right, 2 middle, 3 and up for the others) for an SDL button number
    /// (SDL_BUTTON_LEFT 1, MIDDLE 2, RIGHT 3, X1 4, X2 5); -1 for anything else.
    int buttonFromSdl(std::uint8_t sdlButton);

    /// RmlUi's key modifier bits (`Rml::Input::KeyModifier`) for SDL's `SDL_Keymod` bits.
    int modifiersFromSdl(std::uint16_t sdlMod);

    /// The key a controller button stands for while an overlay document has the controller: the d-pad is the four
    /// arrows, A is Return, B is Escape; every other button is KI_UNKNOWN. `sdlControllerButton` is
    /// `SDL_GameControllerButton`.
    Rml::Input::KeyIdentifier keyFromControllerButton(int sdlControllerButton);

    /// True for the form controls that take typed text: `textarea`, and `input` whose type is empty, `text`,
    /// `password`, `search`, `email`, `url`, `tel` or `number` (case-insensitive).
    bool isTextEntry(std::string_view tag, std::string_view type);

    /// The mouse wheel ticks the right stick produces: `axisValue` (SDL's -32768 to 32767) inside a dead zone of
    /// a quarter of the range is nothing; outside it the speed grows linearly to `kStickTicksPerSecond` at full
    /// deflection (from nothing at the edge of the dead zone), and a push downwards (positive) scrolls down (a
    /// positive tick count is down, as RmlUi's wheel delta).
    inline constexpr float kStickDeadZone = 0.25f;
    inline constexpr float kStickTicksPerSecond = 8.f;
    float stickWheelTicks(int axisValue, float dtSeconds);
}

#endif
