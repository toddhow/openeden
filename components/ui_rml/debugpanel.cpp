#include "debugpanel.hpp"

#include <cstdio>
#include <string>

#include <RmlUi/Core.h>

#include <components/debug/debuglog.hpp>

namespace UiRml
{
    std::string debugPanelMarkup(std::string_view fontFamily)
    {
        std::string markup = R"(<rml>
<head>
<style>
/* RmlUi has no default style sheet: block elements are declared here (the start of the component library's base sheet). */
div, body { display: block; }
body {
    position: absolute;
    left: 16dp;
    top: 16dp;
    width: 260dp;
    padding: 8dp 12dp;
    background-color: #000000b4;
    color: #dfc99a;
    font-family: )";
        markup += fontFamily;
        markup += R"(;
    font-size: 16dp;
}
#title { color: #ffffff; margin-bottom: 4dp; }
#fps { color: #a8d8a0; margin-bottom: 6dp; }
.item { padding-left: 12dp; }
.btn { tab-index: auto; nav: auto; display: block; padding: 3dp 8dp; margin: 4dp 0dp; background-color: #3a2f20; color: #dfc99a; }
.btn:hover { background-color: #5a4a30; }
.btn:focus { background-color: #7a6030; color: #ffffff; }
input.text { display: block; width: 200dp; height: 22dp; margin: 4dp 0dp; padding: 2dp 4dp; background-color: #101010; color: #ffffff; }
input.text:focus { background-color: #302818; }
.line { color: #9ec3d8; font-size: 12dp; }
#list { height: 52dp; overflow-y: scroll; margin-top: 4dp; background-color: #00000060; }
#list div { padding: 1dp 6dp; }
scrollbarvertical { width: 8dp; background-color: #00000080; }
scrollbarvertical sliderbar { width: 8dp; min-height: 12dp; background-color: #dfc99a; }
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { width: 0dp; height: 0dp; }
</style>
</head>
<body nav-capture="true">
<div id="title">OpenEden UI</div>
<div id="fps">FPS: --</div>
<div class="item">RmlUi on VSG</div>
<div class="item">Bitmap font engine</div>
<div id="btn1" class="btn">Button one</div>
<div id="btn2" class="btn">Button two</div>
<input id="field" class="text" type="text" value=""/>
<div id="clicks" class="line">clicks: 0</div>
<div id="echo" class="line">text: </div>
<div id="focus" class="line">focus: none</div>
<div id="scrollpos" class="line">scroll: 0</div>
<div id="list"><div>List 1</div><div>List 2</div><div>List 3</div><div>List 4</div><div>List 5</div><div>List 6</div><div>List 7</div><div>List 8</div><div>List 9</div><div>List 10</div><div>List 11</div><div>List 12</div></div>
</body>
</rml>)";
        return markup;
    }

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
