#ifndef OPENEDEN_GAME_SKYRIMSE_REST_H
#define OPENEDEN_GAME_SKYRIMSE_REST_H

// Waiting and sleeping in a Skyrim SE session (task 0210, milestone M-S7). Off unless [extension.skyrimse] rest.
//
// The pack's side: the rules (when the player may wait, how long, what a rest restores, the Rested state) and the
// script package `core.skyrimse.rest` that the Lua wait and sleep windows read. The windows are Lua on the RmlUi
// library (files/data/openeden/skyrimse/rest_windows.lua); the clock is advanced by a global script
// (`world.advanceTime`). The pack never reads or sets a clock: the script passes the game time in (`set_clock`) and
// the pack stores nothing but the moment the Rested state ends.
//
// What is ours (the data and the engine have no rule for it; named constants below, all tunable):
//  - a wait is 1 to 24 hours (Skyrim's own limit; RECALLED-VERIFY);
//  - sleeping at least kRestedMinSleepHours gives the Rested state for kRestedSeconds of game time (Skyrim gives "Well
//    Rested" after a long sleep in an owned bed; the numbers here are stand-ins);
//  - Rested multiplies skill experience by kRestedSkillGain (a stand-in effect; Skyrim's is +10 percent:
//    RECALLED-VERIFY).
// A rest restores health, magicka and stamina by the regeneration the combat rules use, over the real seconds the game
// hours stand for at the game's time scale (the engine's own rule for recharging items while time passes).
//
// Threading: main thread only (the bindings' Read handlers run on the thread that runs scripts, the main thread
// today; the Mutate calls run there in call order). No engine, renderer or script-language type.

#include <game_skyrimse/furniture.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::rest
{
    inline constexpr int kMinWaitHours = 1;
    inline constexpr int kMaxWaitHours = 24;
    /// Game seconds of the Rested state (8 game hours) and the least sleep that gives it. Ours.
    inline constexpr double kRestedSeconds = 8.0 * 3600.0;
    inline constexpr int kRestedMinSleepHours = 1;
    /// The skill experience multiplier while Rested. Ours.
    inline constexpr float kRestedSkillGain = 1.1f;
    /// The most real seconds of regeneration one rest may apply (a wait of 24 hours at a time scale of 1 is 86,400; the
    /// cap keeps a bad time scale from being absurd).
    inline constexpr double kMaxRegenSeconds = 24.0 * 3600.0;

    /// Why the player may not wait now. The order is the order the checks run in.
    enum class Refusal : std::uint8_t
    {
        None,
        NotReady, ///< the pack's data is not read yet
        InCombat, ///< the player was in combat a moment ago
        EnemiesNear, ///< a hostile is alive and in combat
    };
    /// "" for None, else a short lower-case word ("not_ready", "combat", "enemies"): what the Lua windows turn into
    /// text.
    [[nodiscard]] std::string_view refusalName(Refusal refusal);
    /// Pure: the first reason that holds.
    [[nodiscard]] Refusal decideRest(bool ready, bool inCombat, bool enemiesNear);

    /// Hours as the window may ask for them: 1 to 24; a number that is not finite is 1.
    [[nodiscard]] int clampHours(double hours);
    /// The real seconds of regeneration `hours` of game time stand for at `timeScale` game seconds per real second (the
    /// engine's rule in World::advanceTime). A scale that is not finite or not above 0 counts as 1; the result is cut
    /// to [0, kMaxRegenSeconds].
    [[nodiscard]] double regenSeconds(double hours, double timeScale);

    /// How a bed answers the player's activation.
    enum class BedState : std::uint8_t
    {
        NotABed,
        Free, ///< a bed nobody else owns
        Owned, ///< owned by an NPC or a faction (a trespass in Skyrim; here the player is refused)
    };
    [[nodiscard]] std::string_view bedStateName(BedState state); ///< "none", "free", "owned"

    /// A furniture model is a bed to sleep in when one of its markers is a sleep marker (type 2) that allows an entry
    /// side. The player does not walk to a marker: any sleep marker makes the furniture a bed. A bedroll or a coffin
    /// with a sleep marker counts; a model with only sit or lean markers does not. Pure.
    [[nodiscard]] bool hasSleepMarker(const std::vector<FurnitureMarker>& markers);
    /// The bed's state from what the world knows: `isBed` (the furniture has a sleep marker), and the placement's owner
    /// (`owned` is true when someone but the player owns it). Pure.
    [[nodiscard]] BedState bedStateOf(bool isBed, bool owned);

    /// The Rested state: the game time (seconds, the global clock) at which it ends; negative for none.
    class RestedState
    {
    public:
        /// Grants Rested after `sleptHours` of sleep (at least kRestedMinSleepHours) at game time `now`.
        /// Returns whether
        /// it was granted. A time that is not finite is refused.
        bool grant(double now, int sleptHours);
        [[nodiscard]] bool active(double now) const;
        [[nodiscard]] double hoursLeft(double now) const; ///< 0 when not active
        /// 1 when not active, else kRestedSkillGain.
        [[nodiscard]] float skillGain(double now) const;
        [[nodiscard]] double expiresAt() const { return mExpiresAt; }
        void restore(double expiresAt) { mExpiresAt = expiresAt; }
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }

    private:
        double mExpiresAt = -1.0;
        std::uint64_t mRevision = 0;
    };

    /// The pack's chunk "rest" (version 1): one f64, the game time at which Rested ends (negative: none). Strict: the
    /// version and the size must fit and the number must be finite; a refused chunk changes nothing.
    class RestChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "rest";
        explicit RestChunk(RestedState& state)
            : mState(state)
        {
        }
        void setRestoredSink(std::function<void(const std::string&)> sink) { mRestored = std::move(sink); }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        RestedState& mState;
        std::function<void(const std::string&)> mRestored;
        std::function<void(const std::string&)> mSink;
    };

    struct Hooks
    {
        std::function<bool()> mReady; ///< the pack's data is read (the combat records)
        std::function<bool()> mInCombat; ///< the player fought a moment ago
        std::function<bool()> mEnemiesNear; ///< a live hostile is engaged
        /// What the bed at (placement, base) is; both are form ids.
        std::function<BedState(std::uint64_t placement, std::uint64_t base)> mBed;
        /// A rest happened: restore the player over `regenSeconds` real seconds (health, magicka and stamina).
        std::function<void(double regenSeconds, int hours, bool sleeping)> mApply;
        std::function<void(const std::string&)> mLog;
    };

    class RestService
    {
    public:
        explicit RestService(Hooks hooks);
        ~RestService();
        RestService(const RestService&) = delete;
        RestService& operator=(const RestService&) = delete;

        [[nodiscard]] Refusal refusal() const;
        [[nodiscard]] RestedState& rested() { return mRested; }
        [[nodiscard]] const RestedState& rested() const { return mRested; }
        [[nodiscard]] double now() const { return mNow; }
        void setClock(double gameSeconds);
        /// The rest itself (the Mutate binding): refused unless the player may rest now; returns whether it ran.
        bool perform(int hours, double regenSecondsAsked, bool sleeping);
        [[nodiscard]] std::size_t performed() const { return mPerformed; }
        [[nodiscard]] std::size_t refused() const { return mRefused; }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        RestedState mRested;
        double mNow = 0.0;
        std::size_t mPerformed = 0;
        std::size_t mRefused = 0;
    };
}

#endif
