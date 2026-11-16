#include "game_skyrimse/barter_service.hpp"

#include <algorithm>

// The barter session and its bindings: see the header (task 0189).

namespace game_skyrimse::barter
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

        const char* stateName(BarterService::State state)
        {
            switch (state)
            {
                case BarterService::State::None:
                    return "none";
                case BarterService::State::Pending:
                    return "pending";
                case BarterService::State::Ready:
                    return "ready";
                case BarterService::State::Refused:
                    return "refused";
            }
            return "none";
        }

        // What the window shows after an attempt (buying: the merchant's stock is short; selling: the player's is).
        std::string resultText(TradeStatus status, bool buying)
        {
            switch (status)
            {
                case TradeStatus::Ok:
                    return "ok";
                case TradeStatus::UnknownOwner:
                    return "no merchant";
                case TradeStatus::BadCount:
                    return "bad count";
                case TradeStatus::UnknownItem:
                    return "unknown item";
                case TradeStatus::NotTradable:
                    return "that cannot be traded";
                case TradeStatus::WontBuy:
                    return "the merchant does not buy that";
                case TradeStatus::Equipped:
                    return "that is equipped";
                case TradeStatus::NotEnoughItems:
                    return buying ? "the merchant has not got that many" : "you do not have that many";
                case TradeStatus::NotEnoughGold:
                    return "not enough gold";
                case TradeStatus::NotEnoughVendorGold:
                    return "the merchant cannot afford that";
            }
            return "failed";
        }
    }

    struct BarterService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; // result buffer: valid until the handler is called again
    };

    BarterService::BarterService(inventory::InventoryService& inventory, ServiceHooks hooks, BarterSettings settings)
        : mImpl(std::make_unique<Impl>())
        , mInventory(inventory)
        , mHooks(std::move(hooks))
        , mSettings(settings)
    {
    }

    BarterService::~BarterService() = default;

    void BarterService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    float BarterService::speech() const
    {
        return mHooks.mSpeech ? mHooks.mSpeech() : 15.f;
    }

    void BarterService::refuse(std::string reason)
    {
        mState = State::Refused;
        mReason = std::move(reason);
        mOwner = 0;
        log("BARTER refused for " + mName + " (npc " + inventory::formatId(mNpcBase) + "): " + mReason);
    }

    void BarterService::open(std::uint64_t placement, FormId npcBase, std::string name)
    {
        mState = State::Pending;
        mReason.clear();
        mLastResult.clear();
        mName = std::move(name);
        mPlacement = placement;
        mNpcBase = npcBase;
        mOwner = 0;
        mVendor = VendorInfo();
        mPolls = 0;
        tryStart();
    }

    bool BarterService::tryStart()
    {
        if (!mHooks.mVendorOf)
        {
            refuse("barter is not wired");
            return true;
        }
        const VendorLookup found = mHooks.mVendorOf(mNpcBase, mVendor);
        if (found == VendorLookup::NotVendor)
        {
            refuse("not a vendor");
            return true;
        }
        if (found == VendorLookup::Pending)
            return false;
        // the vendor chest becomes an owner of the inventory model (its stock and gold are its contents)
        const std::uint64_t owner = mVendor.mChestRef;
        if (!mInventory.open(owner, mVendor.mChestBase, "container"))
        {
            if (!mInventory.ready())
                return false; // the item table is still being read
            refuse("the vendor's chest is unknown");
            return true;
        }
        mOwner = owner;
        mState = State::Ready;
        log("BARTER open " + mName + " (npc " + inventory::formatId(mNpcBase) + ", placement "
            + inventory::formatId(mPlacement) + "): chest " + inventory::formatId(mOwner) + ", "
            + std::to_string(mInventory.model().stacks(mOwner).size()) + " stacks, gold "
            + std::to_string(vendorGold()) + ", speech " + std::to_string(speech()) + ", list of "
            + std::to_string(mVendor.mKeywords.size()) + " keywords"
            + (mVendor.mListIsExclusion ? " (exclusion)" : ""));
        return true;
    }

    void BarterService::poll()
    {
        if (mState != State::Pending)
            return;
        ++mPolls;
        if (tryStart())
            return;
        if (mPolls >= kMaxPendingPolls)
            refuse("the records were not ready");
    }

    void BarterService::close()
    {
        if (mState != State::None)
            log("BARTER close " + mName + ", " + std::to_string(mTrades) + " trades");
        mState = State::None;
        mOwner = 0;
        mReason.clear();
        mVendor = VendorInfo();
    }

    std::int64_t BarterService::vendorGold() const
    {
        return mState == State::Ready ? mInventory.model().count(mOwner, inventory::kGoldItem) : 0;
    }

    std::int32_t BarterService::buyPriceOf(FormId item) const
    {
        const inventory::ItemStat* stat = mInventory.table().item(item);
        if (stat == nullptr || item == inventory::kGoldItem)
            return 0;
        return buyPrice(stat->mValue, speech(), mSettings);
    }

    std::int32_t BarterService::sellPriceOf(FormId item) const
    {
        const inventory::ItemStat* stat = mInventory.table().item(item);
        if (stat == nullptr || mState != State::Ready || !vendorBuys(mVendor, item, *stat))
            return 0;
        return sellPrice(stat->mValue, speech(), mSettings);
    }

    TradeStatus BarterService::buy(FormId item, std::int32_t count)
    {
        ++mTrades;
        if (mState != State::Ready)
        {
            mLastResult = "no merchant";
            return TradeStatus::UnknownOwner;
        }
        const TradeResult r
            = barter::buy(mInventory.model(), mInventory.table(), mOwner, item, count, speech(), mSettings);
        mLastResult = resultText(r.mStatus, true);
        log("BARTER buy " + inventory::formatId(item) + " x" + std::to_string(count) + ": "
            + std::string(tradeStatusName(r.mStatus)) + ", unit " + std::to_string(r.mUnitPrice) + ", gold "
            + std::to_string(r.mTotal) + ", player gold "
            + std::to_string(mInventory.model().count(inventory::kPlayerOwner, inventory::kGoldItem))
            + ", vendor gold " + std::to_string(vendorGold()));
        return r.mStatus;
    }

    TradeStatus BarterService::sell(FormId item, std::int32_t count)
    {
        ++mTrades;
        if (mState != State::Ready)
        {
            mLastResult = "no merchant";
            return TradeStatus::UnknownOwner;
        }
        const TradeResult r
            = barter::sell(mInventory.model(), mInventory.table(), mVendor, mOwner, item, count, speech(), mSettings);
        mLastResult = resultText(r.mStatus, false);
        log("BARTER sell " + inventory::formatId(item) + " x" + std::to_string(count) + ": "
            + std::string(tradeStatusName(r.mStatus)) + ", unit " + std::to_string(r.mUnitPrice) + ", gold "
            + std::to_string(r.mTotal) + ", player gold "
            + std::to_string(mInventory.model().count(inventory::kPlayerOwner, inventory::kGoldItem))
            + ", vendor gold " + std::to_string(vendorGold()));
        return r.mStatus;
    }

    openeden::sdk::DeclareResult BarterService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        BarterService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType s3[] = { BindingType::String, BindingType::String, BindingType::String };
        static const BindingType tradeParams[] = { BindingType::String, BindingType::Int32 };
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
        const auto itemOf = [](const BindingValue& v, FormId& out) {
            std::uint64_t id = 0;
            if (!inventory::parseId(v.text, id) || id > 0xFFFFFFFFull)
                return false;
            out = static_cast<FormId>(id);
            return true;
        };
        const auto stringFn = [&](std::string_view name, std::function<std::string(const BarterService&)> get) {
            add(name, {}, true, kStr, BindingAccess::Read,
                [self, &m, get = std::move(get)](std::span<const BindingValue>) {
                    m.mText = get(*self);
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto clampInt = [](std::int64_t v) {
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, 0, 0x7FFFFFFF));
        };

        stringFn("state", [](const BarterService& s) { return std::string(stateName(s.state())); });
        stringFn("reason", [](const BarterService& s) { return s.reason(); });
        stringFn("owner", [](const BarterService& s) {
            return s.state() == State::Ready ? inventory::formatId(s.owner()) : std::string();
        });
        stringFn("last_result", [](const BarterService& s) { return s.lastResult(); });
        add("vendor_gold", {}, true, BindingType::Int32, BindingAccess::Read,
            [self, clampInt](std::span<const BindingValue>) {
                BindingResult r = ok();
                r.value = integer(clampInt(self->vendorGold()));
                return r;
            });
        add("trades", {}, true, BindingType::Int32, BindingAccess::Read,
            [self, clampInt](std::span<const BindingValue>) {
                BindingResult r = ok();
                r.value = integer(clampInt(self->trades()));
                return r;
            });
        add("buy_price", s1, true, BindingType::Int32, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                BindingResult r = ok();
                r.value = integer(self->buyPriceOf(item));
                return r;
            });
        add("sell_price", s1, true, BindingType::Int32, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                BindingResult r = ok();
                r.value = integer(self->sellPriceOf(item));
                return r;
            });
        add("sellable", s1, true, BindingType::Int32, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                BindingResult r = ok();
                r.value = integer(sellableCount(self->mInventory.model(), item));
                return r;
            });
        // ---- Mutate: no result; a refusal is logged and shown through last_result
        add("open", s3, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            std::uint64_t placement = 0, base = 0;
            if (!inventory::parseId(a[0].text, placement) || !inventory::parseId(a[1].text, base)
                || base > 0xFFFFFFFFull)
            {
                self->log("BARTER open refused: bad id");
                return ok();
            }
            self->open(placement, static_cast<FormId>(base), std::string(a[2].text));
            return ok();
        });
        const auto trade = [&](std::string_view name, bool buying) {
            add(name, tradeParams, false, BindingType::Bool, BindingAccess::Mutate,
                [self, itemOf, buying](std::span<const BindingValue> a) {
                    FormId item = 0;
                    if (!itemOf(a[0], item))
                    {
                        self->log("BARTER trade refused: bad id");
                        return ok();
                    }
                    if (buying)
                        (void)self->buy(item, a[1].integer);
                    else
                        (void)self->sell(item, a[1].integer);
                    return ok();
                });
        };
        trade("buy", true);
        trade("sell", false);
        add("close", {}, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue>) {
            self->close();
            return ok();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.barter";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
