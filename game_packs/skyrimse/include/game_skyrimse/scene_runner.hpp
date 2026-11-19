#ifndef OPENEDEN_GAME_SKYRIMSE_SCENE_RUNNER_H
#define OPENEDEN_GAME_SKYRIMSE_SCENE_RUNNER_H

// The quest scene runner (task 0194, milestone M-S6). Plays SCEN records: phases in order; in each phase the actions
// that start there (a dialogue line spoken by an actor of the quest, a package an actor follows, a timer); a phase ends
// when its actions and its completion conditions say so; fragments run at the scene's begin, end and each phase's start
// and completion. Pure: everything the runner touches (aliases, conditions, lines, speech, packages, fragments) is
// behind `SceneEnvironment`, so a test drives it with a fake. Main thread only. Deterministic: no clock (time is the
// sum of the `dt` given to `update`), no randomness, every container is ordered.
//
// The rules below are RECALLED-VERIFY (the Creation Kit wiki could not be consulted; xEdit names the records, the data
// shows the shapes). Each is a named option:
//  - a scene starts from `start` (Scene.Start, a quest stage fragment), or when its quest starts if the scene says
//    "begin on quest start" (flag 1); its own conditions (the 59 CTDA after VNAM) must pass unless forced;
//  - phase N starts when phase N-1 completed and the phase's start conditions pass (it waits otherwise);
//  - a phase completes when every action that ENDS in it is complete AND its completion conditions pass
//    (`PhaseEndRule::ActionsAndConditions`; `ActionsOnly` and `ConditionsOnly` are the alternatives);
//  - an action that ends in a phase that completes while it still runs is stopped;
//  - a dialogue action resolves its topic's line for the speaker alias, speaks it, and is complete when the line's
//    duration has run out; the duration is an ESTIMATE of the spoken length (`mSecondsPerCharacter`, never below
//    `mMinLineSeconds`) plus `mLineGapSeconds`, because the pack does not decode audio; a dialogue action with no topic
//    (DATA 0, 3,121 of 11,991 in the data) is complete at once;
//  - a speaker that is not loaded (the environment refuses `speak`) is waited for up to `mParkSeconds`, then the action
//    is complete without a line (counted);
//  - a package action sets the package on the actor and is complete when the environment says the package finished, or
//    after `mPackageSeconds`;
//  - a timer action is complete when its seconds ran out;
//  - at the end the scene's end fragment runs and, with "stop quest on end" (flag 2), the quest is stopped.

