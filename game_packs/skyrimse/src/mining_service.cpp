#include "game_skyrimse/mining_service.hpp"

#include "game_skyrimse/inventory_service.hpp"

#include <span>

// The mining service and its bindings: see the header (task 0224).

namespace game_skyrimse::mining
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingStatus;
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

        BindingResult fail(BindingStatus status, std::string_view message)
        {
            BindingResult r;
            r.status = status;
            r.message = message;
            return r;
        }

        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
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

    struct MiningService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
    };

    MiningService::MiningService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mChunk(mBook)
    {
    }

    MiningService::~MiningService() = default;

    void MiningService::note(const std::string& line)
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    std::string MiningService::messageOr(FormId message, std::string_view fallback) const
    {
        if (message != kNullFormId && mHooks.mMessage)
        {
            std::string text = mHooks.mMessage(message);
            if (!text.empty())
                return text;
        }
        return std::string(fallback);
    }

    std::string MiningService::nameOf(FormId placement, FormId base) const
    {
        ResourceInfo info;
        std::string name;
        std::string verb;
        if (!mHooks.mResource || !mHooks.mResource(base, info, name, verb) || info.mKind != ResourceKind::Ore)
            return {};
        return mBook.depleted(placement, mNow) ? depletedName(name) : orePrompt(verb, name);
    }

    MiningStatus MiningService::activate(FormId placement, FormId base)
    {
        ResourceInfo info;
        std::string name;
        std::string verb;
        if (!mHooks.mResource || !mHooks.mResource(base, info, name, verb) || info.mKind == ResourceKind::None)
            return MiningStatus::NotResource; // silent: every bench and bed activation reaches this check
        const bool ore = info.mKind == ResourceKind::Ore;
        const std::string tag = "MINING " + inventory::formatId(placement) + (ore ? " vein " : " block ");
        if (ore && mBook.depleted(placement, mNow))
        {
            mState = "depleted";
            mMessage = messageOr(info.mDepletedMessage, kDefaultDepleted);
            ++mRevision;
            note(tag + "is depleted");
            return MiningStatus::Depleted;
        }
        if (info.mToolList != kNullFormId)
        {
            const int tools = mHooks.mToolCount ? mHooks.mToolCount(info.mToolList) : 0;
            if (tools < 0)
            {
                mState = "refused";
                mMessage = std::string(kNotReady);
                ++mRevision;
                note(tag + "waits for the form lists");
                return MiningStatus::Refused;
            }
            if (tools == 0)
            {
                mState = "need tool";
                mMessage = messageOr(info.mFailureMessage, ore ? kDefaultNeedPickaxe : kDefaultNeedAxe);
                ++mRevision;
                note(tag + "needs a tool");
                return MiningStatus::NeedTool;
            }
        }
        if (!mHooks.mAddToPlayer || info.mItem == kNullFormId)
        {
            note(tag + "refused: nothing to give");
            return MiningStatus::Refused;
        }
        const std::string itemName = mHooks.mItemName ? mHooks.mItemName(info.mItem) : std::string();
        const std::string label = itemName.empty() ? std::string("resources") : itemName;

        if (!ore)
        {
            // the script adds ResourceCount a chop and ends the session when the running count reaches the maximum
            const std::int32_t chops = (info.mMaxPerUse + info.mPerDrop - 1) / info.mPerDrop;
            const std::int32_t count = chops * info.mPerDrop;
            if (!mHooks.mAddToPlayer(info.mItem, count))
            {
                note(tag + "refused: the inventory could not take it");
                return MiningStatus::Refused;
            }
            mState = "chopped";
            mMessage = "Chopped " + std::to_string(count) + " " + label + ".";
            ++mRevision;
            note(tag + "gave " + inventory::formatId(info.mItem) + " x" + std::to_string(count));
            return MiningStatus::Ok;
        }

        const VeinState before = mBook.state(placement);
        const bool dropNow = before.mStrikes + 1 >= info.mStrikes;
        std::int32_t gemCount = 0;
        FormId gem = kNullFormId;
        if (dropNow)
        {
            if (!mHooks.mAddToPlayer(info.mItem, info.mPerDrop))
            {
                note(tag + "refused: the inventory could not take it");
                return MiningStatus::Refused;
            }
            if (info.mGems != kNullFormId && mHooks.mPick
                && mHooks.mPick(info.mGems, gemSeed(placement, before.mDrops), gem, gemCount) && gemCount > 0
                && gem != kNullFormId)
            {
                if (!mHooks.mAddToPlayer(gem, gemCount))
                    gem = kNullFormId;
            }
        }
        const double regrowAt = (mNow < 0.0 ? 0.0 : mNow) + kRegrowSeconds;
        const StrikeOutcome outcome = mBook.strike(placement, info, regrowAt);
        ++mRevision;
        std::string line = tag + "strike " + std::to_string(before.mStrikes + 1) + "/" + std::to_string(info.mStrikes);
        if (outcome.mDrop)
        {
            line += " gave " + inventory::formatId(info.mItem) + " x" + std::to_string(info.mPerDrop);
            if (gem != kNullFormId)
                line += " and " + inventory::formatId(gem) + " x" + std::to_string(gemCount);
        }
        if (outcome.mDepleted)
            line += ", depleted";
        note(line);
        if (outcome.mDepleted)
        {
            mState = "depleted";
            mMessage = messageOr(info.mDepletedMessage, kDefaultDepleted);
        }
        else
        {
            mState = "mined";
            if (outcome.mDrop)
                mMessage = "Mined " + label + (info.mPerDrop > 1 ? " x" + std::to_string(info.mPerDrop) : "") + ".";
            else
                mMessage = "You strike the vein.";
        }
        return MiningStatus::Ok;
    }

    std::size_t MiningService::setClock(double gameSeconds)
    {
        mNow = gameSeconds;
        if (!mWarmed)
        {
            mWarmed = true;
            if (mHooks.mWarm)
                mHooks.mWarm();
        }
        std::vector<FormId> grown;
        mBook.collectGrown(mNow, grown);
        for (const FormId placement : grown)
            note("MINING " + inventory::formatId(placement) + " vein is full again");
        if (!grown.empty())
            ++mRevision;
        return grown.size();
    }

    openeden::sdk::DeclareResult MiningService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        MiningService* self = this;
        static const BindingType s2[] = { BindingType::String, BindingType::String };
        static const BindingType clockParams[] = { BindingType::Double };

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

        add("state", {}, true, BindingType::String, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = text(self->mState);
            return r;
        });
        add("message", {}, true, BindingType::String, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = text(self->mMessage);
            return r;
        });
        add("revision", {}, true, BindingType::Int32, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = integer(static_cast<std::int32_t>(self->revision() & 0x7FFFFFFF));
            return r;
        });
        add("activate", s2, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                std::uint64_t placement = 0, base = 0;
                if (!inventory::parseId(a[0].text, placement) || !inventory::parseId(a[1].text, base)
                    || placement > 0xFFFFFFFFull || base > 0xFFFFFFFFull)
                    return fail(BindingStatus::BadArgument, "not an id");
                (void)self->activate(static_cast<FormId>(placement), static_cast<FormId>(base));
                return ok();
            });
        add("set_clock", clockParams, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                (void)self->setClock(a[0].number);
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.mining";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
