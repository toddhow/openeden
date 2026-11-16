#ifndef OPENEDEN_GAME_SKYRIMSE_PICKUP_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_PICKUP_SERVICE_H

// Picking up placed items and pickpocketing in a game (task 0208): the rules of pickup.hpp over the game's hooks, the
// save chunk "pickups", and the script bindings namespace "skyrimse.pickup" (version 1.0). Ids cross as text (8
// lower-case hex digits, a placement 1 to 16), like the inventory's.
//
// Namespace skyrimse.pickup 1.0 (Read unless marked):
//   state() -> string           "" (nothing yet), "taken", "success" (a pickpocket try), "caught" or "refused"
//   message() -> string         the line the window shows after the last try
//   revision() -> int           changes after every pickup or try
//   chance(owner, item) -> int  the pickpocket chance in percent for one unit (0 when unknown)
//   pickup(placement, base) [Mutate]   the player takes a placed item
//   take(owner, item, count) [Mutate]  a pickpocket try for `count` of an item of a living NPC's list
//
// Threads: main thread (the host calls the Mutate functions and the chunk there).

#include <game_skyrimse/pickup.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>

namespace game_skyrimse::pickup
{
    /// What the service needs of the game; a missing hook makes the matching call refuse.
    struct Hooks
    {
        /// An item's value and weight; false when the form is not an item the inventory knows.
        std::function<bool(FormId item, std::int32_t& value, float& weight)> mItem;
        /// The XCNT of a placement (0: none).
        std::function<std::int32_t(FormId placement)> mCount;
        /// The player takes `count` of `item` into the inventory.
        std::function<bool(FormId item, std::int32_t count)> mAddToPlayer;
        /// The placement leaves the world and stays gone.
        std::function<bool(FormId placement)> mDisable;
        /// The placement was activated (a script's OnActivate runs).
        std::function<void(FormId placement)> mActivated;
        /// Taking `count` of `item` out of this placement is theft when somebody else owns it.
        std::function<void(FormId placement, FormId item, std::int32_t count)> mTaken;
        /// How many of an item the NPC (a placement) holds, and moving them to the player.
        std::function<std::int32_t(std::uint64_t owner, FormId item)> mHeldBy;
        std::function<bool(std::uint64_t owner, FormId item, std::int32_t count)> mMoveToPlayer;
        /// The player's Pickpocket skill and its training.
        std::function<float()> mSkill;
        std::function<void(float amount)> mTrain;
        /// A try that was caught: the victim (a placement), the item and the count.
        std::function<void(std::uint64_t owner, FormId item, std::int32_t count)> mCaught;
        /// Task 0227: the victim has noticed the player (the sneak rules). An aware victim always catches the hand; the
        /// roll is still drawn so the seeded sequence does not change. Empty: nobody is aware.
        std::function<bool(std::uint64_t owner)> mAware;
        std::function<void(const std::string&)> mLog;
    };

    enum class PickupStatus : std::uint8_t
    {
        Ok,
        NotAnItem,
        AlreadyTaken,
        Full, ///< the set of taken placements is full
        Refused, ///< a hook is missing or the player could not receive it
    };

    enum class TryStatus : std::uint8_t
    {
        Success,
        Caught,
        BadCount, ///< zero, negative or above what the NPC holds
        Refused, ///< a hook is missing
    };

    class PickupService
    {
    public:
        PickupService(Hooks hooks, PickpocketSettings settings = PickpocketSettings{});
        ~PickupService();
        PickupService(const PickupService&) = delete;
        PickupService& operator=(const PickupService&) = delete;

        [[nodiscard]] PickupBook& book() { return mBook; }
        [[nodiscard]] PickupChunk& chunk() { return mChunk; }
        [[nodiscard]] const PickpocketSettings& settings() const { return mSettings; }

        PickupStatus pickup(FormId placement, FormId base);
        TryStatus take(std::uint64_t owner, FormId item, std::int32_t count);
        /// The pickpocket chance of `count` of an item, in percent (0 when the item is unknown or a hook is missing).
        [[nodiscard]] float chance(std::uint64_t owner, FormId item, std::int32_t count) const;

        [[nodiscard]] const std::string& state() const { return mState; }
        [[nodiscard]] const std::string& message() const { return mMessage; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        void note(const std::string& line);
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        PickpocketSettings mSettings;
        PickupBook mBook;
        PickupChunk mChunk;
        std::string mState;
        std::string mMessage;
        std::uint32_t mRevision = 0;
    };
}

#endif
