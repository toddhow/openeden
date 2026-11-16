#ifndef OPENEDEN_GAME_SKYRIMSE_BARKS_H
#define OPENEDEN_GAME_SKYRIMSE_BARKS_H

// Barks: the short lines Skyrim NPCs say without being talked to (task 0231, docs/tasks/0231-skyrim-npc-barks.md).
//
// A bark is a response of a dialogue topic whose subtype names the occasion: HELO (a greeting as the player comes
// near), IDLE (chatter), ALTC / NOTC (combat starts), ATCK, TAUT (blows and taunts while fighting), HIT_, FLEE, DETH,
// LOTN and COTN (the fight is over). The counts of the five official masters are in the task's survey. This file is the
// pure rule set: WHEN an actor speaks (timers, distances, rate limits, one calm bark at a time). WHICH line it says
// (the INFO, its conditions, say-once) is the dialogue runtime's job (DialogueRuntime::barkLine) and how it is played
// (voice file, subtitle) is the engine's (the scene speech path of task 0194); both reach this class through Hooks.
//
// Numbers. The timing numbers come from the game's own settings (GMSTs of Skyrim.esm, VERIFIED values; their USE here
// is RECALLED-VERIFY): fAIGreetingTimer 5, fIdleChatterCommentTimer 10 and ...Max 20, fCombatDialogueAttackMin/
// MaxElapsedTime 5 and 10, fCombatDialogueTauntMin/MaxElapsedTime 10 and 30, fGameplayVoiceFilePadding 0.5. The data
// has no distance setting for a greeting; the distances and the other limits below are OURS, named and tunable.
//
// Determinism: no clock (the caller's seconds add up), no unseeded random number: every draw is splitmix64 over
// (seed, actor, a per-actor counter, kind). Actors are visited in reference order. Main thread only.
//
// No engine, renderer, Qt or script-language type.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    class SettingsTable;
}

namespace game_skyrimse::crime
{
    enum class Kind : std::uint8_t;
}

namespace game_skyrimse::barks
{
    enum class Kind : std::uint8_t
    {
        Hello, ///< HELO: the player came near
        Idle, ///< IDLE: chatter
        Engage, ///< NOTC: calm to combat
        EngageAlerted, ///< ALTC: an ally's fight woke it (assisted engagement)
        Attack, ///< ATCK: while fighting, every fCombatDialogueAttack interval
        Taunt, ///< TAUT: while fighting, every fCombatDialogueTaunt interval
        Hit, ///< HIT_: hurt and alive
        Flee, ///< FLEE: runs away
        Death, ///< DETH
        Lost, ///< LOTN: gave up the chase
        Calm, ///< COTN: the fight ended
        Assault, ///< ASSA: reaction to the player's crime (the API exists, nothing calls it yet)
        Steal, ///< STEA
        Trespass, ///< TRES
        Murder, ///< MURD (task 0235)
        Notice, ///< NOTA: an actor began to suspect a sneaking player (task 0235)
        Pickpocket, ///< PICN: a pickpocket try the victim caught and did not answer with a fight (task 0243)
        PickpocketCombat, ///< PICC: the same, answered with a fight (task 0243; nothing raises it yet)
        Count
    };
    inline constexpr std::size_t kKindCount = static_cast<std::size_t>(Kind::Count);

    /// The reaction kind a witness of this crime speaks from (task 0243): theft STEA, assault ASSA, murder MURD,
    /// trespass TRES, a caught pickpocket PICN.
    [[nodiscard]] Kind kindOfCrime(crime::Kind crime);

    /// The DIAL subtype (SNAM, four characters) a kind speaks from; empty for Count.
    [[nodiscard]] std::string_view subtypeOf(Kind kind);
    /// A short lower-case name for traces ("hello", "idle", "engage" ...).
    [[nodiscard]] std::string_view nameOf(Kind kind);
    /// True for the kinds that belong to a fight.
    [[nodiscard]] bool isCombat(Kind kind);

