#ifndef OPENEDEN_GAME_SKYRIMSE_VAMPIRISM_H
#define OPENEDEN_GAME_SKYRIMSE_VAMPIRISM_H

// Vampirism, first slice (task 0260, milestone M-S7). Off unless `[extension.skyrimse] vampirism` (needs `papyrus`).
//
// What runs from the game's own data and scripts (the Papyrus host): `PlayerVampireQuestScript` (quest
// PlayerVampireQuest, 0x0EAFD5): `VampireChange` (the turn: removes the other diseases, status 1, the stage 1 spells),
// `VampireProgression` (the spells of stages 1 to 4), `VampireFeed` (back to stage 1) and `OnUpdateGameTime` (a stage
// up after 1, 2 and 3 game days without feeding, checked every 12 game hours). The spells it gives with Actor.AddSpell
// (frost resistance, fire weakness, the sun damage ability, the powers Vampire's Servant, Seduction and Embrace of
// Shadows) reach the combat and the P key through the stones' list of active spells.
//
// What this file adds (HAND-WRITTEN, because nothing in the data does it): the three timers the scripts rely on the
// game engine for (the Sanguinare Vampiris effect script's "three game days later", the quest's
// RegisterForUpdateGameTime(12) and the clock behind GameDaysPassed), the sunlight test for the sun damage abilities
// (the data's own conditions are evaluated, only the clock and the place are ours), the vampire race's disease immunity
// (VampireVampirism, 0x0ED0A8: the player keeps his own race here, so the ability is given by hand), and the save chunk
// with the timers.
//
// Standard library, the SDK bindings and save chunk types and the pack's own data headers only. Main thread.

#include <game_skyrimse/magic_conditions.hpp>
#include <game_skyrimse/perks.hpp>

