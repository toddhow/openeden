#ifndef OPENEDEN_COMPONENTS_UI_RML_IME_H
#define OPENEDEN_COMPONENTS_UI_RML_IME_H

#include <atomic>
#include <string_view>

#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>

// Task 0111 (U2): IME composition for RmlUi's text inputs (note 0045 14.5, proven there with synthetic events).
namespace UiRml
{
    /// The composition state for one text input and the sequence SDL sends: `SDL_TEXTEDITING` events carry the
    /// composition so far (an empty one ends it, either cancelled or committed), `SDL_TEXTINPUT` then carries the
    /// committed text. Character offsets throughout, as `Rml::TextInputContext`.
    class ImeComposition
    {
    public:
        /// Handles one `SDL_TEXTEDITING` event: `text` is the composition (UTF-8, may be empty), `selectionStart` and
        /// `selectionLength` the part of it the input method has selected (SDL passes -1 when it does not say).
        /// Replaces the previous composition in the input with `text`, marks it as the composition range and puts the
        /// selection where the input method says; an empty text after a composition removes it and commits (what
        /// follows as text input is inserted by the caller).
        void edit(Rml::TextInputContext& context, std::string_view text, int selectionStart, int selectionLength);

        /// True between the first non-empty composition and its end.
        bool composing() const { return mStart != mEnd; }

        /// Forgets the composition without touching any input (the input was destroyed or lost its focus).
        void reset();

    private:
        int mStart = 0;
        int mEnd = 0;
    };

    /// The handler the context is given: remembers which text input is active and keeps an `ImeComposition` for it.
    /// All calls come from the thread that runs RmlUi; `active()` is readable from any thread.
    class TextInputTracker final : public Rml::TextInputHandler
    {
    public:
        void OnActivate(Rml::TextInputContext* context) override;
        void OnDeactivate(Rml::TextInputContext* context) override;
        void OnDestroy(Rml::TextInputContext* context) override;

        /// The RmlUi thread's view of the active input, or null.
        Rml::TextInputContext* context() const { return mContext; }
        /// Safe from any thread.
        bool active() const { return mActive.load(std::memory_order_relaxed); }

        /// Passes an `SDL_TEXTEDITING` event to the active input; false when there is none.
        bool edit(std::string_view text, int selectionStart, int selectionLength);

    private:
        Rml::TextInputContext* mContext = nullptr;
        std::atomic<bool> mActive{ false };
        ImeComposition mComposition;
    };
}

#endif
