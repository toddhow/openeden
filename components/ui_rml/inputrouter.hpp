#ifndef OPENEDEN_COMPONENTS_UI_RML_INPUTROUTER_H
#define OPENEDEN_COMPONENTS_UI_RML_INPUTROUTER_H

#include <bitset>
#include <cstdint>

// Task 0111 (U2): who gets which input event, the rules of the task spec as a pure state machine. The engine asks it
// for every event with a snapshot of what the overlay's documents look like right now; it answers with the two
// destinations and remembers what it must for the matching release. No RmlUi, SDL or game type in here.
namespace UiRml
{
    /// What the overlay publishes about its documents (the frame thread writes it, the main thread reads it).
    struct UiState
    {
        /// The overlay has at least one document.
        bool mHasDocuments = false;
        /// The cursor is over an element of a document that takes pointer events.
        bool mOverDocument = false;
        /// A text input (or textarea) has the focus.
        bool mTextFocus = false;
        /// The focused element is inside a document that asked for the controller (`nav-capture`).
        bool mNavCaptured = false;
    };

    struct Routing
    {
        bool mToUi = false;
        bool mToGame = false;
    };

    class InputRouter
    {
    public:
        /// Moves go to RmlUi whenever it has documents (hover, drags) and always to the game, which tracks the cursor.
        Routing mouseMoved(const UiState& state);

        /// A press is the overlay's only over a document; that button is then captured until released.
        Routing mouseButtonDown(int sdlButton, const UiState& state);
        /// The release of a captured button is the overlay's alone; any other release is the game's alone.
        Routing mouseButtonUp(int sdlButton);

        Routing mouseWheel(const UiState& state);

        /// Keys are the overlay's only while a text input has the focus (and it has documents). The release of a key
        /// the overlay took is the overlay's alone, the release of any other key the game's alone; repeats follow
        /// the press.
        Routing keyDown(int scancode, const UiState& state);
        Routing keyUp(int scancode);

        /// Text and IME composition: the overlay's only while a text input has the focus.
        Routing text(const UiState& state);

        /// Controller buttons and the right stick go to the overlay only while `mNavCaptured`; a release follows its
        /// press as keys do.
        Routing controllerButtonDown(int button, const UiState& state);
        Routing controllerButtonUp(int button);
        Routing controllerAxis(int axis, const UiState& state);

        /// Forgets all captured buttons and keys (focus lost, overlay switched off).
        void reset();

        static constexpr int kMaxButtons = 16;
        static constexpr int kMaxKeys = 512;
        static constexpr int kMaxControllerButtons = 32;

    private:
        std::bitset<kMaxButtons> mCapturedButtons;
        std::bitset<kMaxKeys> mUiKeys;
        std::bitset<kMaxControllerButtons> mUiControllerButtons;
    };
}

#endif
