#ifndef OPENEDEN_COMPONENTS_UI_ELEMENTPROTOCOL_H
#define OPENEDEN_COMPONENTS_UI_ELEMENTPROTOCOL_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The element protocol of OpenEden's UI system (decision 0015 point 3, research note 0045, task 0162): the small,
// engine-independent interface the Lua component library (`openeden.ui`) talks to. It names no UI engine, no Lua type
// and no renderer: RmlUi is one implementation (components/ui_rml/rmlprotocol.hpp), the headless one used by tests is
// another (headless.hpp), and a custom engine could be a third.
namespace Ui
{
    /// A handle, not a pointer. 0 means "no element". The top 8 bits name the session that allocated it (one per script
    /// that opened the backend), the low 24 bits count; a session therefore never sees another session's events.
    using ElementId = std::uint32_t;
    inline constexpr ElementId kNoElement = 0;

    inline constexpr ElementId makeElementId(unsigned session, std::uint32_t counter)
    {
        return (static_cast<ElementId>(session & 0xffu) << 24) | (counter & 0xffffffu);
    }
    inline constexpr unsigned sessionOf(ElementId id)
    {
        return static_cast<unsigned>(id >> 24);
    }

    enum class EventKind : std::uint8_t
    {
        Click,
        Change, ///< a text field's value changed; Event::mValue is the new text
        Focus,
        Blur,
        Scroll, ///< a scrollable element scrolled; Event::mValue is its scrollTop in dp
    };

    std::string_view eventKindName(EventKind kind);
    std::optional<EventKind> eventKindFromName(std::string_view name);

    struct Event
    {
        ElementId mTarget = kNoElement;
        EventKind mKind = EventKind::Click;
        std::string mValue;
    };

    /// What a UI engine has to offer (write side). All calls come from one thread (the one that owns the protocol
    /// object); an id the implementation does not know is ignored, never an error, so a script that destroys a parent
    /// and then its child needs no bookkeeping. Property and attribute values are text (RCSS syntax for properties,
    /// `dp` units allowed).
    class ElementProtocol
    {
    public:
        virtual ~ElementProtocol() = default;

        /// A new detached element of the given tag ("div", "span", "input", "img", ...).
        virtual void create(ElementId id, std::string_view tag) = 0;
        /// A new document (the unit of layout: note 0045 13.5). It is shown at once and is the parent of its elements.
        virtual void createDocument(ElementId id, std::string_view name) = 0;
        /// Removes the element and all its descendants. A document id closes the document.
        virtual void destroy(ElementId id) = 0;
        /// Attaches `child` to `parent` before `before` (0: at the end); a child already attached is moved.
        virtual void insertBefore(ElementId parent, ElementId child, ElementId before) = 0;
        /// Takes `child` off its parent without destroying it.
        virtual void detach(ElementId child) = 0;
        virtual void setText(ElementId id, std::string_view text) = 0;
        virtual void setClass(ElementId id, std::string_view classes) = 0;
        virtual void setProperty(ElementId id, std::string_view name, std::string_view value) = 0;
        virtual void setAttribute(ElementId id, std::string_view name, std::string_view value) = 0;
        virtual void listen(ElementId id, EventKind kind, bool on) = 0;
        virtual void focus(ElementId id) = 0;
    };

    /// The write side plus the three things a session needs: ids, batching and events.
    class Backend : public ElementProtocol
    {
    public:
        /// A fresh id of this session.
        virtual ElementId allocateId() = 0;
        /// Makes everything written since the last commit visible to the consumer, as one batch. A commit with nothing
        /// written costs no allocation.
        virtual void commit() = 0;
        /// Appends the events of this session that have arrived since the last call to `out`.
        virtual void takeEvents(std::vector<Event>& out) = 0;
    };
}

#endif
