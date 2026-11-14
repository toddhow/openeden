#include "headless.hpp"

#include <algorithm>

namespace Ui
{
    namespace
    {
        std::string label(ElementId id)
        {
            return "#" + std::to_string(id & 0xffffffu);
        }
    }

    HeadlessBackend::HeadlessBackend(unsigned session)
        : mSession(session)
    {
    }

    void HeadlessBackend::record(std::string line)
    {
        ++mCallCount;
        mCalls.push_back(std::move(line));
    }

    void HeadlessBackend::create(ElementId id, std::string_view tag)
    {
        record("create " + label(id) + " " + std::string(tag));
        Node node;
        node.mTag = std::string(tag);
        mNodes[id] = std::move(node);
    }

    void HeadlessBackend::createDocument(ElementId id, std::string_view name)
    {
        record("createDocument " + label(id) + " " + std::string(name));
        Node node;
        node.mTag = "body";
        node.mName = std::string(name);
        node.mDocument = true;
        mNodes[id] = std::move(node);
    }

    void HeadlessBackend::destroy(ElementId id)
    {
        record("destroy " + label(id));
        auto it = mNodes.find(id);
        if (it == mNodes.end())
            return;
        // Off the parent first, then the subtree.
        if (auto parent = mNodes.find(it->second.mParent); parent != mNodes.end())
        {
            auto& siblings = parent->second.mChildren;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        }
        std::vector<ElementId> stack{ id };
        while (!stack.empty())
        {
            const ElementId current = stack.back();
            stack.pop_back();
            auto node = mNodes.find(current);
            if (node == mNodes.end())
                continue;
            for (ElementId child : node->second.mChildren)
                stack.push_back(child);
            if (mFocused == current)
                mFocused = kNoElement;
            mNodes.erase(node);
        }
    }

    void HeadlessBackend::insertBefore(ElementId parent, ElementId child, ElementId before)
    {
        record("insertBefore " + label(parent) + " " + label(child) + " " + label(before));
        auto p = mNodes.find(parent);
        auto c = mNodes.find(child);
        if (p == mNodes.end() || c == mNodes.end() || parent == child)
            return;
        // Moving: take it off its old parent.
        if (auto old = mNodes.find(c->second.mParent); old != mNodes.end())
        {
            auto& siblings = old->second.mChildren;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
        }
        auto& children = p->second.mChildren;
        auto position = children.end();
        if (before != kNoElement)
            position = std::find(children.begin(), children.end(), before);
        children.insert(position, child);
        c->second.mParent = parent;
    }

    void HeadlessBackend::detach(ElementId child)
    {
        record("detach " + label(child));
        auto c = mNodes.find(child);
        if (c == mNodes.end())
            return;
        if (auto old = mNodes.find(c->second.mParent); old != mNodes.end())
        {
            auto& siblings = old->second.mChildren;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
        }
        c->second.mParent = kNoElement;
    }

    void HeadlessBackend::setText(ElementId id, std::string_view text)
    {
        record("setText " + label(id) + " " + std::string(text));
        if (auto it = mNodes.find(id); it != mNodes.end())
            it->second.mText = std::string(text);
    }

    void HeadlessBackend::setClass(ElementId id, std::string_view classes)
    {
        record("setClass " + label(id) + " " + std::string(classes));
        if (auto it = mNodes.find(id); it != mNodes.end())
            it->second.mClass = std::string(classes);
    }

    void HeadlessBackend::setProperty(ElementId id, std::string_view name, std::string_view value)
    {
        record("setProperty " + label(id) + " " + std::string(name) + " " + std::string(value));
        if (auto it = mNodes.find(id); it != mNodes.end())
            it->second.mProperties[std::string(name)] = std::string(value);
    }

    void HeadlessBackend::setAttribute(ElementId id, std::string_view name, std::string_view value)
    {
        record("setAttribute " + label(id) + " " + std::string(name) + " " + std::string(value));
        if (auto it = mNodes.find(id); it != mNodes.end())
            it->second.mAttributes[std::string(name)] = std::string(value);
    }

    void HeadlessBackend::listen(ElementId id, EventKind kind, bool on)
    {
        record("listen " + label(id) + " " + std::string(eventKindName(kind)) + (on ? " on" : " off"));
        if (auto it = mNodes.find(id); it != mNodes.end())
        {
            if (on)
                it->second.mListens.insert(kind);
            else
                it->second.mListens.erase(kind);
        }
    }

    void HeadlessBackend::focus(ElementId id)
    {
        record("focus " + label(id));
        if (mNodes.count(id) != 0)
            mFocused = id;
    }

    ElementId HeadlessBackend::allocateId()
    {
        return makeElementId(mSession, ++mCounter);
    }

    void HeadlessBackend::commit()
    {
        ++mCommitCount;
    }

    void HeadlessBackend::takeEvents(std::vector<Event>& out)
    {
        for (Event& event : mEvents)
            out.push_back(std::move(event));
        mEvents.clear();
    }

    const HeadlessBackend::Node* HeadlessBackend::node(ElementId id) const
    {
        auto it = mNodes.find(id);
        return it == mNodes.end() ? nullptr : &it->second;
    }

    std::size_t HeadlessBackend::liveElements() const
    {
        return mNodes.size();
    }

    void HeadlessBackend::clearCalls()
    {
        mCalls.clear();
        mCallCount = 0;
    }

    bool HeadlessBackend::emit(ElementId target, EventKind kind, std::string value)
    {
        auto it = mNodes.find(target);
        if (it == mNodes.end() || it->second.mListens.count(kind) == 0)
            return false;
        mEvents.push_back({ target, kind, std::move(value) });
        return true;
    }
}
