#ifndef OPENEDEN_GAME_SKYRIMSE_HUD_STYLE_H
#define OPENEDEN_GAME_SKYRIMSE_HUD_STYLE_H

// The Skyrim-style combat HUD's numbers (task 0244, docs/tasks/0244-skyrim-hud-restyle.md; decision 0015 for the UI,
// task 0180 for the HUD it restyles, task 0097 for the script bindings). Off unless the pack setting `hud style`.
//
// The layout is TrueHUD-like (our own art): the player's three bars at the bottom left, a boss bar at the top centre
// and small bars over the heads of nearby engaged actors. This file is what the pack contributes: WHICH actors get a
// bar and the numbers on it. The look, the damage trail and the projection of a head position onto the screen are done
// by the Lua module files/data/openeden/hud/skyrimstyle.lua (the camera is the script's: `openmw.camera` already gives
// it).
//
// What is shown and for how long is ours (the data has no HUD rules; the boss rule is hudBossRule in hud.hpp): every
// number below is a named constant. No engine, renderer, Qt or script-language type. Main thread (the bindings' Read
// handlers run on the thread that runs scripts and never overlap the pack's own phases).

#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/hud.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::hudstyle
{
    /// The package is `core.skyrimse.hudstyle`, API_VERSION 1.0.
    inline constexpr std::string_view kNamespace = "skyrimse.hudstyle";

    /// An actor is given a bar while it is engaged and within this distance of the player (game units, about 50 m).
    inline constexpr float kBarRange = 3500.f;
    /// At most this many actor bars at once, nearest first.
    inline constexpr std::size_t kMaxActorBars = 8;
    /// An actor keeps its bar this long after it stopped being engaged (so a bar does not flicker between blows).
    inline constexpr float kBarLingerSeconds = 4.f;
    /// The magicka and stamina bars of an actor show this long after the value last changed.
    inline constexpr float kSecondarySeconds = 3.f;
    /// A change of magicka or stamina smaller than this does not count as a change (regeneration noise).
    inline constexpr float kSecondaryChange = 0.5f;
    /// The bar hangs this many game units above the actor's origin (a standing human is about 128 units tall).
    inline constexpr float kHeadOffset = 150.f;
    /// Actors whose secondary timers are remembered; beyond it the oldest is forgotten.
    inline constexpr std::size_t kMaxMemos = 64;

    /// `player()` numbers: health, health maximum, magicka, magicka maximum, stamina, stamina maximum, level.
    inline constexpr std::size_t kPlayerValueCount = 7;
    /// `boss()` numbers: shown (0 or 1), health, health maximum, level, is a boss (0 or 1), dead (0 or 1).
    inline constexpr std::size_t kBossValueCount = 6;
    /// `bars()` numbers per bar: id, x, y, z (the point the bar hangs at), health, health maximum, magicka, magicka
    /// maximum, stamina, stamina maximum, level, ally (0 or 1), flags (bit 0: show the magicka bar, bit 1: show the
    /// stamina bar).
    inline constexpr std::size_t kBarStride = 13;

    /// One actor as the selection sees it.
    struct ActorSample
    {
        openeden::sdk::game::ActorHandle mHandle;
        std::uint16_t mLevel = 1;
        float mHealth = 0.f;
        float mHealthMax = 0.f;
        float mMagicka = 0.f;
        float mMagickaMax = 0.f;
        float mStamina = 0.f;
        float mStaminaMax = 0.f;
        bool mEngaged = false; ///< engaged now or lately (the caller applies kBarLingerSeconds)
        bool mDead = false;
        bool mAlly = false;
        bool mBoss = false;
        bool mShowMagicka = false;
        bool mShowStamina = false;
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
    };

    struct SelectOptions
    {
        float mRange = kBarRange;
        std::size_t mMaxBars = kMaxActorBars;
    };

    /// Which actors get a bar: engaged, alive, with finite numbers, within the range of the player (a distance that is
    /// not finite never passes), nearest first, ties by the actor's reference, at most `mMaxBars`. `out` is cleared
    /// first.
    void selectBars(double playerX, double playerY, double playerZ, std::span<const ActorSample> actors,
        const SelectOptions& options, std::vector<ActorSample>& out);

    /// The boss: among the samples that are engaged, alive and bosses (ActorSample::mBoss), the one with the greatest
    /// maximum health (ties by the smallest reference). Returns its index or `actors.size()` when there is none.
    [[nodiscard]] std::size_t chooseBoss(std::span<const ActorSample> actors);

    /// What the top bar shows, gathered once a combat update.
    struct Boss
    {
        bool mShown = false;
        bool mIsBoss = false; ///< a boss by the rule; false when it is just the enemy fought last
        bool mDead = false;
        float mHealth = 0.f;
        float mHealthMax = 0.f;
        std::uint16_t mLevel = 1;
    };

    class StyleHud
    {
    public:
        /// Everything the HUD reads, as functions so that a test needs no combat rules.
        struct Source
        {
            /// Calls the visitor for every ready actor fighter (SkyrimCombat::forEachFighter).
            std::function<void(const SkyrimCombat::FighterVisitor&)> mFighters;
            /// A fighter's view (SkyrimCombat::fighterView); the player's is asked for its level.
            std::function<bool(openeden::sdk::game::CombatSubject, SkyrimCombat::FighterView&)> mLookup;
            /// Whether an actor fights on the player's side (a follower); may be empty.
            std::function<bool(openeden::sdk::game::WorldId)> mIsAlly;
        };

        StyleHud(const CombatHud& hud, Source source);

        /// Once per combatUpdate, after the CombatHud's own update (positions are only readable there).
        void update(float seconds, openeden::sdk::game::CombatRequestWriter& writer);

        [[nodiscard]] bool active() const { return mUpdates > 0; }
        [[nodiscard]] const std::vector<ActorSample>& bars() const { return mBars; }
        [[nodiscard]] std::string_view barName(std::size_t index) const;
        [[nodiscard]] const Boss& boss() const { return mBoss; }
        [[nodiscard]] std::string_view bossName() const { return mBossName; }
        [[nodiscard]] std::uint16_t playerLevel() const { return mPlayerLevel; }
        void playerValues(std::array<double, kPlayerValueCount>& out) const;
        void bossValues(std::array<double, kBossValueCount>& out) const;
        /// kBarStride numbers per bar; `out` is cleared first.
        void barValues(std::vector<double>& out) const;

    private:
        struct Memo
        {
            openeden::sdk::game::ActorHandle mHandle;
            float mMagicka = 0.f;
            float mStamina = 0.f;
            float mSecondaryLeft = 0.f; ///< seconds the magicka and stamina bars still show
            float mEngagedAgo = 1.0e9f; ///< seconds since it was last engaged
            bool mSeen = false; ///< the baselines hold a real value
            bool mTouched = false; ///< seen in the current update
        };
        Memo& memoOf(openeden::sdk::game::ActorHandle handle);
        void visit(const openeden::sdk::game::CombatSubject& subject, const SkyrimCombat::FighterView& view);

        const CombatHud& mHud;
        Source mSource;
        std::vector<Memo> mMemos;
        std::vector<ActorSample> mCandidates;
        std::vector<ActorSample> mInRange;
        std::vector<ActorSample> mBars;
        std::vector<std::string> mNames;
        Boss mBoss;
        std::string mBossName;
        std::uint16_t mPlayerLevel = 1;
        std::size_t mUpdates = 0;
        SkyrimCombat::FighterVisitor mVisitor; // built once, so an update allocates nothing
        float mSeconds = 0.f;
        openeden::sdk::game::CombatRequestWriter* mWriter = nullptr;
        double mPlayerX = 0.0;
        double mPlayerY = 0.0;
        double mPlayerZ = 0.0;
    };

    /// The package `core.skyrimse.hudstyle` 1.0, all Read: `active() -> bool`, `player() -> number[7]`,
    /// `boss() -> number[6]`, `boss_name() -> string`, `bars() -> number[]` (kBarStride numbers per bar) and
    /// `bar_names() -> string[]` (one per bar). The HUD must outlive the declaration.
    class StyleBindings
    {
    public:
        explicit StyleBindings(const StyleHud& hud);
        ~StyleBindings();
        StyleBindings(const StyleBindings&) = delete;
        StyleBindings& operator=(const StyleBindings&) = delete;
        [[nodiscard]] openeden::sdk::DeclareResult declareTo(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
