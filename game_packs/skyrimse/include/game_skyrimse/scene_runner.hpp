#ifndef OPENEDEN_GAME_SKYRIMSE_SCENE_RUNNER_H
#define OPENEDEN_GAME_SKYRIMSE_SCENE_RUNNER_H

// The quest scene runner (task 0194, milestone M-S6). Plays SCEN records: phases in order; in each phase the actions that
// start there (a dialogue line spoken by an actor of the quest, a package an actor follows, a timer); a phase ends when
// its actions and its completion conditions say so; fragments run at the scene's begin, end and each phase's start and
// completion. Pure: everything the runner touches (aliases, conditions, lines, speech, packages, fragments) is behind
// `SceneEnvironment`, so a test drives it with a fake. Main thread only. Deterministic: no clock (time is the sum of the
// `dt` given to `update`), no randomness, every container is ordered.
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

    enum class PhaseEndRule : std::uint8_t
    {
        ActionsAndConditions,
        ActionsOnly,
        ConditionsOnly,
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

        SceneRunner(const SceneIndex& scenes, SceneEnvironment& env, Options options = {});
        ~SceneRunner();
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

        /// fn_550 IsSceneActionComplete.
        [[nodiscard]] bool actionComplete(FormId scene, std::uint32_t actionIndex) const;
        [[nodiscard]] std::size_t playingCount() const { return mPlaying.size(); }
        /// The phase a playing scene is in (-1: not playing).
        [[nodiscard]] int phaseOf(FormId scene) const;
        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        struct Instance;
        void finish(FormId scene, bool ended);

        const SceneIndex& mScenes;
        SceneEnvironment& mEnv;
        Options mOptions;
        std::map<FormId, std::unique_ptr<Instance>> mPlaying;
        Stats mStats;
        std::map<FormId, std::set<std::uint32_t>> mDone; ///< scene -> action indexes complete (survives the end)
    };
}

#endif
