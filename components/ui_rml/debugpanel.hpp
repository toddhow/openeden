#ifndef OPENEDEN_COMPONENTS_UI_RML_DEBUGPANEL_H
#define OPENEDEN_COMPONENTS_UI_RML_DEBUGPANEL_H

#include <string>
#include <string_view>

namespace Rml
{
    class ElementDocument;
}

namespace UiRml
{
    /// The RML document of the test panel (task 0104): a title, a frame rate line and a short list, in the font family
    /// `fontFamily`. Only plain style (colours, padding, positions): no shadows, filters, gradients or rounded clipping
    /// (decision 0015: forbidden in the first themes).
    std::string debugPanelMarkup(std::string_view fontFamily);

    /// "FPS: 59.9" for `frames` frames in `seconds` seconds (0 seconds: "FPS: --").
    std::string formatFps(unsigned frames, double seconds);

    /// The panel on screen: counts frames and rewrites the frame rate line about twice a second. The line is its own
    /// element, so a change re-lays out that line only (note 0045 13.5: live values need their own documents or
    /// small elements; this panel is small enough for one document).
    class DebugPanel
    {
    public:
        explicit DebugPanel(Rml::ElementDocument* document);

        /// Call once per drawn frame with the RmlUi clock in seconds.
        void frame(double now);

        static constexpr double kRefreshSeconds = 0.5;

    private:
        Rml::ElementDocument* mDocument;
        unsigned mFrames = 0;
        double mSince = -1.0;
    };
}

#endif
