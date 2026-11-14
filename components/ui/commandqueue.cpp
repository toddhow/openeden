#include "commandqueue.hpp"

#include <array>
#include <set>
#include <stdexcept>
#include <string>

namespace Ui
{
    namespace
    {
        enum class Op : std::uint8_t
        {
            Create,
            CreateDocument,
            Destroy,
            InsertBefore,
            Detach,
            SetText,
            SetClass,
            SetProperty,
            SetAttribute,
            Listen,
            Focus,
        };

        // One protocol call. Strings live in the batch's arena (offset and length), so a command costs no allocation of
        // its own.
        struct Command
        {
            Op mOp = Op::Create;
            bool mFlag = false;
            EventKind mKind = EventKind::Click;
            ElementId mA = kNoElement;
            ElementId mB = kNoElement;
            ElementId mC = kNoElement;
            std::uint32_t mOffset1 = 0;
            std::uint32_t mLength1 = 0;
            std::uint32_t mOffset2 = 0;
            std::uint32_t mLength2 = 0;
        };

        struct Batch
        {
            std::vector<Command> mCommands;
            std::string mArena;
        };

        constexpr std::size_t kSessionSlots = 256;
    }

    struct CommandQueue::Impl
    {
        mutable std::mutex mMutex;
        std::vector<Batch> mCommitted;
        std::array<bool, kSessionSlots> mOpen{};
        std::array<std::vector<Event>, kSessionSlots> mInboxes;
    };

    // The producer side of one session.
    class QueueBackend final : public Backend
    {
    public:
        QueueBackend(std::shared_ptr<CommandQueue> queue, unsigned session)
            : mQueue(std::move(queue))
            , mSession(session)
        {
        }

        ~QueueBackend() override
        {
            // Documents the script left open are closed with it.
            for (ElementId document : mDocuments)
                destroyNow(document);
            commit();
            std::lock_guard lock(mQueue->mImpl->mMutex);
            mQueue->mImpl->mOpen[mSession] = false;
            mQueue->mImpl->mInboxes[mSession].clear();
        }

        void create(ElementId id, std::string_view tag) override
        {
            Command& c = add(Op::Create);
            c.mA = id;
            setString1(c, tag);
        }
        void createDocument(ElementId id, std::string_view name) override
        {
            Command& c = add(Op::CreateDocument);
            c.mA = id;
            setString1(c, name);
            mDocuments.insert(id);
        }
        void destroy(ElementId id) override
        {
            destroyNow(id);
            mDocuments.erase(id);
        }
        void insertBefore(ElementId parent, ElementId child, ElementId before) override
        {
            Command& c = add(Op::InsertBefore);
            c.mA = parent;
            c.mB = child;
            c.mC = before;
        }
        void detach(ElementId child) override { add(Op::Detach).mA = child; }
        void setText(ElementId id, std::string_view text) override
        {
            Command& c = add(Op::SetText);
            c.mA = id;
            setString1(c, text);
        }
        void setClass(ElementId id, std::string_view classes) override
        {
            Command& c = add(Op::SetClass);
            c.mA = id;
            setString1(c, classes);
        }
        void setProperty(ElementId id, std::string_view name, std::string_view value) override
        {
            Command& c = add(Op::SetProperty);
            c.mA = id;
            setString1(c, name);
            setString2(c, value);
        }
        void setAttribute(ElementId id, std::string_view name, std::string_view value) override
        {
            Command& c = add(Op::SetAttribute);
            c.mA = id;
            setString1(c, name);
            setString2(c, value);
        }
        void listen(ElementId id, EventKind kind, bool on) override
        {
            Command& c = add(Op::Listen);
            c.mA = id;
            c.mKind = kind;
            c.mFlag = on;
        }
        void focus(ElementId id) override { add(Op::Focus).mA = id; }

        ElementId allocateId() override { return makeElementId(mSession, ++mCounter); }

        void commit() override
        {
            if (mPending.mCommands.empty())
                return;
            std::lock_guard lock(mQueue->mImpl->mMutex);
            mQueue->mImpl->mCommitted.push_back(std::move(mPending));
            mPending = Batch{};
        }

