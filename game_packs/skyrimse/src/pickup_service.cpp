#include "game_skyrimse/pickup_service.hpp"

#include "game_skyrimse/inventory_service.hpp"

#include <algorithm>
#include <span>

// The pickup service and its bindings: see the header (task 0208).

namespace game_skyrimse::pickup
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

    struct PickupService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
    };

    PickupService::PickupService(Hooks hooks, PickpocketSettings settings)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mSettings(settings)
        , mChunk(mBook)
    {
    }

    PickupService::~PickupService() = default;

    void PickupService::note(const std::string& line)
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    PickupStatus PickupService::pickup(FormId placement, FormId base)
    {
        std::int32_t value = 0;
        float weight = 0.f;
        if (!mHooks.mItem || !mHooks.mItem(base, value, weight))
        {
            note("PICKUP " + inventory::formatId(placement) + " refused: " + inventory::formatId(base)
                + " is not an item");
            return PickupStatus::NotAnItem;
        }
        if (mBook.taken(placement))
            return PickupStatus::AlreadyTaken;
        if (mBook.placements().size() >= kMaxTaken)
            return PickupStatus::Full;
        if (!mHooks.mAddToPlayer)
            return PickupStatus::Refused;
        const std::int32_t count = placedCount(mHooks.mCount ? mHooks.mCount(placement) : 0);
        if (!mHooks.mAddToPlayer(base, count))
        {
            note("PICKUP " + inventory::formatId(placement) + " refused: the inventory could not take it");
            return PickupStatus::Refused;
        }
        // Owned by somebody else: theft (the crime rules decide who saw it), before the object leaves the world.
        if (mHooks.mTaken)
            mHooks.mTaken(placement, base, count);
        // A script's OnActivate is queued before the object goes.
        if (mHooks.mActivated)
            mHooks.mActivated(placement);
        const bool disabled = mHooks.mDisable && mHooks.mDisable(placement);
        // Remembered even when the world could not be told: the item is in the inventory, so it must not be taken
        // twice.
        (void)mBook.markTaken(placement);
        mState = "taken";
        mMessage = std::string();
        ++mRevision;
        note("PICKUP " + inventory::formatId(placement) + " took " + inventory::formatId(base) + " x"
            + std::to_string(count) + (disabled ? "" : " (the object could not be removed)"));
        return PickupStatus::Ok;
    }

    float PickupService::chance(std::uint64_t owner, FormId item, std::int32_t count) const
    {
        (void)owner;
        std::int32_t value = 0;
        float weight = 0.f;
        if (!mHooks.mItem || !mHooks.mSkill || !mHooks.mItem(item, value, weight))
            return 0.f;
        return withBonus(owner, pickpocketChance(mSettings, mHooks.mSkill(), value, weight, count));
    }

    float PickupService::withBonus(std::uint64_t owner, float percent) const
    {
        // task 0275: Mod Pickpocket Chance (entry point 56) of the player's perks, in percentage points
        return mHooks.mChanceBonus ? chanceWithBonus(mSettings, percent, mHooks.mChanceBonus(owner)) : percent;
    }

    TryStatus PickupService::take(std::uint64_t owner, FormId item, std::int32_t count)
    {
        std::int32_t value = 0;
        float weight = 0.f;
        if (!mHooks.mItem || !mHooks.mHeldBy || !mHooks.mMoveToPlayer || !mHooks.mSkill)
            return TryStatus::Refused;
        if (count <= 0 || mHooks.mHeldBy(owner, item) < count || !mHooks.mItem(item, value, weight))
            return TryStatus::BadCount;
        const float percent = withBonus(owner, pickpocketChance(mSettings, mHooks.mSkill(), value, weight, count));
        const bool rolled = pickpocketRoll(owner, item, mBook.takeAttempt(), percent);
        const bool success = rolled && !(mHooks.mAware && mHooks.mAware(owner)); // task 0227
        if (mHooks.mTrain)
            mHooks.mTrain(pickpocketTraining(value, count));
        ++mRevision;
        if (success && mHooks.mMoveToPlayer(owner, item, count))
        {
            mState = "success";
            mMessage = "You took it.";
            note("PICKPOCKET " + inventory::formatId(owner) + " took " + inventory::formatId(item) + " x"
                + std::to_string(count) + " at " + std::to_string(static_cast<int>(percent)) + "%");
            return TryStatus::Success;
        }
        mState = "caught";
        mMessage = "You were caught!";
        if (mHooks.mCaught)
            mHooks.mCaught(owner, item, count);
        note("PICKPOCKET " + inventory::formatId(owner) + " caught trying for " + inventory::formatId(item) + " x"
            + std::to_string(count) + " at " + std::to_string(static_cast<int>(percent)) + "%");
        return TryStatus::Caught;
    }

    TryStatus PickupService::plant(std::uint64_t owner, FormId item, std::int32_t count)
    {
        std::int32_t value = 0;
        float weight = 0.f;
        if (!mHooks.mItem || !mHooks.mHeldBy || !mHooks.mMoveFromPlayer || !mHooks.mSkill)
            return TryStatus::Refused;
        if (count <= 0 || mHooks.mHeldBy(inventory::kPlayerOwner, item) < count
            || !mHooks.mItem(item, value, weight))
            return TryStatus::BadCount;
        const float percent = withBonus(owner, pickpocketChance(mSettings, mHooks.mSkill(), value, weight, count));
        // the same chance as a take, a draw of its own (planting and stealing the same item are not the same roll)
        const bool rolled = pickpocketRoll(owner ^ kPlantSalt, item, mBook.takeAttempt(), percent);
        const bool success = rolled && !(mHooks.mAware && mHooks.mAware(owner));
        if (mHooks.mTrain)
            mHooks.mTrain(pickpocketTraining(value, count));
        ++mRevision;
        if (success && mHooks.mMoveFromPlayer(owner, item, count))
        {
            mState = "planted";
            mMessage = "You slipped it in.";
            note("PICKPOCKET " + inventory::formatId(owner) + " planted " + inventory::formatId(item) + " x"
                + std::to_string(count) + " at " + std::to_string(static_cast<int>(percent)) + "%");
            return TryStatus::Success;
        }
        mState = "caught";
        mMessage = "You were caught!";
        if (mHooks.mCaught)
            mHooks.mCaught(owner, item, count);
        note("PICKPOCKET " + inventory::formatId(owner) + " caught trying to plant " + inventory::formatId(item) + " x"
            + std::to_string(count) + " at " + std::to_string(static_cast<int>(percent)) + "%");
        return TryStatus::Caught;
    }

    openeden::sdk::DeclareResult PickupService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        PickupService* self = this;
        static const BindingType s2[] = { BindingType::String, BindingType::String };
        static const BindingType s3[] = { BindingType::String, BindingType::String, BindingType::Int32 };

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
            r.value = integer(static_cast<std::int32_t>(self->mRevision & 0x7FFFFFFF));
            return r;
        });
        add("chance", s2, true, BindingType::Int32, BindingAccess::Read, [self, idOf](std::span<const BindingValue> a) {
            std::uint64_t owner = 0, item = 0;
            if (!idOf(a[0], owner) || !idOf(a[1], item) || item > 0xFFFFFFFFull)
                return fail(BindingStatus::BadArgument, "not an id");
            BindingResult r = ok();
            r.value = integer(static_cast<std::int32_t>(self->chance(owner, static_cast<FormId>(item), 1) + 0.5f));
            return r;
        });
        add("pickup", s2, false, BindingType::Bool, BindingAccess::Mutate,
            [self, idOf](std::span<const BindingValue> a) {
                std::uint64_t placement = 0, base = 0;
                if (!idOf(a[0], placement) || !idOf(a[1], base) || placement > 0xFFFFFFFFull || base > 0xFFFFFFFFull)
                    return fail(BindingStatus::BadArgument, "not an id");
                (void)self->pickup(static_cast<FormId>(placement), static_cast<FormId>(base));
                return ok();
            });
        add("take", s3, false, BindingType::Bool, BindingAccess::Mutate, [self, idOf](std::span<const BindingValue> a) {
            std::uint64_t owner = 0, item = 0;
            if (!idOf(a[0], owner) || !idOf(a[1], item) || item > 0xFFFFFFFFull)
                return fail(BindingStatus::BadArgument, "not an id");
            (void)self->take(owner, static_cast<FormId>(item), a[2].integer);
            return ok();
        });

        add("plant", s3, false, BindingType::Bool, BindingAccess::Mutate, [self, idOf](std::span<const BindingValue> a) {
            std::uint64_t owner = 0, item = 0;
            if (!idOf(a[0], owner) || !idOf(a[1], item) || item > 0xFFFFFFFFull)
                return fail(BindingStatus::BadArgument, "not an id");
            (void)self->plant(owner, static_cast<FormId>(item), a[2].integer);
            return ok();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.pickup";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
