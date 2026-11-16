#ifndef OPENMW_MWGUI_MENUREPLACEMENT_H
#define OPENMW_MWGUI_MENUREPLACEMENT_H

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

// Task 0207 (Skyrim SE menus): what a Lua menu script (`openmw.menu`'s underscore functions) tells the engine's own
// menu and loading screen.
//
//  - A menu script that draws the main menu and the pause menu itself (a game pack's, on RmlUi) sets the replacement
//    switch; the engine's MyGUI main menu then stays invisible while its GUI mode (the pause or the start screen) is
//    still on the stack, so Escape, saving, loading and quitting work as they always did.
//  - The loading screen shows the tip the script handed over last (text only; Lua does not run during a load, so the
//    script hands over the next tip ahead of time).
//
// Both are process-wide and thread safe (the script sets them on the main thread, the loading screen reads them there
// too, but a loader thread may ask). With nothing set (every Morrowind session) nothing changes.

namespace MWGui::MenuReplacement
{
    inline constexpr std::size_t kMaxTipBytes = 1024;

    namespace detail
    {
        struct State
        {
            std::mutex mMutex;
            bool mReplaced = false;
            std::string mTip;
            std::function<void()> mListener;
        };

        inline State& state()
        {
            static State s;
            return s;
        }
    }

    [[nodiscard]] inline bool mainMenuReplaced()
    {
        detail::State& s = detail::state();
        const std::lock_guard lock(s.mMutex);
        return s.mReplaced;
    }

    /// Sets the switch; the listener (the main menu window) is called, outside the lock, when the value changed.
    inline void setMainMenuReplaced(bool replaced)
    {
        detail::State& s = detail::state();
        std::function<void()> listener;
        {
            const std::lock_guard lock(s.mMutex);
            if (s.mReplaced == replaced)
                return;
            s.mReplaced = replaced;
            listener = s.mListener;
        }
        if (listener)
            listener();
    }

    /// The main menu window registers here (and withdraws with an empty function when it goes).
    inline void setChangeListener(std::function<void()> listener)
    {
        detail::State& s = detail::state();
        const std::lock_guard lock(s.mMutex);
        s.mListener = std::move(listener);
    }

    /// Is the main menu window shown, given that the engine wants it shown (`requested`)?
    [[nodiscard]] inline bool mainMenuVisibleFor(bool requested, bool replaced)
    {
        return requested && !replaced;
    }

    [[nodiscard]] inline std::string loadingTip()
    {
        detail::State& s = detail::state();
        const std::lock_guard lock(s.mMutex);
        return s.mTip;
    }

    /// Keeps a tip: control characters other than a newline are dropped, the text is cut to kMaxTipBytes at a
    /// character boundary (it is UTF-8).
    inline void setLoadingTip(std::string_view text)
    {
        std::string clean;
        clean.reserve(text.size());
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 && c != '\n')
                continue;
            if (u == 0x7F)
                continue;
            clean.push_back(c);
        }
        if (clean.size() > kMaxTipBytes)
        {
            std::size_t cut = kMaxTipBytes;
            while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80)
                --cut;
            clean.resize(cut);
        }
        detail::State& s = detail::state();
        const std::lock_guard lock(s.mMutex);
        s.mTip = std::move(clean);
    }

    /// The label of the loading screen. A neutral session has none of Morrowind's strings, so the message the engine
    /// asks for ("#{sLoadingMessage14}") reaches the screen as its bare key: while a tip shows, such a key becomes
    /// "Loading...". Every other label is kept.
    [[nodiscard]] inline std::string loadingLabel(std::string_view label, bool hasTip)
    {
        // the engine asks for "#{sLoadingMessage14}"; with the string missing the screen shows "LoadingMessage14"
        constexpr std::string_view kKeyPrefix = "LoadingMessage";
        constexpr std::string_view kTokenPrefix = "#{sLoadingMessage";
        constexpr std::string_view kEngineToken = "#{OMWEngine:LoadingInProgress}"; // maps to the missing message
        const bool isKey = label.substr(0, kKeyPrefix.size()) == kKeyPrefix
            || label.substr(0, kTokenPrefix.size()) == kTokenPrefix || label == kEngineToken;
        if (hasTip && isKey)
            return "Loading...";
        return std::string(label);
    }

    /// For tests: everything as at the start.
    inline void reset()
    {
        detail::State& s = detail::state();
        const std::lock_guard lock(s.mMutex);
        s.mReplaced = false;
        s.mTip.clear();
        s.mListener = nullptr;
    }
}

#endif
