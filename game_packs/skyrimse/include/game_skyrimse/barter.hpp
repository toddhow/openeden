#ifndef OPENEDEN_GAME_SKYRIMSE_BARTER_H
#define OPENEDEN_GAME_SKYRIMSE_BARTER_H

// Skyrim SE barter: what a vendor buys, what things cost, and one trade (task 0189, milestone M-S6).
//
// Pure rules over the inventory model of task 0182; no engine type, no clock, no random number.
//
//  - A VENDOR is an NPC in a faction whose FACT has a buy list (VEND, an FLST of keywords) and a chest (VENC, a REFR of
//    a CONT). The chest holds the vendor's stock AND its gold (Gold001; the stock is the seeded pick of the chest's
//    CNTO and leveled lists, so it is the same on every run). The chest is an owner of the inventory model, so every
//    trade is an ordinary transfer and is saved with the inventory chunk.
//  - The vendor BUYS an item whose keywords include one of the list's (inverted when the faction says "buy/sell
//    everything not in the list"); it never buys gold or a worthless (value 0) item; it SELLS everything in its chest
//    except gold.
//  - PRICES. GMST values VERIFIED in Skyrim.esm: fBarterMin 2.0, fBarterMax 3.3, fMinBuyMult 1.05. The formula is
//    RECALLED-VERIFY and tunable (`BarterSettings`): haggle = clamp(speech, 0, 100) / 100;
//    ratio = fBarterMax - (fBarterMax - fBarterMin) * haggle (3.3 at Speech 0, 2.0 at 100);
//    the player PAYS value * max(fMinBuyMult, ratio / 2) and SELLS at value / ratio, each unit rounded to the nearest
//    whole gold and at least 1. No perk, disposition or merchant-gold multiplier.
//  - A TRADE validates everything first and then moves the item and the gold, or changes nothing. The player's equipped
//    copy of an item is not for sale.

#include <game_skyrimse/inventory.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    namespace barter
    {
        /// What the pack knows about a merchant: where its stock is and what it buys.
        struct VendorInfo
        {
            FormId mFaction = kNullFormId;
            FormId mChestRef = kNullFormId; ///< the REFR: the inventory owner id of the vendor's stock
            FormId mChestBase = kNullFormId; ///< the CONT the stock is read from
            std::vector<FormId> mKeywords; ///< the buy list (FLST entries)
            bool mListIsExclusion = false;
            bool mBuysStolen = false; ///< carried, not used (stolen goods are not tracked)
            std::uint16_t mStartHour = 0; ///< carried, not enforced
            std::uint16_t mEndHour = 24;
        };

        enum class VendorLookup : std::uint8_t
        {
            Ready,
            Pending, ///< the records are not read yet: ask again next frame
            NotVendor,
        };

        /// The GMST values the price formula uses (VERIFIED defaults; the formula itself is RECALLED-VERIFY).
        struct BarterSettings
        {
            float mBarterMin = 2.0f; ///< fBarterMin
            float mBarterMax = 3.3f; ///< fBarterMax
            float mMinBuyMult = 1.05f; ///< fMinBuyMult
        };

        /// fBarterMax - (fBarterMax - fBarterMin) * haggle; the speech is clamped to 0..100 (a non-finite one is 0).
        [[nodiscard]] float priceRatio(float speech, const BarterSettings& settings);
        /// What the player pays per unit of value, and what the player is given per unit of value.
        [[nodiscard]] float buyMultiplier(float speech, const BarterSettings& settings);
        [[nodiscard]] float sellMultiplier(float speech, const BarterSettings& settings);
        /// Whole-gold unit prices: 0 for an item of value 0 or below, else rounded to nearest and at least 1.
        [[nodiscard]] std::int32_t buyPrice(std::int32_t value, float speech, const BarterSettings& settings);
        [[nodiscard]] std::int32_t sellPrice(std::int32_t value, float speech, const BarterSettings& settings);

        /// Whether the vendor would buy this item from the player (keyword rule; never gold; never worthless).
        [[nodiscard]] bool vendorBuys(const VendorInfo& vendor, FormId item, const inventory::ItemStat& stat);

        enum class TradeStatus : std::uint8_t
        {
            Ok,
            UnknownOwner, ///< the vendor's chest is not registered
            BadCount, ///< zero, negative, or beyond kMaxStackCount
            UnknownItem, ///< no such item record
            NotTradable, ///< gold, or an item of no value
            WontBuy, ///< the vendor's list refuses it (selling only)
            Equipped, ///< selling: the player holds only the equipped copy
            NotEnoughItems,
            NotEnoughGold, ///< the player cannot pay (buying)
            NotEnoughVendorGold, ///< the vendor cannot pay (selling)
        };
        [[nodiscard]] std::string_view tradeStatusName(TradeStatus status);

        struct TradeResult
        {
            TradeStatus mStatus = TradeStatus::UnknownOwner;
            std::int32_t mUnitPrice = 0;
            std::int64_t mTotal = 0; ///< gold that changed hands
        };

        /// The player buys `count` of `item` from the vendor chest `vendorOwner`.
        [[nodiscard]] TradeResult buy(inventory::InventoryModel& model, const inventory::ItemTable& table,
            std::uint64_t vendorOwner, FormId item, std::int32_t count, float speech, const BarterSettings& settings);
        /// The player sells `count` of `item` to the vendor.
        [[nodiscard]] TradeResult sell(inventory::InventoryModel& model, const inventory::ItemTable& table,
            const VendorInfo& vendor, std::uint64_t vendorOwner, FormId item, std::int32_t count, float speech,
            const BarterSettings& settings);

        /// How many the player may sell: the held count, less one when the item is equipped.
        [[nodiscard]] std::int32_t sellableCount(const inventory::InventoryModel& model, FormId item);
    }
}

#endif
