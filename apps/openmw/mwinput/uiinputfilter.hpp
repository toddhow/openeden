#ifndef MWINPUT_UIINPUTFILTER_H
#define MWINPUT_UIINPUTFILTER_H

#include <components/sdlutil/events.hpp>
#include <components/ui_rml/inputrouter.hpp>

namespace UiRml
{
    class InputTarget;
}

namespace MWInput
{
    /// Task 0111 (decision 0015, UI system U2): sits between SDL's event dispatch (`SDLUtil::InputWrapper`) and the
    /// three managers that handle mouse, keyboard and controller events, and lets the RmlUi overlay take the events
    /// that are its own (`UiRml::InputRouter`, the rules are in `docs/tasks/0111-ui-u2-input-scaling.md`).
    ///
    /// With no target set (`[GUI] rmlui` off) every call is forwarded to the manager at once and nothing else
    /// happens: the filter changes nothing. Owner: InputManager. Everything runs on the main thread.
    class UiInputFilter final : public SDLUtil::MouseListener,
                                public SDLUtil::KeyListener,
                                public SDLUtil::ControllerListener
    {
    public:
        UiInputFilter(SDLUtil::MouseListener& mouse, SDLUtil::KeyListener& keys, SDLUtil::ControllerListener& controller);

        /// The overlay (not owned, must outlive the filter or be reset to null first); null switches the filter off.
        void setTarget(UiRml::InputTarget* target);
        UiRml::InputTarget* target() const { return mTarget; }

        /// The configured focus entry (`[GUI] rmlui focus button` / `focus key`): SDL's controller button and scancode,
        /// -1 for none. In GUI mode only, as every other overlay input.
        void setFocusEntry(int controllerButton, int scancode);

        // MouseListener
        void mouseMoved(const SDLUtil::MouseMotionEvent& arg) override;
        void mousePressed(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseWheelMoved(const SDL_MouseWheelEvent& arg) override;

        // KeyListener
        void textInput(const SDL_TextInputEvent& arg) override;
        void textEditing(const SDL_TextEditingEvent& arg) override;
        void keyPressed(const SDL_KeyboardEvent& arg) override;
        void keyReleased(const SDL_KeyboardEvent& arg) override;

        // ControllerListener
        void buttonPressed(int deviceID, const SDL_ControllerButtonEvent& evt) override;
        void buttonReleased(int deviceID, const SDL_ControllerButtonEvent& evt) override;
        void axisMoved(int deviceID, const SDL_ControllerAxisEvent& arg) override;
        void controllerAdded(int deviceID, const SDL_ControllerDeviceEvent& arg) override;
        void controllerRemoved(const SDL_ControllerDeviceEvent& arg) override;
        void touchpadMoved(int deviceId, const SDLUtil::TouchEvent& arg) override;
        void touchpadPressed(int deviceId, const SDLUtil::TouchEvent& arg) override;
        void touchpadReleased(int deviceId, const SDLUtil::TouchEvent& arg) override;

    private:
        /// The overlay's documents as they look now. In game mode (no cursor) nothing counts as over a document, and
        /// no focus counts either: the overlay is told once to let go of the one it holds.
        UiRml::UiState currentState();

        SDLUtil::MouseListener& mMouse;
        SDLUtil::KeyListener& mKeys;
        SDLUtil::ControllerListener& mController;
        UiRml::InputTarget* mTarget = nullptr;
        UiRml::InputRouter mRouter;
        // The wheel ticks the overlay took (SDL's 120 per notch, as MouseMotionEvent::z), taken out of what the
        // mouse manager sees, which forwards the sum to MyGUI with every move.
        Sint32 mSuppressedZ = 0;
        // Set by the move event that comes with every wheel event, read by the wheel event itself.
        bool mWheelToUi = false;
        // The overlay was told to let go of its focus because the game left GUI mode; cleared in GUI mode.
        bool mBlurRequested = false;
    };
}

#endif
