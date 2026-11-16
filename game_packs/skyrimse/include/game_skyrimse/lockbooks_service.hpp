#ifndef OPENEDEN_GAME_SKYRIMSE_LOCKBOOKS_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_LOCKBOOKS_SERVICE_H

// Lock picking and books as a service of the pack (task 0203, milestone M-S6): the minigame's attempt, the book index
// (read on a worker, never waited for), the effects of reading, the save chunk `lockbooks` and the script namespace
// `skyrimse.lockbooks` 1.0. Sits on the InventoryService (lockpicks and tomes are items of the player's list).
//
// Namespace skyrimse.lockbooks (Read unless marked; items are 8 hex digits, a placement is hex text):
//   ready() -> bool, revision() -> int, picks() -> int (Lockpicks held)
//   lock_state() -> "none", "picking", "opened", "broke", "cancelled", "refused";  lock_reason() -> string
//   lock_tier() -> string, lock_level() -> int, lock_progress() -> double, lock_strain() -> double,
//   lock_window() -> double (degrees), lock_placement() -> string
//   lock_picked(placement) -> bool
//   lock_serial() -> int               counts every pick_begin (also a refused one): a window waits for it to move
//   book_known(item) -> bool, book_title(item) -> string, book_kind(item) -> "book" | "skill" | "spell",
//   book_teaches(item) -> string (a skill's or a spell's name, "" for a plain book), book_pages(item) -> string[],
//   book_result(item) -> string (what reading did, "" before it was read), book_read(item) -> bool,
//   spells() -> string[]
//   pick_begin(placement, level)  [Mutate]  starts (or restarts) a picking attempt with a new Lockpick
//   pick_input(angle, turning, dt_ms)  [Mutate]  one step of the minigame
//   pick_cancel()  [Mutate]
//   read_book(item)  [Mutate]  reads: shows, and the first reading of a skill book or spell tome does its effect
// The Mutate functions are queued by the core and run on the main thread in call order. Nothing waits.

#include <game_skyrimse/inventory_service.hpp>
#include <game_skyrimse/lockbooks.hpp>

#include <openeden/game/bindings.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace game_skyrimse
{
    namespace lockbooks
    {
        /// MISC Lockpick (Skyrim.esm 0x0A).
        inline constexpr FormId kLockpickItem = 0x0000000A;

        struct BookInputs
        {
            std::vector<std::filesystem::path> mPaths;
            LoadOrder mOrder;
        };

        struct ServiceHooks
        {
            /// Fills the inputs once the world view is built (false: ask again).
            std::function<bool(BookInputs& out)> mInputs;
            /// The text of a localised string of plugin `plugin` (an index into mPaths); empty when not found.
            std::function<std::string(const LString& text, std::uint32_t plugin, StringTableKind kind)> mResolve;
            /// The display name of an item (the inventory's), the fallback of a book's title.
            std::function<std::string(FormId)> mName;
            /// The player's Lockpicking skill.
            std::function<float()> mSkill;
            /// A use of a skill (slot, amount): the progression's skillUsed.
            std::function<void(std::size_t slot, float amount)> mSkillUsed;
            /// Raises a skill by one level; false when it cannot (the progression is off, the skill is at 100).
            std::function<bool(std::size_t slot)> mRaiseSkill;
            std::function<std::string(std::size_t slot)> mSkillName;
            /// The name of a spell ("" when unknown) and the call that makes it the player's.
            std::function<std::string(FormId spell)> mSpellName;
            std::function<void(FormId spell)> mSpellLearned;
            std::function<void(const std::string&)> mLog;
        };

        class LockBooksService
        {
        public:
            enum class ReadStatus : std::uint8_t
            {
                Ok,
                NotReady,
                NotABook, ///< not a BOOK record
                NotHeld,
            };

            LockBooksService(inventory::InventoryService& inventory, ServiceHooks hooks, PickSettings settings = {});
            ~LockBooksService();
            LockBooksService(const LockBooksService&) = delete;
            LockBooksService& operator=(const LockBooksService&) = delete;

            /// Starts the worker when the inputs are ready, adopts its result; true once ready. Never blocks.
            bool poll();
            [[nodiscard]] bool ready() const { return mReady; }

            [[nodiscard]] LockBooksModel& model() { return mModel; }
            [[nodiscard]] LockBooksChunk& chunk() { return mChunk; }
            [[nodiscard]] const BookIndex& books() const { return mBooks; }
            [[nodiscard]] const Attempt& attempt() const { return mAttempt; }
            [[nodiscard]] const std::string& reason() const { return mReason; }
            [[nodiscard]] std::int32_t serial() const { return mSerial; }
            void logLine(const std::string& line) const
            {
                if (mHooks.mLog)
                    mHooks.mLog(line);
            }

            // ---- locks
            /// Starts an attempt with a Lockpick of the player's. False with `reason()` when refused (no lockpick, a
            /// lock that needs its key, an unknown level, a service that is not ready).
            bool pickBegin(std::uint64_t placement, std::int32_t level);
            void pickInput(float angle, bool turning, float dt);
            void pickCancel();
            [[nodiscard]] std::int32_t picks() const;
            [[nodiscard]] bool lockPicked(std::uint64_t placement) const { return mModel.isPicked(placement); }

            // ---- books
            ReadStatus readBook(FormId item);
            /// Task 0234: reads a book lying in the world: the effect of the first reading is the inventory read's, but
            /// the book need not be held and a tome is not consumed (it is not in the inventory).
            ReadStatus readWorldBook(FormId item);
            /// Null when not a book, or the index is not ready.
            [[nodiscard]] const BookRecord* book(FormId item) const;
            [[nodiscard]] std::string bookTitle(FormId item);
            [[nodiscard]] const std::vector<std::string>& bookPages(FormId item);
            [[nodiscard]] std::string bookTeaches(FormId item);
            [[nodiscard]] const std::string& bookResult(FormId item) const;

            [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        private:
            ReadStatus readImpl(FormId item, bool held);
            struct Impl;
            std::unique_ptr<Impl> mImpl;
            inventory::InventoryService& mInventory;
            ServiceHooks mHooks;
            PickSettings mSettings;
            LockBooksModel mModel;
            LockBooksChunk mChunk;
            BookIndex mBooks;
            Attempt mAttempt;
            std::string mReason;
            std::int32_t mSerial = 0;
            bool mReady = false;
            std::atomic<int> mState{ 0 };
            std::thread mThread;
            BookIndex mBuilt;
        };
    }
}

#endif
