#include "game_skyrimse/werewolf_service.hpp"

#include "game_skyrimse/inventory_service.hpp"

#include <algorithm>
#include <cmath>
#include <span>

// The werewolf service and its bindings: see the header (task 0259).

namespace game_skyrimse::werewolf
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
        /// The notices kept (the oldest go first): a form announces a handful of lines in a whole game.
        constexpr std::size_t kMaxNotices = 64;
        const std::string kNoText;

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };
    }

    struct WerewolfService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mScratch;
    };

    WerewolfService::WerewolfService(FeedHook feed)
        : mImpl(std::make_unique<Impl>())
        , mFeed(std::move(feed))
    {
    }

    WerewolfService::~WerewolfService() = default;

    void WerewolfService::setState(bool active, float secondsLeft)
    {
        mActive = active;
        mSecondsLeft = active && std::isfinite(secondsLeft) && secondsLeft > 0.f ? secondsLeft : 0.f;
    }

    void WerewolfService::announce(std::string text)
    {
        if (mNotices.size() >= kMaxNotices)
            mNotices.erase(mNotices.begin());
        mNotices.push_back(std::move(text));
    }

    const std::string& WerewolfService::noticeAt(std::size_t number) const
    {
        return number >= 1 && number <= mNotices.size() ? mNotices[number - 1] : kNoText;
    }

    bool WerewolfService::feed(std::uint64_t placement, FormId base)
    {
        return mActive && mFeed && mFeed(placement, base);
    }

    openeden::sdk::DeclareResult WerewolfService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        WerewolfService* self = this;
        static const BindingType intParam[] = { BindingType::Int32 };
        static const BindingType s2[] = { BindingType::String, BindingType::String };

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        const auto number = [](std::int32_t v) {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = v;
            return r;
        };

        add("active", {}, true, BindingType::Int32, BindingAccess::Read,
            [self, number](std::span<const BindingValue>) { return number(self->mActive ? 1 : 0); });
        add("seconds_left", {}, true, BindingType::Int32, BindingAccess::Read,
            [self, number](std::span<const BindingValue>) {
                return number(static_cast<std::int32_t>(std::min(self->mSecondsLeft, 100000.f)));
            });
        add("notification_count", {}, true, BindingType::Int32, BindingAccess::Read,
            [self, number](std::span<const BindingValue>) {
                return number(static_cast<std::int32_t>(self->mNotices.size() & 0x7FFFFFFF));
            });
        add("notification", intParam, true, BindingType::String, BindingAccess::Read,
            [self, &m](std::span<const BindingValue> a) {
                m.mScratch = !a.empty() && a[0].integer >= 1 ? self->noticeAt(static_cast<std::size_t>(a[0].integer))
                                                             : std::string();
                BindingResult r;
                r.value.type = BindingType::String;
                r.value.text = m.mScratch;
                return r;
            });
        add("feed", s2, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            std::uint64_t placement = 0, base = 0;
            if (a.size() < 2 || !inventory::parseId(a[0].text, placement) || !inventory::parseId(a[1].text, base)
                || base > 0xFFFFFFFFull)
            {
                BindingResult r;
                r.status = BindingStatus::BadArgument;
                r.message = "not an id";
                return r;
            }
            (void)self->feed(placement, static_cast<FormId>(base));
            return BindingResult();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.werewolf";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
