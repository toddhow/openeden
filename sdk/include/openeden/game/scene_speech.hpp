#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/actor_rules.hpp>
#include <openeden/version.hpp>

// Lines that a game's quest scenes make its actors say (task 0194, milestone M-S6 "scenes and more story events").
// The rules (which scene plays, which line, when) stay in the pack; only the actor, the subtitle text and the voice
// files cross. The core asks for the lines once a frame, plays each voice file through the sound system from the
// actor's head (one after the other) and shows the text as a subtitle.
//
// ABI choice: like DialogueSource, a NEW interface found by dynamic_cast on the game's registered ActorRules, so the
// frozen ActorRules / ActorRequestWriter (actor_rules.hpp) are untouched. A pack that does not implement it simply has
// scenes that are silent for the engine.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "nothing" default, a trailing field a
// consumer only reads) raise the minor number. Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kSceneSpeechVersion{ 0, 1 };

    /// One line an actor says.
    struct SceneSpeech
    {
        ActorHandle actor;
        /// The subtitle; may be empty (nothing is shown then).
        std::string text;
        /// The VFS path of each voice file in order, relative to the game's sound folder (the core prefixes "sound/"),
        /// exactly as DialogueTurn::voice. Empty when the pack has no voice for the line.
        std::vector<std::string> voice;
    };

    /// Implemented by an ActorRules whose game has scenes.
    class SceneSpeechSource
    {
    public:
        virtual ~SceneSpeechSource() = default;

        /// Takes the oldest line the pack wants said (true and `out` filled), or false when there is none. Called by
        /// the core once or more per frame, after ActorRules::update. A line for an actor the core does not know (any
        /// more) is dropped and counted by the core.
        [[nodiscard]] virtual bool takeSpeech(SceneSpeech& out) = 0;
    };
}
