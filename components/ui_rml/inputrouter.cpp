#include "inputrouter.hpp"

namespace UiRml
{
    namespace
    {
        // The right stick's vertical axis, the only axis the overlay takes (SDL_CONTROLLER_AXIS_RIGHTY).
        constexpr int sRightStickY = 3;

        template <std::size_t N>
        bool inRange(const std::bitset<N>&, int index)
        {
            return index >= 0 && static_cast<std::size_t>(index) < N;
        }
    }

    Routing InputRouter::mouseMoved(const UiState& state)
    {
        return { state.mHasDocuments, true };
    }

    Routing InputRouter::mouseButtonDown(int button, const UiState& state)
    {
        if (!inRange(mCapturedButtons, button))
            return { false, true };
        const bool ui = state.mHasDocuments && state.mOverDocument;
        mCapturedButtons.set(static_cast<std::size_t>(button), ui);
        return { ui, !ui };
    }

    Routing InputRouter::mouseButtonUp(int button)
    {
        if (!inRange(mCapturedButtons, button))
            return { false, true };
        const bool ui = mCapturedButtons.test(static_cast<std::size_t>(button));
        mCapturedButtons.reset(static_cast<std::size_t>(button));
        return { ui, !ui };
    }

    Routing InputRouter::mouseWheel(const UiState& state)
    {
        const bool ui = state.mHasDocuments && state.mOverDocument;
        return { ui, !ui };
    }

    Routing InputRouter::keyDown(int scancode, const UiState& state)
    {
        if (!inRange(mUiKeys, scancode))
            return { false, true };
        const std::size_t index = static_cast<std::size_t>(scancode);
        // A repeat belongs to whoever took the press.
        const bool ui = mUiKeys.test(index) || (state.mHasDocuments && state.mTextFocus);
        mUiKeys.set(index, ui);
        return { ui, !ui };
    }

    Routing InputRouter::keyUp(int scancode)
    {
        if (!inRange(mUiKeys, scancode))
            return { false, true };
        const std::size_t index = static_cast<std::size_t>(scancode);
        const bool ui = mUiKeys.test(index);
        mUiKeys.reset(index);
        return { ui, !ui };
    }

    Routing InputRouter::text(const UiState& state)
    {
        const bool ui = state.mHasDocuments && state.mTextFocus;
        return { ui, !ui };
    }

    Routing InputRouter::controllerButtonDown(int button, const UiState& state)
    {
        if (!inRange(mUiControllerButtons, button))
            return { false, true };
        const std::size_t index = static_cast<std::size_t>(button);
        const bool ui = mUiControllerButtons.test(index) || (state.mHasDocuments && state.mNavCaptured);
        mUiControllerButtons.set(index, ui);
        return { ui, !ui };
    }

    Routing InputRouter::controllerButtonUp(int button)
    {
        if (!inRange(mUiControllerButtons, button))
            return { false, true };
        const std::size_t index = static_cast<std::size_t>(button);
        const bool ui = mUiControllerButtons.test(index);
        mUiControllerButtons.reset(index);
        return { ui, !ui };
    }

    Routing InputRouter::controllerAxis(int axis, const UiState& state)
    {
        const bool ui = axis == sRightStickY && state.mHasDocuments && state.mNavCaptured;
        return { ui, !ui };
    }

    void InputRouter::reset()
    {
        mCapturedButtons.reset();
        mUiKeys.reset();
        mUiControllerButtons.reset();
    }
}
