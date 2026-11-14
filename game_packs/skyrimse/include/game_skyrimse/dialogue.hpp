#ifndef OPENEDEN_GAME_SKYRIMSE_DIALOGUE_H
#define OPENEDEN_GAME_SKYRIMSE_DIALOGUE_H

// Skyrim SE dialogue records and the choice of a response (task 0137, milestone M-S4 "talk to an NPC", slice 1,
// docs/design/skyrim-se-implementation-plan.md sections 4.16, 4.20 and 5.2). Pack side only: decode DIAL (topics),
// INFO (responses), GLOB (global values) and FLST (form lists); merge a load order; pick the INFO a topic gives a
// speaker, with the conditions engine (conditions.hpp) over a `SkyrimConditionWorld` that answers what the data alone
// can answer. Plain structs; no engine, renderer, UI or Qt type. Nothing calls it from the engine yet.
//
// Layout status of what is read (VERIFIED = checked on the five installed masters by tools/skyrimse_dialogue_check,
// aggregates in the task log; RECALLED-VERIFY = from memory of xEdit's TES5 definitions or the Creation Kit wiki,
// which the data does not settle):
//  - DIAL: PNAM float priority, QNAM quest, BNAM branch, DATA 4 bytes (u8 flags, u8 category, u16 subtype), SNAM 4
//    characters (the subtype's name, "HELO" = Hello), TIFC u32 info count, FULL name. VERIFIED sizes on 15,037 topics;
//    TIFC equals the INFO count of the topic's group for all of them. Category 7 is "Miscellaneous" and carries HELO
//    (RECALLED-VERIFY names of the categories: 0 Topic, 1 Favor, 2 Scene, 3 Combat, 4 Favors, 5 Detection, 6 Service,
//    7 Miscellaneous).
//  - INFO is always in a group of type 7 whose label is its topic's form id (VERIFIED, 31,465 of 31,465). Skyrim SE
//    has NO previous-INFO subrecord (VERIFIED: no PNAM in any INFO), so the order of the responses of a topic is the
//    order they have in the file; a later plugin's override keeps its place, a new INFO of a later plugin goes last
//    (RECALLED-VERIFY: that this is the order the game uses).
//  - INFO ENAM 4 bytes: u16 flags, u16 reset hours (VERIFIED size; flag 0x2000 "audio output override" appears
//    exactly when ONAM does, 30,541 of 30,541). The other flag names are RECALLED-VERIFY (xEdit): 0x1 Goodbye,
//    0x2 Random, 0x4 Say Once, 0x10 Info Refusal, 0x20 Random End, 0x40 Invisible Continue, 0x80 Walk Away.
//  - Responses: TRDT 24 bytes starts one (VERIFIED: u32 emotion type 0 to 7, u32 emotion value 0 to 100, u32 zero,
//    u8 response number 1..n in order for 30,609 of 31,465 infos, three zero bytes, form id of a sound descriptor or
//    0, u8 use-emotion-animation, three zero bytes), NAM1 (4 bytes in a localised plugin: an id into the
//    .ILSTRINGS table) is its text, NAM2 and NAM3 are script notes and edit notes (left out).
//  - INFO conditions are CTDA subrecords (conditions.hpp); QNAM quest (1,662), ANAM speaker NPC_ (223, VERIFIED
//    type), CNAM u8 favor level, RNAM prompt text (an id into the .STRINGS table, NOT .ILSTRINGS, VERIFIED: 1,927 of
//    1,927), TCLT links to topics (7,804, all DIAL), DNAM u32 (left raw), SNAM and
//    LNAM idle animations (left raw), DATA 8 bytes (left raw, meaning unknown).
//  - GLOB: FNAM one character type, FLTV float value. FLST: LNAM form ids, in order.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/speech.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace TopicCategory
    {
        inline constexpr std::uint8_t Topic = 0;
        inline constexpr std::uint8_t Favor = 1;
        inline constexpr std::uint8_t Scene = 2;
        inline constexpr std::uint8_t Combat = 3;
        inline constexpr std::uint8_t Favors = 4;
        inline constexpr std::uint8_t Detection = 5;
        inline constexpr std::uint8_t Service = 6;
        inline constexpr std::uint8_t Miscellaneous = 7;
    }

    namespace InfoFlags
    {
        inline constexpr std::uint16_t Goodbye = 0x0001;
        inline constexpr std::uint16_t Random = 0x0002;
        inline constexpr std::uint16_t SayOnce = 0x0004;
        inline constexpr std::uint16_t InfoRefusal = 0x0010;
        inline constexpr std::uint16_t RandomEnd = 0x0020;
        inline constexpr std::uint16_t InvisibleContinue = 0x0040;
        inline constexpr std::uint16_t WalkAway = 0x0080;
        inline constexpr std::uint16_t AudioOutputOverride = 0x2000; ///< VERIFIED: set exactly when ONAM is present
    }

    struct DialogTopic
    {
        RecordCommon mCommon;
        LString mName; ///< FULL
        float mPriority = 0.0f; ///< PNAM
        FormId mQuest = kNullFormId; ///< QNAM
        FormId mBranch = kNullFormId; ///< BNAM
        bool mHasData = false;
        std::uint8_t mFlags = 0;
        std::uint8_t mCategory = 0;
        std::uint16_t mSubtype = 0;
        std::string mSubtypeName; ///< SNAM, four characters ("HELO", "GBYE", "CUST", "SCEN" ...)
        std::uint32_t mInfoCount = 0; ///< TIFC
        std::uint32_t mPlugin = 0; ///< index into DialogueIndex's plugins: the plugin whose record won
    };

    struct DialogResponse
    {
        std::uint32_t mEmotionType = 0;
        std::uint32_t mEmotionValue = 0;
        std::uint8_t mNumber = 0;
        FormId mSound = kNullFormId;
        bool mUseEmotionAnimation = false;
        LString mText; ///< NAM1; localised: an id into the plugin's .ILSTRINGS
    };

    struct DialogInfo
    {
        RecordCommon mCommon;
        FormId mTopic = kNullFormId; ///< the label of the group the record sits in
        FormId mQuest = kNullFormId; ///< QNAM (0: the topic's quest)
        FormId mSpeaker = kNullFormId; ///< ANAM
        std::uint16_t mFlags = 0;
        std::uint16_t mResetHours = 0;
        std::uint8_t mFavorLevel = 0;
        LString mPrompt; ///< RNAM
        std::vector<DialogResponse> mResponses;
        std::vector<FormId> mLinksTo; ///< TCLT
        std::vector<survey::Ctda> mConditions; ///< as stored (plugin-local form ids), see DialogueIndex::resolverOf
        bool mHasScript = false; ///< a VMAD subrecord is present (script fragments, not run; task 0141)
        /// The name of the INFO's fragment script ("TIF__" plus 8 hex digits, the fileName of the VMAD fragment
        /// section), empty when the VMAD names none (task 0155). Found by scanning the VMAD for that wstring, not by
        /// walking the script and property tables.
        std::string mFragmentScript;
        bool mConditionsOk = true; ///< false: the CTDA list was damaged, the INFO never passes
        std::uint32_t mPlugin = 0; ///< index into DialogueIndex's plugins
    };

    /// The fragment script name inside a VMAD subrecord: a wstring (u16 length, then the characters) that reads
    /// "TIF__" and eight hexadecimal digits, the first such. Empty when there is none.
    [[nodiscard]] std::string findFragmentScriptName(std::span<const std::byte> vmad);

    struct GlobalValue
    {
        RecordCommon mCommon;
        char mType = 'f';
        float mValue = 0.0f;
    };

    struct FormListRecord
    {
        RecordCommon mCommon;
        std::vector<FormId> mEntries;
    };

    /// A VTYP record: only its editor id is read (the name of the folder that holds the voice files of the NPCs that
    /// have this voice type; task 0143).
    struct VoiceTypeRecord
    {
        RecordCommon mCommon;
    };

    struct DialogueLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxResponses = 64;
        std::uint32_t mMaxConditions = 4096;
        std::uint32_t mMaxListEntries = 65536;
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    /// What one plugin holds. Infos are in file order.
    struct DialogueRecords
    {
        std::string mName;
        std::vector<DialogTopic> mTopics;
        std::vector<DialogInfo> mInfos;
        std::vector<GlobalValue> mGlobals;
        std::vector<FormListRecord> mFormLists;
        std::vector<VoiceTypeRecord> mVoiceTypes;

        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy; ///< "TYPE.SUB" -> count
        std::uint64_t mInfosOutsideTopicGroup = 0; ///< INFO records not inside a type 7 group (damaged file)
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseDialogueRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const DialogueLimits& limits, DialogueRecords& out);

    [[nodiscard]] bool parseDialogueRecordsBytes(const std::string& bytes, const std::string& name,
        const LoadOrder& order, const DialogueLimits& limits, DialogueRecords& out, Content::Error& error);

    /// The dialogue of a whole load order. Plugins are added in load order; a later record replaces an earlier one with
    /// the same form id (a deleted INFO is removed); a new INFO goes last in its topic.
    class DialogueIndex
    {
    public:
        /// `resolver` turns the stored form ids of this plugin's conditions into load-order ids.
        void add(DialogueRecords records, FormIdResolver resolver);

        [[nodiscard]] const DialogTopic* topic(FormId id) const;
        [[nodiscard]] const GlobalValue* global(FormId id) const;
        [[nodiscard]] const FormListRecord* formList(FormId id) const;

        /// The plugin that made an INFO and the id that plugin stores it with (task 0143): voice files are in the
        /// folder of this plugin, named with that id. Nullopt when no plugin of the index owns the id.
        struct VoiceOrigin
        {
            std::string mPlugin;
            std::uint32_t mStoredId = 0;
        };
        [[nodiscard]] std::optional<VoiceOrigin> voiceOriginOf(const DialogInfo& info) const;

        /// The file name of the plugin at `index` (DialogInfo::mPlugin / DialogTopic::mPlugin); empty out of range.
        [[nodiscard]] const std::string& pluginName(std::uint32_t index) const;
        /// The editor id of a voice type (VTYP); empty when unknown.
        [[nodiscard]] const std::string& voiceTypeName(FormId id) const;

        /// The responses of a topic in the order the game asks them; empty for an unknown topic.
        [[nodiscard]] std::vector<const DialogInfo*> infosOf(FormId topic) const;

        /// Topic ids sorted by id (deterministic), optionally only those with this subtype name.
        [[nodiscard]] std::vector<FormId> topicIds(const std::string& subtypeName = std::string()) const;

        [[nodiscard]] const FormIdResolver& resolverOf(const DialogInfo& info) const
        {
            return mResolvers[info.mPlugin];
        }
        [[nodiscard]] const FormIdResolver& resolverOf(const DialogTopic& topic) const
        {
            return mResolvers[topic.mPlugin];
        }

        [[nodiscard]] std::size_t topicCount() const { return mTopics.size(); }
        [[nodiscard]] std::size_t infoCount() const { return mInfoCount; }

    private:
        struct Slot
        {
            std::unordered_map<FormId, std::size_t> mByInfo; ///< info id -> index in mList
            std::vector<DialogInfo> mList;
            std::vector<bool> mAlive;
        };

        std::vector<FormIdResolver> mResolvers;
        std::vector<std::string> mPluginNames;
        std::unordered_map<FormId, std::string> mVoiceTypes;
        std::unordered_map<FormId, DialogTopic> mTopics;
        std::unordered_map<FormId, Slot> mSlots;
        std::unordered_map<FormId, GlobalValue> mGlobals;
        std::unordered_map<FormId, FormListRecord> mFormLists;
        std::size_t mInfoCount = 0;
    };

    // ---- the conditions world -----------------------------------------------------------------------------------

    /// What the caller knows about the person talking. Everything optional is 0 for "not given".
    struct DialogueSpeaker
    {
        FormId mBase = kNullFormId; ///< the NPC_ record (load-order id)
        FormId mReference = kNullFormId; ///< the placed ACHR, for GetIsReference
        FormId mCell = kNullFormId; ///< the CELL the speaker stands in, for GetInCell
        FormId mWorldspace = kNullFormId; ///< for GetInWorldspace
        std::uint16_t mLevel = 1;
        /// Task 0156. Is the cell an interior? Unset: not known, IsInInterior stays unanswered.
        std::optional<bool> mInterior;
        /// The location (LCTN) the cell names with XLCN; kNullFormId with `mLocationKnown` means "the cell has none".
        FormId mLocation = kNullFormId;
        bool mLocationKnown = false;
    };

    /// Answers, from the records alone, the condition queries a data file can settle for a speaker: GetIsID,
    /// GetIsRace, GetIsSex, GetInFaction / GetFactionRank (template aware), GetIsVoiceType, HasKeyword (the NPC's and
    /// its race's), GetGlobalValue (the GLOB default), IsInList (FLST), GetInCell and GetInWorldspace (when the caller
    /// gave them), GetIsReference, GetIsCurrentWeather (when the caller gave one). Everything else keeps the base
    /// class's default answer; `answers` says which conditions that does not affect.
    ///
    /// Form ids in a CTDA are the plugin-local ones: the world resolves each query's id with `setPlugin`'s resolver.
    /// A stored id the load order cannot resolve matches nothing. NOT thread safe (it holds the current plugin).
    class LocationIndex;
    class QuestVariables;
    class PlacedRefIndex;
    class PackageIndex;

    class SkyrimConditionWorld final : public conditions::ConditionWorld
    {
    public:
        static constexpr conditions::Subject kPlayer = 0x100000007ull; ///< the player (his NPC_ is 0x00000007)

        SkyrimConditionWorld(const ActorIndex& actors, const DialogueIndex& dialogue);

        void setSpeaker(const DialogueSpeaker& speaker);
        /// The stand-in player (task 0155). With it set the world answers GetActorValue / GetBaseActorValue (for the
        /// player and for the speaker, whose numbers come from actor_mechanics), GetLevel, GetGold (the player's),
        /// GetIntimidateSuccess and GetBribeSuccess; without it (the default) all of those keep the default answers.
        /// The pointer must outlive the world.
        void setPlayer(const PlayerStandIn* player)
        {
            mPlayer = player;
            mStatsKnown = false;
        }
        /// The voice type (VTYP) of the speaker given to `setSpeaker`: the traits record's VTCK (after templates), else
        /// the race's voice type for its sex; kNullFormId when none (task 0143).
        [[nodiscard]] FormId speakerVoiceType() const;
        void setCurrentWeather(FormId weather) { mWeather = weather; }
        /// Quest data and state (task 0139). Both null (the default): no quest is known, every quest function keeps the
        /// default answer (stage 0, nothing done, not running) and `answers` says so.
        void setQuests(const QuestIndex* index, const QuestState* state)
        {
            mQuestIndex = index;
            mQuestState = state;
        }
        /// The quest that owns the condition list being evaluated (GetIsAliasRef and run-on quest alias read it).
        /// selectInfo sets it from the INFO's quest, else the topic's.
        void setQuestContext(FormId quest) { mContextQuest = quest; }
        /// The plugin whose stored ids the next queries use (nullptr: ids are taken as load-order ids).
        void setPlugin(const FormIdResolver* resolver) { mResolver = resolver; }

        // ---- task 0156: state a conversation and a fresh game can answer. Every setter is off by default, so a world
        // that never calls one (the package brain's) answers exactly as before.
        /// IsInDialogueWithPlayer for the speaker: the conversation runtime says it is open (true) or not (false).
        void setConversationOpen(bool open)
        {
            mConversationKnown = true;
            mConversationOpen = open;
        }
        /// GetTalkedToPC for the speaker (the runtime sets it after the first greeting). Default: not yet.
        void setTalkedToPlayer(bool talked) { mTalked = talked; }
        /// GetRandomPercent: a seeded generator (splitmix64); the world owns it. Unseeded: 0, unanswered.
        void setRandomSeed(std::uint64_t seed)
        {
            mRandomState = seed;
            mRandomSeeded = true;
        }
        /// The locations (LCTN); with them and `DialogueSpeaker::mLocationKnown` fn_359 and fn_562 are answered.
        void setLocations(const LocationIndex* locations) { mLocations = locations; }
        /// The quest scripts' values before any script ran (fn_629). Null: unanswered.
        void setQuestVariables(const QuestVariables* variables) { mQuestVariables = variables; }

        // ---- task 0160: other references, relationships, positions and the current package.
        /// Every placed reference of the load order (placed_refs.hpp). With it, a condition run on another reference
        /// (run-on 2, a quest alias that names a reference or an actor) is answered from the data: the base's
        /// factions, sex, race and inventory, the reference's initial disabled and dead state, GetDistance between
        /// two references of one cell, GetRelationshipRank from the RELA records. Null (the default): as before.
        void setPlacedRefs(const PlacedRefIndex* refs) { mPlacedRefs = refs; }
        /// The packages (for GetOffersServicesNow). Null: unanswered.
        void setPackages(const PackageIndex* packages) { mPackages = packages; }
        /// What package an actor runs now, by the placed reference's id: nullopt when the caller does not know that
        /// actor (the condition then stays unanswered), 0 for "none". Null function (the default): unanswered.
        using CurrentPackageLookup = std::function<std::optional<FormId>(FormId reference)>;
        void setCurrentPackageLookup(CurrentPackageLookup lookup) { mCurrentPackage = std::move(lookup); }

        /// Subjects of other references: a placed reference (its load-order id in the low 32 bits) and an actor known
        /// by its base only (a unique actor of an alias that has no single placed reference).
        static constexpr conditions::Subject kPlacedTag = 0x200000000ull;
        static constexpr conditions::Subject kBaseTag = 0x300000000ull;

        /// True if the condition's answer comes from data, not from a default (for example a quest stage). Needs the
        /// facts given with setSpeaker / setCurrentWeather.
        [[nodiscard]] bool answers(const survey::Ctda& condition) const;

        // ConditionWorld
        conditions::Subject resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const override;
        bool hasBaseId(conditions::Subject subject, std::uint32_t formId) const override;
        float questStage(std::uint32_t quest) const override;
        bool questStageDone(std::uint32_t quest, std::uint32_t stage) const override;
        bool questRunning(std::uint32_t quest) const override;
        bool questCompleted(std::uint32_t quest) const override;
        bool isAliasRef(conditions::Subject subject, std::uint32_t alias) const override;
        conditions::Subject resolveSubject(const survey::Ctda& condition) const override;
        bool inFaction(conditions::Subject subject, std::uint32_t faction) const override;
        float globalValue(std::uint32_t global) const override;
        bool hasVoiceType(conditions::Subject subject, std::uint32_t voiceOrList) const override;
        bool inCell(conditions::Subject subject, std::uint32_t cell) const override;
        int sexOf(conditions::Subject subject) const override;
        bool hasKeyword(conditions::Subject subject, std::uint32_t keyword) const override;
        float factionRank(conditions::Subject subject, std::uint32_t faction) const override;
        bool isRace(conditions::Subject subject, std::uint32_t race) const override;
        bool inWorldspace(conditions::Subject subject, std::uint32_t worldspace) const override;
        bool isReference(conditions::Subject subject, std::uint32_t reference) const override;
        bool isCurrentWeather(std::uint32_t weather) const override;
        bool baseInList(conditions::Subject subject, std::uint32_t formList) const override;
        conditions::Subject playerSubject() const override { return kPlayer; }
        float actorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const override;
        float baseActorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const override;
        float levelOf(conditions::Subject subject) const override;
        float goldOf(conditions::Subject subject) const override;
        // task 0156
        bool isDead(conditions::Subject subject) const override;
        bool inDialogueWithPlayer(conditions::Subject subject) const override;
        float randomPercent() const override;
        bool inInterior(conditions::Subject subject) const override;
        bool inLocation(conditions::Subject subject, std::uint32_t location) const override;
        bool locationHasKeyword(conditions::Subject subject, std::uint32_t keyword) const override;
        bool talkedToPlayer(conditions::Subject subject) const override;
        float actorValuePercent(conditions::Subject subject, std::uint32_t actorValueIndex) const override;
        float questVariable(std::uint32_t quest, const std::string& name) const override;
        /// fn_47 GetItemCount for the stand-in player: his gold for Gold001 (Skyrim.esm 0x0000000F), nothing else.
        float itemCount(conditions::Subject subject, std::uint32_t item) const override;
        bool playerCanIntimidate(conditions::Subject subject) const override;
        bool playerCanBribe(conditions::Subject subject) const override;
        // task 0160
        bool isDisabled(conditions::Subject subject) const override;
        bool isSneaking(conditions::Subject subject) const override;
        bool isEquipped(conditions::Subject subject, std::uint32_t object) const override;
        float distanceTo(conditions::Subject subject, std::uint32_t reference) const override;
        float relationshipRank(conditions::Subject subject, std::uint32_t target) const override;
        bool runsPackage(conditions::Subject subject, std::uint32_t package) const override;
        bool offersServicesNow(conditions::Subject subject) const override;

    private:
        // ---- task 0160 helpers
        struct ActorView
        {
            FormId mBase = kNullFormId;
            const PlacedEntry* mRef = nullptr; ///< null: only the base is known
        };
        /// The actor or object a subject stands for (the speaker, a placed reference, a base). False for the player and
        /// for anything unknown.
        [[nodiscard]] bool viewOf(conditions::Subject subject, ActorView& out) const;
        [[nodiscard]] static bool isOtherSubject(conditions::Subject subject)
        {
            const std::uint64_t tag = subject & 0xFFFFFFFF00000000ull;
            return tag == kPlacedTag || tag == kBaseTag;
        }
        /// Is the NPC_ resolvable the same way every game (no leveled template in its chain)?
        [[nodiscard]] bool actorStable(FormId base) const;
        /// The template-resolved record facts of the NPC_ a subject stands for (the speaker's, or another actor's when
        /// its resolution does not depend on a random leveled pick); null when they cannot be given.
        [[nodiscard]] const ActorIndex::Resolved* resolvedOf(conditions::Subject subject) const;
        [[nodiscard]] bool distanceKnown(conditions::Subject subject, std::uint32_t reference) const;
        [[nodiscard]] bool relationKnown(conditions::Subject subject, std::uint32_t target) const;
        [[nodiscard]] FormId referenceOf(conditions::Subject subject) const; ///< a placed id, or 0
        enum class ItemState : std::uint8_t
        {
            Unknown,
            None,
            Carried,
            Worn
        };
        /// The initial inventory of an actor or a container reference: the count of `item` (or of the form list's
        /// entries), the state and whether the inventory is closed (no leveled entry, no death item to come).
        struct InventoryAnswer
        {
            bool mKnown = false;
            float mCount = 0.f;
            bool mInOutfit = false;
        };
        [[nodiscard]] InventoryAnswer inventoryOf(const ActorView& view, FormId item) const;
        [[nodiscard]] bool otherAnswers(const survey::Ctda& c, conditions::Subject subject) const;
        [[nodiscard]] FormId baseOfActorReference(FormId reference) const; ///< a placed actor or an NPC_ id; else 0

        [[nodiscard]] FormId resolve(std::uint32_t stored) const; ///< kNullFormId when it cannot be resolved
        [[nodiscard]] bool isSpeaker(conditions::Subject subject) const;
        [[nodiscard]] bool inList(FormId list, FormId value, int depth) const;

        const ActorIndex& mActors;
        const DialogueIndex& mDialogue;
        DialogueSpeaker mSpeaker;
        ActorIndex::Resolved mResolved;
        bool mHasResolved = false;
        FormId mWeather = kNullFormId;
        const FormIdResolver* mResolver = nullptr;
        const QuestIndex* mQuestIndex = nullptr;
        const QuestState* mQuestState = nullptr;
        FormId mContextQuest = kNullFormId;
        const PlayerStandIn* mPlayer = nullptr;
        bool mConversationKnown = false;
        bool mConversationOpen = false;
        bool mTalked = false;
        bool mRandomSeeded = false;
        mutable std::uint64_t mRandomState = 0;
        const LocationIndex* mLocations = nullptr;
        const QuestVariables* mQuestVariables = nullptr;
        const PlacedRefIndex* mPlacedRefs = nullptr;
        const PackageIndex* mPackages = nullptr;
        CurrentPackageLookup mCurrentPackage;
        mutable std::unordered_map<FormId, std::optional<ActorIndex::Resolved>> mResolvedCache;
        /// The speaker or the player: the two subjects whose fresh-game state is known.
        [[nodiscard]] bool isKnownActor(conditions::Subject subject) const
        {
            return isSpeaker(subject) || (mPlayer != nullptr && subject == kPlayer);
        }
        mutable bool mStatsKnown = false;
        mutable bool mStatsOk = false;
        mutable openeden::sdk::game::ActorStats mStats;
        [[nodiscard]] bool speakerStats() const; ///< the speaker's numbers, worked out once; false when unknown

        [[nodiscard]] bool aliasIsSpeaker(
            FormId quest, std::uint32_t alias) const; ///< filled by data, with the speaker
        [[nodiscard]] bool questKnown(std::uint32_t storedQuest) const;
        [[nodiscard]] bool aliasKnown(std::uint32_t alias) const; ///< the context quest's alias is filled from data
    };

    // ---- choosing a response ------------------------------------------------------------------------------------

    struct SelectionOptions
    {
        /// Is the topic's (or INFO's) quest running? Null: every quest counts as running. With a `QuestState`, give
        /// `[&](FormId q) { return !state.exists(q) || state.running(q); }` (an unknown quest is not held back).
        std::function<bool(FormId quest)> mQuestRunning;
        /// Has this say-once INFO been said? Null: nothing has.
        std::function<bool(FormId info)> mSaid;
        /// The quests, so that an INFO of a quest is skipped when the quest's own dialogue conditions fail (quest.hpp,
        /// QuestRecord::mConditions; RECALLED-VERIFY: they gate all of the quest's dialogue). Null: not asked. The
        /// world needs `setQuests` for the conditions that read quest state.
        const QuestIndex* mQuests = nullptr;
        /// Chooses among INFOs with the Random flag that pass: index = pick % count (the caller draws it).
        std::uint32_t mPick = 0;
    };

    struct Selection
    {
        const DialogInfo* mInfo = nullptr; ///< null: nothing passes
        std::uint32_t mConsidered = 0; ///< INFOs looked at
        std::uint32_t mPassing = 0; ///< INFOs whose conditions were true (and not skipped)
        /// Before the pick was found: INFOs ahead of (and including) it with a condition the world could not answer
        /// from data. The pick is only as sure as these are zero.
        std::uint32_t mUncertainBefore = 0;
        const DialogTopic* mTopic = nullptr; ///< set by pickFromTopics
    };

    /// The INFO `speaker` gets from one topic: infos in order; one skipped if its quest is not running, it is Say Once
    /// and said, or it names another speaker (ANAM); the first whose conditions pass wins; if that one has the Random
    /// flag, the infos with the Random flag that pass (in the topic, from that one on) are collected and `pick`
    /// chooses among them. (RECALLED-VERIFY: the Random rule.)
    [[nodiscard]] Selection selectInfo(const DialogueIndex& index, FormId topic, SkyrimConditionWorld& world,
        const DialogueSpeaker& speaker, const SelectionOptions& options, conditions::Evaluator* evaluator = nullptr);

    /// The topics with this subtype name (for example "HELO"), highest priority first (ties by form id), and the first
    /// that gives an INFO. (RECALLED-VERIFY: that the game orders competing topics by priority.)
    [[nodiscard]] Selection pickFromTopics(const DialogueIndex& index, const std::string& subtypeName,
        SkyrimConditionWorld& world, const DialogueSpeaker& speaker, const SelectionOptions& options,
        conditions::Evaluator* evaluator = nullptr);

    /// Counts the conditions of `info` that are not answered from data (see SkyrimConditionWorld::answers); a
    /// condition run on anybody but the subject counts too, except GetIsID run on the player.
    [[nodiscard]] std::uint32_t unansweredConditions(const DialogInfo& info, const SkyrimConditionWorld& world);
}

#endif
