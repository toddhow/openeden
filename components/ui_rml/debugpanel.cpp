#include "debugpanel.hpp"

#include <cstdio>
#include <string>

#include <RmlUi/Core.h>

#include <components/debug/debuglog.hpp>

namespace UiRml
{
    std::string formatFps(unsigned frames, double seconds)
    {
        if (!(seconds > 0.0))
            return "FPS: --";
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "FPS: %.1f", frames / seconds);
        return buffer;
    }

    class DebugPanel::Listener final : public Rml::EventListener
    {
    public:
        explicit Listener(Rml::ElementDocument* document)
            : mDocument(document)
        {
        }

        void ProcessEvent(Rml::Event& event) override
        {
            Rml::Element* target = event.GetTargetElement();
            if (target == nullptr)
                return;
            const Rml::String id = target->GetId();
            if (event == "click" && target->IsClassSet("btn"))
            {
                ++mClicks;
                setLine("clicks", "clicks: " + std::to_string(mClicks) + " (" + id + ")");
                Log(Debug::Info) << "[UI] panel: click " << id << " (count " << mClicks << ")";
            }
            else if (event == "change" && id == "field")
            {
                const Rml::String value = target->GetAttribute<Rml::String>("value", "");
                setLine("echo", "text: " + value);
                Log(Debug::Info) << "[UI] panel: field = \"" << value << "\"";
            }
            else if (event == "focus")
            {
                setLine("focus", "focus: " + (id.empty() ? Rml::String("none") : id));
                Log(Debug::Info) << "[UI] panel: focus " << (id.empty() ? "none" : id);
            }
            else if (event == "blur")
            {
                // A focus that moves sends this and then the focus event of the next control, which wins.
                setLine("focus", "focus: none");
                Log(Debug::Info) << "[UI] panel: blur " << (id.empty() ? "none" : id);
            }
            else if (event == "scroll" && id == "list")
            {
                const int top = static_cast<int>(target->GetScrollTop());
                if (top != mLastScroll)
                {
                    mLastScroll = top;
                    setLine("scrollpos", "scroll: " + std::to_string(top));
                    // RmlUi scrolls smoothly: one event for every frame of the animation, so not Info.
                    Log(Debug::Verbose) << "[UI] panel: list scroll " << top;
                }
            }
        }

    private:
        void setLine(const char* id, const std::string& text)
        {
            if (Rml::Element* line = mDocument->GetElementById(id))
                line->SetInnerRML(text);
        }

        Rml::ElementDocument* mDocument;
        int mClicks = 0;
        int mLastScroll = 0;
    };

    DebugPanel::DebugPanel(Rml::ElementDocument* document)
        : mDocument(document)
        , mListener(std::make_unique<Listener>(document))
    {
        if (mDocument == nullptr)
            return;
        mDocument->AddEventListener("click", mListener.get());
        mDocument->AddEventListener("change", mListener.get());
        mDocument->AddEventListener("focus", mListener.get(), true);
        mDocument->AddEventListener("blur", mListener.get(), true);
        if (Rml::Element* list = mDocument->GetElementById("list"))
            list->AddEventListener("scroll", mListener.get());
    }

    DebugPanel::~DebugPanel()
    {
        // The overlay destroys the panel before RmlUi shuts down, so the document is still alive here.
        if (mDocument == nullptr)
            return;
        mDocument->RemoveEventListener("click", mListener.get());
        mDocument->RemoveEventListener("change", mListener.get());
        mDocument->RemoveEventListener("focus", mListener.get(), true);
        mDocument->RemoveEventListener("blur", mListener.get(), true);
        if (Rml::Element* list = mDocument->GetElementById("list"))
            list->RemoveEventListener("scroll", mListener.get());
    }

    void DebugPanel::frame(double now)
    {
        if (mSince < 0.0)
            mSince = now;
        ++mFrames;
        const double elapsed = now - mSince;
        if (elapsed < kRefreshSeconds || mDocument == nullptr)
            return;
        if (Rml::Element* line = mDocument->GetElementById("fps"))
            line->SetInnerRML(formatFps(mFrames, elapsed));
        mFrames = 0;
        mSince = now;
    }
}
