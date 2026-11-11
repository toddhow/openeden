#include "controllerfocus.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/ElementDocument.h>

namespace UiRml
{
    namespace
    {
        // Depth first, in document order: the first element that can take the focus.
        Rml::Element* focusFirstFocusable(Rml::Element& parent)
        {
            for (int i = 0; i < parent.GetNumChildren(); ++i)
            {
                Rml::Element* child = parent.GetChild(i);
                if (child->IsVisible() && child->GetComputedValues().tab_index() != Rml::Style::TabIndex::None
                    && child->Focus())
                    return child;
                if (Rml::Element* found = focusFirstFocusable(*child))
                    return found;
            }
            return nullptr;
        }
    }

    Rml::Element* focusedControl(Rml::Context& context)
    {
        Rml::Element* focus = context.GetFocusElement();
        if (focus == nullptr || focus == context.GetRootElement())
            return nullptr;
        if (focus->GetOwnerDocument() == focus)
            return nullptr;
        return focus;
    }

    bool wantsController(Rml::ElementDocument& document)
    {
        return document.HasAttribute("nav-capture") && document.IsVisible();
    }

    bool hasNavCaptureDocument(Rml::Context& context)
    {
        for (int i = 0; i < context.GetNumDocuments(); ++i)
        {
            Rml::ElementDocument* document = context.GetDocument(i);
            if (document != nullptr && wantsController(*document))
                return true;
        }
        return false;
    }

    bool toggleControllerFocus(Rml::Context& context)
    {
        if (Rml::Element* focus = focusedControl(context))
        {
            Rml::ElementDocument* document = focus->GetOwnerDocument();
            if (document != nullptr && wantsController(*document))
            {
                focus->Blur();
                return true;
            }
        }
        for (int i = 0; i < context.GetNumDocuments(); ++i)
        {
            Rml::ElementDocument* document = context.GetDocument(i);
            // focusFirstFocusable moves the focus when it finds a control
            if (document != nullptr && wantsController(*document) && focusFirstFocusable(*document) != nullptr)
                return true;
        }
        return false;
    }
}
