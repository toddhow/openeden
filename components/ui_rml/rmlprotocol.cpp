#include "rmlprotocol.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StringUtilities.h>

#include <components/debug/debuglog.hpp>

namespace UiRml
{
    namespace
    {
        const char* rmlEventName(Ui::EventKind kind)
        {
            switch (kind)
            {
                case Ui::EventKind::Click:
                    return "click";
                case Ui::EventKind::Change:
                    return "change";
                case Ui::EventKind::Focus:
                    return "focus";
                case Ui::EventKind::Blur:
                    return "blur";
                case Ui::EventKind::Scroll:
                    return "scroll";
            }
            return "";
        }

        std::optional<Ui::EventKind> kindFromRml(const Rml::String& name)
        {
            for (Ui::EventKind kind : { Ui::EventKind::Click, Ui::EventKind::Change, Ui::EventKind::Focus,
                     Ui::EventKind::Blur, Ui::EventKind::Scroll })
                if (name == rmlEventName(kind))
                    return kind;
            return std::nullopt;
        }

        // The Rml string of a view.
        Rml::String str(std::string_view s)
        {
            return Rml::String(s.data(), s.size());
        }
    }

    std::string RmlProtocol::documentDefaults(const std::string& fontFamily)
    {
        // RmlUi has no default style sheet: block elements are declared here, and a plain font and colour so that a
        // document with no theme still reads. A theme's base sheet is linked after this and wins.
        std::string css = "div, body { display: block; }\n";
        css += "body { color: #ffffff; font-size: 16dp;";
        if (!fontFamily.empty())
            css += " font-family: " + fontFamily + ";";
        css += " }\n";
        css += "input { display: inline-block; }\nimg { display: inline-block; }\n";
        return css;
    }

    struct RmlProtocol::Impl final : public Rml::EventListener
    {
        struct Entry
        {
            Rml::Element* mElement = nullptr;
            Rml::ElementPtr mDetached; ///< owns the element while it has no parent
            Rml::ElementDocument* mDocument = nullptr; ///< set for documents
            Ui::ElementId mParent = Ui::kNoElement;
            std::vector<Ui::ElementId> mChildren;
            std::set<Ui::EventKind> mListens;
        };

        Rml::Context& mContext;
        Options mOptions;
        std::unordered_map<Ui::ElementId, Entry> mEntries;
        std::unordered_map<Rml::Element*, Ui::ElementId> mByElement;
        std::set<std::string> mWarned;

        Impl(Rml::Context& context, Options options)
            : mContext(context)
            , mOptions(std::move(options))
        {
        }

        ~Impl() override
        {
            // Close what is still open; the elements of documents are released by the context, a frame later, so they
            // must not still point at this listener.
            for (auto& [id, entry] : mEntries)
                removeListeners(entry);
            std::vector<Rml::ElementDocument*> documents;
            for (auto& [id, entry] : mEntries)
                if (entry.mDocument != nullptr)
                    documents.push_back(entry.mDocument);
            for (Rml::ElementDocument* document : documents)
                document->Close();
            mEntries.clear(); // detached elements are freed here
        }

        Entry* find(Ui::ElementId id)
        {
            auto it = mEntries.find(id);
            return it == mEntries.end() ? nullptr : &it->second;
        }

        void warnOnce(const std::string& key, const std::string& message)
        {
            if (mWarned.insert(key).second)
                Log(Debug::Warning) << "[UI] " << message;
        }

        void ProcessEvent(Rml::Event& event) override
        {
            if (!mOptions.mEventSink)
                return;
            Rml::Element* current = event.GetCurrentElement();
            auto byElement = mByElement.find(current);
            if (byElement == mByElement.end())
                return;
            const auto kind = kindFromRml(event.GetType());
            if (!kind)
                return;
            Ui::Event out;
            out.mTarget = byElement->second;
            out.mKind = *kind;
            if (*kind == Ui::EventKind::Change)
                out.mValue = event.GetParameter<Rml::String>("value", "");
            else if (*kind == Ui::EventKind::Scroll)
            {
                const float ratio = mContext.GetDensityIndependentPixelRatio();
                const float dp = ratio > 0.f ? current->GetScrollTop() / ratio : current->GetScrollTop();
                out.mValue = std::to_string(dp);
            }
            mOptions.mEventSink(std::move(out));
        }

