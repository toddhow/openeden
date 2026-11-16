#ifndef OPENEDEN_GAME_SKYRIMSE_WORLDBOOKS_H
#define OPENEDEN_GAME_SKYRIMSE_WORLDBOOKS_H

// Books lying in the world (task 0234, milestone M-S6): the player activates a placed BOOK with the activate key and
// reads it where it lies; the book stays, a Take button moves it to the inventory through the pickup path (task 0208,
// theft included). Sits on the LockBooksService (task 0203): the same read-once set, so a skill book raises its skill
// once whether it was read in the world or in the inventory.
//
// What reading in place does (decisions in task 0234): a plain book or note only shows; a skill book (first time)
// raises
// its skill; a spell tome (first time) teaches its spell but is NOT consumed (it is not in the inventory); every
// reading tells the Papyrus host (OnRead) of the placement, so a note's script runs.
//
// Namespace skyrimse.worldbooks 1.0 (Read unless marked; an item is 8 hex digits, a placement hex text):
//   is_book(item) -> bool        the base is a BOOK the book index knows (false while the index is not ready)
//   can_take(item) -> bool       false for a book whose flag says it cannot be taken
//   read_count() -> int          readings so far (changes after every read)
//   read(placement, item) [Mutate]   reads the placed book: the effect of the first reading, then OnRead
// Threads: main thread (the host calls the Mutate function there).

#include <game_skyrimse/lockbooks_service.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>

namespace game_skyrimse::lockbooks
{
    struct WorldBooksHooks
    {
        /// A placed book was read: the Papyrus host sends OnRead to its instances.
        std::function<void(std::uint64_t placement)> mRead;
        std::function<void(const std::string&)> mLog;
    };

    class WorldBooksService
    {
    public:
        WorldBooksService(LockBooksService& books, WorldBooksHooks hooks);
        ~WorldBooksService();
        WorldBooksService(const WorldBooksService&) = delete;
        WorldBooksService& operator=(const WorldBooksService&) = delete;

        [[nodiscard]] bool isBook(FormId item) const;
        /// BOOK DATA flag 2 ("cannot be taken", VERIFIED on the five masters: 16 books, Dragonborn's).
        [[nodiscard]] bool canTake(FormId item) const;
        [[nodiscard]] std::uint32_t readCount() const { return mReadCount; }

        /// Reads the placed book `item`: false (nothing happens, nothing is told to the scripts) when the book index is
        /// not ready or the item is not a book.
        bool read(std::uint64_t placement, FormId item);

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        LockBooksService& mBooks;
        WorldBooksHooks mHooks;
        std::uint32_t mReadCount = 0;
    };
}

#endif
