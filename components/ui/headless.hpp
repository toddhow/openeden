#ifndef OPENEDEN_COMPONENTS_UI_HEADLESS_H
#define OPENEDEN_COMPONENTS_UI_HEADLESS_H

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "elementprotocol.hpp"

namespace Ui
{
    /// A backend with no UI engine behind it: it keeps the element tree and a log of every protocol call, and lets a
    /// test simulate events. Used by the unit tests of the Lua library and anything else that needs a protocol (task
    /// 0162). Not thread safe. Owner: the test.
    class HeadlessBackend final : public Backend
    {
    public:
        struct Node
        {
            std::string mTag;
            std::string mName; ///< documents only
            bool mDocument = false;
            ElementId mParent = kNoElement;
            std::vector<ElementId> mChildren;
            std::string mClass;
            std::string mText;
            std::map<std::string, std::string> mProperties;
            std::map<std::string, std::string> mAttributes;
            std::set<EventKind> mListens;
        };

        explicit HeadlessBackend(unsigned session = 1);

        void create(ElementId id, std::string_view tag) override;
        void createDocument(ElementId id, std::string_view name) override;
        void destroy(ElementId id) override;
        void insertBefore(ElementId parent, ElementId child, ElementId before) override;
        void detach(ElementId child) override;
        void setText(ElementId id, std::string_view text) override;
        void setClass(ElementId id, std::string_view classes) override;
        void setProperty(ElementId id, std::string_view name, std::string_view value) override;
        void setAttribute(ElementId id, std::string_view name, std::string_view value) override;
        void listen(ElementId id, EventKind kind, bool on) override;
        void focus(ElementId id) override;

        ElementId allocateId() override;
        void commit() override;
        void takeEvents(std::vector<Event>& out) override;

        // Inspection and simulation (tests).
        const Node* node(ElementId id) const;
        const std::map<ElementId, Node>& nodes() const { return mNodes; }
        /// Elements and documents that exist now.
        std::size_t liveElements() const;
        /// Every ElementProtocol call made so far (`allocateId`, `commit` and `takeEvents` are not protocol calls).
        std::size_t callCount() const { return mCallCount; }
        std::size_t commitCount() const { return mCommitCount; }
        /// One text line per protocol call: "create #3 div", "setText #3 hello", ... (`#n` is the id's counter).
        const std::vector<std::string>& calls() const { return mCalls; }
        void clearCalls();
        ElementId focused() const { return mFocused; }
        /// Delivers an event to `takeEvents` if the element exists and listens to the kind; otherwise it is dropped.
        /// Returns whether it was delivered.
        bool emit(ElementId target, EventKind kind, std::string value = {});

    private:
        void record(std::string line);

        unsigned mSession;
        std::uint32_t mCounter = 0;
        std::map<ElementId, Node> mNodes;
        std::vector<std::string> mCalls;
        std::size_t mCallCount = 0;
        std::size_t mCommitCount = 0;
        std::vector<Event> mEvents;
        ElementId mFocused = kNoElement;
    };
}

#endif