        void takeEvents(std::vector<Event>& out) override
        {
            std::lock_guard lock(mQueue->mImpl->mMutex);
            auto& inbox = mQueue->mImpl->mInboxes[mSession];
            for (Event& event : inbox)
                out.push_back(std::move(event));
            inbox.clear();
        }

    private:
        void destroyNow(ElementId id) { add(Op::Destroy).mA = id; }

        Command& add(Op op)
        {
            mPending.mCommands.emplace_back();
            Command& c = mPending.mCommands.back();
            c.mOp = op;
            return c;
        }
        void setString1(Command& c, std::string_view s)
        {
            c.mOffset1 = static_cast<std::uint32_t>(mPending.mArena.size());
            c.mLength1 = static_cast<std::uint32_t>(s.size());
            mPending.mArena.append(s);
        }
        void setString2(Command& c, std::string_view s)
        {
            c.mOffset2 = static_cast<std::uint32_t>(mPending.mArena.size());
            c.mLength2 = static_cast<std::uint32_t>(s.size());
            mPending.mArena.append(s);
        }

        std::shared_ptr<CommandQueue> mQueue;
        unsigned mSession;
        std::uint32_t mCounter = 0;
        Batch mPending;
        std::set<ElementId> mDocuments;
    };

    CommandQueue::CommandQueue()
        : mImpl(std::make_unique<Impl>())
    {
    }

    CommandQueue::~CommandQueue() = default;

    std::unique_ptr<Backend> CommandQueue::openSession()
    {
        unsigned session = 0;
        {
            std::lock_guard lock(mImpl->mMutex);
            for (unsigned i = 1; i <= kMaxSessions; ++i)
            {
                if (!mImpl->mOpen[i])
                {
                    session = i;
                    mImpl->mOpen[i] = true;
                    mImpl->mInboxes[i].clear();
                    break;
                }
            }
        }
        if (session == 0)
            throw std::runtime_error("UI: too many open sessions");
        return std::make_unique<QueueBackend>(shared_from_this(), session);
    }

    std::size_t CommandQueue::drain(ElementProtocol& target)
    {
        std::vector<Batch> batches;
        {
            std::lock_guard lock(mImpl->mMutex);
            if (mImpl->mCommitted.empty())
                return 0;
            batches.swap(mImpl->mCommitted);
        }
        std::size_t applied = 0;
        for (const Batch& batch : batches)
        {
            for (const Command& c : batch.mCommands)
            {
                const std::string_view s1(batch.mArena.data() + c.mOffset1, c.mLength1);
                const std::string_view s2(batch.mArena.data() + c.mOffset2, c.mLength2);
                switch (c.mOp)
                {
                    case Op::Create:
                        target.create(c.mA, s1);
                        break;
                    case Op::CreateDocument:
                        target.createDocument(c.mA, s1);
                        break;
                    case Op::Destroy:
                        target.destroy(c.mA);
                        break;
                    case Op::InsertBefore:
                        target.insertBefore(c.mA, c.mB, c.mC);
                        break;
                    case Op::Detach:
                        target.detach(c.mA);
                        break;
                    case Op::SetText:
                        target.setText(c.mA, s1);
                        break;
                    case Op::SetClass:
                        target.setClass(c.mA, s1);
                        break;
                    case Op::SetProperty:
                        target.setProperty(c.mA, s1, s2);
                        break;
                    case Op::SetAttribute:
                        target.setAttribute(c.mA, s1, s2);
                        break;
                    case Op::Listen:
                        target.listen(c.mA, c.mKind, c.mFlag);
                        break;
                    case Op::Focus:
                        target.focus(c.mA);
                        break;
                }
                ++applied;
            }
        }
        return applied;
    }

    void CommandQueue::pushEvent(Event event)
    {
        const unsigned session = sessionOf(event.mTarget);
        if (session == 0 || session >= kSessionSlots)
            return;
        std::lock_guard lock(mImpl->mMutex);
        if (!mImpl->mOpen[session])
            return;
        auto& inbox = mImpl->mInboxes[session];
        if (inbox.size() >= kMaxInboxEvents)
            return;
        inbox.push_back(std::move(event));
    }

    std::size_t CommandQueue::pendingBatches() const
    {
        std::lock_guard lock(mImpl->mMutex);
        return mImpl->mCommitted.size();
    }
}