    struct Settings
    {
        // ---- the game's own numbers (GMSTs; see fromGameSettings)
        float mHelloGap = 5.f; ///< fAIGreetingTimer: no hello within this long after another
        float mIdleMin = 10.f; ///< fIdleChatterCommentTimer
        float mIdleMax = 20.f; ///< fIdleChatterCommentTimerMax
        float mAttackMin = 5.f; ///< fCombatDialogueAttackMinElapsedTime
        float mAttackMax = 10.f; ///< fCombatDialogueAttackMaxElapsedTime
        float mTauntMin = 10.f; ///< fCombatDialogueTauntMinElapsedTime
        float mTauntMax = 30.f; ///< fCombatDialogueTauntMaxElapsedTime
        float mVoicePadding = 0.5f; ///< fGameplayVoiceFilePadding: silence after a line
        // ---- OURS
        /// A hello is said when the player comes within this horizontal distance (units) and this height difference.
        float mHelloDistance = 400.f;
        float mHelloHeight = 200.f;
        /// The same actor says hello again no sooner than this (seconds).
        float mHelloReset = 300.f;
        /// Idle chatter is only said within this horizontal distance of the player.
        float mIdleDistance = 800.f;
        /// A combat bark is only said within this distance of the player (further, it could not be heard).
        float mHearDistance = 2500.f;
        /// The same actor does not remark on a sneaking player's noise again within this long (seconds), and does not
        /// comment on a drawn weapon again within mStanceGap (task 0235). OURS.
        float mNoticeGap = 20.f;
        float mStanceGap = 30.f;
        /// The same actor says a "hurt" line no sooner than this (seconds).
        float mHitGap = 3.f;
        /// After any combat bark no other combat bark starts for this long (seconds): keeps a crowd from shouting
        /// over itself.
        float mCombatGap = 0.75f;
        /// A blow cry or taunt that finds no line (or whose line is refused) is not asked for again for this long
        /// (seconds; task 0243, OURS: it used to ask on every update while its timer was due).
        float mNoLineGap = 5.f;
        /// How long a line is assumed to last: max(mMinSpeech, characters * mSecondsPerChar), plus mVoicePadding.
        float mSecondsPerChar = 0.07f;
        float mMinSpeech = 1.5f;
        /// A calm bark that finds no line or no voice is tried again for the same actor after this long (seconds).
        float mRetry = 1.f;
        /// A test aid: nothing runs and the bark clock does not start before the caller's own seconds (the sum of the
        /// updates' dt) reach this, so a run's timeline does not depend on how fast the dialogue data was read. 0: off.
        float mStartAt = 0.f;
        std::uint64_t mSeed = 0x5eed;
        /// Event queue cap (the rest are dropped and counted).
        std::size_t mMaxEvents = 64;
    };

    /// `table` null: the defaults above. Else each GMST the table has replaces its default.
    [[nodiscard]] Settings fromGameSettings(const SettingsTable* table);

    /// One actor the world offers the system each update.
    struct Actor
    {
        std::uint64_t mReference = 0;
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        bool mDead = false;
    };

    /// A line to say: the subtitle and the voice files (VFS paths, as DialogueTurn::voice).
    struct Line
    {
        std::string mText;
        std::vector<std::string> mVoice;
    };

    struct Hooks
    {
        /// Chooses the line `reference` says for `kind`; `pick` is a deterministic number. False: no line passes.
        std::function<bool(Kind kind, std::uint64_t reference, std::uint32_t pick, Line& out)> mChoose;
        /// Plays it. False: the actor is not known to the core (or its queue is full).
        std::function<bool(std::uint64_t reference, const Line& line)> mSpeak;
        /// False until the world can answer (the dialogue data is read): while it is false update() does nothing, the
        /// clock stands still and queued events are dropped, so a run's bark timeline starts at the same moment of its
        /// own whatever the worker's speed. May be empty (always ready).
        std::function<bool()> mReady;
        /// A readable line for the log; may be empty.
        std::function<void(const std::string&)> mTrace;
    };

    struct Stats
    {
        std::uint64_t mSpoken[kKindCount] = {};
        std::uint64_t mNoLine = 0; ///< no INFO passed
        std::uint64_t mNoVoice = 0; ///< the INFO has no voice file: not said
        std::uint64_t mRefused = 0; ///< the speak hook said no
        std::uint64_t mEventsDropped = 0;
    };

    class BarkSystem
    {
    public:
        BarkSystem(Settings settings, Hooks hooks);

        /// The settings can be replaced once the game's own numbers are read; timers already running keep their times.
        void configure(Settings settings) { mSettings = std::move(settings); }
        [[nodiscard]] const Settings& settings() const { return mSettings; }

