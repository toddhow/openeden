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
#include <game_skyrimse/journal.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/speech.hpp>

#include <openeden/game/dialogue_source.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
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

    /// Task 0187: a shown response whose INFO has script fragments. The sink runs them (the begin fragment, then the
    /// end fragment: the pack does not know when a spoken line ends, so both run when the line is shown, in that
    /// order).
    struct InfoFragmentCall
    {
        FormId mInfo = kNullFormId;
        std::shared_ptr<const vmad::Vmad> mVmad;
        FormId mSpeakerReference = kNullFormId; ///< the placed ACHR, 0 when only the base is known
        FormId mSpeakerBase = kNullFormId;
    };
    using InfoFragmentSink = std::function<void(const InfoFragmentCall&)>;

    struct DialogueRuntimeLimits
    {
        std::uint32_t mMaxTopics = 256; ///< topics offered at once (the rest are counted as cut)
        std::uint32_t mMaxTextBytes = 16 * 1024; ///< one response, joined
    };

    /// Task 0228: what the text tags of a conversation need from outside the dialogue data. Every field may be empty.
    struct DialogueTagSources
    {
        /// What fills the alias `alias` of `quest` (the journal's own resolver: one function for both).
        std::function<std::string(FormId quest, std::string_view alias)> mAliasName;
        /// 1 female, 0 male, -1 unknown, for the alias's pronouns.
        std::function<int(FormId quest, std::string_view alias)> mAliasFemale;
        /// The gold the player owes in the speaker's crime faction (`<CrimeGold>`); 0 when absent.
        std::function<float()> mCrimeGold;
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
            std::uint64_t mFragmentCalls = 0; ///< shown responses whose script fragments were handed to the sink (0187)
            std::uint64_t mServices = 0; ///< shown responses that are a service the pack cannot run (task 0155)
        };

        DialogueRuntime(const DialogueRuntime&) = delete; // the world points at mState
        DialogueRuntime& operator=(const DialogueRuntime&) = delete;

        /// All references must outlive the runtime. `quests` is copied: the runtime changes its own copy.
        DialogueRuntime(const ActorIndex& actors, const DialogueIndex& dialogue, const QuestIndex& quests,
            QuestState state, DialogueTextResolver resolver, DialogueRuntimeLimits limits = {});

        /// The greeting (subtype "HELO" through `pickFromTopics`) and the topic list. False with `error` when no
        /// greeting passes.
        [[nodiscard]] bool begin(
            const DialogueSpeaker& speaker, openeden::sdk::game::DialogueTurn& out, std::string& error);

        /// The response to a topic offered by the last turn.
        [[nodiscard]] bool choose(std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error);

        void end();

        /// Task 0194: the line a scene's dialogue action gives `speaker` from `topic` (a DIAL of category Scene): the
        /// topic's INFO by the usual selection, its text and voice, and its script fragments through the sink. Does not
        /// touch an open conversation (the speaker, voice type and say-once of it are kept). False when no INFO passes.
        [[nodiscard]] bool sceneLine(
            FormId topic, const DialogueSpeaker& speaker, std::uint32_t pick, openeden::sdk::game::DialogueTurn& out);

        /// Task 0231: the line `speaker` says unprompted for a DIAL subtype ("HELO", "ATCK" ...): the topics of that
        /// subtype by priority, the first INFO that passes (a Random one chosen by `pick`), its text, voice and script
        /// fragments. Like sceneLine it leaves an open conversation alone (speaker, voice type and the conversation
        /// flags are kept); say-once is shared with the conversations. False when no INFO passes.
        /// Task 0243: `speakerIsVictim`: the speaker is the victim of the crime it remarks on (IsActorAVictim).
        [[nodiscard]] bool barkLine(std::string_view subtype, const DialogueSpeaker& speaker, std::uint32_t pick,
            openeden::sdk::game::DialogueTurn& out, bool speakerIsVictim = false);

        /// Task 0258: the guard's forced greeting. The topics of subtype `subtype` ("PFGT") by priority, the first INFO
        /// that passes with a response text (and a voice file when voice is on); like barkLine it marks nothing said
        /// and runs no fragment. False when none passes (with no crime source set none does: the lines need
        /// GetAlarmed).
        [[nodiscard]] bool forcedGreetingAvailable(
            const DialogueSpeaker& speaker, std::string_view subtype, std::string* why = nullptr);
        /// Task 0258: opens a conversation whose greeting is the forced one and whose choices are the greeting's
        /// link-to topics (TCLT) that have an INFO passing now, each shown by its INFO's prompt else its topic's name.
        /// Choosing one gives its response (its fragments run); a response with no link-to topics of its own ends the
        /// conversation. False with `error` when no forced greeting passes. `begin` and `end` leave this mode.
        [[nodiscard]] bool beginForced(const DialogueSpeaker& speaker, std::string_view subtype,
            openeden::sdk::game::DialogueTurn& out, std::string& error);
        /// Task 0264: opens a conversation whose greeting is the first INFO of `topic` that passes now (a ForceGreet
        /// package's PDTO topic); like `beginForced` its choices are the greeting's link-to topics. False with `error`
        /// when no INFO of the topic passes.
        [[nodiscard]] bool beginForcedTopic(
            const DialogueSpeaker& speaker, FormId topic, openeden::sdk::game::DialogueTurn& out, std::string& error);
        /// Task 0258: what the crime rules say to the conditions and the text tag <CrimeGold>. Without it: nothing.
        void setCrimeSource(CrimeConditionSource source)
        {
            mCrimeSource = std::move(source);
            mWorld.setCrimeSource(&mCrimeSource);
        }

        /// Turns voice on: `probe` says which files exist. Without it no turn has voice.
        /// Task 0187: where the script fragments of a shown response go. Without a sink they are not run (counted in
        /// `mScriptsNotRun`, as before).
        void setFragmentSink(InfoFragmentSink sink) { mFragmentSink = std::move(sink); }

        void setVoiceProbe(VoiceProbe probe) { mVoiceProbe = std::move(probe); }

        /// Task 0228: the text tags (`<Global=...>`, `<Alias=...>`, `<BaseName>`...) of every line, prompt and subtitle
        /// are filled in; without sources only the tags the dialogue data alone can answer are.
        void setTagSources(DialogueTagSources sources) { mTagSources = std::move(sources); }
        /// Fills the tags of `text`, for the quest `quest` (the topic's) and the open speaker.
        [[nodiscard]] std::string expandText(const std::string& text, FormId quest) const;

        /// The stand-in player the speech checks and level conditions read (task 0155); the default is a new game's.
        void setPlayer(const PlayerStandIn& player)
        {
            mPlayer = player;
            mWorld.setPlayer(&mPlayer);
        }
        [[nodiscard]] const PlayerStandIn& player() const { return mPlayer; }
        /// Task 0184: the player's gold from the inventory (see SkyrimConditionWorld::setGoldSource); empty: the
        /// stand-in's.
        void setGoldSource(std::function<float()> source) { mWorld.setGoldSource(std::move(source)); }
        /// Task 0270: the player's worn items and item counts (see SkyrimConditionWorld).
        void setActivatorVoiceLookup(std::function<FormId(FormId)> lookup) // task 0338
        {
            mWorld.setActivatorVoiceLookup(std::move(lookup));
        }
        void setPlayerWornSource(std::function<bool(std::uint32_t)> source)
        {
            mWorld.setPlayerWornSource(std::move(source));
        }
        void setPlayerItemSource(std::function<float(std::uint32_t)> source)
        {
            mWorld.setPlayerItemSource(std::move(source));
        }

        /// Locations (LCTN) and the quest scripts' initial values (task 0156); both must outlive the runtime. Without
        /// them fn_359, fn_562 and fn_629 stay unanswered.
        void setLocations(const LocationIndex* locations) { mWorld.setLocations(locations); }
        /// Task 0217: what scripts stored on locations (fn_606 and fn_651 in a conversation's conditions).
        void setKeywordData(const LocationKeywordData* data) { mWorld.setKeywordData(data); }
        void setQuestVariables(const QuestVariables* variables) { mWorld.setQuestVariables(variables); }
        /// Task 0160: the placed references, the packages and where actors are in their packages (see
        /// SkyrimConditionWorld).
        void setPlacedRefs(const PlacedRefIndex* refs) { mWorld.setPlacedRefs(refs); }
        void setPackages(const PackageIndex* packages) { mWorld.setPackages(packages); }
        void setCurrentPackageLookup(SkyrimConditionWorld::CurrentPackageLookup lookup)
        {
            mWorld.setCurrentPackageLookup(std::move(lookup));
        }
        /// Task 0239: what the follower scripts changed wins over the data in the conversation's conditions.
        void setFollowerOverlay(const FollowerOverlay* overlay) { mWorld.setFollowerOverlay(overlay); }
        /// Task 0277: see SkyrimConditionWorld::setLivePlaces.
        void setLivePlaces(SkyrimConditionWorld::PlaceLookup actor, SkyrimConditionWorld::PlaceLookup target)
        {
            mWorld.setLivePlaces(std::move(actor), std::move(target));
        }
        /// Task 0326: the references killed in this session (GetRefTypeAliveCount).
        void setDeadLookup(std::function<bool(FormId)> lookup) { mWorld.setDeadLookup(std::move(lookup)); }
        /// task 0337: a line whose condition runs on the speaker's LINKED REFERENCE (Eltrys's entry topic: Weylin is dead)
        void setLinkedRefLookup(SkyrimConditionWorld::LinkedRefLookup lookup)
        {
            mWorld.setLinkedRefLookup(std::move(lookup));
        }
        void setVendorServicesLookup(SkyrimConditionWorld::VendorServicesLookup lookup) // task 0189
        {
            mWorld.setVendorServicesLookup(std::move(lookup));
        }
        /// Task 0235: the player's stance for the conditions (see SkyrimConditionWorld::setPlayerStance).
        void setPlayerStance(bool sneaking, std::uint8_t drawState) { mWorld.setPlayerStance(sneaking, drawState); }

        /// Turns service detection on: `probe` names what a fragment script refers to. Without it no turn has a
        /// service.
        void setScriptProbe(ScriptNameProbe probe) { mScriptProbe = std::move(probe); }
        /// Task 0219: with this on a training, rent or gift service is named "open:<name>" (services.hpp) so the engine
        /// opens its window; off, the names are the classifier's and the engine logs "service not implemented".
        void setOpenServices(bool on) { mOpenServices = on; }
        /// Task 0298: with this on, a blocking branch (the Civil War's Rikke) says its first line that passes instead of the Hello greeting.
        void setBlockingBranches(bool on)
        {
            mBlockingBranches = on;
            mWorld.setCountRefTypes(on); // GetRefTypeAliveCount counts the placed references (Fort Hraggstad's bandits)
        }
        /// Task 0315: with this on, ending a conversation runs the fragment of the walk-away topic (TWAT) of the last
        /// response that named one: the response itself is not shown (no line, no voice), only its script work.
        void setWalkAwayTopics(bool on) { mWalkAwayTopics = on; }
        /// Task 0326: with this on, a response with no walk-away topic of its own still lets the conversation's last topic
        /// run its walk-away INFO (flag 0x80, the first that passes) at the end (the Legion's CW01RikkeGreetTopic INFO
        /// 0D5EE9, which sets CW01A stage 160). Off: only the TWAT topics of setWalkAwayTopics.
        void setWalkAwayInfos(bool on) { mWalkAwayInfos = on; }
        /// Task 0304: a nameless Custom topic that starts a quest's dialogue branch (the quest's own greeting, such as
        /// Faralda's "Welcome to the College") is spoken at the start of a conversation, before the Hello lines, when
        /// one of its INFOs passes. Off by default.
        void setQuestGreetings(bool on) { mQuestGreetings = on; }
        /// task 0343 (`dark brotherhood`): a quest start whose INFOs all carry prompts is a menu topic, not a greeting
        void setPromptStartsAreMenus(bool on) { mPromptStartsAreMenus = on; }
        /// Task 0304: a line of trace for each quest greeting looked at (the topic, how many INFOs were considered and
        /// passed, the INFO chosen); unset: none.
        /// Task 0304: runs the script work queued by the INFO fragments of the response just shown, so that the next INFO
        /// of a chain is chosen with their effects (NiryaInit set by "Excellent." decides which spell offer follows).
        void setFragmentFlush(std::function<void()> flush) { mFragmentFlush = std::move(flush); }
        void setTrace(std::function<void(const std::string&)> trace) { mTrace = std::move(trace); }

        [[nodiscard]] bool open() const { return mOpen; }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] const QuestState& quests() const { return mState; }
        /// Task 0176: the Papyrus host changes the quest state the conversations read.
        [[nodiscard]] QuestState& questsMutable() { return mState; }
        /// Task 0261: a script's live value of a quest variable wins over the script's initial one in the conditions.
        void setLiveQuestVariable(SkyrimConditionWorld::LiveQuestVariable source)
        {
            mWorld.setLiveQuestVariable(std::move(source));
        }
        void setRankMinusOneMembers(std::set<FormId> factions) { mWorld.setRankMinusOneMembers(std::move(factions)); }
        /// Task 0261: do the conditions of a quest's log entry hold now? The subject is the player (the last speaker
        /// stands for his place: the conversation world's location); `resolver` resolves the stored form ids.
        /// Task 0312: `playerLocation` (when not none) is where the player stands, for GetInCurrentLoc on him.
        [[nodiscard]] bool evaluateQuestConditions(const std::vector<survey::Ctda>& list, FormId quest,
            const FormIdResolver& resolver, FormId playerLocation = kNullFormId);

    private:
        static constexpr std::size_t kMaxFollowDepth = 8; ///< task 0304 (4); 0338: Nelacar says four in a row
        void followNamelessLinks(openeden::sdk::game::DialogueTurn& out, const DialogInfo* from = nullptr);
        bool mQuestGreetings = false;
        bool mPromptStartsAreMenus = false; ///< task 0343
        bool mLastContinues = false; ///< task 0340: the last INFO said was flagged InvisibleContinue
        std::function<void(const std::string&)> mTrace;
        std::function<void()> mFragmentFlush;
        std::vector<FormId> mGreetingTopics; ///< task 0304: DialogueIndex::questGreetingTopics, found once
        bool mGreetingTopicsKnown = false;
        void fillTurn(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out);
        void nameVoice(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out);
        void offerTopics(openeden::sdk::game::DialogueTurn& out);
        /// Task 0261: the INFO whose responses an INFO gives: itself, or the INFO named by its DNAM when it has none.
        [[nodiscard]] const DialogInfo& responderOf(const DialogInfo& info) const;
        [[nodiscard]] SelectionOptions options(std::uint32_t pick);
        /// Task 0258: the link-to topics (TCLT) of `from` that have an INFO passing now, as the turn's choices.
        /// Task 0271: a link whose topic has no name and whose INFO has no prompt is not a choice: the speaker goes on
        /// with that INFO at once (its text joins the turn, its fragments run) when nothing else is offered, to
        /// `depth` 4.
        void offerLinks(const DialogInfo& from, openeden::sdk::game::DialogueTurn& out, int depth = 0);
        /// Task 0243 (barkLine's helpers; none changes a conversation). Has the INFO a response with text? No side
        /// effect. Has it a voice file for the speaker (always true with no probe)? The probe's counts are restored.
        [[nodiscard]] bool hasResponseText(const DialogInfo& info) const;
        [[nodiscard]] bool hasVoiceFile(const DialogInfo& info);
        /// The INFO a bark of `subtype` takes: `selectInfo`'s rules over the topics of the subtype by priority, except
        /// that an INFO with no response text, or none with a voice file, is not a candidate. Nothing is marked said
        /// and no fragment runs: the caller runs `fillTurn` on the one returned.
        [[nodiscard]] const DialogInfo* selectBarkInfo(
            std::string_view subtype, std::uint32_t pick, std::string* why = nullptr);

        DialogueTagSources mTagSources;
        CrimeConditionSource mCrimeSource; // task 0258
        bool mLinkMode = false; // task 0258: the open conversation offers link-to topics only
        const ActorIndex& mActors;
        const DialogueIndex& mDialogue;
        const QuestIndex& mQuestIndex;
        QuestState mState;
        SkyrimConditionWorld mWorld;
        DialogueTextResolver mResolver;
        DialogueRuntimeLimits mLimits;
        VoiceProbe mVoiceProbe;
        ScriptNameProbe mScriptProbe;
        bool mOpenServices = false; ///< task 0219
        bool mBlockingBranches = false; ///< task 0298
        bool mWalkAwayTopics = false; ///< task 0315
        bool mWalkAwayInfos = false; ///< task 0326
        FormId mLastTopic = 0; ///< task 0326: the topic of the last response shown
        FormId mWalkAway = 0; ///< the walk-away topic of the last response shown
        [[nodiscard]] bool hidesWalkAway(FormId topic) const; ///< task 0325
        InfoFragmentSink mFragmentSink;
        std::map<std::string, std::string> mServiceOfScript; ///< fragment script -> service ("" for none), probed once
        PlayerStandIn mPlayer;
        std::string mVoiceType; ///< editor id of the speaker's voice type, lower case; empty when unknown
        DialogueSpeaker mSpeaker;
        std::set<FormId> mSaid;
        std::set<FormId> mOffered;
        /// Task 0261: the topics the last response linked to (TCLT): the next list offers only those that pass.
        std::vector<FormId> mChoices;
        bool mOpen = false;
        Stats mStats;
    };
}

#endif
