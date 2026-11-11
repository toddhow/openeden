#ifndef OPENEDEN_COMPONENTS_UI_RML_CLIPBOARD_H
#define OPENEDEN_COMPONENTS_UI_RML_CLIPBOARD_H

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

// Task 0111 follow-up (decision 0015, UI system U2): the clipboard of the RmlUi overlay.
//
// RmlUi asks for and sets the clipboard on the thread that records the GUI; SDL says the clipboard calls belong to the
// main thread. The bridge keeps the text in between and does the SDL calls only from the main thread's two entry
// points. No SDL or RmlUi type here, so the rules are tested with a fake backend.
namespace UiRml
{
    /// The system clipboard. Called from the main thread only (ClipboardBridge::refresh and pump).
    class ClipboardBackend
    {
    public:
        virtual ~ClipboardBackend() = default;
        virtual std::string get() = 0;
        virtual void set(const std::string& text) = 0;
    };

    /// Owner: the overlay. Threads: setFromUi / textForUi from the GUI thread, refresh / pump from the main thread;
    /// all four are safe to call at the same time.
    class ClipboardBridge
    {
    public:
        /// Longer texts are cut (at a character boundary of UTF-8) so a huge clipboard cannot be copied around.
        static constexpr std::size_t kMaxBytes = 1u << 20;

        explicit ClipboardBridge(ClipboardBackend& backend)
            : mBackend(backend)
        {
        }

        /// GUI thread: RmlUi's SetClipboardText. The text is visible to textForUi at once and written to the system
        /// clipboard by the next pump.
        void setFromUi(const std::string& text);
        /// GUI thread: RmlUi's GetClipboardText. The text as of the last refresh, or the UI's own last copy.
        std::string textForUi() const;

        /// Main thread: reads the system clipboard (after writing any copy that is still waiting, so a paste right
        /// after a copy gets the copy). Called when the key that pastes is about to be queued for the GUI thread.
        void refresh();
        /// Main thread, once a frame: writes the copy that is waiting.
        void pump();

        /// The longest prefix of `text` that is at most kMaxBytes and does not end inside a UTF-8 sequence.
        static std::string clip(const std::string& text);

    private:
        ClipboardBackend& mBackend;
        mutable std::mutex mMutex;
        std::string mText;
        std::optional<std::string> mPending;
    };
}

#endif
