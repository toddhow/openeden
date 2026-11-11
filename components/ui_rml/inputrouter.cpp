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
        return { state.mHasDocuments, true, false };
    }

    Routing InputRouter::mouseButtonDown(int button, const UiState& state)
    {
        if (!inRange(mCapturedButtons, button))
            return { false, true, false };
        const bool ui = state.mHasDocuments && state.mOverDocument;
        mCapturedButtons.set(static_cast<std::size_t>(button), ui);
        return { ui, !ui, false };
    }

    Routing InputRouter::mouseButtonUp(int button)
    {
        if (!inRange(mCapturedButtons, button))
            return { false, true, false };
        const bool ui = mCapturedButtons.test(static_cast<std::size_t>(button));
        mCapturedButtons.reset(static_cast<std::size_t>(button));
        return { ui, !ui, false };
    }

    Routing InputRouter::mouseWheel(const UiState& state)
    {
        const bool ui = state.mHasDocuments && state.mOverDocument;
        return { ui, !ui, false };
    }

    Routing InputRouter::keyDown(int scancode, const UiState& state)
    {
        if (!inRange(mUiKeys, scancode))
            return { false, true, false };
        const std::size_t index = static_cast<std::size_t>(scancode);
        if (scancode == mEntryScancode)
        {
            // A repeat of an entry press that was taken is swallowed without entering again.
            if (mSwallowedKeys.test(index))
                return { false, false, false };
            if (state.mHasDocuments && (state.mHasNavDocument || state.mNavCaptured))
            {
                mSwallowedKeys.set(index);
                return { false, false, true };
            }
        }
        // A repeat belongs to whoever took the press.
        const bool ui = mUiKeys.test(index) || (state.mHasDocuments && state.mTextFocus);
        mUiKeys.set(index, ui);
        return { ui, !ui, false };
    }

    Routing InputRouter::keyUp(int scancode)
    {
        if (!inRange(mUiKeys, scancode))
            return { false, true, false };
        const std::size_t index = static_cast<std::size_t>(scancode);
        if (mSwallowedKeys.test(index))
        {
            mSwallowedKeys.reset(index);
            return { false, false, false };
        }
        const bool ui = mUiKeys.test(index);
        mUiKeys.reset(index);
        return { ui, !ui, false };
    }

    Routing InputRouter::text(const UiState& state)
    {
        const bool ui = state.mHasDocuments && state.mTextFocus;
        return { ui, !ui, false };
    }

    Routing InputRouter::controllerButtonDown(int button, const UiState& state)
    {
        if (!inRange(mUiControllerButtons, button))
            return { false, true, false };
        const std::size_t index = static_cast<std::size_t>(button);
        if (button == mEntryButton)
        {
            if (mSwallowedControllerButtons.test(index))
                return { false, false, false };
            if (state.mHasDocuments && (state.mHasNavDocument || state.mNavCaptured))
            {
                mSwallowedControllerButtons.set(index);
                return { false, false, true };
            }
        }
        const bool ui = mUiControllerButtons.test(index) || (state.mHasDocuments && state.mNavCaptured);
        mUiControllerButtons.set(index, ui);
        return { ui, !ui, false };
    }

    Routing InputRouter::controllerButtonUp(int button)
    {
        if (!inRange(mUiControllerButtons, button))
            return { false, true, false };
        const std::size_t index = static_cast<std::size_t>(button);
        if (mSwallowedControllerButtons.test(index))
        {
            mSwallowedControllerButtons.reset(index);
            return { false, false, false };
        }
        const bool ui = mUiControllerButtons.test(index);
        mUiControllerButtons.reset(index);
        return { ui, !ui, false };
    }

    Routing InputRouter::controllerAxis(int axis, const UiState& state)
    {
        const bool ui = axis == sRightStickY && state.mHasDocuments && state.mNavCaptured;
        return { ui, !ui, false };
    }

    void InputRouter::setFocusEntry(int controllerButton, int scancode)
    {
        mEntryButton = controllerButton;
        mEntryScancode = scancode;
    }

    void InputRouter::reset()
    {
        mSwallowedKeys.reset();
        mSwallowedControllerButtons.reset();
        mCapturedButtons.reset();
        mUiKeys.reset();
        mUiControllerButtons.reset();
    }
}