        void removeListeners(Entry& entry)
        {
            for (Ui::EventKind kind : entry.mListens)
                entry.mElement->RemoveEventListener(rmlEventName(kind), this);
            entry.mListens.clear();
        }

        void forget(Ui::ElementId id)
        {
            std::vector<Ui::ElementId> stack{ id };
            while (!stack.empty())
            {
                const Ui::ElementId current = stack.back();
                stack.pop_back();
                auto it = mEntries.find(current);
                if (it == mEntries.end())
                    continue;
                for (Ui::ElementId child : it->second.mChildren)
                    stack.push_back(child);
                removeListeners(it->second);
                mByElement.erase(it->second.mElement);
                mEntries.erase(it);
            }
        }

        void takeOffParent(Entry& child, Ui::ElementId childId)
        {
            if (child.mParent == Ui::kNoElement)
                return;
            if (Entry* parent = find(child.mParent))
            {
                auto& siblings = parent->mChildren;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), childId), siblings.end());
                if (parent->mElement != nullptr && child.mElement->GetParentNode() == parent->mElement)
                    child.mDetached = parent->mElement->RemoveChild(child.mElement);
            }
            child.mParent = Ui::kNoElement;
        }
    };

    RmlProtocol::RmlProtocol(Rml::Context& context, Options options)
        : mImpl(std::make_unique<Impl>(context, std::move(options)))
    {
    }

    RmlProtocol::~RmlProtocol() = default;

    std::size_t RmlProtocol::elementCount() const
    {
        return mImpl->mEntries.size();
    }

    void RmlProtocol::create(Ui::ElementId id, std::string_view tag)
    {
        if (id == Ui::kNoElement || mImpl->find(id) != nullptr)
            return;
        const Rml::String name = str(tag);
        Rml::ElementPtr element = Rml::Factory::InstanceElement(nullptr, name, name, Rml::XMLAttributes());
        if (!element)
        {
            mImpl->warnOnce("create:" + name, "RmlUi could not make an element of tag '" + name + "'");
            return;
        }
        Impl::Entry entry;
        entry.mElement = element.get();
        entry.mDetached = std::move(element);
        mImpl->mByElement[entry.mElement] = id;
        mImpl->mEntries.emplace(id, std::move(entry));
    }

    void RmlProtocol::createDocument(Ui::ElementId id, std::string_view name)
    {
        if (id == Ui::kNoElement || mImpl->find(id) != nullptr)
            return;
        Rml::ElementDocument* document = nullptr;
        if (!mImpl->mOptions.mDocumentFile.empty())
        {
            document = mImpl->mContext.LoadDocument(mImpl->mOptions.mDocumentFile);
            if (document != nullptr)
                document->SetTitle(str(name));
            else
                mImpl->warnOnce("documentfile",
                    "RmlUi could not load " + mImpl->mOptions.mDocumentFile
                        + ": scripts' documents are made from memory (no theme reload)");
        }
        if (document == nullptr)
        {
            std::string markup = "<rml><head><title>" + Rml::StringUtilities::EncodeRml(str(name)) + "</title><style>"
                + documentDefaults(mImpl->mOptions.mFontFamily) + "</style>";
            if (mImpl->mOptions.mLoadBaseSheet)
                markup += "<link type=\"text/rcss\" href=\"ui/base.rcss\"/>";
            markup += "</head><body></body></rml>";
            document = mImpl->mContext.LoadDocumentFromMemory(markup);
        }
        if (document == nullptr)
        {
            mImpl->warnOnce(
                "document:" + std::string(name), "RmlUi could not load the document '" + std::string(name) + "'");
            return;
        }
        document->Show();
        Impl::Entry entry;
        entry.mElement = document;
        entry.mDocument = document;
        mImpl->mByElement[document] = id;
        mImpl->mEntries.emplace(id, std::move(entry));
    }

    void RmlProtocol::destroy(Ui::ElementId id)
    {
        Impl::Entry* entry = mImpl->find(id);
        if (entry == nullptr)
            return;
        if (entry->mDocument != nullptr)
        {
            Rml::ElementDocument* document = entry->mDocument;
            mImpl->forget(id);
            document->Close();
            return;
        }
        // Take it off its parent (the parent element releases it, we drop the pointer), then forget the subtree.
        mImpl->takeOffParent(*entry, id);
        // `mDetached` now owns the element: it is freed when the entry is erased, with everything below it.
        mImpl->forget(id);
    }

    void RmlProtocol::insertBefore(Ui::ElementId parentId, Ui::ElementId childId, Ui::ElementId beforeId)
    {
        Impl::Entry* parent = mImpl->find(parentId);
        Impl::Entry* child = mImpl->find(childId);
        if (parent == nullptr || child == nullptr || parentId == childId || child->mDocument != nullptr)
            return;
        // Never make an element its own ancestor.
        for (Ui::ElementId up = parentId; up != Ui::kNoElement;)
        {
            if (up == childId)
                return;
            Impl::Entry* ancestor = mImpl->find(up);
            up = ancestor ? ancestor->mParent : Ui::kNoElement;
        }
        mImpl->takeOffParent(*child, childId);
        if (!child->mDetached)
            return;
        Rml::Element* beforeElement = nullptr;
        if (beforeId != Ui::kNoElement)
        {
            Impl::Entry* before = mImpl->find(beforeId);
            if (before != nullptr && before->mParent == parentId)
                beforeElement = before->mElement;
        }
        if (beforeElement != nullptr)
            parent->mElement->InsertBefore(std::move(child->mDetached), beforeElement);
        else
            parent->mElement->AppendChild(std::move(child->mDetached));
        child->mParent = parentId;
        auto& siblings = parent->mChildren;
        auto position = siblings.end();
        if (beforeElement != nullptr)
            position = std::find(siblings.begin(), siblings.end(), beforeId);
        siblings.insert(position, childId);
    }

    void RmlProtocol::detach(Ui::ElementId childId)
    {
        Impl::Entry* child = mImpl->find(childId);
        if (child == nullptr || child->mDocument != nullptr)
            return;
        mImpl->takeOffParent(*child, childId);
    }

    void RmlProtocol::setText(Ui::ElementId id, std::string_view text)
    {
        Impl::Entry* entry = mImpl->find(id);
        if (entry == nullptr)
            return;
        // An element that already holds one text node changes that node (no new element, no new layout of the
        // markup); the first text is made as markup, encoded so that it stays text.
        Rml::Element* element = entry->mElement;
        if (element->GetNumChildren(true) == 1)
            if (auto* node = rmlui_dynamic_cast<Rml::ElementText*>(element->GetChild(0)))
            {
                node->SetText(str(text));
                return;
            }
        element->SetInnerRML(Rml::StringUtilities::EncodeRml(str(text)));
    }

    void RmlProtocol::setClass(Ui::ElementId id, std::string_view classes)
    {
        if (Impl::Entry* entry = mImpl->find(id))
            entry->mElement->SetClassNames(str(classes));
    }

    void RmlProtocol::setProperty(Ui::ElementId id, std::string_view name, std::string_view value)
    {
        Impl::Entry* entry = mImpl->find(id);
        if (entry == nullptr)
            return;
        if (!entry->mElement->SetProperty(str(name), str(value)))
            mImpl->warnOnce("property:" + std::string(name) + "=" + std::string(value),
                "RmlUi did not accept the property " + std::string(name) + ": " + std::string(value));
    }

    void RmlProtocol::setAttribute(Ui::ElementId id, std::string_view name, std::string_view value)
    {
        if (Impl::Entry* entry = mImpl->find(id))
            entry->mElement->SetAttribute(str(name), str(value));
    }

    void RmlProtocol::listen(Ui::ElementId id, Ui::EventKind kind, bool on)
    {
        Impl::Entry* entry = mImpl->find(id);
        if (entry == nullptr)
            return;
        if (on)
        {
            if (entry->mListens.insert(kind).second)
                entry->mElement->AddEventListener(rmlEventName(kind), mImpl.get());
        }
        else if (entry->mListens.erase(kind) != 0)
            entry->mElement->RemoveEventListener(rmlEventName(kind), mImpl.get());
    }

    void RmlProtocol::focus(Ui::ElementId id)
    {
        if (Impl::Entry* entry = mImpl->find(id))
            entry->mElement->Focus();
    }
}
