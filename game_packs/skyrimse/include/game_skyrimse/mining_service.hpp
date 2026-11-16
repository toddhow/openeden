#ifndef OPENEDEN_GAME_SKYRIMSE_MINING_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_MINING_SERVICE_H

// Mining and woodcutting in a game (task 0224): the rules of mining.hpp over the game's hooks, the save chunk "mining",
// and the script bindings namespace "skyrimse.mining" (version 1.0). Ids cross as text (8 lower-case hex digits).
//
// Namespace skyrimse.mining 1.0 (Read unless marked):
//   state() -> string      "" (nothing yet), "mined", "chopped", "need tool", "depleted" or "refused"
//   message() -> string    the line to show after the last try
//   revision() -> int      changes after every try and every regrowth
//   activate(placement, base) [Mutate]    the player strikes a vein or uses the chopping block
//   set_clock(seconds) [Mutate]           the game time; depleted veins that are due fill again
//
// Threads: main thread (the host calls the Mutate functions and the chunk there).
#include <game_skyrimse/mining.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>

namespace game_skyrimse::mining
{
    struct Hooks
    {
        /// The resource data of a base, its display name and its activate text; false when the base is not a vein or a
        /// block.
        std::function<bool(FormId base, ResourceInfo& out, std::string& name, std::string& activateText)> mResource;
        /// How many items of the form list the player carries (the sum over the list's items); negative when the list
        /// cannot be read yet (the form lists come from the records worker): the try is refused for now, not a missing
        /// tool.
        std::function<int(FormId list)> mToolCount;
        /// Called once, at the first clock tick: starts the reading of the form lists ahead of the first strike.
        std::function<void()> mWarm;
        /// Resolves a leveled list by the seeded pick to one item and its count; false when none.
        std::function<bool(FormId leveled, std::uint64_t seed, FormId& item, std::int32_t& count)> mPick;
        std::function<bool(FormId item, std::int32_t count)> mAddToPlayer;
        std::function<std::string(FormId item)> mItemName;
        /// The text of a MESG record; empty when it cannot be read (the default line is used).
        std::function<std::string(FormId message)> mMessage;
        std::function<void(const std::string&)> mLog;
    };

    enum class MiningStatus : std::uint8_t
    {
        Ok,
        NotResource,
        NeedTool,
        Depleted,
        Refused, ///< a hook is missing or the player could not receive the item
    };

    class MiningService
    {
    public:
        explicit MiningService(Hooks hooks);
        ~MiningService();
        MiningService(const MiningService&) = delete;
        MiningService& operator=(const MiningService&) = delete;

        [[nodiscard]] VeinBook& book() { return mBook; }
        [[nodiscard]] MiningChunk& chunk() { return mChunk; }

        MiningStatus activate(FormId placement, FormId base);
        /// The game time (seconds, negative: not known). Depleted veins that are due fill again; returns how many.
        std::size_t setClock(double gameSeconds);
        [[nodiscard]] double now() const { return mNow; }
        /// The name the player sees over the placement: "Mine Iron Ore Vein", "Iron Ore Vein (depleted)"; empty for a
        /// base that is not a vein (a chopping block keeps its own name).
        [[nodiscard]] std::string nameOf(FormId placement, FormId base) const;

        [[nodiscard]] const std::string& state() const { return mState; }
        [[nodiscard]] const std::string& message() const { return mMessage; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision + mBook.revision(); }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        void note(const std::string& line);
        [[nodiscard]] std::string messageOr(FormId message, std::string_view fallback) const;
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        VeinBook mBook;
        MiningChunk mChunk;
        std::string mState;
        std::string mMessage;
        double mNow = -1.0;
        bool mWarmed = false;
        std::uint32_t mRevision = 0;
    };
}

#endif
