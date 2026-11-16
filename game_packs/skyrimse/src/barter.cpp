#include "game_skyrimse/barter.hpp"

#include <algorithm>
#include <cmath>

// The rules are described in the header (task 0189).

namespace game_skyrimse::barter
{
    namespace
    {
        double clampedHaggle(float speech)
        {
            if (!std::isfinite(speech))
                return 0.0;
            return std::clamp(static_cast<double>(speech), 0.0, 100.0) / 100.0;
        }

        std::int32_t wholeGold(double value)
        {
            const double rounded = std::floor(value + 0.5);
            return static_cast<std::int32_t>(std::clamp(rounded, 1.0, 1.0e9));
        }

        // The price of one unit, 0 for an item that is worth nothing.
        std::int64_t gold(std::int64_t unit, std::int32_t count)
        {
            return unit * static_cast<std::int64_t>(count);
        }
    }

    float priceRatio(float speech, const BarterSettings& settings)
    {
        const double h = clampedHaggle(speech);
        const double high = settings.mBarterMax;
        return static_cast<float>(high - (high - settings.mBarterMin) * h);
    }

    float buyMultiplier(float speech, const BarterSettings& settings)
    {
        return std::max(settings.mMinBuyMult, priceRatio(speech, settings) / 2.f);
    }

    float sellMultiplier(float speech, const BarterSettings& settings)
    {
        const float ratio = priceRatio(speech, settings);
        return ratio > 0.f ? 1.f / ratio : 0.f;
    }

    std::int32_t buyPrice(std::int32_t value, float speech, const BarterSettings& settings)
    {
        if (value <= 0)
            return 0;
        return wholeGold(static_cast<double>(value) * buyMultiplier(speech, settings));
    }

    std::int32_t sellPrice(std::int32_t value, float speech, const BarterSettings& settings)
    {
        if (value <= 0)
            return 0;
        return wholeGold(static_cast<double>(value) * sellMultiplier(speech, settings));
    }

    bool vendorBuys(const VendorInfo& vendor, FormId item, const inventory::ItemStat& stat)
    {
        if (item == inventory::kGoldItem || stat.mValue <= 0)
            return false;
        bool listed = false;
        for (const FormId keyword : stat.mKeywords)
            if (std::find(vendor.mKeywords.begin(), vendor.mKeywords.end(), keyword) != vendor.mKeywords.end())
            {
                listed = true;
                break;
            }
        return vendor.mListIsExclusion ? !listed : listed;
    }

    std::string_view tradeStatusName(TradeStatus status)
    {
        switch (status)
        {
            case TradeStatus::Ok:
                return "ok";
            case TradeStatus::UnknownOwner:
                return "unknown vendor";
            case TradeStatus::BadCount:
                return "bad count";
            case TradeStatus::UnknownItem:
                return "unknown item";
            case TradeStatus::NotTradable:
                return "not tradable";
            case TradeStatus::WontBuy:
                return "the vendor will not buy it";
            case TradeStatus::Equipped:
                return "equipped";
            case TradeStatus::NotEnoughItems:
                return "not enough items";
            case TradeStatus::NotEnoughGold:
                return "not enough gold";
            case TradeStatus::NotEnoughVendorGold:
                return "the vendor has not enough gold";
        }
        return "unknown";
    }

    std::int32_t sellableCount(const inventory::InventoryModel& model, FormId item)
    {
        const std::int32_t held = model.count(inventory::kPlayerOwner, item);
        return std::max(0, held - (model.isEquipped(item) ? 1 : 0));
    }

