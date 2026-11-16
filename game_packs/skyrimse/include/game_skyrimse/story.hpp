#ifndef OPENEDEN_GAME_SKYRIMSE_STORY_H
#define OPENEDEN_GAME_SKYRIMSE_STORY_H

// Skyrim SE story manager (task 0187, milestone M-S6 "dialogue and quest scripts"). Pack side only: decode the story
// manager's nodes (SMBN branch, SMQN quest, SMEN event), merge a load order, and walk the tree when an event is raised,
// which says which quests start. Pure: no engine, Papyrus, renderer or Qt type; the caller starts the quests.
//
// Layout, VERIFIED on Skyrim.esm (571 nodes: SMEN 24, SMBN 99, SMQN 448; aggregates by tools of the task):
//  - every node: EDID (optional), PNAM u32 parent node, SNAM u32 sibling, CITC u32 condition count, CTDA..., DNAM u32,
//    XNAM u32. SMEN adds ENAM (four characters, the event: 24 distinct, CLOC, SCPT, LEVL, KILL, ADIA ...). SMQN adds QNAM
//    u32 (the quest count), NNAM u32 per quest, optional RNAM f32 per quest (hours until reset), MNAM u32 (number of
//    quests to run; only with the 0x40000 DNAM bit, 17 nodes) and FNAM (7 nodes, not read).
//  - the 24 event nodes (SMEN) have the story manager's root (0x5B, a branch whose own parent is 0) as parent; every other
//    node's parent is a node of the file.
//  - SNAM names the PREVIOUS sibling: the children of one parent form a chain, the first has SNAM 0 (RECALLED-VERIFY: of
//    the two readings, only this one puts "WEPriorityQuests" and "WESpecificQuestsNode" before the general "WEQuests"
//    in Skyrim.esm's own world event node). The decoder orders children by that chain; a broken chain falls back to
//    form id order.
//  - DNAM low bits (RECALLED-VERIFY, names from the Creation Kit's node dialog): SMBN and SMEN bit 0 "random order";
//    SMQN bit 0 "do all quests before repeating", bit 1 "shares the event" (the walk goes on after this node starts a
//    quest). Bit 18 (0x40000) says MNAM is the number of quests to run. Bits 16 and 17 occur and are not read.
//  - A condition of a node and a quest's own start conditions (QuestRecord::mStartConditions) may use fn_576
//    GetEventData(function, member, form): param 1 is two 16 bit halves, the low half the function and the high half two
//    characters naming the member (K1 the keyword the event was sent on, L1 the location or the old location, L2 the new
//    location, R1 and R2 the references, V1 and V2 the numbers; VERIFIED: 36 of 36 distinct member codes in the data
//    read this way). Function 0 "the member is this form", 2 "the number of the member", 3 "the member's location has this
//    keyword" are the only ones that occur in the nodes; others answer 0.
//
// The walk (RECALLED-VERIFY, not settled by the data; every rule is a named option of `StoryManager`):
//  - the event node of the event type is the root; children are tried in sibling order (random order for a node with
//    the random bit, a seeded shuffle);
//  - a node whose conditions fail is skipped with its children; a branch or event node whose conditions pass is entered;
//  - a quest node whose conditions pass starts quests: its quests in order, each one not running and not completed whose
//    own start conditions pass, up to the node's number (MNAM, else 1); with "do all before repeating" a quest the node
//    started is skipped until every quest of the node has been started once;
//  - when a node started a quest and does not share the event, the walk ends.

