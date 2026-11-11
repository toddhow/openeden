#include "ime.hpp"

#include <string>

#include <RmlUi/Core/StringUtilities.h>

namespace UiRml
{
    // The sequence is the one RmlUi's own SDL backend handles (Backends/RmlUi_Platform_SDL.cpp,
    // TextInputMethodEditor_SDL::HandleEdit, MIT): the composition range is [mStart, mEnd) in characters.
    void ImeComposition::edit(Rml::TextInputContext& context, std::string_view text, int selectionStart, int selectionLength)
    {
        const std::string string(text);
        const int length = static_cast<int>(Rml::StringUtilities::LengthUTF8(string));

        const bool wasComposing = composing();
        if (!wasComposing)
            context.GetSelectionRange(mStart, mEnd);

        if (wasComposing || length > 0)
            context.SetText(string, mStart, mEnd);

        mEnd = mStart + length;
        context.SetCompositionRange(mStart, mEnd);

        if (length > 0 && selectionStart >= 0 && selectionLength >= 0)
            context.SetSelectionRange(mStart + selectionStart, mStart + selectionStart + selectionLength);
        else if (wasComposing)
            context.SetCursorPosition(mEnd);

        // Committing: SDL sends an empty composition and then the committed text as text input.
        if (wasComposing && length == 0)
            context.CommitComposition(Rml::StringView());
    }

    void ImeComposition::reset()
    {
        mStart = 0;
        mEnd = 0;
    }

    void TextInputTracker::OnActivate(Rml::TextInputContext* context)
    {
        if (mContext != context)
            mComposition.reset();
        mContext = context;
        mActive.store(true, std::memory_order_relaxed);
    }

    void TextInputTracker::OnDeactivate(Rml::TextInputContext* context)
    {
        if (mContext != context)
            return;
        mContext = nullptr;
        mComposition.reset();
        mActive.store(false, std::memory_order_relaxed);
    }

    void TextInputTracker::OnDestroy(Rml::TextInputContext* context)
    {
        OnDeactivate(context);
    }

    bool TextInputTracker::edit(std::string_view text, int selectionStart, int selectionLength)
    {
        if (mContext == nullptr)
            return false;
        mComposition.edit(*mContext, text, selectionStart, selectionLength);
        return true;
    }
}