    namespace
    {
        // Moves the item and then the gold; if the gold cannot move (it was checked, so only an overflow can stop it)
        // the item goes back. Returns false when nothing changed.
        bool settle(inventory::InventoryModel& model, std::uint64_t itemFrom, std::uint64_t itemTo, FormId item,
            std::int32_t count, std::int64_t total)
        {
            if (total > inventory::kMaxStackCount)
                return false;
            if (model.transfer(itemFrom, itemTo, item, count) != inventory::TransferStatus::Ok)
                return false;
            if (model.transfer(itemTo, itemFrom, inventory::kGoldItem, static_cast<std::int32_t>(total))
                != inventory::TransferStatus::Ok)
            {
                (void)model.transfer(itemTo, itemFrom, item, count);
                return false;
            }
            return true;
        }
    }

    TradeResult buy(inventory::InventoryModel& model, const inventory::ItemTable& table, std::uint64_t vendorOwner,
        FormId item, std::int32_t count, float speech, const BarterSettings& settings)
    {
        TradeResult r;
        if (!model.hasOwner(vendorOwner) || vendorOwner == inventory::kPlayerOwner)
            return r;
        if (count <= 0 || count > inventory::kMaxStackCount)
        {
            r.mStatus = TradeStatus::BadCount;
            return r;
        }
        const inventory::ItemStat* stat = table.item(item);
        if (stat == nullptr)
        {
            r.mStatus = TradeStatus::UnknownItem;
            return r;
        }
        const std::int32_t unit = buyPrice(stat->mValue, speech, settings);
        if (item == inventory::kGoldItem || unit <= 0)
        {
            r.mStatus = TradeStatus::NotTradable;
            return r;
        }
        if (model.count(vendorOwner, item) < count)
        {
            r.mStatus = TradeStatus::NotEnoughItems;
            return r;
        }
        const std::int64_t total = gold(unit, count);
        if (model.count(inventory::kPlayerOwner, inventory::kGoldItem) < total)
        {
            r.mStatus = TradeStatus::NotEnoughGold;
            return r;
        }
        if (!settle(model, vendorOwner, inventory::kPlayerOwner, item, count, total))
        {
            r.mStatus = TradeStatus::BadCount;
            return r;
        }
        r.mStatus = TradeStatus::Ok;
        r.mUnitPrice = unit;
        r.mTotal = total;
        return r;
    }

    TradeResult sell(inventory::InventoryModel& model, const inventory::ItemTable& table, const VendorInfo& vendor,
        std::uint64_t vendorOwner, FormId item, std::int32_t count, float speech, const BarterSettings& settings)
    {
        TradeResult r;
        if (!model.hasOwner(vendorOwner) || vendorOwner == inventory::kPlayerOwner)
            return r;
        if (count <= 0 || count > inventory::kMaxStackCount)
        {
            r.mStatus = TradeStatus::BadCount;
            return r;
        }
        const inventory::ItemStat* stat = table.item(item);
        if (stat == nullptr)
        {
            r.mStatus = TradeStatus::UnknownItem;
            return r;
        }
        const std::int32_t unit = sellPrice(stat->mValue, speech, settings);
        if (item == inventory::kGoldItem || unit <= 0)
        {
            r.mStatus = TradeStatus::NotTradable;
            return r;
        }
        if (!vendorBuys(vendor, item, *stat))
        {
            r.mStatus = TradeStatus::WontBuy;
            return r;
        }
        const std::int32_t held = model.count(inventory::kPlayerOwner, item);
        if (held < count)
        {
            r.mStatus = TradeStatus::NotEnoughItems;
            return r;
        }
        if (sellableCount(model, item) < count)
        {
            r.mStatus = TradeStatus::Equipped;
            return r;
        }
        const std::int64_t total = gold(unit, count);
        if (model.count(vendorOwner, inventory::kGoldItem) < total)
        {
            r.mStatus = TradeStatus::NotEnoughVendorGold;
            return r;
        }
        if (!settle(model, inventory::kPlayerOwner, vendorOwner, item, count, total))
        {
            r.mStatus = TradeStatus::BadCount;
            return r;
        }
        r.mStatus = TradeStatus::Ok;
        r.mUnitPrice = unit;
        r.mTotal = total;
        return r;
    }
}
