#include "clipboard.hpp"

namespace UiRml
{
    std::string ClipboardBridge::clip(const std::string& text)
    {
        if (text.size() <= kMaxBytes)
            return text;
        std::size_t end = kMaxBytes;
        // a continuation byte is 10xxxxxx: move back to the start of the sequence that would be cut
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end;
        return text.substr(0, end);
    }

    void ClipboardBridge::setFromUi(const std::string& text)
    {
        std::string clipped = clip(text);
        std::lock_guard lock(mMutex);
        mText = clipped;
        mPending = std::move(clipped);
    }

    std::string ClipboardBridge::textForUi() const
    {
        std::lock_guard lock(mMutex);
        return mText;
    }

    void ClipboardBridge::pump()
    {
        std::optional<std::string> pending;
        {
            std::lock_guard lock(mMutex);
            pending.swap(mPending);
        }
        if (pending)
            mBackend.set(*pending);
    }

    void ClipboardBridge::refresh()
    {
        pump();
        std::string fresh = clip(mBackend.get());
        std::lock_guard lock(mMutex);
        // a copy that arrived since the pump is newer than what was read
        if (!mPending)
            mText = std::move(fresh);
    }
}