#include <game_skyrimse/scene.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// One resolved line of dialogue: the subtitle and the voice files in order.
    struct SceneLine
    {
        std::string mText;
        std::vector<std::string> mVoice;
    };

    class SceneEnvironment
    {
    public:
        virtual ~SceneEnvironment() = default;
        [[nodiscard]] virtual bool questRunning(FormId quest) const = 0;
        /// The reference filling an alias of the quest; kNullFormId when it is empty.
        [[nodiscard]] virtual FormId aliasReference(FormId quest, std::uint32_t alias) const = 0;
        /// Do the conditions pass? `scene` is the scene being asked about (fn_550 reads its state).
        virtual bool evaluate(const std::vector<survey::Ctda>& conditions, FormId scene, FormId quest) = 0;
        /// The line the topic gives the speaker. False when the topic gives none.
        virtual bool resolveLine(FormId scene, FormId topic, FormId speakerReference, SceneLine& out) = 0;
        /// The speaker says the line. False when it cannot now (the actor is not loaded).
        virtual bool speak(FormId speakerReference, const SceneLine& line) = 0;
        /// An actor takes part in a scene (stands and waits for its lines) or stops taking part.
        virtual void hold(FormId actorReference, bool on) = 0;
        virtual bool setPackage(FormId actorReference, FormId package) = 0;
        virtual void clearPackage(FormId actorReference) = 0;
        /// Has the package the actor was given finished?
        virtual bool packageFinished(FormId actorReference) = 0;
        virtual void runFragment(FormId scene, const std::string& script, const std::string& function) = 0;
        virtual void stopQuest(FormId quest) = 0;
        virtual void log(const std::string& line) = 0;
        /// Is the actor a reference the world has built (so that it can speak)? A scene does not start when none of
        /// its actors is. Default: yes.
        [[nodiscard]] virtual bool actorLoaded(FormId actorReference) const
        {
            (void)actorReference;
            return true;
        }
    };

    /// Task 0211: what a save keeps of a scene that is playing: the phase, whether that phase was entered (its start
    /// fragment ran), and the stage of each action (0 waiting, 1 running, 2 done). Elapsed times are not kept: a line
    /// or timer that was running starts again after a load.
    struct SceneSaveEntry
    {
        FormId mScene = kNullFormId;
        std::int32_t mPhase = 0;
        bool mEntered = false;
        std::vector<std::uint8_t> mActions;
    };

    struct SceneRestoreResult
    {
        std::size_t mResumed = 0;
        std::size_t mDropped
            = 0; ///< the scene or its quest is gone, or the quest is not running, or the actions differ
    };

    /// The bytes of a scene save (version 1, little endian): u32 count, per scene u32 id, i32 phase, u8 entered (0 or
    /// 1), u32 action count and one byte per action. No scene is NOT special: it is the 4 bytes of a zero count.
    [[nodiscard]] std::vector<std::uint8_t> encodeSceneState(const std::vector<SceneSaveEntry>& entries);
    /// Reads the bytes; false with a located message for anything damaged (caps: 64 scenes, 4096 actions each, every
    /// stage byte 0 to 2, a phase from 0 to 4096, no trailing bytes, no scene twice). `out` is cleared first.
    [[nodiscard]] bool decodeSceneState(
        std::span<const std::uint8_t> bytes, std::vector<SceneSaveEntry>& out, std::string& error);

    enum class PhaseEndRule : std::uint8_t
    {
        ActionsAndConditions,
        ActionsOnly,
        ConditionsOnly,
        /// Task 0329: a phase that lists completion conditions ends when they pass (its remaining actions are
        /// stopped); one that lists none ends when its actions do. The Raven Rock arrival scene's phase 5 lists "the
        /// timer OR the forced greeting is done" while its other package runs on: only this rule lets it end.
        ConditionsWhenPresent,
    };

    class SceneRunner
    {
    public:
        struct Options
        {
            PhaseEndRule mPhaseEnd = PhaseEndRule::ActionsAndConditions;
            float mSecondsPerCharacter = 0.065f; ///< estimate of speech: about 15 characters a second
            float mMinLineSeconds = 1.5f;
            float mLineGapSeconds = 0.4f;
            float mParkSeconds = 120.f;
            float mPackageSeconds = 60.f;
            float mRetrySeconds = 1.f;
            /// Task 0268: a scene that begins on quest start and found none of its actors loaded waits and asks again
            /// every `mRetrySeconds` while its quest runs (the opening of the main quest moves its actors after the
            /// quest started). Off by default: other quests' scenes are dropped as before.
            bool mRetryQuestStart = false;
            bool mSkipFailedPhases = false; ///< task 0308
            std::uint32_t mMaxPlaying = 64;
        };
        struct Stats
        {
            std::uint64_t mStarted = 0;
            std::uint64_t mCompleted = 0;
            std::uint64_t mStopped = 0;
            std::uint64_t mRefused = 0; ///< a start that did not happen
            std::uint64_t mPhases = 0;
            std::uint64_t mLines = 0;
            std::uint64_t mLinesSilent = 0; ///< a dialogue action complete with nothing said
            std::uint64_t mParked = 0; ///< a dialogue action that waited for its speaker
            std::uint64_t mParkExpired = 0;
            std::uint64_t mPackages = 0;
            std::uint64_t mTimers = 0;
            std::uint64_t mFragments = 0;
            std::uint64_t mNotLoaded = 0; ///< a start refused because no actor of the scene is loaded
        };

        SceneRunner(const SceneIndex& scenes, SceneEnvironment& env, Options options);
        /// GCC rejects `Options options = {}` as a default argument (the nested struct has member initializers).
        SceneRunner(const SceneIndex& scenes, SceneEnvironment& env)
            : SceneRunner(scenes, env, Options())
        {
        }
        ~SceneRunner();
        /// Task 0268: see Options::mRetryQuestStart.
        void setRetryQuestStart(bool on) { mOptions.mRetryQuestStart = on; }
        /// Task 0308: a phase whose start conditions fail is skipped, not waited for (the Whiterun scene's phase 7 needs
        /// quest stage 82, which only one dialogue path sets; RECALLED-VERIFY: the CK's meaning of a start condition)
        void setSkipFailedPhases(bool on) { mOptions.mSkipFailedPhases = on; }
        void setPhaseEnd(PhaseEndRule rule) { mOptions.mPhaseEnd = rule; } // task 0329
        // Task 0287: how long a line waits for a speaker who is not loaded (default 120 s, as before); cut to a range
        static constexpr float kMinParkSeconds = 0.5f;
        static constexpr float kMaxParkSeconds = 600.f;
        static constexpr float kDefaultParkSeconds = 120.f;
        [[nodiscard]] static float clampParkSeconds(float seconds)
        {
            if (!(seconds == seconds)) // NaN
                return kDefaultParkSeconds;
            return seconds < kMinParkSeconds ? kMinParkSeconds : (seconds > kMaxParkSeconds ? kMaxParkSeconds : seconds);
        }
        /// Task 0316 (toddhow, 2026-11-18): the park time to use. A set value (above 0) wins; otherwise 5 s (ours, task
        /// 0287) with `quest alias packages` on, else 0 (the runner keeps its own 120 s).
        static constexpr float kQuestAliasParkSeconds = 5.f;
        [[nodiscard]] static float parkSecondsFor(float setting, bool questAliasPackages)
        {
            if (setting > 0.f)
                return setting;
            return questAliasPackages ? kQuestAliasParkSeconds : 0.f;
        }
        void setParkSeconds(float seconds) { mOptions.mParkSeconds = clampParkSeconds(seconds); }
        SceneRunner(const SceneRunner&) = delete;
        SceneRunner& operator=(const SceneRunner&) = delete;

        /// Scene.Start: false when the scene is unknown, already playing, its quest is not running, too many scenes
        /// play, no actor of it has a reference, or (unless `force`, Scene.ForceStart) its conditions fail.
        bool start(FormId scene, bool force = false);
        /// Scene.Stop: ends the scene now (its end fragment does not run). False when it was not playing.
        bool stop(FormId scene);
        [[nodiscard]] bool isPlaying(FormId scene) const;
        /// The quest started: starts its scenes that begin on quest start. Returns how many started.
        std::size_t questStarted(FormId quest);
        /// The quest stopped: ends its scenes.
        void questStopped(FormId quest);
        void update(float dt);

        /// Task 0211: the playing scenes, ordered by scene id.
        [[nodiscard]] std::vector<SceneSaveEntry> saveState() const;
        /// Resumes saved scenes: each is started (its conditions are not asked, the begin fragment does not run again)
        /// and put back at its phase; actions that were done stay done, a phase's start fragment does not run again.
        /// The quests must have been restored first. Entries that cannot be resumed are dropped and counted.
        SceneRestoreResult restoreState(const std::vector<SceneSaveEntry>& entries);
        /// A load replaces the world: ends every playing scene without its end fragment and releases the actors it
        /// holds. Returns how many were playing.
        std::size_t stopAll();

        /// fn_550 IsSceneActionComplete.
        [[nodiscard]] bool actionComplete(FormId scene, std::uint32_t actionIndex) const;
        [[nodiscard]] std::size_t playingCount() const { return mPlaying.size(); }
        /// The phase a playing scene is in (-1: not playing).
        [[nodiscard]] int phaseOf(FormId scene) const;
        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        struct Instance;
        bool startImpl(FormId scene, bool force, const SceneSaveEntry* resume);
        void finish(FormId scene, bool ended);

        const SceneIndex& mScenes;
        SceneEnvironment& mEnv;
        Options mOptions;
        std::map<FormId, std::unique_ptr<Instance>> mPlaying;
        std::vector<FormId> mWaitingStart; ///< task 0268: begin-on-quest-start scenes that found nobody loaded
        float mWaitingClock = 0.f;
        Stats mStats;
        std::map<FormId, std::set<std::uint32_t>> mDone; ///< scene -> action indexes complete (survives the end)
    };
}

#endif
