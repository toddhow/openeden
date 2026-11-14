#ifndef OPENEDEN_GAME_SKYRIMSE_DIALOGUE_RUNTIME_H
#define OPENEDEN_GAME_SKYRIMSE_DIALOGUE_RUNTIME_H

// Skyrim SE conversation state (task 0141, milestone M-S4 "talk to an NPC", slice 1). Pure pack code over the
// dialogue, quest and actor indexes: opens a conversation with a speaker (the greeting the data gives, the topics the
// player may choose) and answers a chosen topic. Text comes through a resolver (string tables); no engine, UI or Qt
// type. Not thread safe; one conversation at a time.
//
// Voice (task 0143): with a voice probe set (`setVoiceProbe`), a response's lines that have a voice file are named in
// `DialogueTurn::voice` (see voice.hpp for the file names). The pack never reads the audio; no probe, no voice.
//
// Slice 1 limits (each counted in `Stats`, none silent): no lip sync, camera or idle; no INFO script
// fragments (VMAD is only noticed, `hadUnrunEffects`); no link-to topics (TCLT) or choices; no speech challenges,
// barter, training or other services; say-once is remembered for the session only.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/speech.hpp>

#include <openeden/game/dialogue_source.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// Text of a localised or plain string of a record of plugin `plugin` (an index into the DialogueIndex's plugins).
    /// Returns an empty string when the text cannot be found.
    using DialogueTextResolver
        = std::function<std::string(const LString& text, std::uint32_t plugin, StringTableKind kind)>;

    /// True when the VFS holds the file `path` (relative to the sound folder, lower case, '/' separators).
    using VoiceProbe = std::function<bool(const std::string& path)>;

    /// The lower-case names a compiled fragment script refers to (the .pex string table); empty when it cannot be read.
    /// `script` is the script's name as the VMAD gives it ("TIF__000A1B2C").
    using ScriptNameProbe = std::function<std::vector<std::string>(const std::string& script)>;

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
            std::uint64_t mVoiceFound = 0; ///< response lines whose voice file exists (task 0143)
            std::uint64_t mVoiceMissing = 0; ///< response lines with a text and no voice file
            std::uint64_t mVoiceUnnamed = 0; ///< shown responses for which no voice path could be made
            std::uint64_t mServices = 0; ///< shown responses that are a service the pack cannot run (task 0155)
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

        /// Turns voice on: `probe` says which files exist. Without it no turn has voice.
        void setVoiceProbe(VoiceProbe probe) { mVoiceProbe = std::move(probe); }

        /// The stand-in player the speech checks and level conditions read (task 0155); the default is a new game's.
        void setPlayer(const PlayerStandIn& player)
        {
            mPlayer = player;
            mWorld.setPlayer(&mPlayer);
        }
        [[nodiscard]] const PlayerStandIn& player() const { return mPlayer; }

        /// Locations (LCTN) and the quest scripts' initial values (task 0156); both must outlive the runtime. Without
        /// them fn_359, fn_562 and fn_629 stay unanswered.
        void setLocations(const LocationIndex* locations) { mWorld.setLocations(locations); }
        void setQuestVariables(const QuestVariables* variables) { mWorld.setQuestVariables(variables); }
        /// Task 0160: the placed references, the packages and where actors are in their packages (see
        /// SkyrimConditionWorld).
        void setPlacedRefs(const PlacedRefIndex* refs) { mWorld.setPlacedRefs(refs); }
        void setPackages(const PackageIndex* packages) { mWorld.setPackages(packages); }
        void setCurrentPackageLookup(SkyrimConditionWorld::CurrentPackageLookup lookup)
        {
            mWorld.setCurrentPackageLookup(std::move(lookup));
        }

        /// Turns service detection on: `probe` names what a fragment script refers to. Without it no turn has a service.
        void setScriptProbe(ScriptNameProbe probe) { mScriptProbe = std::move(probe); }

        [[nodiscard]] bool open() const { return mOpen; }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] const QuestState& quests() const { return mState; }

    private:
        void fillTurn(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out);
        void nameVoice(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out);
        void offerTopics(openeden::sdk::game::DialogueTurn& out);
        [[nodiscard]] SelectionOptions options(std::uint32_t pick);

        const ActorIndex& mActors;
        const DialogueIndex& mDialogue;
        const QuestIndex& mQuestIndex;
        QuestState mState;
        SkyrimConditionWorld mWorld;
        DialogueTextResolver mResolver;
        DialogueRuntimeLimits mLimits;
        VoiceProbe mVoiceProbe;
        ScriptNameProbe mScriptProbe;
        std::map<std::string, std::string> mServiceOfScript; ///< fragment script -> service ("" for none), probed once
        PlayerStandIn mPlayer;
        std::string mVoiceType; ///< editor id of the speaker's voice type, lower case; empty when unknown
        DialogueSpeaker mSpeaker;
        std::set<FormId> mSaid;
        std::set<FormId> mOffered;
        bool mOpen = false;
        Stats mStats;
    };
}

#endif
