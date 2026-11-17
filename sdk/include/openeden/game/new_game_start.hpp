#pragma once

#include <string>

#include <openeden/version.hpp>

// Where a new game of this game's session starts (task 0268, the Skyrim main quest opening at Helgen). A neutral
// session starts where `--start` says; a game that has its own opening (Skyrim: the cart at Helgen) says so here and the
// core uses it instead. The text is the `--start` text: an interior cell's editor id, or `@<world editor id>,<x>,<y>[,<z>]`.
//
// ABI choice: like DisplayNameSource, a NEW interface found by dynamic_cast on the game's registered WorldCellSource, so
// the frozen WorldCellSource is untouched. A pack that does not implement it starts where `--start` says.
//
// Version policy: DRAFT 0.1. Threading: main thread only, when a new game starts. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kNewGameStartVersion{ 0, 1 };

    class NewGameStartSource
    {
    public:
        virtual ~NewGameStartSource() = default;

        /// The start place of a new game, or an empty text when the game has none of its own (the core then uses
        /// `--start`).
        [[nodiscard]] virtual std::string newGameStart() = 0;
    };
}
