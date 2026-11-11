#include "systeminterface.hpp"

#include <components/debug/debuglog.hpp>

namespace UiRml
{
    double SystemInterface::GetElapsedTime()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - mStart).count();
    }

    void SystemInterface::SetClipboardText(const Rml::String& text)
    {
        if (mClipboard != nullptr)
            mClipboard->setFromUi(text);
        else
            mLocalClipboard = text;
    }

    void SystemInterface::GetClipboardText(Rml::String& text)
    {
        text = mClipboard != nullptr ? mClipboard->textForUi() : mLocalClipboard;
    }

    bool SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
    {
        Debug::Level level = Debug::Info;
        switch (type)
        {
            case Rml::Log::LT_ALWAYS:
            case Rml::Log::LT_INFO:
                level = Debug::Info;
                break;
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                level = Debug::Error;
                break;
            case Rml::Log::LT_WARNING:
                level = Debug::Warning;
                break;
            default:
                level = Debug::Verbose;
                break;
        }
        Log(level) << "[UI] " << message;
        return true;
    }
}
