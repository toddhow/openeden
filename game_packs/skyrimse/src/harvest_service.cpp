#include "game_skyrimse/harvest_service.hpp"

#include "game_skyrimse/inventory_service.hpp"

#include <span>

// The harvest service and its bindings: see the header (task 0221).

namespace game_skyrimse::harvest
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

    struct HarvestService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mPromptCache; // the text a prompt() result points at stays valid until the next call
    };

    HarvestService::HarvestService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mChunk(mBook)
    {
    }

    HarvestService::~HarvestService() = default;

    void HarvestService::note(const std::string& line)
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    std::string HarvestService::promptOf(FormId base) const
    {
        PlantInfo plant;
        if (!mHooks.mPlant || !mHooks.mPlant(base, plant) || plant.mIngredient == kNullFormId)
            return {};
        return promptFor(plant.mActivateText, plant.mName);
    }

    HarvestStatus HarvestService::harvest(FormId placement, FormId base)
    {
        PlantInfo plant;
        if (!mHooks.mPlant || !mHooks.mPlant(base, plant) || plant.mIngredient == kNullFormId)
        {
            note("HARVEST " + inventory::formatId(placement) + " refused: " + inventory::formatId(base)
                + " is not harvestable");
            return HarvestStatus::NotHarvestable;
        }
        if (mBook.has(placement))
        {
            mState = "already";
            mMessage = "It has already been harvested.";
            ++mRevision;
            return HarvestStatus::AlreadyHarvested;
        }
        if (!mHooks.mPick || !mHooks.mAddToPlayer)
            return HarvestStatus::Refused;
        const float chance = chanceIn(plant.mChances, seasonOfMonth(mMonth));
        if (!(chance > 0.f))
        {
            mState = "nothing";
            mMessage = "There is nothing to harvest.";
            ++mRevision;
            note("HARVEST " + inventory::formatId(placement) + " out of season");
            return HarvestStatus::OutOfSeason;
        }
        const std::uint32_t attempt = mBook.takeAttempt();
        const double due = (mNow < 0.0 ? 0.0 : mNow) + kRegrowSeconds;
        const bool success = harvestRoll(placement, attempt, chance);
        FormId item = kNullFormId;
        std::int32_t count = 0;
        if (success && (!mHooks.mPick(plant.mIngredient, pickSeed(placement, attempt), item, count) || count <= 0))
        {
            note("HARVEST " + inventory::formatId(placement) + " refused: no item for "
                + inventory::formatId(plant.mIngredient));
            return HarvestStatus::Refused;
        }
        if (success && !mHooks.mAddToPlayer(item, count))
        {
            note("HARVEST " + inventory::formatId(placement) + " refused: the inventory could not take it");
            return HarvestStatus::Refused;
        }
        // Remembered even when the world could not be told: the plant gave its yield, so it must not give it twice.
        (void)mBook.mark(placement, due);
        const bool hidden = plant.mHide && mHooks.mHide && mHooks.mHide(placement);
        ++mRevision;
        if (!success)
        {
            mState = "nothing";
            mMessage = "You find nothing.";
            note("HARVEST " + inventory::formatId(placement) + " found nothing at "
                + std::to_string(static_cast<int>(chance)) + "%");
            return HarvestStatus::Nothing;
        }
        mState = "harvested";
        const std::string itemName = mHooks.mItemName ? mHooks.mItemName(item) : std::string();
        mMessage = itemName.empty() ? std::string("Harvested.") : "Harvested " + itemName + ".";
        note("HARVEST " + inventory::formatId(placement) + " took " + inventory::formatId(item) + " x"
            + std::to_string(count) + " at " + std::to_string(static_cast<int>(chance)) + "%"
            + (hidden ? "" : " (the plant stays in view)"));
        return HarvestStatus::Ok;
    }

    std::size_t HarvestService::setClock(double gameSeconds, int month)
    {
        mNow = gameSeconds;
        mMonth = month;
        std::vector<FormId> grown;
        mBook.collectGrown(mNow, grown);
        for (const FormId placement : grown)
        {
            if (mHooks.mShow)
                (void)mHooks.mShow(placement);
            note("HARVEST " + inventory::formatId(placement) + " grew back");
        }
        if (!grown.empty())
            ++mRevision;
        return grown.size();
    }

    openeden::sdk::DeclareResult HarvestService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        HarvestService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType s2[] = { BindingType::String, BindingType::String };
        static const BindingType clockParams[] = { BindingType::Double, BindingType::Int32 };

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
        const auto idOf = [](const BindingValue& v, std::uint64_t& out) { return inventory::parseId(v.text, out); };
        Impl* impl = &m;

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
        add("prompt", s1, true, BindingType::String, BindingAccess::Read,
            [self, idOf, impl](std::span<const BindingValue> a) {
                std::uint64_t base = 0;
                if (!idOf(a[0], base) || base > 0xFFFFFFFFull)
                    return fail(BindingStatus::BadArgument, "not an id");
                impl->mPromptCache = self->promptOf(static_cast<FormId>(base));
                BindingResult r = ok();
                r.value = text(impl->mPromptCache);
                return r;
            });
        add("harvest", s2, false, BindingType::Bool, BindingAccess::Mutate,
            [self, idOf](std::span<const BindingValue> a) {
                std::uint64_t placement = 0, base = 0;
                if (!idOf(a[0], placement) || !idOf(a[1], base) || placement > 0xFFFFFFFFull || base > 0xFFFFFFFFull)
                    return fail(BindingStatus::BadArgument, "not an id");
                (void)self->harvest(static_cast<FormId>(placement), static_cast<FormId>(base));
                return ok();
            });
        add("set_clock", clockParams, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                (void)self->setClock(a[0].number, a[1].integer);
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.harvest";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