#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace StoryFlags
    {
        inline constexpr std::uint32_t Random = 0x1; ///< SMBN, SMEN: children in random order
        inline constexpr std::uint32_t DoAllBeforeRepeating = 0x1; ///< SMQN
        inline constexpr std::uint32_t SharesEvent = 0x2; ///< SMQN
        inline constexpr std::uint32_t NumQuestsToRun = 0x40000; ///< SMQN: MNAM holds the number
    }

    enum class StoryNodeKind : std::uint8_t
    {
        Branch,
        Quest,
        Event,
    };

    struct StoryNode
    {
        RecordCommon mCommon;
        StoryNodeKind mKind = StoryNodeKind::Branch;
        FormId mParent = kNullFormId;
        FormId mSibling = kNullFormId; ///< the previous sibling (0: first)
        std::uint32_t mFlags = 0; ///< DNAM
        std::uint32_t mMaxConcurrent = 0; ///< XNAM (read, not used)
        std::uint32_t mQuestsToRun = 0; ///< MNAM (0: none stored)
        std::string mEvent; ///< SMEN: ENAM, four characters
        std::vector<survey::Ctda> mConditions;
        bool mConditionsOk = true; ///< false: a CTDA was damaged, the node never passes
        std::vector<FormId> mQuests; ///< SMQN: NNAM in file order
        std::uint32_t mPlugin = 0; ///< index into StoryIndex's plugins
    };

    struct StoryLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxConditions = 4096;
        std::uint32_t mMaxQuests = 65536;
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    struct StoryRecords
    {
        std::string mName;
        std::vector<StoryNode> mNodes;
        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseStoryRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const StoryLimits& limits, StoryRecords& out);

    [[nodiscard]] bool parseStoryRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const StoryLimits& limits, StoryRecords& out, Content::Error& error);

    /// The nodes of a whole load order: a later record replaces an earlier one with the same form id (a deleted one is
    /// removed).
    class StoryIndex
    {
    public:
        void add(StoryRecords records, FormIdResolver resolver);

        [[nodiscard]] const StoryNode* node(FormId id) const;
        /// The children of a node in sibling order (see the header). Valid after the last add().
        [[nodiscard]] const std::vector<FormId>& children(FormId parent) const;
        /// The event nodes (SMEN) of an event type ("CLOC"), by form id; there may be several (a mod adds one).
        [[nodiscard]] std::vector<FormId> eventNodes(const std::string& event) const;
        [[nodiscard]] const FormIdResolver& resolverOf(const StoryNode& node) const { return mResolvers[node.mPlugin]; }
        [[nodiscard]] std::size_t nodeCount() const { return mNodes.size(); }
        /// Counts, for the log: nodes by kind.
        [[nodiscard]] std::size_t count(StoryNodeKind kind) const;

    private:
        void rebuild() const;

        std::vector<FormIdResolver> mResolvers;
        std::unordered_map<FormId, StoryNode> mNodes;
        mutable bool mDirty = true;
        mutable std::unordered_map<FormId, std::vector<FormId>> mChildren;
        static const std::vector<FormId> kNoChildren;
    };

    /// What an event carries (the data of Keyword.SendStoryEvent, or of a location change).
    struct StoryEvent
    {
        std::string mType; ///< "SCPT", "CLOC", ...
        FormId mKeyword = kNullFormId; ///< K1: the keyword SendStoryEvent was called on
        FormId mLocation1 = kNullFormId; ///< L1: akLoc, or the location the player left
        FormId mLocation2 = kNullFormId; ///< L2: the location the player came to
        FormId mRef1 = kNullFormId; ///< R1
        FormId mRef2 = kNullFormId; ///< R2
        std::int32_t mValue1 = 0; ///< V1
        std::int32_t mValue2 = 0; ///< V2
    };

    /// What fn_576 reads from a condition world: the event being walked and a few lookups. A world that is not a
    /// `StoryEventSource` answers every GetEventData with 0. `resolve` turns a stored (plugin-local) form id of the
    /// condition being evaluated into a load-order id.
    class StoryEventSource
    {
    public:
        virtual ~StoryEventSource() = default;
        [[nodiscard]] virtual const StoryEvent* currentStoryEvent() const = 0;
        [[nodiscard]] virtual FormId resolveStoryForm(std::uint32_t stored) const = 0;
        /// Is the location `location` the location `ancestor` or inside it?
        [[nodiscard]] virtual bool storyLocationWithin(FormId location, FormId ancestor) const = 0;
        [[nodiscard]] virtual bool storyLocationHasKeyword(FormId location, FormId keyword) const = 0;
        /// The base form of a placed reference (0 when unknown).
        [[nodiscard]] virtual FormId storyBaseOf(FormId reference) const = 0;
    };

    /// fn_576 GetEventData for the story walk: the extra function a `conditions::Evaluator` needs (the global table does
    /// not know the index).
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& storyExtraFunctions();

    class StoryManager
    {
    public:
        struct Options
        {
            /// Quests started by one quest node when it stores no number.
            std::uint32_t mDefaultQuestsToRun = 1;
            /// A walk that started a quest ends unless the node shares the event.
            bool mStopAfterStart = true;
        };

        struct Stats
        {
            std::uint64_t mEvents = 0;
            std::uint64_t mEventsWithNoNode = 0; ///< an event type the data has no event node for
            std::uint64_t mNodesVisited = 0;
            std::uint64_t mNodesPassed = 0;
            std::uint64_t mQuestsStarted = 0;
            std::uint64_t mQuestsSkipped = 0; ///< running, completed, or its start conditions failed
        };

        /// All references must outlive the manager. `world` answers every condition (it owns the event data given by
        /// `raise`: see SkyrimConditionWorld::setStoryEvent).
        StoryManager(const StoryIndex& nodes, const QuestIndex& quests, Options options = {});

        /// Raises an event: walks the tree and returns the quests to start, in order. `isStartable(quest)` says whether
        /// the quest may start (exists, is not running or completed); `evaluate(list, resolver)` evaluates a condition
        /// list written by the plugin the resolver belongs to against the event. Deterministic: the only varying input is
        /// the event counter (the seed of the random shuffle).
        using Evaluate = std::function<bool(const std::vector<survey::Ctda>&, const FormIdResolver&)>;
        using Startable = std::function<bool(FormId quest)>;
        [[nodiscard]] std::vector<FormId> raise(const StoryEvent& event, const Evaluate& evaluate, const Startable& startable);

        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        struct Walk;
        bool visit(FormId node, Walk& walk);

        const StoryIndex& mNodes;
        const QuestIndex& mQuests;
        Options mOptions;
        Stats mStats;
        std::uint64_t mEventCounter = 0;
        std::map<FormId, std::set<FormId>> mCycle; ///< node -> quests it started in the current cycle
    };
}

#endif
