#include "game_skyrimse/forcegreet.hpp"

#include <functional>
#include <span>

namespace game_skyrimse
{
    namespace
    {
        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<openeden::sdk::BindingResult(std::span<const openeden::sdk::BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            openeden::sdk::BindingResult call(std::span<const openeden::sdk::BindingValue> args) override
            {
                return mBody(args);
            }

        private:
            Body mBody;
        };
    }

    struct ForceGreet::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; ///< the answer's text points into it until the next call
    };

    ForceGreet::ForceGreet()
        : mImpl(std::make_unique<Impl>())
    {
    }

    ForceGreet::~ForceGreet() = default;

    bool ForceGreet::request(FormId reference, FormId base, FormId topic)
    {
        if (reference == kNullFormId || topic == kNullFormId || mEntries.count(reference) != 0
            || mEntries.size() >= kMaxRequests)
            return false;
        Entry entry;
        entry.mBase = base;
        entry.mTopic = topic;
        mEntries[reference] = entry;
        ++mSerial;
        return true;
    }

    void ForceGreet::cancel(FormId reference)
    {
        mEntries.erase(reference);
    }

    FormId ForceGreet::topicFor(FormId reference) const
    {
        const auto it = mEntries.find(reference);
        return it != mEntries.end() && it->second.mState == State::Waiting ? it->second.mTopic : kNullFormId;
    }

    void ForceGreet::began(FormId reference)
    {
        const auto it = mEntries.find(reference);
        if (it != mEntries.end() && it->second.mState == State::Waiting)
            it->second.mState = State::Open;
    }

    void ForceGreet::ended(FormId reference)
    {
        const auto it = mEntries.find(reference);
        if (it == mEntries.end())
            return;
        // a conversation that failed to open ends at once: the request is over either way
        if (it->second.mState == State::Open)
            it->second.mState = State::Done;
        else if (it->second.mState == State::Waiting)
            it->second.mState = State::Failed;
    }

    bool ForceGreet::finished(FormId reference)
    {
        const auto it = mEntries.find(reference);
        if (it == mEntries.end())
            return true;
        Entry& e = it->second;
        if (e.mState == State::Waiting && ++e.mChecks > kTimeoutChecks)
        {
            e.mState = State::Failed;
            ++mTimeouts;
        }
        if (e.mState == State::Done || e.mState == State::Failed)
        {
            mEntries.erase(it);
            return true;
        }
        return false;
    }

    ForceGreet::State ForceGreet::stateOf(FormId reference) const
    {
        const auto it = mEntries.find(reference);
        return it == mEntries.end() ? State::Done : it->second.mState;
    }

    std::string ForceGreet::pendingBase() const
    {
        for (const auto& [reference, e] : mEntries)
        {
            if (e.mState == State::Waiting)
                return std::to_string(e.mBase);
        }
        return {};
    }

    openeden::sdk::DeclareResult ForceGreet::declare(openeden::sdk::BindingsService& service)
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;
        Impl& m = *mImpl;
        ForceGreet* self = this;
        const auto add = [&m](std::string_view name, BindingType result, Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = {};
            f.hasResult = true;
            f.result = result;
            f.access = openeden::sdk::BindingAccess::Read;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        add("pending_base", BindingType::String, [self, &m](std::span<const BindingValue>) {
            m.mText = self->pendingBase();
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.text = m.mText;
            return r;
        });
        add("pending_serial", BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(self->serial() & 0x7FFFFFFF);
            return r;
        });
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.forcegreet";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
