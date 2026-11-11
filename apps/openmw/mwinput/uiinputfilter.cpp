#include "uiinputfilter.hpp"

#include <SDL_keyboard.h>

#include <components/ui_rml/inputtarget.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

namespace MWInput
{
    namespace
    {
        // The right stick's vertical axis (SDL_CONTROLLER_AXIS_RIGHTY) is the only one the overlay scrolls with.
        constexpr int sRightStickY = SDL_CONTROLLER_AXIS_RIGHTY;

        int currentMods()
        {
            return static_cast<int>(SDL_GetModState());
        }
    }

    UiInputFilter::UiInputFilter(
        SDLUtil::MouseListener& mouse, SDLUtil::KeyListener& keys, SDLUtil::ControllerListener& controller)
        : mMouse(mouse)
        , mKeys(keys)
        , mController(controller)
    {
    }

    void UiInputFilter::setTarget(UiRml::InputTarget* target)
    {
        if (mTarget != target)
        {
            mRouter.reset();
            mSuppressedZ = 0;
            mWheelToUi = false;
        }
        mTarget = target;
    }

    UiRml::UiState UiInputFilter::currentState()
    {
        UiRml::UiState state = mTarget->state();
        const MWBase::WindowManager* windows = MWBase::Environment::get().getWindowManager();
        if (windows != nullptr && windows->isGuiMode())
        {
            mBlurRequested = false;
            return state;
        }
        // Game mode: no cursor, so nothing is over a document; and a focus the overlay still holds (a text field
        // when a script or dialogue closed the GUI mode) must not keep the keyboard or the controller from the game,
        // so it is let go once and no event is routed to it.
        if ((state.mTextFocus || state.mNavCaptured) && !mBlurRequested)
        {
            mTarget->blur();
            mBlurRequested = true;
        }
        state.mOverDocument = false;
        state.mTextFocus = false;
        state.mNavCaptured = false;
        return state;
    }

    void UiInputFilter::mouseMoved(const SDLUtil::MouseMotionEvent& arg)
    {
        if (mTarget == nullptr)
        {
            mMouse.mouseMoved(arg);
            return;
        }
        const UiRml::UiState state = currentState();
        SDLUtil::MouseMotionEvent forwarded = arg;
        if (arg.type == SDL_MOUSEWHEEL)
        {
            // The wheel that the overlay takes must not reach MyGUI through the running total either.
            mWheelToUi = mRouter.mouseWheel(state).mToUi;
            if (mWheelToUi)
            {
                mSuppressedZ += arg.zrel;
                forwarded.zrel = 0; // the game sees a move without a wheel
            }
        }
        else if (mRouter.mouseMoved(state).mToUi)
        {
            // A real move: the overlay needs the position for hover and drags (only while the game shows a cursor).
            const MWBase::WindowManager* windows = MWBase::Environment::get().getWindowManager();
            if (windows != nullptr && windows->isGuiMode())
                mTarget->mouseMoved(arg.x, arg.y, currentMods());
        }
        forwarded.z -= mSuppressedZ;
        mMouse.mouseMoved(forwarded);
    }

    void UiInputFilter::mousePressed(const SDL_MouseButtonEvent& arg, Uint8 id)
    {
        if (mTarget == nullptr)
        {
            mMouse.mousePressed(arg, id);
            return;
        }
        const UiRml::UiState state = currentState();
        const UiRml::Routing route = mRouter.mouseButtonDown(id, state);
        // A press that is not the overlay's drops the focus it holds (a click away from a text field ends the typing).
        if (!route.mToUi && (state.mTextFocus || state.mNavCaptured))
            mTarget->blur();
        if (route.mToUi)
        {
            // The position first: the overlay hit-tests the press at the place it happened (the cursor may not have
            // moved since the last frame), so the move must come before the button in its queue.
            mTarget->mouseMoved(arg.x, arg.y, currentMods());
            mTarget->mouseButton(id, true, currentMods());
        }
        if (route.mToGame)
            mMouse.mousePressed(arg, id);
    }

