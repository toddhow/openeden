#ifndef OPENEDEN_COMPONENTS_UI_RML_CONTROLLERFOCUS_H
#define OPENEDEN_COMPONENTS_UI_RML_CONTROLLERFOCUS_H

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

// Task 0111 follow-up (decision 0015, UI system U2): moving the controller focus into and out of the overlay's panels
// with the configured focus entry key or button. These work on a RmlUi context and nothing else, so they are tested
// with a context that draws nothing. They run on the thread that owns the context (the GUI thread).
namespace UiRml
{
    /// The focused element when it is a control inside a document (not the document itself or the root).
    Rml::Element* focusedControl(Rml::Context& context);

    /// A visible document that asked for the controller (`nav-capture` on its `body`, which RmlUi makes the document
    /// element).
    bool wantsController(Rml::ElementDocument& document);
    bool hasNavCaptureDocument(Rml::Context& context);

    /// The focus entry: if the focus is inside a visible `nav-capture` document it is let go; otherwise the first
    /// control that can take the focus (document order, depth first, `tab-index` not none) of the first such document
    /// is focused. Returns false when there was nothing to do.
    bool toggleControllerFocus(Rml::Context& context);
}

#endif
