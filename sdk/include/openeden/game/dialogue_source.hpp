#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// What the core needs to run a CONVERSATION with a game's actor (task 0140, milestone M-S4 "talk to an NPC",
// decision 0017): the greeting, the topics the player may choose, the response to a chosen topic. The rules (which
// response passes, quests, say once) stay in the pack; only plain text and opaque topic ids cross.
//
// ABI choice: like ActorAppearanceSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen WorldCellSource (world_source.hpp 1.x) is untouched. A pack that does not implement
// it simply has actors that cannot be talked to.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default, a trailing field a
// consumer only reads) raise the minor number. Threading: main thread only. Plain values only: no engine, renderer,
// UI or Qt type.

namespace openeden::sdk::game
{
    inline constexpr Version kDialogueSourceVersion{ 0, 1 };

    /// A topic the player may choose. `id` is opaque to the core.
    struct DialogueTopicChoice
    {
        std::uint32_t id = 0;
        std::string text;

        friend bool operator==(const DialogueTopicChoice&, const DialogueTopicChoice&) = default;
    };

    /// What the actor says and what the player may answer.
    struct DialogueTurn
    {
        std::string speakerName; ///< may be empty; the core then uses the object's own name
        /// The response lines in order, already joined by the pack for display (text only; no voice, no animation).
        std::string text;
        std::vector<DialogueTopicChoice> topics;
        /// The actor's line ends the conversation (a goodbye response); the core closes after showing it.
        bool endsConversation = false;
        /// The response had a script or other effect the pack does not run yet (counted by the pack, logged once).
        bool hadUnrunEffects = false;

        friend bool operator==(const DialogueTurn&, const DialogueTurn&) = default;
    };

    /// Implemented by a WorldCellSource that can run conversations.
    class DialogueSource
    {
    public:
        virtual ~DialogueSource() = default;

        /// Starts a conversation with the actor base `base` placed as `reference` (WorldObject::base / ::id of an
        /// ObjectKind::Actor object) standing in `cell`. Fills the greeting and the topic list. False with `error`
        /// when there is nothing to say (an actor with no greeting, an unknown actor). Main thread only.
        [[nodiscard]] virtual bool beginConversation(
            WorldId base, WorldId reference, WorldId cell, DialogueTurn& out, std::string& error)
            = 0;

        /// The response to a topic from the list of the last turn. False with `error` for an id that was not offered
        /// or when no conversation is open.
        [[nodiscard]] virtual bool chooseTopic(std::uint32_t topicId, DialogueTurn& out, std::string& error) = 0;

        /// Ends the conversation; safe to call when none is open.
        virtual void endConversation() = 0;
    };
}
