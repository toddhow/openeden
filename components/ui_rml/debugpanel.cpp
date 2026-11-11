#include "debugpanel.hpp"

#include <cstdio>

#include <RmlUi/Core.h>

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
    left: 16px;
    top: 16px;
    width: 260px;
    padding: 8px 12px;
    background-color: #000000b4;
    color: #dfc99a;
    font-family: )";
        markup += fontFamily;
        markup += R"(;
    font-size: 16px;
}
#title { color: #ffffff; margin-bottom: 4px; }
#fps { color: #a8d8a0; margin-bottom: 6px; }
.item { padding-left: 12px; }
</style>
</head>
<body>
<div id="title">OpenEden UI</div>
<div id="fps">FPS: --</div>
<div class="item">RmlUi on VSG</div>
<div class="item">Bitmap font engine</div>
<div class="item">No input yet</div>
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

    DebugPanel::DebugPanel(Rml::ElementDocument* document)
        : mDocument(document)
    {
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
