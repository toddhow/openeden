#ifndef OPENEDEN_GAME_SKYRIMSE_QUEST_H
#define OPENEDEN_GAME_SKYRIMSE_QUEST_H

// Skyrim SE quests (task 0139, milestone M-S4 "talk to an NPC", slice 2; docs/design/skyrim-se-implementation-plan.md
// sections 4.16 and 4.20). Pack side only: decode QUST (flags, stages with their log entries, objectives, aliases and
// how each alias is filled, the quest's dialogue conditions and its start conditions), merge a load order, and keep a pure `QuestState`
// (running, current stage, stages done, completed, alias fills) that starts from what the data alone says. Plain
// structs; no engine, renderer, UI or Qt type. Nothing calls it from the engine yet.
//
// Layout status of what is read. VERIFIED = checked on the five installed masters by throw-away walkers and by
// tools/skyrimse_quest_check (aggregates in the task log: 2,391 QUST). RECALLED-VERIFY = from memory of xEdit's TES5
// definitions or the Creation Kit wiki, not settled by the data. Sources allowed by the decision 0014 amendment;
// nothing is copied.
//
//  - Order of subrecords (VERIFIED): EDID, [VMAD], [FULL], DNAM, [ENAM], [FLTR], CTDA* (the quest's dialogue
//    conditions), NEXT (empty marker), CTDA* (the quest's start conditions, for the story manager), then stages (INDX
//    ... ), objectives (QOBJ ...), ANAM (next alias id), then aliases (ALST or ALLS ... ALED). Each CTDA may be
//    followed by CIS1/CIS2. A CTDA belongs to what came last: the head (before or after NEXT), a log entry (after
//    QSDT), an objective target (after QSTA) or an alias. WHICH SIDE OF NEXT IS WHICH is settled by the data: before
//    NEXT 435 CTDA in 230 quests, mostly GetIsVoiceType (98), GetInFaction (75), GetIsAliasRef (51), the shape of
//    dialogue conditions; after NEXT 625 CTDA in 325 quests, 317 of which have an ENAM start event, mostly GetStage
//    (87), GetGlobalValue (85), GetRandomPercent-like and GetLevel (40), the shape of start conditions. The names
//    (dialogue conditions before, start conditions after) are from xEdit's definition, RECALLED-VERIFY.
//  - DNAM 12 bytes (VERIFIED size on 2,391 of 2,391): u16 flags, u8 priority, u8 (form version, ignored), u32 (0 for
//    2,110, else other small numbers, left raw), u32 quest type. Flag bits VERIFIED by the value distribution
//    (0x0001 on 455 quests that dialogue-heavy generic quests use, 0x0100 on the main quest) and RECALLED-VERIFY by
//    name: 0x1 start game enabled, 0x2 completed, 0x4 add idle topics to hello, 0x8 allow repeated stages, 0x100 run
//    once, 0x200 exclude from dialogue export, 0x400 warn on alias fill failure. Quest type values 0 to 11 all occur
//    (RECALLED-VERIFY names: 1 main quest, 2 mages guild, 3 thieves guild, 4 dark brotherhood, 5 companions,
//    6 miscellaneous, 7 daedric, 8 side quest, 9 civil war, 10 dlc01 vampire, 11 dlc02 dragonborn).
//  - ENAM four characters (the event that starts the quest: "ADIA" 906, "SCPT" 278, "CLOC" 186 ...), FLTR a string,
//    ANAM u32 next alias id (VERIFIED: always above every alias id of the quest).
//  - Stage: INDX 4 bytes (VERIFIED size): u16 stage index, u8 flags, u8 (garbage in the data, 0xAB, 0x74 ..., never
//    read). Flag values seen: 0, 0x2, 0x4, 0x8, 0xA; names RECALLED-VERIFY (0x2 start up stage, 0x4 shut down stage,
//    0x8 keep instance data from here on). One stage has one or more log entries; each starts with QSDT (1 byte
//    flags: 0 for 7,312, 1 for 495, 2 for 104; RECALLED-VERIFY 1 complete quest, 2 fail quest), then optional CTDA,
//    CNAM (log text, an id into .STRINGS in a localised plugin), NAM0 (next quest), and script fragment subrecords
//    (SCHR/SCTX/QNAM, left out). Stage ids repeat in one quest once (VERIFIED: 1 of the quests).
//  - Objective: QOBJ u16 index, FNAM u32 flags, NNAM text, then targets QSTA (8 bytes: u32 alias id, u32 flags;
//    VERIFIED to be the alias id, flag 1 on 13 of 2,785) each followed by its CTDA.
//  - Alias: ALST (reference alias) or ALLS (location alias) u32 id (VERIFIED unique within a quest), ALID name, FNAM u32
//    flags (RECALLED-VERIFY names, see AliasFlags), then at most one kind of fill. Which subrecord shows the fill is
//    from xEdit (RECALLED-VERIFY) but the combinations in the data fit: ALFR a specific placed reference (never 0:
//    VERIFIED), ALUA a unique actor (an NPC_, never 0), ALFL a specific location, ALFA (+ ALRT) a reference found in a
//    location alias (the value is an alias id of the same quest: VERIFIED 2,684 of 2,684), ALFI an alias to force the reference into when it is
//    filled (an alias id of the same quest: VERIFIED 190 of 190; it comes beside ALFR, ALUA and conditions, so it is
//    an extra, not a way of filling; the name is xEdit's, RECALLED-VERIFY), ALEQ + ALEA an alias of another quest, ALCO/ALCA/ALCL a
//    created object, ALFE + ALFD from an event, ALNA + ALNT near another alias; no fill subrecord at all means "find a
//    matching reference" by the alias's CTDA conditions. Other alias subrecords (ALFC factions, ALPC packages, ALSP
//    spells, CNTO/COCT items, VTCK voice types, KNAM, ECOR, ALDN, ALEQ ...) are not used here.
//
// A form id stored in a QUST is a plugin-local id; the parser resolves the ones it stores as fields (ALFR, ALUA, ...)
// with the plugin's resolver. The CTDA lists keep plugin-local ids (like dialogue.hpp), resolved at query time.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace QuestFlags
    {
        inline constexpr std::uint16_t StartGameEnabled = 0x0001;
        inline constexpr std::uint16_t Completed = 0x0002;
        inline constexpr std::uint16_t AddIdleTopicsToHello = 0x0004;
        inline constexpr std::uint16_t AllowRepeatedStages = 0x0008;
        inline constexpr std::uint16_t RunOnce = 0x0100;
        inline constexpr std::uint16_t ExcludeFromDialogueExport = 0x0200;
        inline constexpr std::uint16_t WarnOnAliasFillFailure = 0x0400;
    }

    namespace StageFlags
    {
        inline constexpr std::uint8_t StartUp = 0x02;
        inline constexpr std::uint8_t ShutDown = 0x04;
        inline constexpr std::uint8_t KeepInstanceData = 0x08;
    }

    namespace LogEntryFlags
    {
        inline constexpr std::uint8_t CompleteQuest = 0x01;
        inline constexpr std::uint8_t FailQuest = 0x02;
    }

    namespace AliasFlags
    {
        inline constexpr std::uint32_t ReservesReference = 0x0001;
        inline constexpr std::uint32_t Optional = 0x0002;
        inline constexpr std::uint32_t QuestObject = 0x0004;
        inline constexpr std::uint32_t AllowReuseInQuest = 0x0008;
        inline constexpr std::uint32_t AllowDead = 0x0010;
        inline constexpr std::uint32_t MatchingRefInLoadedArea = 0x0020;
        inline constexpr std::uint32_t Essential = 0x0040;
        inline constexpr std::uint32_t AllowDisabled = 0x0080;
        inline constexpr std::uint32_t StoresText = 0x0100;
        inline constexpr std::uint32_t AllowReserved = 0x0200;
        inline constexpr std::uint32_t Protected = 0x0400;
    }

    /// How an alias is filled (by which subrecord, in this precedence when more than one is present).
    enum class AliasFillKind : std::uint8_t
    {
        FindMatching, ///< no fill subrecord: a reference found by the alias's conditions (the game decides at run time)
        SpecificReference, ///< ALFR
        UniqueActor, ///< ALUA
        SpecificLocation, ///< ALFL
        ExternalAlias, ///< ALEQ + ALEA
        CreatedObject, ///< ALCO / ALCA / ALCL
        FromEvent, ///< ALFE
        NearAlias, ///< ALNA
        InLocationAlias, ///< ALFA (+ ALRT)
    };

    struct QuestLogEntry
    {
        std::uint8_t mFlags = 0; ///< QSDT
        std::vector<survey::Ctda> mConditions;
        LString mText; ///< CNAM
        FormId mNextQuest = kNullFormId; ///< NAM0
    };

    struct QuestStage
    {
        std::uint16_t mIndex = 0;
        std::uint8_t mFlags = 0;
        std::vector<QuestLogEntry> mEntries;
        [[nodiscard]] bool startUp() const { return (mFlags & StageFlags::StartUp) != 0; }
        [[nodiscard]] bool shutDown() const { return (mFlags & StageFlags::ShutDown) != 0; }
        /// Some log entry of the stage completes the quest.
        [[nodiscard]] bool completesQuest() const;
        [[nodiscard]] bool failsQuest() const;
    };

    struct QuestObjectiveTarget
    {
        std::uint32_t mAlias = 0;
        std::uint32_t mFlags = 0;
        std::vector<survey::Ctda> mConditions;
    };

    struct QuestObjective
    {
        std::uint16_t mIndex = 0;
        std::uint32_t mFlags = 0;
        LString mText; ///< NNAM
        std::vector<QuestObjectiveTarget> mTargets;
    };

    struct QuestAlias
    {
        std::uint32_t mId = 0;
        bool mIsLocation = false; ///< ALLS
        std::string mName; ///< ALID
        std::uint32_t mFlags = 0; ///< FNAM
        AliasFillKind mFill = AliasFillKind::FindMatching;
        FormId mReference = kNullFormId; ///< ALFR
        FormId mActor = kNullFormId; ///< ALUA
        FormId mLocation = kNullFormId; ///< ALFL
        FormId mExternalQuest = kNullFormId; ///< ALEQ
        std::uint32_t mOtherAlias = 0; ///< ALEA (external), ALFA (in location alias), ALNA (near)
        std::uint32_t mForceIntoAlias = 0; ///< ALFI: also put the reference into this alias when filled (0: none)
        FormId mReferenceType = kNullFormId; ///< ALRT
        std::vector<survey::Ctda> mConditions;
        /// The data alone says who fills it: a specific reference or a unique actor.
        [[nodiscard]] bool dataFillable() const
        {
            return mFill == AliasFillKind::SpecificReference || mFill == AliasFillKind::UniqueActor;
        }
    };

    struct QuestRecord
    {
        RecordCommon mCommon;
        LString mName; ///< FULL
        std::uint16_t mFlags = 0;
        std::uint8_t mPriority = 0;
        std::uint32_t mType = 0;
        std::string mEvent; ///< ENAM, four characters or empty
        std::string mFilter; ///< FLTR
        std::uint32_t mNextAliasId = 0; ///< ANAM
        bool mHasDnam = false;
        std::vector<survey::Ctda> mConditions; ///< the quest's dialogue conditions (before NEXT)
        std::vector<survey::Ctda> mStartConditions; ///< the start conditions (after NEXT; the story manager's)
        bool mConditionsOk = true; ///< false: a CTDA of the dialogue conditions was damaged
        bool mStartConditionsOk = true; ///< false: a CTDA of the start conditions was damaged
        std::vector<QuestStage> mStages; ///< file order
        std::vector<QuestObjective> mObjectives;
        std::vector<QuestAlias> mAliases;
        std::uint32_t mPlugin = 0; ///< index into QuestIndex's plugins: the plugin whose record won

        [[nodiscard]] bool startsEnabled() const { return (mFlags & QuestFlags::StartGameEnabled) != 0; }
        [[nodiscard]] const QuestStage* stage(std::uint16_t index) const;
        /// The start up stage with the lowest index (RECALLED-VERIFY: the game runs it when the quest starts), or null.
        [[nodiscard]] const QuestStage* startUpStage() const;
        [[nodiscard]] const QuestAlias* alias(std::uint32_t id) const;
    };

    struct QuestLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxStages = 4096;
        std::uint32_t mMaxEntriesPerStage = 256;
        std::uint32_t mMaxObjectives = 1024;
        std::uint32_t mMaxAliases = 4096;
        std::uint32_t mMaxConditions = 4096; ///< per list
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    struct QuestRecords
    {
        std::string mName;
        std::vector<QuestRecord> mQuests;
        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy; ///< "TYPE.SUB" -> count
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseQuestRecords(const Content::Plugin& plugin, const LoadOrder& order,
        const QuestLimits& limits, QuestRecords& out);

    [[nodiscard]] bool parseQuestRecordsBytes(const std::string& bytes, const std::string& name,
        const LoadOrder& order, const QuestLimits& limits, QuestRecords& out, Content::Error& error);

    /// The quests of a whole load order: a later record replaces an earlier one with the same form id (a deleted one
    /// is removed).
    class QuestIndex
    {
    public:
        void add(QuestRecords records, FormIdResolver resolver);

        [[nodiscard]] const QuestRecord* quest(FormId id) const;
        [[nodiscard]] std::vector<FormId> questIds() const; ///< sorted
        [[nodiscard]] std::size_t questCount() const { return mQuests.size(); }
        [[nodiscard]] const FormIdResolver& resolverOf(const QuestRecord& quest) const
        {
            return mResolvers[quest.mPlugin];
        }

    private:
        std::vector<FormIdResolver> mResolvers;
        std::unordered_map<FormId, QuestRecord> mQuests;
    };

    // ---- the state -----------------------------------------------------------------------------------------------

    /// Who fills an alias right now. `mKnown` is false for an alias the data cannot fill (a conditional find, a
    /// location, ...): then nothing about it is answered.
    struct AliasFill
    {
        bool mKnown = false;
        FormId mReference = kNullFormId; ///< a placed reference
        FormId mActor = kNullFormId; ///< an NPC_ (unique actor); every reference of that base counts
    };

    /// What the data alone says about the quests at the start of a game, counted.
    struct InitialStateReport
    {
        std::uint64_t mQuests = 0;
        std::uint64_t mStartEnabled = 0;
        std::uint64_t mStartEnabledWithStartUpStage = 0;
        std::uint64_t mAlreadyCompleted = 0;
        std::uint64_t mAliases = 0; ///< all aliases of all quests
        std::uint64_t mReferenceAliases = 0;
        std::uint64_t mLocationAliases = 0;
        std::uint64_t mAliasesDataFillable = 0; ///< specific reference or unique actor (filled in the state)
        std::uint64_t mAliasesConditional = 0; ///< left empty
        std::map<AliasFillKind, std::uint64_t> mByFill;
    };

    /// Pure model of the quests' run-time state. Not thread safe. No clock, no randomness.
    class QuestState
    {
    public:
        /// Start-game-enabled quests are running; one with a start up stage is at that stage (the lowest index) and
        /// has it done, else at stage 0; a quest with the Completed flag is completed. Specific-reference and
        /// unique-actor aliases are filled from data; the others stay empty. Quests that do not start enabled exist
        /// but are not running.
        static QuestState initial(const QuestIndex& index, InitialStateReport* report = nullptr);

        [[nodiscard]] bool exists(FormId quest) const;
        [[nodiscard]] bool running(FormId quest) const;
        [[nodiscard]] bool completed(FormId quest) const;
        /// The current stage (0 for an unknown or never staged quest).
        [[nodiscard]] float stage(FormId quest) const;
        [[nodiscard]] bool stageDone(FormId quest, std::uint32_t stage) const;
        [[nodiscard]] AliasFill aliasFill(FormId quest, std::uint32_t alias) const;

        /// The state changes a script would make (the caller wires them; nothing here runs scripts).
        void start(FormId quest);
        void stop(FormId quest);
        /// Sets the stage: it becomes current and done; a log entry that completes the quest completes it; a shut
        /// down stage stops it. A quest that is not running is started first (RECALLED-VERIFY: the game does).
        void setStage(const QuestIndex& index, FormId quest, std::uint32_t stage);
        void setAliasFill(FormId quest, std::uint32_t alias, const AliasFill& fill);

    private:
        struct Entry
        {
            bool mRunning = false;
            bool mCompleted = false;
            std::uint32_t mStage = 0;
            std::set<std::uint32_t> mDone;
            std::map<std::uint32_t, AliasFill> mAliases;
        };
        std::map<FormId, Entry> mQuests;
    };
}

#endif
