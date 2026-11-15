#ifndef OPENEDEN_GAME_SKYRIMSE_PAPYRUS_HOST_H
#define OPENEDEN_GAME_SKYRIMSE_PAPYRUS_HOST_H

// The Skyrim SE game pack's Papyrus host (task 0176, milestone M-S6). It owns a `ScriptPapyrus::PapyrusRuntime`
// (components/script_papyrus), loads ".pex" files through a reader the pack gives it, attaches the scripts that VMAD
// subrecords name (quests, quest fragments, alias scripts, scripts on placed references), fills their properties,
// raises the events the neutral world can raise, runs them on the main thread in the pack's frame phase and answers
// the natives a script calls from what the pack knows. Everything not answered is tolerant: the runtime logs it once
// and returns the zero of its type.
//
// What it does NOT do (see the task): no save chunk (the host leaves `serialiseState()` and `restoreState()` as the
// hook for the saves work), no OnHit or OnDeath (the combat work), no array properties, no scene, package or perk
// fragments, no INFO fragments, no scripts on base forms.
//
// Threading: every call, and every native, runs on the main thread. Determinism: no clock and no randomness of its
// own; time is the sum of the `dt` given to `tick`, the random numbers come from a fixed seed, every container the
// host iterates is ordered.
//
// Attach keys: the runtime allows one instance per `AttachKey`. A form with several scripts uses
// `AttachKind::Form`, the form id and the script's ordinal in the VMAD (0 for the first) in `effectKey`; an alias
// script uses `AttachKind::Alias`, the quest, the alias id in `aliasIndex` and the ordinal in `effectKey`. The saves
// work must write the same keys.

#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/vmad.hpp>

