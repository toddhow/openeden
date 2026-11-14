#include "inputmap.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>

#include <SDL_gamecontroller.h>
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <SDL_mouse.h>

namespace UiRml
{
    using namespace Rml::Input;

    KeyIdentifier keyFromSdl(std::int32_t key)
    {
        if (key >= SDLK_a && key <= SDLK_z)
            return static_cast<KeyIdentifier>(KI_A + (key - SDLK_a));
        if (key >= SDLK_0 && key <= SDLK_9)
            return static_cast<KeyIdentifier>(KI_0 + (key - SDLK_0));
        if (key >= SDLK_F1 && key <= SDLK_F12)
            return static_cast<KeyIdentifier>(KI_F1 + (key - SDLK_F1));
        if (key >= SDLK_F13 && key <= SDLK_F24)
            return static_cast<KeyIdentifier>(KI_F13 + (key - SDLK_F13));
        if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
            return static_cast<KeyIdentifier>(KI_NUMPAD1 + (key - SDLK_KP_1));
        switch (key)
        {
            case SDLK_KP_0:
                return KI_NUMPAD0;
            case SDLK_KP_ENTER:
                return KI_NUMPADENTER;
            case SDLK_KP_MULTIPLY:
                return KI_MULTIPLY;
            case SDLK_KP_PLUS:
                return KI_ADD;
            case SDLK_KP_MINUS:
                return KI_SUBTRACT;
            case SDLK_KP_PERIOD:
                return KI_DECIMAL;
            case SDLK_KP_DIVIDE:
                return KI_DIVIDE;
            case SDLK_RETURN:
                return KI_RETURN;
            case SDLK_ESCAPE:
                return KI_ESCAPE;
            case SDLK_BACKSPACE:
                return KI_BACK;
            case SDLK_TAB:
                return KI_TAB;
            case SDLK_SPACE:
                return KI_SPACE;
            case SDLK_DELETE:
                return KI_DELETE;
            case SDLK_INSERT:
                return KI_INSERT;
            case SDLK_HOME:
                return KI_HOME;
            case SDLK_END:
                return KI_END;
            case SDLK_PAGEUP:
                return KI_PRIOR;
            case SDLK_PAGEDOWN:
                return KI_NEXT;
            case SDLK_LEFT:
                return KI_LEFT;
            case SDLK_RIGHT:
                return KI_RIGHT;
            case SDLK_UP:
                return KI_UP;
            case SDLK_DOWN:
                return KI_DOWN;
            case SDLK_SEMICOLON:
                return KI_OEM_1;
            case SDLK_SLASH:
                return KI_OEM_2;
            case SDLK_BACKQUOTE:
                return KI_OEM_3;
            case SDLK_LEFTBRACKET:
                return KI_OEM_4;
            case SDLK_BACKSLASH:
                return KI_OEM_5;
            case SDLK_RIGHTBRACKET:
                return KI_OEM_6;
            case SDLK_QUOTE:
                return KI_OEM_7;
            case SDLK_EQUALS:
                return KI_OEM_PLUS;
            case SDLK_COMMA:
                return KI_OEM_COMMA;
            case SDLK_MINUS:
                return KI_OEM_MINUS;
            case SDLK_PERIOD:
                return KI_OEM_PERIOD;
            case SDLK_CAPSLOCK:
                return KI_CAPITAL;
            case SDLK_NUMLOCKCLEAR:
                return KI_NUMLOCK;
            case SDLK_SCROLLLOCK:
                return KI_SCROLL;
            case SDLK_PAUSE:
                return KI_PAUSE;
            case SDLK_PRINTSCREEN:
                return KI_SNAPSHOT;
            case SDLK_LSHIFT:
                return KI_LSHIFT;
            case SDLK_RSHIFT:
                return KI_RSHIFT;
            case SDLK_LCTRL:
                return KI_LCONTROL;
            case SDLK_RCTRL:
                return KI_RCONTROL;
            case SDLK_LALT:
                return KI_LMENU;
            case SDLK_RALT:
                return KI_RMENU;
            case SDLK_LGUI:
                return KI_LMETA;
            case SDLK_RGUI:
                return KI_RMETA;
            case SDLK_APPLICATION:
                return KI_APPS;
            default:
                return KI_UNKNOWN;
        }
    }

    int buttonFromSdl(std::uint8_t button)
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
                return 0;
            case SDL_BUTTON_RIGHT:
                return 1;
            case SDL_BUTTON_MIDDLE:
                return 2;
            case SDL_BUTTON_X1:
                return 3;
            case SDL_BUTTON_X2:
                return 4;
            default:
                return -1;
        }
    }

    int modifiersFromSdl(std::uint16_t mod)
    {
        int out = 0;
        if (mod & KMOD_CTRL)
            out |= KM_CTRL;
        if (mod & KMOD_SHIFT)
            out |= KM_SHIFT;
        if (mod & KMOD_ALT)
            out |= KM_ALT;
        if (mod & KMOD_GUI)
            out |= KM_META;
        if (mod & KMOD_CAPS)
            out |= KM_CAPSLOCK;
        if (mod & KMOD_NUM)
            out |= KM_NUMLOCK;
        return out;
    }

    KeyIdentifier keyFromControllerButton(int button)
    {
        switch (button)
        {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                return KI_UP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                return KI_DOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                return KI_LEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                return KI_RIGHT;
            case SDL_CONTROLLER_BUTTON_A:
                return KI_RETURN;
            case SDL_CONTROLLER_BUTTON_B:
                return KI_ESCAPE;
            default:
                return KI_UNKNOWN;
        }
    }

    bool isTextEntry(std::string_view tag, std::string_view type)
    {
        const auto equals = [](std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        };
        if (equals(tag, "textarea"))
            return true;
        if (!equals(tag, "input"))
            return false;
        if (type.empty())
            return true;
        for (const char* name : { "text", "password", "search", "email", "url", "tel", "number" })
            if (equals(type, name))
                return true;
        return false;
    }

    float stickWheelTicks(int axisValue, float dt)
    {
        if (!(dt > 0.f))
            return 0.f;
        const float value = std::clamp(static_cast<float>(axisValue) / 32767.f, -1.f, 1.f);
        const float magnitude = std::abs(value);
        if (magnitude <= kStickDeadZone)
            return 0.f;
        const float speed = (magnitude - kStickDeadZone) / (1.f - kStickDeadZone) * kStickTicksPerSecond;
        return std::copysign(speed, value) * dt;
    }

    bool isPasteChord(std::int32_t key, std::uint16_t mod)
    {
        if (key == SDLK_v)
            return (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        if (key == SDLK_INSERT)
            return (mod & KMOD_SHIFT) != 0 && (mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0;
        return false;
    }

    int controllerButtonFromName(std::string_view name)
    {
        if (name.empty())
            return -1;
        const std::string copy(name);
        const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(copy.c_str());
        return button == SDL_CONTROLLER_BUTTON_INVALID ? -1 : static_cast<int>(button);
    }

    int scancodeFromName(std::string_view name)
    {
        if (name.empty())
            return -1;
        const std::string copy(name);
        const SDL_Scancode code = SDL_GetScancodeFromName(copy.c_str());
        return code == SDL_SCANCODE_UNKNOWN ? -1 : static_cast<int>(code);
    }
}
