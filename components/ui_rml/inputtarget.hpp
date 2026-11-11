#ifndef OPENEDEN_COMPONENTS_UI_RML_INPUTTARGET_H
#define OPENEDEN_COMPONENTS_UI_RML_INPUTTARGET_H

#include <string_view>

#include "inputrouter.hpp"

// Task 0111 (U2): what the game's input code sees of the overlay. No RmlUi or SDL type, so apps/openmw/mwinput needs
// neither header.
namespace UiRml
{
    /// Everything is called on the main thread, between frames. The overlay queues the events and applies them on the
    /// thread that runs RmlUi (task 0111, thread rule); `state()` is what that thread published at the end of its
    /// last frame, so it is at most one frame old.
    class InputTarget
    {
    public:
        virtual ~InputTarget() = default;

        virtual UiState state() const = 0;

        /// `x`, `y` in framebuffer pixels, origin top left (SDL's window coordinates).
        virtual void mouseMoved(int x, int y, int sdlMods) = 0;
        virtual void mouseButton(int sdlButton, bool down, int sdlMods) = 0;
        /// SDL's sign: positive y is away from the user (up).
        virtual void mouseWheel(float x, float y, int sdlMods) = 0;
        virtual void key(int sdlKeycode, int sdlMods, bool down) = 0;
        virtual void text(std::string_view utf8) = 0;
        /// `SDL_TEXTEDITING`: the composition so far and the selection inside it.
        virtual void textEditing(std::string_view utf8, int start, int length) = 0;
        virtual void controllerButton(int sdlButton, bool down) = 0;
        /// Only the right stick's vertical axis is passed (the router decides); `value` is SDL's.
        virtual void controllerAxis(int sdlAxis, int value) = 0;
        /// The configured focus entry key or button was pressed (`InputRouter` decided it is one): the controller focus
        /// moves into the first `nav-capture` document, or out of it when it is already inside one. Default: nothing
        /// (a target without a controller path ignores it).
        virtual void focusEntry() {}
        /// The game left GUI mode: the focus the overlay holds (a text field, a control the controller drives) is
        /// let go.
        virtual void blur() = 0;

        /// Starts and stops SDL's text input (and tells it where the caret is) to match the focus; once a frame, on
        /// the main thread.
        virtual void syncTextInput() = 0;
    };
}

#endif