    void UiInputFilter::mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 id)
    {
        if (mTarget == nullptr)
        {
            mMouse.mouseReleased(arg, id);
            return;
        }
        const UiRml::Routing route = mRouter.mouseButtonUp(id);
        if (route.mToUi)
            mTarget->mouseButton(id, false, currentMods());
        if (route.mToGame)
            mMouse.mouseReleased(arg, id);
    }

    void UiInputFilter::mouseWheelMoved(const SDL_MouseWheelEvent& arg)
    {
        if (mTarget == nullptr)
        {
            mMouse.mouseWheelMoved(arg);
            return;
        }
        // Decided with the move that came just before (InputWrapper sends one for every wheel event).
        const bool toUi = mWheelToUi;
        mWheelToUi = false;
        if (toUi)
            mTarget->mouseWheel(static_cast<float>(arg.x), static_cast<float>(arg.y), currentMods());
        else
            mMouse.mouseWheelMoved(arg);
    }

    void UiInputFilter::textInput(const SDL_TextInputEvent& arg)
    {
        if (mTarget != nullptr && mRouter.text(currentState()).mToUi)
            mTarget->text(arg.text);
        else
            mKeys.textInput(arg);
    }

    void UiInputFilter::textEditing(const SDL_TextEditingEvent& arg)
    {
        if (mTarget != nullptr && mRouter.text(currentState()).mToUi)
            mTarget->textEditing(arg.text, arg.start, arg.length);
        else
            mKeys.textEditing(arg);
    }

    void UiInputFilter::keyPressed(const SDL_KeyboardEvent& arg)
    {
        if (mTarget == nullptr)
        {
            mKeys.keyPressed(arg);
            return;
        }
        const UiRml::Routing route = mRouter.keyDown(arg.keysym.scancode, currentState());
        if (route.mToUi)
            mTarget->key(arg.keysym.sym, arg.keysym.mod, true);
        if (route.mToGame)
            mKeys.keyPressed(arg);
    }

    void UiInputFilter::keyReleased(const SDL_KeyboardEvent& arg)
    {
        if (mTarget == nullptr)
        {
            mKeys.keyReleased(arg);
            return;
        }
        const UiRml::Routing route = mRouter.keyUp(arg.keysym.scancode);
        if (route.mToUi)
            mTarget->key(arg.keysym.sym, arg.keysym.mod, false);
        if (route.mToGame)
            mKeys.keyReleased(arg);
    }

    void UiInputFilter::buttonPressed(int deviceID, const SDL_ControllerButtonEvent& evt)
    {
        if (mTarget == nullptr)
        {
            mController.buttonPressed(deviceID, evt);
            return;
        }
        const UiRml::Routing route = mRouter.controllerButtonDown(evt.button, currentState());
        if (route.mToUi)
            mTarget->controllerButton(evt.button, true);
        if (route.mToGame)
            mController.buttonPressed(deviceID, evt);
    }

    void UiInputFilter::buttonReleased(int deviceID, const SDL_ControllerButtonEvent& evt)
    {
        if (mTarget == nullptr)
        {
            mController.buttonReleased(deviceID, evt);
            return;
        }
        const UiRml::Routing route = mRouter.controllerButtonUp(evt.button);
        if (route.mToUi)
            mTarget->controllerButton(evt.button, false);
        if (route.mToGame)
            mController.buttonReleased(deviceID, evt);
    }

    void UiInputFilter::axisMoved(int deviceID, const SDL_ControllerAxisEvent& arg)
    {
        if (mTarget == nullptr)
        {
            mController.axisMoved(deviceID, arg);
            return;
        }
        const UiRml::Routing route = mRouter.controllerAxis(arg.axis, currentState());
        if (route.mToUi)
            mTarget->controllerAxis(sRightStickY, arg.value);
        else if (arg.axis == sRightStickY)
            mTarget->controllerAxis(sRightStickY, 0); // the capture ended: the stick no longer scrolls
        if (route.mToGame)
            mController.axisMoved(deviceID, arg);
    }

    void UiInputFilter::controllerAdded(int deviceID, const SDL_ControllerDeviceEvent& arg)
    {
        mController.controllerAdded(deviceID, arg);
    }

    void UiInputFilter::controllerRemoved(const SDL_ControllerDeviceEvent& arg)
    {
        mController.controllerRemoved(arg);
    }

    void UiInputFilter::touchpadMoved(int deviceId, const SDLUtil::TouchEvent& arg)
    {
        mController.touchpadMoved(deviceId, arg);
    }

    void UiInputFilter::touchpadPressed(int deviceId, const SDLUtil::TouchEvent& arg)
    {
        mController.touchpadPressed(deviceId, arg);
    }

    void UiInputFilter::touchpadReleased(int deviceId, const SDLUtil::TouchEvent& arg)
    {
        mController.touchpadReleased(deviceId, arg);
    }
}
