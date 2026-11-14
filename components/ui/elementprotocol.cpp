#include "elementprotocol.hpp"

namespace Ui
{
    std::string_view eventKindName(EventKind kind)
    {
        switch (kind)
        {
            case EventKind::Click:
                return "click";
            case EventKind::Change:
                return "change";
            case EventKind::Focus:
                return "focus";
            case EventKind::Blur:
                return "blur";
            case EventKind::Scroll:
                return "scroll";
        }
        return "";
    }

    std::optional<EventKind> eventKindFromName(std::string_view name)
    {
        for (EventKind kind : { EventKind::Click, EventKind::Change, EventKind::Focus, EventKind::Blur,
                 EventKind::Scroll })
            if (eventKindName(kind) == name)
                return kind;
        return std::nullopt;
    }
}