        /// A combat event, queued and handled by the next update (which has the player's place). Safe to call from
        /// inside the combat update. `kind` is one of Engage, EngageAlerted, Hit, Flee, Death, Lost, Calm, or a
        /// reaction (Assault, Steal, Trespass); the periodic kinds (Attack, Taunt) and Hello and Idle come from update.
        /// Task 0243: `subjectIsVictim`: the actor is the victim of the crime it remarks on; `speakerIsVictim()` says so
        /// while the line is being chosen (IsActorAVictim).
        void onEvent(Kind kind, std::uint64_t reference, bool subjectIsVictim = false);

        /// Advances the clock by `seconds`, handles the queued events and starts at most one calm bark and, for
        /// engaged actors whose timer is due, combat barks. `havePlayer` false: no distance rule can be answered, so
        /// nothing is said and the queued events are dropped.
        void update(float seconds, const std::vector<Actor>& actors, bool havePlayer, double px, double py, double pz);

        /// Task 0235: a weapon or a spell is drawn (true) or everything is put away. A change to drawn, while the
        /// player is known and an actor is within the hello distance, arms that actor's hello once, at most once every
        /// mStanceGap (the hello lines that care what the player has out are chosen by their conditions; the system
        /// only supplies the occasion). Safe to call from inside the combat update; handled by the next update.
        ///
        /// `sneaking`: the player is sneaking; an actor does not greet a sneaking player (its hello waits until he
        /// stands up), but its idle lines, some of which are written for a sneaking player, still run.
        void onPlayerStance(bool weaponDrawn, bool sneaking = false);

        /// True while `Hooks::mChoose` runs for an event whose actor is the victim of the crime (task 0243).
        [[nodiscard]] bool speakerIsVictim() const { return mVictimNow; }

        [[nodiscard]] float clock() const { return mClock; }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] bool engaged(std::uint64_t reference) const;
        [[nodiscard]] std::size_t engagedCount() const { return mEngaged.size(); }

    private:
        struct State
        {
            bool mSeen = false;
            bool mNear = false; ///< within hello distance at the last update
            bool mArmed = false; ///< came near and has not been greeted yet
            float mRetryAt = 0.f; ///< a calm bark that failed is not tried again before this
            float mHelloReady = 0.f;
            float mNoticeReady = 0.f;
            float mStanceReady = 0.f;
            bool mStanceArmed = false; ///< a drawn weapon was seen near it: its hello is due
            float mNextIdle = 0.f;
            float mHitReady = 0.f;
            float mSpeakingUntil = 0.f;
            std::uint32_t mCounter = 0;
        };
        struct Engaged
        {
            float mNextAttack = 0.f;
            float mNextTaunt = 0.f;
        };
        struct Pending
        {
            Kind mKind;
            std::uint64_t mReference;
            bool mVictim = false; ///< task 0243
        };

        [[nodiscard]] std::uint64_t mix(std::uint64_t reference, std::uint32_t counter, Kind kind) const;
        [[nodiscard]] float draw(std::uint64_t reference, State& state, Kind kind, float low, float high) const;
        /// Chooses and plays a line; false (counted) when none, none with voice, or refused. On success the actor is
        /// busy until the line is over.
        bool trySpeak(Kind kind, std::uint64_t reference, State& state, bool victim = false);

        Settings mSettings;
        Hooks mHooks;
        float mClock = 0.f;
        float mSim = 0.f; ///< the sum of the updates' seconds, ready or not
        bool mStarted = false;
        bool mPlayerDrawn = false; ///< the last stance told
        bool mStanceEdge = false; ///< the weapon was drawn since the last update
        bool mPlayerSneaking = false; ///< the player is sneaking: no hello
        bool mVictimNow = false;
        float mCalmFree = 0.f; ///< no calm bark starts before this
        float mCombatFree = 0.f; ///< no combat bark starts before this
        float mHelloFree = 0.f; ///< no hello starts before this (fAIGreetingTimer after the last)
        std::map<std::uint64_t, State> mStates;
        std::map<std::uint64_t, Engaged> mEngaged;
        std::vector<Pending> mQueue;
        std::vector<const Actor*> mSorted; ///< reused: the roster by reference
        Stats mStats;
    };
}

#endif
