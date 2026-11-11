#ifndef OPENEDEN_COMPONENTS_UI_RML_DEBUGPANEL_H
#define OPENEDEN_COMPONENTS_UI_RML_DEBUGPANEL_H

#include <memory>
#include <string>
#include <string_view>

namespace Rml
{
    class ElementDocument;
}

namespace UiRml
{
    /// The RML document of the test panel (task 0104, extended in task 0111): a title, a frame rate line, two buttons,
    /// a text field and a scrolling list, in the font family `fontFamily`. Sizes are in `dp` so the panel follows the
    /// GUI scaling factor. Only plain style (colours, padding, positions): no shadows, filters, gradients or rounded
    /// clipping (decision 0015: forbidden in the first themes). The `body` carries `nav-capture`: while a control in
    /// it has the focus the controller drives it (inputrouter.hpp).
    std::string debugPanelMarkup(std::string_view fontFamily);

    /// "FPS: 59.9" for `frames` frames in `seconds` seconds (0 seconds: "FPS: --").
    std::string formatFps(unsigned frames, double seconds);

    /// The panel on screen: counts frames and rewrites the frame rate line about twice a second. The line is its own
    /// element, so a change re-lays out that line only (note 0045 13.5: live values need their own documents or
    /// small elements; this panel is small enough for one document). Task 0111: it also listens to its own controls
    /// and writes what happened to the log (`[UI] panel: ...`) and into the lines under the controls, which is what
    /// the in-game input checks read.
    class DebugPanel
    {
    public:
        explicit DebugPanel(Rml::ElementDocument* document);
        ~DebugPanel();
        DebugPanel(const DebugPanel&) = delete;
        DebugPanel& operator=(const DebugPanel&) = delete;

        /// Call once per drawn frame with the RmlUi clock in seconds.
        void frame(double now);

        static constexpr double kRefreshSeconds = 0.5;

    private:
        class Listener;
        Rml::ElementDocument* mDocument;
        std::unique_ptr<Listener> mListener;
        unsigned mFrames = 0;
        double mSince = -1.0;
    };
}

#endif