#include <components/script_papyrus/papyrusruntime.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// The pack setting (`[extension.skyrimse] papyrus`, bool, off by default) that runs the Papyrus host.
    inline constexpr std::string_view kPapyrusKey = "papyrus";

    /// What the host reads from the pack. Every function runs on the main thread.
    class PapyrusGameData
    {
    public:
        virtual ~PapyrusGameData() = default;

        /// The bytes of `scripts/<lowerName>.pex` ("" is never asked). False when there is no such file.
        virtual bool readPex(const std::string& lowerName, std::vector<std::uint8_t>& bytes) = 0;

        /// "Quest", "Actor", "ObjectReference", "Cell", "Keyword", ... for a form id; empty when unknown.
        [[nodiscard]] virtual std::string formClass(FormId form) const = 0;

        /// The position of a placed reference as the data gives it (game units). False for any other form.
        [[nodiscard]] virtual bool referencePosition(FormId reference, double& x, double& y, double& z) const
        {
            (void)reference;
            (void)x;
            (void)y;
            (void)z;
            return false;
        }

        /// The base form of a placed reference, 0 for any other form.
        [[nodiscard]] virtual FormId baseOf(FormId reference) const
        {
            (void)reference;
            return 0;
        }

        /// Task 0181: does the form (a placed reference, or a base form) have the keyword? For a reference the answer
        /// is its base form's. False when the data cannot say.
        [[nodiscard]] virtual bool hasKeyword(FormId form, FormId keyword) const
        {
            (void)form;
            (void)keyword;
            return false;
        }

        /// Task 0181: whether the data places the reference enabled (false for one flagged initially disabled). A
        /// script's Enable and Disable start from this.
        [[nodiscard]] virtual bool initiallyEnabled(FormId reference) const
        {
            (void)reference;
            return true;
        }

        /// The editor id or the name of a form for GetName; empty when none.
        [[nodiscard]] virtual std::string nameOf(FormId form) const
        {
            (void)form;
            return {};
        }
    };

    struct PapyrusHostOptions
    {
        /// Seed of Utility.RandomInt and RandomFloat.
        std::uint64_t mRandomSeed = 0x9E3779B97F4A7C15ull;
        /// Game seconds per real second for Utility.GetCurrentGameTime (Skyrim's default timescale is 20;
        /// RECALLED-VERIFY).
        double mTimeScale = 20.0;
        /// Scripts one load may pull in through the names in their string tables (a bound on the walk).
        std::uint32_t mMaxLoadedScripts = 4096;
        /// Instances the host attaches in total.
        std::uint32_t mMaxInstances = 65536;
    };

    /// Counts of what happened since the host was made. Deterministic.
    struct PapyrusHostCounters
    {
        std::uint64_t mScriptsLoaded = 0;
        std::uint64_t mScriptsMissing = 0; ///< a script a record names that has no .pex
        std::uint64_t mScriptsRefused = 0; ///< a .pex the reader or loader refused
        std::uint64_t mInstancesAttached = 0;
        std::uint64_t mAttachRefused = 0;
        std::uint64_t mPropertiesSet = 0;
        std::uint64_t mPropertiesRejected = 0; ///< the runtime refused the value (not an auto property, wrong type)
        std::uint64_t mPropertiesSkipped = 0; ///< an array property, or one the script does not have
        std::uint64_t mFragmentsRun = 0; ///< a quest stage fragment function was queued
        std::uint64_t mStageSets = 0; ///< Quest.SetStage calls that changed a stage
        std::uint64_t mUpdatesFired = 0; ///< OnUpdate events sent for RegisterForSingleUpdate or RegisterForUpdate
        std::uint64_t mTicks = 0;
        std::uint64_t mStacksRun = 0;
        std::uint64_t mScriptErrors = 0; ///< a stack that ended with an error
        std::map<std::string, std::uint64_t> mEvents; ///< events sent, by name (lower case)
        std::map<std::string, std::uint64_t> mNatives; ///< natives the host answered, by "class.function" (lower case)
    };

    /// The answer of PapyrusHost::restoreSavedState: ok, or why the state was refused (nothing changed then).
    struct PapyrusRestoreOutcome
    {
        bool mOk = false;
        std::string mError;
    };

    class PapyrusHost
    {
    public:
        using LogFunction = std::function<void(bool error, const std::string& line)>;
        /// Called when a script changes a quest's stage, after the state changed: quest, new stage.
        using QuestChanged = std::function<void(FormId quest, std::uint32_t stage)>;
        /// Called when a script enables or disables a placed reference (the engine cannot be told yet: no SDK path).
        using EnableChanged = std::function<void(FormId reference, bool enabled)>;
        using QuestVmadLookup = std::function<const vmad::Vmad*(FormId quest)>;

        explicit PapyrusHost(PapyrusGameData& data, PapyrusHostOptions options = {});
        ~PapyrusHost();
        PapyrusHost(const PapyrusHost&) = delete;
        PapyrusHost& operator=(const PapyrusHost&) = delete;

        void setLog(LogFunction log);
        void setQuestChanged(QuestChanged changed);
        void setEnableChanged(EnableChanged changed);
        /// The quest records and their run-time state, and where a quest's VMAD is. The state is changed by
        /// Quest.SetStage, Start and Stop; the host does not own any of the three.
        void setQuests(const QuestIndex* index, QuestState* state, QuestVmadLookup vmads);

        /// Attaches the scripts of every start-game-enabled quest of the index (sorted by form id) and queues the
        /// fragment of its start up stage. Once; later calls do nothing. Needs setQuests.
        void startGame();

        /// Attaches the scripts and the fragment script of one quest, then its alias scripts. False when the quest
        /// is unknown or has nothing to attach. A quest already attached is not attached twice.
        bool attachQuest(FormId quest);

        /// Attaches scripts a VMAD put on a placed reference (already resolved to load order ids). `actor` makes the
        /// reference an Actor, otherwise an ObjectReference.
        std::size_t attachReference(
            FormId reference, FormId base, bool actor, const std::vector<vmad::Script>& scripts);

        /// A cell's references finished loading: sends OnCellLoad and then OnLoad to the instances attached to them.
        void onCellLoaded(FormId cell, const std::vector<FormId>& references);

        /// The player (or an actor) used a placed reference: sends OnActivate(akActionRef) to its instances.
        void onActivate(FormId target, FormId actionRef);

        /// Advances script time by \a dt seconds: fires the updates that are due, runs the queued events and wakes
        /// the waiting scripts.
        void tick(float dt);

        /// The runtime's own state as bytes (empty inside a tick). Part of saveState().
        [[nodiscard]] openeden::script::SaveBlob serialiseState() const;
        bool restoreState(const openeden::script::SaveBlob& blob);

        /// Task 0181: everything a save needs to resume the scripts (the pack's `papyrus` chunk): the runtime's state,
        /// the host's ledger of instances (so that its own tables are rebuilt), script time, timers, enable and position
        /// overrides, forced alias fills, objective flags, recorded spells, the quest state it changed, the sets of
        /// attached quests and references, and the counters. Canonical: restoring and saving again gives the same
        /// bytes. Empty before the game has started, inside a tick, and when the runtime cannot write its state.
        [[nodiscard]] std::vector<std::uint8_t> saveState() const;
        /// Replaces the host's state with a saved one. The scripts the state names are loaded first; the state is checked
        /// whole (including every attach key, against the game data) before anything changes: a refusal changes nothing.
        /// Needs setQuests. A host that restored counts as started (startGame does nothing afterwards). For every quest
        /// with a stage above 0 the QuestChanged sink is told, so the package brain's own copy follows. Not inside a
        /// tick.
        PapyrusRestoreOutcome restoreSavedState(std::span<const std::uint8_t> bytes);
        /// startGame() ran, or a state was restored.
        [[nodiscard]] bool started() const;

        /// The enabled state a script gave a reference (nullopt when no script ever enabled or disabled it), and all of
        /// them in form id order: what the pack pushes to the world and replays after a restore.
        [[nodiscard]] std::optional<bool> enableOverride(FormId reference) const;
        [[nodiscard]] std::vector<std::pair<FormId, bool>> enableOverrides() const;
        /// Where MoveTo put a reference (nullopt when it was never moved). Recorded only: the engine is not moved.
        [[nodiscard]] std::optional<std::array<double, 3>> positionOverride(FormId reference) const;
        /// Whether Actor.AddSpell recorded the spell for the actor (recorded only: no effect).
        [[nodiscard]] bool hasSpell(FormId actor, FormId spell) const;

        [[nodiscard]] const PapyrusHostCounters& counters() const;
        [[nodiscard]] const ScriptPapyrus::Stats& runtimeStats() const;
        /// Calls of natives nobody answered (tolerant: the zero came back), across a save and load: the runtime's own
        /// count of this run plus what the state that was restored had counted.
        [[nodiscard]] std::uint64_t tolerantCalls() const;
        /// Natives a script called that nobody answered ("class.function", lower case, sorted).
        [[nodiscard]] std::vector<std::string> tolerantNatives() const;
        /// The enabled state of a reference as scripts left it (true when no script changed it).
        [[nodiscard]] bool referenceEnabled(FormId reference) const;
        [[nodiscard]] std::size_t instanceCount() const;
        [[nodiscard]] double scriptTime() const;

        // ---- reading and calling, for the tests and the pack's own checks (never needed by a game)
        /// A numeric variable or property (int, float, bool) of the instance of  script attached to  form (the
        /// form id of a quest or a placed reference); nullopt when there is no such instance or name.
        [[nodiscard]] std::optional<double> numberVariable(
            FormId form, std::string_view script, std::string_view name) const;
        [[nodiscard]] std::optional<std::string> stringVariable(
            FormId form, std::string_view script, std::string_view name) const;
        /// An object variable that names a form (an alias handle is not a form: nullopt).
        [[nodiscard]] std::optional<FormId> formVariable(
            FormId form, std::string_view script, std::string_view name) const;
        [[nodiscard]] std::optional<double> aliasScriptNumber(
            FormId quest, std::uint32_t alias, std::string_view script, std::string_view name) const;
        /// Runs a function of the instance now and returns whether it finished without an error (the function runs
        /// to its end or its first wait; natives it calls are answered as usual). Not for use inside a tick.
        bool callFunction(FormId form, std::string_view script, std::string_view function);

        struct Impl;

    private:
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
