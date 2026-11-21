#pragma once

// Task 0302 (draugr ambushes): the rule that wakes a scripted sleeper. Skyrim's ambush draugr lie in a coffin or an
// alcove under a package that sleeps while their actor value Variable01 is 0, with Aggression 0; their script
// (masterAmbushScript) waits for the engine's OnGetUp (or a hit, or combat) and then lets them out: it sets Variable01
// to 1 and Aggression to 2 and activates the furniture it is linked to. Nothing here knows that script. The pack asks
// two questions only: which sleepers have a script that handles OnGetUp, and has the player come near one. The answer
// to the second is OUR rule (the data has none): the player within kWakeDistance horizontally and kWakeHeight in height
// of the bed. All pure: no clock, no random numbers.
#include <openeden/game/actor_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

namespace game_skyrimse::ambush
{
    /// OURS: how near the player comes before a scripted sleeper is woken (units, 7 metres: a corridor or an alcove's
    /// own room), and how far above or below. Tuned by looking at Bleak Falls Barrow, not read from the data.
    inline constexpr double kWakeDistance = 512.0;
    inline constexpr double kWakeHeight = 192.0;
    /// At most this many sleepers wake in one update (a crypt's whole row is released over several frames).
    inline constexpr std::size_t kMaxWakesPerUpdate = 8;
    /// The woken placements remembered (a sleeper wakes once; the script goes to its next state and stays there).
    inline constexpr std::size_t kMaxRemembered = 4096;

    struct Rules
    {
        double mDistance = kWakeDistance;
        double mHeight = kWakeHeight;
    };

    struct Sleeper
    {
        openeden::sdk::game::WorldId mReference = openeden::sdk::game::kNoWorldId;
        double mX = 0.0, mY = 0.0, mZ = 0.0;
    };

    struct Player
    {
        bool mKnown = false;
        double mX = 0.0, mY = 0.0, mZ = 0.0;
    };

    /// Is the player within the wake range of the sleeper (an unknown or non-finite place is never in range)?
    [[nodiscard]] bool inRange(const Sleeper& sleeper, const Player& player, const Rules& rules);

    /// A script's actor value for Aggression as the combat AI's number: rounded, cut to 0..3, NaN is 0.
    [[nodiscard]] std::uint8_t aggressionFromValue(double value);

    /// The sleepers that wake: each placement once, in ascending placement order, at most kMaxWakesPerUpdate a call.
    class Watch
    {
    public:
        explicit Watch(Rules rules = Rules())
            : mRules(rules)
        {
        }

        /// Appends to `wake` the placements of `sleepers` (any order) that are in range and were not woken before, and
        /// remembers them. Returns how many were appended.
        std::size_t update(
            std::span<const Sleeper> sleepers, const Player& player, std::vector<openeden::sdk::game::WorldId>& wake);
        /// A woken placement may wake again (a load, or the actor lay down again).
        void forget(openeden::sdk::game::WorldId reference) { mWoken.erase(reference); }
        void clear() { mWoken.clear(); }
        [[nodiscard]] bool woken(openeden::sdk::game::WorldId reference) const { return mWoken.count(reference) != 0; }
        [[nodiscard]] std::size_t wokenCount() const { return mWoken.size(); }
        [[nodiscard]] const Rules& rules() const { return mRules; }

    private:
        Rules mRules;
        std::set<openeden::sdk::game::WorldId> mWoken;
    };
}
