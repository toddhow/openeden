#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// What the core needs to run a CONVERSATION with a game's actor (task 0141, milestone M-S4 "talk to an NPC",
// decision 0017): the greeting, the topics the player may choose, the response to a chosen topic. The rules (which
// response passes, quests, say once) stay in the pack; only plain text and opaque topic ids cross.
//
// ABI choice: like ActorAppearanceSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen WorldCellSource (world_source.hpp 1.x) is untouched. A pack that does not implement
// it simply has actors that cannot be talked to.
//
// Version policy: DRAFT 0.4 (0.2, task 0143: DialogueTurn::voice; 0.3, task 0155: DialogueTurn::service, trailing
// fields; 0.4, task 0135 stage S6g: the three "get ready to talk" virtuals at the end, default bodies). Additive
// changes (a new trailing virtual with a "not supported" default, a trailing field a consumer only reads) raise the
// minor number. Threading: main thread only. Plain values only: no engine, renderer, UI or Qt type.

namespace openeden::sdk::game
{
    inline constexpr Version kDialogueSourceVersion{ 0, 4 };

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
        /// Voice lines of this response, in order, one per response line that has a file: the VFS path of each,
        /// relative to the game's sound folder (the core prefixes "sound/"), exactly as the VFS knows it. Empty when
        /// the pack has no voice for this speaker or none of the files exist. The core plays them one after the other;
        /// the subtitle stays `text`. Added in 0.2.
        std::vector<std::string> voice;
        /// The response is a SERVICE the pack knows it cannot run yet (task 0155): a short name such as "barter",
        /// "training" or "rent a room"; empty for an ordinary line. A turn with a service ends the conversation (the
        /// service's own menu would replace the dialogue), and the core logs "service not implemented". Added in 0.3.
        std::string service;

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

        /// Stage S6g (added in 0.4): the player activated the actor placed as `reference`, BEFORE beginConversation.
        /// The conversation box pauses the world, so an actor that must get ready first (a sleeper stands up) has to be
        /// asked while the world still runs. Returns true when the actor can talk now (the default); false when it is
        /// getting ready: the core then polls readyForConversation once a frame and calls beginConversation when it
        /// says true, or after its own time limit. Main thread only.
        [[nodiscard]] virtual bool prepareConversation(WorldId reference)
        {
            (void)reference;
            return true;
        }

        /// True when the actor asked by prepareConversation can talk (also true for an unknown actor). Added in 0.4.
        [[nodiscard]] virtual bool readyForConversation(WorldId reference)
        {
            (void)reference;
            return true;
        }

        /// The core will not begin the conversation after all (nothing to say, the player left): the actor goes back
        /// to what it was doing. Safe to call without a prepare. Added in 0.4.
        virtual void cancelPreparedConversation(WorldId reference) { (void)reference; }
    };

    /// Task 0338 (optional, found by dynamic_cast on the registered source like the other optional interfaces; no
    /// version change): the source can also run a conversation with a TALKING ACTIVATOR (a placed object that is no
    /// actor: Azura's altar). The core then talks to it when the player activates it, instead of the object's other
    /// use.
    class TalkingActivatorSource
    {
    public:
        virtual ~TalkingActivatorSource() = default;
        /// True when the object of this base record speaks.
        [[nodiscard]] virtual bool speaksAsActivator(WorldId base) = 0;
    };
}
