#ifndef OPENEDEN_COMPONENTS_UI_RML_RMLPROTOCOL_H
#define OPENEDEN_COMPONENTS_UI_RML_RMLPROTOCOL_H

#include <functional>
#include <memory>
#include <string>

#include <components/ui/elementprotocol.hpp>

namespace Rml
{
    class Context;
}

namespace UiRml
{
    /// The element protocol (components/ui/elementprotocol.hpp) over an RmlUi context: the RmlUi implementation of
    /// decision 0015's backend (task 0162). One `createDocument` is one `Rml::ElementDocument` (the unit of layout,
    /// note 0045 13.5); elements are RmlUi elements made detached and attached later; ids map to elements and nothing
    /// else leaks out. Events RmlUi delivers (click, change, focus, blur, scroll) go to `Options::mEventSink`.
    ///
    /// Thread: RmlUi's, that is the one that records the GUI command graph. Owner: the Overlay, which creates it after
    /// the context and destroys it before `Rml::Shutdown`. The context must outlive it. A document the script did not
    /// close is closed with the protocol.
    class RmlProtocol final : public Ui::ElementProtocol
    {
    public:
        struct Options
        {
            /// The font family written into every document's default style (`body { font-family }`); empty: none.
            std::string mFontFamily;
            /// Link `ui/base.rcss` (the theme's base style sheet) into every document, after the defaults, so it wins.
            bool mLoadBaseSheet = false;
            /// A VFS document every `createDocument` loads (it links `ui/base.rcss`): a document loaded from a file can
            /// reload its style sheets when the theme changes (task 0163). Empty, or a file RmlUi cannot load: the
            /// document is made from memory with `documentDefaults` (no live theme reload).
            std::string mDocumentFile;
            /// Receives every event the elements listen to. Called from RmlUi's event dispatch.
            std::function<void(Ui::Event&&)> mEventSink;
        };

        RmlProtocol(Rml::Context& context, Options options);
        ~RmlProtocol() override;
        RmlProtocol(const RmlProtocol&) = delete;
        RmlProtocol& operator=(const RmlProtocol&) = delete;

        void create(Ui::ElementId id, std::string_view tag) override;
        void createDocument(Ui::ElementId id, std::string_view name) override;
        void destroy(Ui::ElementId id) override;
        void insertBefore(Ui::ElementId parent, Ui::ElementId child, Ui::ElementId before) override;
        void detach(Ui::ElementId child) override;
        void setText(Ui::ElementId id, std::string_view text) override;
        void setClass(Ui::ElementId id, std::string_view classes) override;
        void setProperty(Ui::ElementId id, std::string_view name, std::string_view value) override;
        void setAttribute(Ui::ElementId id, std::string_view name, std::string_view value) override;
        void listen(Ui::ElementId id, Ui::EventKind kind, bool on) override;
        void focus(Ui::ElementId id) override;

        /// Elements and documents the protocol currently knows (tests, leak checks).
        std::size_t elementCount() const;

        /// The default style every document gets (before the base sheet); public for tests.
        static std::string documentDefaults(const std::string& fontFamily);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
