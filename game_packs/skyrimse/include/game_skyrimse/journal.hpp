#ifndef OPENEDEN_GAME_SKYRIMSE_JOURNAL_H
#define OPENEDEN_GAME_SKYRIMSE_JOURNAL_H

// The quest journal of a Skyrim SE session and its script bindings (task 0185, milestone M-S6).
//
// `journal::build` is pure: it turns the quest records, the run-time `QuestState` and two hooks (text of a localised
// string, the displayed and completed flags of an objective) into a `Snapshot` of what the journal shows. The
// `JournalService` keeps the snapshot, rebuilds it lazily when the quest state's revision moved, and declares the
// bindings namespace "skyrimse.journal" (version 1.0), all Read:
//
//   ready() -> bool                     the world view is built and the quest state is reachable
//   revision() -> int                   changes when the quest state (or an objective flag) changes
//   quests(completed) -> string[]       the ids (8 lower-case hex digits) of the listed quests, in the journal's order
//   name(id), entry(id), status(id)     status is "running", "completed" or "failed"
//   log(id) -> string[]                 the log texts of the stages that are done, oldest first (the last is current)
//   objectives(id) -> string[]          the displayed objectives' texts, in index order
//   objective_done(id) -> bool[]        same order
//
// Threading: everything here runs on the main thread (the game phase callback and the script update, which in this tree
// runs on the main thread too); there is no lock. Read functions of an unknown id answer "nothing" (an empty text or
// array), never an error.
//
// Which quests are listed (RECALLED-VERIFY against the game, decided here): a quest the state knows that is running or
// completed, that has a name, and that has a log entry text or a displayed objective. The entry of a done stage is the
// FIRST entry of the stage with a text (the entries' own conditions are not evaluated).

#include <game_skyrimse/quest.hpp>

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::journal
{
    inline constexpr std::size_t kMaxQuestsPerList = 256;
    inline constexpr std::size_t kMaxLogEntries = 64;
    inline constexpr std::size_t kMaxObjectives = 32;
    inline constexpr std::size_t kMaxTextBytes = 4096;

    enum class Status : std::uint8_t
    {
        Running,
        Completed,
        Failed,
    };
    [[nodiscard]] const char* statusName(Status status);

    struct ObjectiveFlags
    {
        bool mDisplayed = false;
        bool mCompleted = false;
    };

    struct ObjectiveView
    {
        std::uint16_t mIndex = 0;
        std::string mText;
        bool mDone = false;
    };

    struct QuestView
    {
        FormId mQuest = kNullFormId;
        std::string mName;
        Status mStatus = Status::Running;
        std::string mEntry; ///< the text of the current stage's entry (else the last log text)
        std::vector<std::string> mLog; ///< done stages, ascending; the current stage's text is the last
        std::vector<ObjectiveView> mObjectives;
    };

    struct Snapshot
    {
        std::uint64_t mRevision = 0;
        std::vector<QuestView> mRunning; ///< status running
        std::vector<QuestView> mCompleted; ///< status completed or failed
        std::uint64_t mDropped = 0; ///< quests left out by the per-list cap
        // what the last build saw, for the log: quests running or completed, of them with no name, with no log text or
        // objective
        std::uint64_t mActive = 0;
        std::uint64_t mNoName = 0;
        std::uint64_t mNoText = 0;
        [[nodiscard]] const QuestView* find(FormId quest) const;
    };

    /// Skyrim log and objective texts carry tags (`<Alias=Staff>`, `<Global=Gold>`): the game fills them in at run
    /// time, and the UI would take `<` for markup. An `<Alias=...>` or `<Alias.ShortName=...>` tag becomes its
    /// alias name (the editor name of the alias, not the reference's display name: decision for toddhow), any other
    /// tag becomes `...`, and a stray `<` or `>` is dropped. Applied to every text the journal shows.
    [[nodiscard]] std::string expandTags(std::string_view text);

    using TextFn = std::function<std::string(const LString&, std::uint32_t plugin, StringTableKind)>;
    using ObjectiveFn = std::function<ObjectiveFlags(FormId quest, std::uint16_t index)>;

    struct Inputs
    {
        const QuestIndex* mQuests = nullptr;
        const QuestState* mState = nullptr;
        TextFn mText; ///< empty: only plain (not localised) texts are read
        ObjectiveFn mObjective; ///< empty: no objective is displayed
    };

    /// Fills `out` (cleared first; its revision is left to the caller). Pure and deterministic.
    void build(const Inputs& inputs, Snapshot& out);

    /// A developer aid (the pack setting `journal test quests`): moves real quests to a stage that has log text, so
    /// that a fresh session has something to show. The first `count` quests (ascending form id) that have a name and
    /// a stage whose log entry has a text and neither completes nor fails the quest are set to their lowest such
    /// stage; the next quest that has a completing stage with a text is completed. Returns how many quests were set.
    /// Deterministic.
    std::size_t applyTestQuests(const QuestIndex& index, QuestState& state, const TextFn& text, std::size_t count);

    struct Access
    {
        const QuestIndex* mQuests = nullptr;
        const QuestState* mState = nullptr;
        TextFn mText;
    };

    struct Hooks
    {
        /// Fills `out` and returns true when the quest data is reachable; false means "ask again".
        std::function<bool(Access& out)> mAccess;
        ObjectiveFn mObjective;
        /// Changes whenever an objective flag changes (0 when there is no source).
        std::function<std::uint64_t()> mObjectiveRevision;
        /// One line per rebuild (the pack logs it). May be empty.
        std::function<void(const std::string&)> mLog;
    };

    [[nodiscard]] std::string formatQuestId(FormId id); ///< 8 lower-case hex digits
    [[nodiscard]] bool parseQuestId(std::string_view text, FormId& out);

    class JournalService
    {
    public:
        explicit JournalService(Hooks hooks);
        ~JournalService();
        JournalService(const JournalService&) = delete;
        JournalService& operator=(const JournalService&) = delete;

        /// Asks for the quest data until it is there. Never blocks. True once usable.
        bool poll();
        [[nodiscard]] bool ready() const { return mReady; }

        /// The state's revision combined with the objectives' (0 before ready).
        [[nodiscard]] std::uint64_t revision() const;
        /// The snapshot, rebuilt first when the revision moved since the last build.
        [[nodiscard]] const Snapshot& snapshot();
        [[nodiscard]] std::uint64_t builds() const { return mBuilds; }

        /// Declares the namespace. The service must outlive the host's use of the handlers.
        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        Access mAccess;
        Snapshot mSnapshot;
        bool mReady = false;
        bool mBuilt = false;
        std::uint64_t mBuilds = 0;
    };
}

#endif
