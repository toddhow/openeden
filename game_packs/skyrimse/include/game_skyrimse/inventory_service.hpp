#ifndef OPENEDEN_GAME_SKYRIMSE_INVENTORY_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_INVENTORY_SERVICE_H

// The inventory of a Skyrim SE session and its script bindings (task 0182, milestone M-S6).
//
// `InventoryService` owns the item table (read on a worker, never waited for), the model and the save chunk, and
// declares the bindings namespace "skyrimse.inventory" (version 1.0). Ids cross the binding as TEXT: an item or a base
// is its load-order form id as 8 lower-case hex digits, an owner is "player" or the placement id as 1 to 16 hex digits.
// (A load-order id passes INT32_MAX for any plugin from the eighth on, so Int32 would be wrong, and a Handle cannot go
// through a script event.)
//
// Namespace skyrimse.inventory (Read unless marked):
//   ready() -> bool                    the item table is built (open and transfer need it)
//   revision() -> int                  changes on every change of any list; a window re-reads when it differs
//   known(owner) -> bool               the owner's contents are registered
//   items(owner) -> array of string    the owner's items, sorted by category, name, id
//   count(owner, item) -> int
//   name(item), category(item) -> string;  weight(item) -> double;  value(item) -> int
//   gold_item() -> string              the id of Gold001
//   carried() -> double, capacity() -> double   the player's carried weight and the base carry weight
//   open(owner, base, kind)  [Mutate]  registers a container ("container") or a corpse ("actor") from its base record
//   transfer(from, to, item, count) [Mutate]
//   take_all(from, to)       [Mutate]
// The Mutate functions are queued by the core and run on the main thread in call order; they log a refusal. Read
// functions allocate nothing that outlives the call except the pack's own reusable buffers.

#include <game_skyrimse/inventory.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/host.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace game_skyrimse
{
    namespace inventory
    {
        /// What the worker needs, copied out of the world view on the main thread once it is built.
        struct Inputs
        {
            std::vector<std::filesystem::path> mPaths;
            LoadOrder mOrder;
            std::vector<LeveledList> mLeveled; ///< LVLI of every plugin, load order (later wins in the table)
        };

        struct Hooks
        {
            /// Fills `out` and returns true when the world view is built; false means "ask again".
            std::function<bool(Inputs& out)> mInputs;
            /// FULL of a base or item form ("" when none).
            std::function<std::string(FormId)> mName;
            /// The CNTO entries (and outfit items) of an NPC_ base; false when unknown (not ready, not an NPC).
            std::function<bool(FormId base, std::vector<InventoryEntry>& out)> mNpcContents;
            /// Receives one line per refusal and per open (the pack logs it). May be empty.
            std::function<void(const std::string&)> mLog;
        };

        [[nodiscard]] std::string formatId(std::uint64_t id); ///< 8 lower-case hex digits (more when needed)
        [[nodiscard]] bool parseId(std::string_view text, std::uint64_t& out); ///< 1 to 16 hex digits
        [[nodiscard]] bool parseOwner(std::string_view text, std::uint64_t& out); ///< "player" or hex

        class InventoryService
        {
        public:
            InventoryService(Hooks hooks, float startingGold, std::uint16_t level);
            ~InventoryService();
            InventoryService(const InventoryService&) = delete;
            InventoryService& operator=(const InventoryService&) = delete;

            /// Starts the worker when the inputs are ready, adopts its result; true once the table is usable. Never
            /// blocks. Main thread.
            bool poll();
            [[nodiscard]] bool ready() const { return mReady; }

            [[nodiscard]] InventoryModel& model() { return mModel; }
            [[nodiscard]] const ItemTable& table() const { return mTable; }
            [[nodiscard]] InventoryChunk& chunk() { return mChunk; }

            /// Registers an owner from its base (see the namespace comment); false when not ready or unknown.
            bool open(std::uint64_t owner, FormId base, std::string_view kind);

            /// Declares the namespace. The service must outlive the host's use of the handlers.
            [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

            /// Test aids: the worker's stats.
            [[nodiscard]] std::uint64_t builtMillis() const { return mMillis; }

        private:
            struct Impl;
            std::unique_ptr<Impl> mImpl;
            Hooks mHooks;
            ItemTable mTable;
            InventoryModel mModel;
            InventoryChunk mChunk;
            std::uint16_t mLevel;
            std::atomic<int> mState{ 0 }; ///< 0 idle, 1 reading, 2 read
            std::thread mThread;
            ItemTable mBuilt;
            std::uint64_t mMillis = 0;
            bool mReady = false;
        };
    }
}

#endif
