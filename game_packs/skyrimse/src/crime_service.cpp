#include "game_skyrimse/crime_service.hpp"

#include <algorithm>
#include <span>

// The crime service and its bindings: see the header (task 0204).

namespace game_skyrimse::crime
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
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

        BindingResult ok()
        {
            return BindingResult{};
        }

        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }

        BindingValue number(double v)
        {
            BindingValue b;
            b.type = BindingType::Double;
            b.number = v;
            return b;
        }

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }

        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }
    }

    struct CrimeService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; // result buffer: valid until the handler is called again
    };

    CrimeService::CrimeService(ServiceHooks hooks, Settings settings)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mChunk(mLedger)
        , mSystem(mLedger, *this, settings)
    {
    }

    CrimeService::~CrimeService() = default;

    std::int32_t CrimeService::stolenHeld(FormId item) const
    {
        const std::int32_t stolen = mLedger.stolen(item);
        if (stolen <= 0 || !mHooks.mItemCount)
            return stolen;
        return std::min(stolen, std::max<std::int32_t>(0, mHooks.mItemCount(item)));
    }

    const JailDestination& CrimeService::jailOf(FormId faction)
    {
        const auto it = mJail.find(faction);
        if (it != mJail.end())
            return it->second;
        JailDestination dest;
        if (faction == kNullFormId || !mHooks.mJail || !mHooks.mJail(faction, dest))
            dest = {};
        if (mJail.size() < 256)
            return mJail.emplace(faction, std::move(dest)).first->second;
        mNoJail = std::move(dest);
        return mNoJail;
    }

    ChoiceStatus CrimeService::choose(std::string_view choice)
    {
        const FormId faction = mSystem.offerFaction();
        const ChoiceStatus status = mSystem.choose(choice);
        if (status == ChoiceStatus::Ok && mSystem.state() == State::Jailed && !jailOf(faction).mValid)
        {
            log("CRIME no jail destination for the faction: the bounty is cleared without a sentence");
            mSystem.setMessage("There is no jail here: your bounty is cleared.");
            mSystem.released();
        }
        return status;
    }

    void CrimeService::actors(std::vector<CrimeActor>& out)
    {
        if (mHooks.mActors)
            mHooks.mActors(out);
    }

    bool CrimeService::actor(openeden::sdk::game::ActorHandle handle, CrimeActor& out)
    {
        return mHooks.mActor && mHooks.mActor(handle, out);
    }

    const CrimeData* CrimeService::crimeData(FormId faction)
    {
        return mHooks.mCrimeData ? mHooks.mCrimeData(faction) : nullptr;
    }

    std::string CrimeService::factionName(FormId faction)
    {
        return mHooks.mFactionName ? mHooks.mFactionName(faction) : std::string();
    }

    std::int32_t CrimeService::itemValue(FormId item)
    {
        return mHooks.mItemValue ? mHooks.mItemValue(item) : 0;
    }

    std::int32_t CrimeService::playerGold()
    {
        return mHooks.mGold ? mHooks.mGold() : 0;
    }

    bool CrimeService::takeGold(std::int32_t amount)
    {
        return mHooks.mTakeGold && mHooks.mTakeGold(amount);
    }

    std::int32_t CrimeService::takeItem(FormId item, std::int32_t count)
    {
        return mHooks.mTakeItem ? mHooks.mTakeItem(item, count) : 0;
    }

    std::int32_t CrimeService::itemCount(FormId item)
    {
        return mHooks.mItemCount ? mHooks.mItemCount(item) : 0;
    }

    void CrimeService::guardGoTo(
        openeden::sdk::game::ActorHandle guard, double x, double y, double z, openeden::sdk::game::ActorSpeed speed)
    {
        if (mHooks.mGoTo)
            mHooks.mGoTo(guard, x, y, z, speed);
    }

    void CrimeService::guardHold(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mHold)
            mHooks.mHold(guard);
    }

    void CrimeService::guardRelease(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mRelease)
            mHooks.mRelease(guard);
    }

    void CrimeService::guardAttack(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mAttack)
            mHooks.mAttack(guard);
    }

    void CrimeService::log(const std::string& line)
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    openeden::sdk::DeclareResult CrimeService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        CrimeService* self = this;
        static const BindingType s1[] = { BindingType::String };
        constexpr BindingType kStr = BindingType::String;

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
        const auto stringFn = [&](std::string_view name, std::function<std::string(CrimeService&)> get) {
            add(name, {}, true, kStr, BindingAccess::Read,
                [self, &m, get = std::move(get)](std::span<const BindingValue>) {
                    m.mText = get(*self);
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto intFn = [&](std::string_view name, std::function<std::int64_t(CrimeService&)> get) {
            add(name, {}, true, BindingType::Int32, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = integer(static_cast<std::int32_t>(std::clamp<std::int64_t>(get(*self), 0, 0x7FFFFFFF)));
                    return r;
                });
        };
        const auto doubleFn = [&](std::string_view name, std::function<double(CrimeService&)> get) {
            add(name, {}, true, BindingType::Double, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = number(get(*self));
                    return r;
                });
        };
        // The faction whose jail is meant: the offer's, or the one the player was sent to.
        const auto jailFaction = [](CrimeService& s) {
            return s.mSystem.jailFaction() != kNullFormId ? s.mSystem.jailFaction() : s.mSystem.offerFaction();
        };

        stringFn("state", [](CrimeService& s) { return std::string(stateName(s.mSystem.state())); });
        stringFn("guard_name", [](CrimeService& s) { return s.mSystem.guardName(); });
        stringFn("message", [](CrimeService& s) { return s.mSystem.message(); });
        intFn("bounty", [](CrimeService& s) { return s.mLedger.totalBounty(); });
        intFn("offer_bounty", [](CrimeService& s) { return s.mSystem.offerBounty(); });
        intFn("stolen_count", [](CrimeService& s) { return s.mLedger.stolenTotal(); });
        intFn("revision", [](CrimeService& s) {
            return static_cast<std::int64_t>(s.mSystem.revision() + s.mLedger.revision() * 7u);
        });
        add("can_pay", {}, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = boolean(self->mSystem.canPay());
            return r;
        });
        add("jail_available", {}, true, BindingType::Bool, BindingAccess::Read,
            [self, jailFaction](std::span<const BindingValue>) {
                BindingResult r = ok();
                r.value = boolean(self->jailOf(jailFaction(*self)).mValid);
                return r;
            });
        stringFn("jail_cell", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mCell; });
        doubleFn("jail_x", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mX; });
        doubleFn("jail_y", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mY; });
        doubleFn("jail_z", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mZ; });
        doubleFn("jail_heading", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mHeading; });
        doubleFn("jail_seconds",
            [](CrimeService& s) { return static_cast<double>(s.mSystem.settings().mJailSeconds); });
        add("choose", s1, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            const ChoiceStatus status = self->choose(a[0].text);
            if (status != ChoiceStatus::Ok)
                self->log("CRIME choice " + std::string(a[0].text) + " refused ("
                    + (status == ChoiceStatus::NoOffer
                              ? "no offer"
                              : status == ChoiceStatus::NotEnoughGold ? "not enough gold" : "unknown choice")
                    + ")");
            return ok();
        });
        add("released", {}, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue>) {
            self->mSystem.released();
            return ok();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.crime";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
