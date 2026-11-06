#include "eventbus.hpp"

#include <algorithm>
#include <exception>

namespace CSMModule
{
    std::uint64_t EventBus::subscribe(std::string owner, Handler handler)
    {
        if (!handler)
            return 0;
        const std::uint64_t id = mNextId++;
        mSubscribers.push_back({ id, std::move(owner), std::move(handler) });
        return id;
    }

    void EventBus::unsubscribe(std::uint64_t id)
    {
        mSubscribers.erase(std::remove_if(mSubscribers.begin(), mSubscribers.end(),
                               [id](const Subscriber& subscriber) { return subscriber.mId == id; }),
            mSubscribers.end());
    }

    void EventBus::removeOwner(const std::string& owner)
    {
        mSubscribers.erase(std::remove_if(mSubscribers.begin(), mSubscribers.end(),
                               [&owner](const Subscriber& subscriber) { return subscriber.mOwner == owner; }),
            mSubscribers.end());
    }

    void EventBus::publish(const openeden::editor::EditorEvent& event)
    {
        if (mDispatching)
        {
            if (mQueue.size() >= kMaxQueuedEvents)
            {
                ++mDropped;
                return;
            }
            mQueue.push_back(event);
            return;
        }

        mDispatching = true;
        deliver(event);
        while (!mQueue.empty())
        {
            const openeden::editor::EditorEvent next = std::move(mQueue.front());
            mQueue.pop_front();
            deliver(next);
        }
        mDispatching = false;
    }

    void EventBus::deliver(const openeden::editor::EditorEvent& event)
    {
        // The ids present now: a handler may add, remove or unsubscribe while this runs.
        std::vector<std::uint64_t> ids;
        ids.reserve(mSubscribers.size());
        for (const Subscriber& subscriber : mSubscribers)
            ids.push_back(subscriber.mId);

        for (const std::uint64_t id : ids)
        {
            const auto found = std::find_if(
                mSubscribers.begin(), mSubscribers.end(), [id](const Subscriber& s) { return s.mId == id; });
            if (found == mSubscribers.end())
                continue;
            // Copies: the entry may be erased by the handler itself.
            const Handler handler = found->mHandler;
            const std::string owner = found->mOwner;

            std::string failure;
            bool failed = false;
            try
            {
                handler(event);
            }
            catch (const std::exception& e)
            {
                failed = true;
                failure = e.what();
            }
            catch (...)
            {
                failed = true;
                failure = "unknown exception";
            }
            if (!failed)
                continue;

            unsubscribe(id);
            if (mReport)
            {
                try
                {
                    mReport(owner, failure);
                }
                catch (...)
                {
                }
            }
        }
    }

    void SelectionModel::select(const openeden::editor::Selection& selection)
    {
        openeden::editor::Selection next = selection;
        if (next.kind.empty())
            next.id.clear();
        if (next == mCurrent)
            return;
        mCurrent = next;
        openeden::editor::EditorEvent event;
        event.type = openeden::editor::EditorEventType::SelectionChanged;
        event.subject = mCurrent.kind;
        event.detail = mCurrent.id;
        mBus.publish(event);
    }
}
