#ifndef OPENEDEN_GAME_SKYRIMSE_HARVEST_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_HARVEST_SERVICE_H

// Harvesting flora in a game (task 0221): the rules of harvest.hpp over the game's hooks, the save chunk "harvest", and
// the script bindings namespace "skyrimse.harvest" (version 1.0). Ids cross as text (8 lower-case hex digits), like the
// inventory's.
//
// Namespace skyrimse.harvest 1.0 (Read unless marked):
//   state() -> string            "" (nothing yet), "harvested", "nothing" (the roll failed), "already" or "refused"
//   message() -> string          the line to show after the last try
//   revision() -> int            changes after every try and every regrowth
//   prompt(base) -> string       "Harvest Canis Root" (empty when the base is not harvestable)
//   harvest(placement, base) [Mutate]    the player harvests a placed plant
//   set_clock(seconds, month) [Mutate]   the game time and the calendar month 0 to 11 (-1: unknown); plants grown back
//
// Threads: main thread (the host calls the Mutate functions and the chunk there).
#include <game_skyrimse/harvest.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>

namespace game_skyrimse::harvest
{
    /// What the data says of a harvestable base.
    struct PlantInfo
    {
        FormId mIngredient = kNullFormId; ///< PFIG: an ingredient or a leveled list
        std::array<std::uint8_t, 4> mChances{}; ///< PFPC: spring, summer, fall, winter percent
        std::string mActivateText; ///< RNAM ("Harvest", "Pick", "Catch"); empty for a TREE
        std::string mName; ///< the display name
        bool mHide = true; ///< the plant is hidden while harvested
    };

    struct Hooks
    {
        /// The plant data of a base; false when the base is not a harvestable FLOR or TREE.
        std::function<bool(FormId base, PlantInfo& out)> mPlant;
        /// Resolves the ingredient (a leveled list by the seeded pick) to one item and its count; false when none.
        std::function<bool(FormId ingredient, std::uint64_t seed, FormId& item, std::int32_t& count)> mPick;
        std::function<bool(FormId item, std::int32_t count)> mAddToPlayer;
        std::function<std::string(FormId item)> mItemName;
        /// The placement leaves the world / comes back.
        std::function<bool(FormId placement)> mHide;
        std::function<bool(FormId placement)> mShow;
        std::function<void(const std::string&)> mLog;
    };

    enum class HarvestStatus : std::uint8_t
    {
        Ok,
        NotHarvestable,
        AlreadyHarvested,
        OutOfSeason, ///< the chance of this season is 0
        Nothing, ///< the roll failed: the plant is empty until it grows back
        Refused, ///< a hook is missing or the player could not receive it
    };

    class HarvestService
    {
    public:
        explicit HarvestService(Hooks hooks);
        ~HarvestService();
        HarvestService(const HarvestService&) = delete;
        HarvestService& operator=(const HarvestService&) = delete;

        [[nodiscard]] HarvestBook& book() { return mBook; }
        [[nodiscard]] HarvestChunk& chunk() { return mChunk; }

        HarvestStatus harvest(FormId placement, FormId base);
        /// The game time (seconds, negative: not known) and month 0 to 11 (-1: not known). Plants whose time has come
        /// grow back (mShow). Returns how many.
        std::size_t setClock(double gameSeconds, int month);
        [[nodiscard]] double now() const { return mNow; }
        [[nodiscard]] std::string promptOf(FormId base) const;

        [[nodiscard]] const std::string& state() const { return mState; }
        [[nodiscard]] const std::string& message() const { return mMessage; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision + mBook.revision(); }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        void note(const std::string& line);
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        HarvestBook mBook;
        HarvestChunk mChunk;
        std::string mState;
        std::string mMessage;
        double mNow = -1.0;
        int mMonth = -1;
        std::uint32_t mRevision = 0;
    };
}

#endif
