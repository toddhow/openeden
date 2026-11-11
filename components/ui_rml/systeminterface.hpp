#ifndef OPENEDEN_COMPONENTS_UI_RML_SYSTEMINTERFACE_H
#define OPENEDEN_COMPONENTS_UI_RML_SYSTEMINTERFACE_H

#include <chrono>

#include <RmlUi/Core/SystemInterface.h>

namespace UiRml
{
    /// RmlUi's system interface: its clock is a steady clock started when the interface is made, and its messages go
    /// to the game's log (openmw.log) with the "[UI]" prefix. Clipboard, cursor and keyboard hooks are left to the
    /// input work that comes later (not wired in task 0104).
    class SystemInterface final : public Rml::SystemInterface
    {
    public:
        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    private:
        std::chrono::steady_clock::time_point mStart = std::chrono::steady_clock::now();
    };
}

#endif