#include <openeden/game/save_chunks.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::vampirism
{
    /// DiseaseSanguinareVampiris (SPEL, VERIFIED on Skyrim.esm) and the quest that holds the vampire state (QUST
    /// PlayerVampireQuest, the number in QF_PlayerVampireQuest_000EAFD5; VERIFIED: its script PlayerVampireQuestScript
    /// is attached by the record's VMAD).
    inline constexpr FormId kSanguinare = 0x0B8780;
    inline constexpr FormId kQuest = 0x0EAFD5;
    inline constexpr std::string_view kQuestScript = "PlayerVampireQuestScript";
    /// The race ability that gives a vampire disease resistance 100 (AbResistDisease, SPEL VampireVampirism).
    inline constexpr FormId kVampirismAbility = 0x0ED0A8;
    /// The four sun damage abilities (VampireSunDamage01 to 04: type ability, per-effect conditions: not in an
    /// interior, GameHour between 5 and 19, not in the Forgotten Vale's worldspace; VERIFIED by the CTDA records).
    inline constexpr std::array<FormId, 4> kSunDamageSpells = { 0x0C1E8B, 0x0ED09A, 0x0ED09B, 0x0ED09C };
    /// The GLOB GameHour (0x38) the sun conditions read.
    inline constexpr FormId kGameHourGlobal = 0x38;
    inline constexpr FormId kPlayerReference = 0x14;

    /// VampireDiseaseEffectScript: VampireChangeTimer = GameDaysPassed.Value + 3 (VERIFIED from the decompiled source).
    inline constexpr double kChangeDays = 3.0;
    /// PlayerVampireQuestScript.VampireChange: RegisterForUpdateGameTime(12) (hours).
    inline constexpr double kTickDays = 12.0 / 24.0;
    inline constexpr int kMaxStage = 4;

    [[nodiscard]] bool isSunDamageSpell(FormId spell);

    // ---- the races (task 0279)
    /// The vampire race the quest script's own race properties give for a playable race (SetRace in VampireChange;
    /// VERIFIED: the RACE editor ids ArgonianRaceVampire ... WoodElfRaceVampire). A race with no vampire counterpart
    /// (and a vampire race) is returned as it is.
    [[nodiscard]] FormId vampireRaceOf(FormId race);
    /// The race a body request with no race gets (the stand-in player is a Nord: NordRace).
    inline constexpr FormId kDefaultPlayerRace = 0x13746;
    /// The way back (VampireCure's SetRace); a race that is no vampire race is returned as it is.
    [[nodiscard]] FormId humanRaceOf(FormId race);

    // ---- the timers
    // --------------------------------------------------------------------------------------------------

    /// The two clocks the game's engine kept for the scripts. Pure; days are game days (game seconds / 86400).
    class Machine
    {
    public:
        enum class Action : std::uint8_t
        {
            None,
            Change, ///< three days after the infection: the player turns (VampireChange)
            Tick, ///< a vampire's 12 hour update (OnUpdateGameTime)
        };

        /// Called each poll: whether the player holds Sanguinare Vampiris and is no vampire yet. The first poll that
        /// sees it starts the three days; losing it (a cure) forgets them.
        void observeInfection(bool infected, double days);
        /// What is due at `days`. Change repeats until becameVampire; Tick moves the next tick on by 12 game hours.
        [[nodiscard]] Action step(double days);
        void becameVampire(double days);
        void cured();
        /// A feeding or any other reset of the update clock.
        void restartTicks(double days);

        [[nodiscard]] bool vampire() const { return mVampire; }
        [[nodiscard]] bool infected() const { return mInfectedAt >= 0.0; }
        [[nodiscard]] double infectedAt() const { return mInfectedAt; }
        [[nodiscard]] double nextTick() const { return mNextTick; }
        void restore(bool vampire, double infectedAt, double nextTick);

    private:
        bool mVampire = false;
        double mInfectedAt = -1.0;
        double mNextTick = -1.0;
    };

    // ---- the sun
    // -------------------------------------------------------------------------------------------------------

    /// The hour (0 to 24) of a game clock in seconds.
    [[nodiscard]] float hourOfClock(double gameSeconds);

    /// Does sunlight hurt the player now: the per-effect conditions of the sun damage spell hold (they test the GLOB
    /// GameHour, IsInInterior and GetInWorldspace; the clock and the place are the caller's). A spell the library does
    /// not know, or with an unanswered condition, is "no sun". Pure but for the evaluator's own counters.
    /// `otherGlobals` answers every other GLOB a condition reads (Dawnguard's version of the spell adds a test of one
    /// of its own globals: its stored value, or what a script set); without it such a list is unanswered and fails
    /// closed.
    [[nodiscard]] bool sunlit(const PerkLibrary& library, FormId sunSpell, double gameSeconds,
        const effectconditions::Place& place, const effectconditions::Evaluator::GlobalLookup& otherGlobals = {});

    /// `spells` with the sun damage spells left out unless `sun`, and, when `vampire`, the vampire race's ability
    /// added.
    [[nodiscard]] std::vector<FormId> filterAbilities(std::span<const FormId> spells, bool vampire, bool sun);

    // ---- the service
    // ---------------------------------------------------------------------------------------------------

    /// How the service reaches the quest script (the pack implements it over PapyrusHost; tests use a fake).
    class ScriptPort
    {
    public:
        virtual ~ScriptPort() = default;
        /// The quest's script instance exists.
        [[nodiscard]] virtual bool ready() const = 0;
        /// PlayerVampireQuestScript.VampireChange(player): the turn (it waits a few seconds inside).
        virtual bool change() = 0;
        /// VampireFeed(): back to stage 1.
        virtual bool feed() = 0;
        /// Queues OnUpdateGameTime.
        virtual bool tick() = 0;
        /// VampireProgression(player, stage): the spells of a stage (a test aid).
        virtual bool progress(int stage) = 0;
        /// VampireStatus (0 not a vampire, 1 to 4); -1 when unknown.
        [[nodiscard]] virtual int status() const = 0;
        /// A test aid: VampireStatus and LastFeedTime as if the stage had been reached.
        virtual bool setStage(int stage, double lastFeedDays) = 0;
        /// GameDaysPassed.
        virtual void setDays(double days) = 0;
        /// Task 0279: PlayerVampireQuestScript.VampireCure(player), the call the Falion quest VC01's fragment makes.
        /// A port written before it refuses.
        virtual bool cure() { return false; }
    };

    class VampirismService
    {
    public:
        struct Hooks
        {
            std::function<double()> mDays; ///< game days now
            std::function<bool()> mSanguinare; ///< the player holds Sanguinare Vampiris
            std::function<void()> mCureSanguinare; ///< the turn took it away
            std::function<bool()> mSafe; ///< true when the quest may act (no combat); empty: always
            std::function<void(const std::string&)> mLog;
        };

        VampirismService(ScriptPort& port, Hooks hooks);
        ~VampirismService();
        VampirismService(const VampirismService&) = delete;
        VampirismService& operator=(const VampirismService&) = delete;

        /// Once a frame.
        void poll();
        /// Task 0279: the cure, through the game's own VampireCure. False when he is no vampire or the port refuses.
        /// A cure the game's scripts make themselves (the quest VC01) is seen by `poll` too (status 0).
        bool cure();
        /// A test aid: cure at this game day (once, when he is a vampire by then).
        void scheduleCure(double days);
        /// The player feeds on a sleeper: VampireFeed. False when he is no vampire or the script is not ready.
        bool feed();
        /// A test aid: becomes a vampire at this stage as soon as the script is ready (VampireChange, then the stage).
        void startAs(int stage);

        [[nodiscard]] bool vampire() const { return mMachine.vampire(); }
        [[nodiscard]] int stage() const { return mStage; }
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }
        [[nodiscard]] const Machine& machine() const { return mMachine; }

        /// The chunk "vampirism" (version 1): the vampire flag and the two timers. Strict.
        [[nodiscard]] openeden::sdk::SaveChunkHandler& chunk();
        void restore(bool vampire, double infectedAt, double nextTick);

    private:
        struct Impl;
        void log(const std::string& line) const;
        std::unique_ptr<Impl> mImpl;
        ScriptPort& mPort;
        Hooks mHooks;
        Machine mMachine;
        int mStage = 0;
        bool mChanging = false;
        int mStartStage = 0;
        int mStartStep = 0;
        std::uint64_t mRevision = 1;
        double mCureAt = -1.0;
    };
}

#endif
