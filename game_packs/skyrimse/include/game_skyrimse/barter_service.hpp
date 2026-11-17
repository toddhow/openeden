#ifndef OPENEDEN_GAME_SKYRIMSE_BARTER_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_BARTER_SERVICE_H

// The barter session of a Skyrim SE game and its script bindings (task 0189, milestone M-S6).
//
// `BarterService` sits on top of the `InventoryService` of task 0182. A session is opened for one merchant (the NPC the
// player talked to): the pack finds its vendor faction, registers the vendor chest as an owner of the inventory model
// and from then on a trade is `barter::buy` / `barter::sell` over that model. Ids cross the binding as text exactly as
// in the inventory namespace (an item is 8 hex digits, an owner is the chest's hex id).
//
// Namespace skyrimse.barter 1.0 (Read unless marked):
//   state() -> string                 "none", "pending" (the data is being read), "ready" or "refused"
//   reason() -> string                why a session was refused ("" otherwise)
//   owner() -> string                 the vendor chest's id (hex) once ready, else ""
//   vendor_gold() -> int              Gold001 in the vendor chest
//   buy_price(item) -> int            the player's price per unit (0: not tradable)
//   sell_price(item) -> int           what the vendor pays per unit (0: it will not buy this item)
//   sellable(item) -> int             how many of the item the player may sell (held, less an equipped copy)
//   last_result() -> string           the outcome of the last buy or sell ("" before the first)
//   trades() -> int                   counts every attempt, so a window can show a changed status line
//   open(placement, npc_base, name)  [Mutate]  starts a session for a merchant
//   buy(item, count), sell(item, count)  [Mutate]
//   close()                          [Mutate]  ends the session (the chest keeps what was traded)
// The Mutate functions are queued by the core and run on the main thread in call order. Nothing here waits: a session
// that needs data that is not read yet stays `pending` and is retried every frame by `poll()`.

#include <game_skyrimse/barter.hpp>
#include <game_skyrimse/inventory_service.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>

namespace game_skyrimse
{
    namespace barter
    {
        struct ServiceHooks
        {
            /// Finds the vendor data of an NPC_ base (Ready, Pending or NotVendor).
            std::function<VendorLookup(FormId npcBase, VendorInfo& out)> mVendorOf;
            /// The player's Speechcraft.
            std::function<float()> mSpeech;
            /// One line per open, refusal and trade. May be empty.
            std::function<void(const std::string&)> mLog;
            /// Task 0200: a trade went through; the gold it moved (trains the Speech skill). May be empty.
            std::function<void(float gold)> mTraded;
            /// Task 0204 (crime): how many of an item the player holds are stolen (0 when none), and told when a fence
            /// bought `count` of them. May be empty.
            std::function<std::int32_t(FormId item)> mStolenHeld;
            std::function<void(FormId item, std::int32_t count)> mStolenSold;
        };

        class BarterService
        {
        public:
            enum class State : std::uint8_t
            {
                None,
                Pending,
                Ready,
                Refused,
            };
            /// A session still pending after this many polls is refused ("the records were not ready").
            static constexpr std::uint32_t kMaxPendingPolls = 3000;

            BarterService(inventory::InventoryService& inventory, ServiceHooks hooks, BarterSettings settings = {});
            ~BarterService();
            BarterService(const BarterService&) = delete;
            BarterService& operator=(const BarterService&) = delete;

            /// Starts a session (replacing any); tries at once, otherwise `poll()` goes on. Main thread.
            void open(std::uint64_t placement, FormId npcBase, std::string name);
            /// Once a frame: retries a pending session. Never waits.
            void poll();
            void close();

            [[nodiscard]] State state() const { return mState; }
            [[nodiscard]] const std::string& reason() const { return mReason; }
            [[nodiscard]] std::uint64_t owner() const { return mState == State::Ready ? mOwner : 0; }
            [[nodiscard]] const VendorInfo& vendor() const { return mVendor; }
            [[nodiscard]] const std::string& vendorName() const { return mName; }

            TradeStatus buy(FormId item, std::int32_t count);
            TradeStatus sell(FormId item, std::int32_t count);

            [[nodiscard]] std::int32_t buyPriceOf(FormId item) const;
            [[nodiscard]] std::int32_t sellPriceOf(FormId item) const;
            [[nodiscard]] std::int64_t vendorGold() const;
            [[nodiscard]] std::uint32_t trades() const { return mTrades; }
            [[nodiscard]] const std::string& lastResult() const { return mLastResult; }
            [[nodiscard]] float speech() const;

            [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        private:
            struct Impl;
            bool tryStart();
            void refuse(std::string reason);
            void log(const std::string& line) const;

            std::unique_ptr<Impl> mImpl;
            inventory::InventoryService& mInventory;
            ServiceHooks mHooks;
            BarterSettings mSettings;
            State mState = State::None;
            std::string mReason;
            std::string mName;
            std::string mLastResult;
            std::uint64_t mPlacement = 0;
            FormId mNpcBase = kNullFormId;
            std::uint64_t mOwner = 0;
            VendorInfo mVendor;
            std::uint32_t mPolls = 0;
            std::uint32_t mTrades = 0;
        };
    }
}

#endif
