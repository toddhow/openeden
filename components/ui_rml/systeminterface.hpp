#ifndef OPENEDEN_COMPONENTS_UI_RML_SYSTEMINTERFACE_H
#define OPENEDEN_COMPONENTS_UI_RML_SYSTEMINTERFACE_H

#include <chrono>

#include <string>

#include <RmlUi/Core/SystemInterface.h>

#include "clipboard.hpp"

namespace UiRml
{
    /// RmlUi's system interface: its clock is a steady clock started when the interface is made, and its messages go
    /// to the game's log (openmw.log) with the "[UI]" prefix. The clipboard goes through a ClipboardBridge (task 0111
    /// follow-up); without one it is a private string. Cursor and keyboard hooks are not wired.
    class SystemInterface final : public Rml::SystemInterface
    {
    public:
        /// `clipboard` is not owned and must outlive this object; null keeps the clipboard inside the process.
        explicit SystemInterface(ClipboardBridge* clipboard = nullptr)
            : mClipboard(clipboard)
        {
        }

        void SetClipboardText(const Rml::String& text) override;
        void GetClipboardText(Rml::String& text) override;
        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    private:
        std::chrono::steady_clock::time_point mStart = std::chrono::steady_clock::now();
        ClipboardBridge* mClipboard;
        std::string mLocalClipboard;
    };
}

#endif
