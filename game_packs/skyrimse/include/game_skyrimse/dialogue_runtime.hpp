#ifndef OPENEDEN_GAME_SKYRIMSE_DIALOGUE_RUNTIME_H
#define OPENEDEN_GAME_SKYRIMSE_DIALOGUE_RUNTIME_H

// Skyrim SE conversation state (task 0140, milestone M-S4 "talk to an NPC", slice 1). Pure pack code over the
// dialogue, quest and actor indexes: opens a conversation with a speaker (the greeting the data gives, the topics the
// player may choose) and answers a chosen topic. Text comes through a resolver (string tables); no engine, UI or Qt
// type. Not thread safe; one conversation at a time.
//
// Slice 1 limits (each counted in `Stats`, none silent): no voice, lip sync, camera or idle; no INFO script
// fragments (VMAD is only noticed, `hadUnrunEffects`); no link-to topics (TCLT) or choices; no speech challenges,
// barter, training or other services; say-once is remembered for the session only.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/quest.hpp>

#include <openeden/game/dialogue_source.hpp>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// Text of a localised or plain string of a record of plugin `plugin` (an index into the DialogueIndex's plugins).
    /// Returns an empty string when the text cannot be found.
    using DialogueTextResolver
        = std::function<std::string(const LString& text, std::uint32_t plugin, StringTableKind kind)>;

    struct DialogueRuntimeLimits
    {
        std::uint32_t mMaxTopics = 256; ///< topics offered at once (the rest are counted as cut)
        std::uint32_t mMaxTextBytes = 16 * 1024; ///< one response, joined
    };

    class DialogueRuntime
    {
    public:
        struct Stats
        {
            std::uint64_t mConversations = 0;
            std::uint64_t mResponses = 0;
            std::uint64_t mScriptsNotRun = 0; ///< shown responses whose INFO has a script (VMAD)
            std::uint64_t mLinksIgnored = 0; ///< shown responses with link-to topics (TCLT)
            std::uint64_t mTopicsCut = 0; ///< topics that passed but did not fit mMaxTopics
            std::uint64_t mUncertainTopics = 0; ///< offered topics chosen with a condition the data cannot answer
        };

        DialogueRuntime(const DialogueRuntime&) = delete; // the world points at mState
        DialogueRuntime& operator=(const DialogueRuntime&) = delete;

        /// All references must outlive the runtime. `quests` is copied: the runtime changes its own copy.
        DialogueRuntime(const ActorIndex& actors, const DialogueIndex& dialogue, const QuestIndex& quests,
            QuestState state, DialogueTextResolver resolver, DialogueRuntimeLimits limits = {});

        /// The greeting (subtype "HELO" through `pickFromTopics`) and the topic list. False with `error` when no
        /// greeting passes.
        [[nodiscard]] bool begin(const DialogueSpeaker& speaker, openeden::sdk::game::DialogueTurn& out,
            std::string& error);

        /// The response to a topic offered by the last turn.
        [[nodiscard]] bool choose(std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error);

        void end();

        [[nodiscard]] bool open() const { return mOpen; }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] const QuestState& quests() const { return mState; }

    private:
        void fillTurn(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out);
        void offerTopics(openeden::sdk::game::DialogueTurn& out);
        [[nodiscard]] SelectionOptions options(std::uint32_t pick);

        const ActorIndex& mActors;
        const DialogueIndex& mDialogue;
        const QuestIndex& mQuestIndex;
        QuestState mState;
        SkyrimConditionWorld mWorld;
        DialogueTextResolver mResolver;
        DialogueRuntimeLimits mLimits;
        DialogueSpeaker mSpeaker;
        std::set<FormId> mSaid;
        std::set<FormId> mOffered;
        bool mOpen = false;
        Stats mStats;
    };
}

#endif
