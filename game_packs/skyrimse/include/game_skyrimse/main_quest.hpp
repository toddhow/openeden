#pragma once

#include <cstdint>

#include <game_skyrimse/records.hpp>

// Task 0268 (M-S8, the main quest opening at Helgen). The pack setting `main quest start` makes a new game begin the
// way Skyrim does: on the cart. Nothing of the opening is hand-written; the game's own quest MQ101 ("Unbound"), its
// scene MQ101Scene1 (begins on quest start), aliases and Papyrus fragments run it. Pure constants and helpers, no
// state.

namespace game_skyrimse::mainquest
{
    /// `[extension.skyrimse] main quest start` (default false): a new game starts at the cart and MQ101 is started.
    inline constexpr const char* kMainQuestStartKey = "main quest start";

    /// MQ101 "Unbound" (Skyrim.esm 0x03372B, VERIFIED by the data: name Unbound, start-up stage 0).
    inline constexpr FormId kUnboundQuest = 0x0003372B;

    /// How far from the player (units, in the plane) a script's MoveTo makes an actor or an object again (the loaded
    /// area; ours).
    inline constexpr double kRelocateRange = 7000.0;

    /// Task 0274: the seconds from a cart rider's exit idle to the animation event "ExitCartEnd" that the game's
    /// scripts wait for (OURS: the exit clips are not played; the rider is stood beside the cart at once).
    inline constexpr double kCartExitSeconds = 3.0;

    /// The `--start` text of the cart: the world Tamriel and the place of MQ101PlayerStartMarker (REFR 0x04678E,
    /// position 22499.803, -94862.59, 13905.831, VERIFIED by the data). The quest itself moves the player to that
    /// marker (stage 10); this spot only has to be in the right cell so the exterior around Helgen loads.
    inline constexpr const char* kHelgenStart = "@Tamriel,22499.8,-94862.6,13905.8";
}
