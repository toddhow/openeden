#ifndef OPENEDEN_GAME_SKYRIMSE_HUD_H
#define OPENEDEN_GAME_SKYRIMSE_HUD_H

// The combat HUD's numbers (task 0180, stage S5-11 of research note 0056; decision 0015 for the UI, task 0097 for the
// script bindings).
//
// The pack owns the combat numbers; the HUD is drawn by a Lua script on the RmlUi library (files/data/openeden/hud/
// skyrimcombat.lua), which reads them once a frame through the read-only script package `core.skyrimse.combat`. This
// file is the pack's side of that: CombatHud keeps what the screen shows (the player's three bars and the bar of the
// enemy hit last or hitting last), HudWriter watches the damage and death reports that pass through the combat
// rules' writer without changing them, and CombatHudBindings declares the three functions of the package.
//
// What is shown and for how long is ours (the data has no HUD rules): the enemy bar stays kHudEnemyBarSeconds after the
// last hit given or taken, and kHudDeadBarSeconds after the target's death.
//
// No engine, renderer, Qt or script-language type. Main thread (the bindings' Read handlers run on the thread that
// runs scripts, which is the main thread today, and never overlap the pack's own phases).

#include <game_skyrimse/combat.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/bindings.hpp>
#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// Seconds the enemy bar stays after the last hit given or taken, and after its target dies (ours, tunable).
    inline constexpr float kHudEnemyBarSeconds = 6.f;
    inline constexpr float kHudDeadBarSeconds = 2.f;
    /// The longest actor name the HUD keeps (UTF-8 bytes); a longer one is cut at a character boundary.
    inline constexpr std::size_t kMaxHudNameBytes = 64;
    /// Actors whose names the HUD remembers; beyond it a new actor is shown with no name.
    inline constexpr std::size_t kMaxHudActors = 4096;
    /// The bar shows the player's real magicka (task 0186) when it has a maximum; without one it shows this full
    /// stand-in (ours).
    inline constexpr float kHudMagickaStandIn = 100.f;
    /// Numbers `hud()` returns, in this order: player health, health maximum, magicka, magicka maximum, stamina,
    /// stamina maximum, enemy shown (0 or 1), enemy health, enemy health maximum, enemy dead (0 or 1).
    inline constexpr std::size_t kHudValueCount = 10;
    /// The script package is `core.skyrimse.combat`, API_VERSION 1.0.
    inline constexpr std::string_view kHudNamespace = "skyrimse.combat";

    struct HudSnapshot
    {
        float mPlayerHealth = 0.f;
        float mPlayerHealthMax = 0.f;
        float mPlayerMagicka = 0.f;
        float mPlayerMagickaMax = 0.f;
        float mPlayerStamina = 0.f;
        float mPlayerStaminaMax = 0.f;
        bool mEnemyShown = false;
        float mEnemyHealth = 0.f;
        float mEnemyHealthMax = 0.f;
        bool mEnemyDead = false;
    };

    class CombatHud
    {
    public:
        CombatHud();

        /// Where the bars of a fighter come from (SkyrimCombat::fighterView); set once. Without it the player's
        /// stamina and an enemy's live health are not refreshed (the last report's numbers stay).
        using FighterLookup = std::function<bool(openeden::sdk::game::CombatSubject, SkyrimCombat::FighterView&)>;
        void setFighterLookup(FighterLookup lookup) { mLookup = std::move(lookup); }

        /// An actor entered or left the world. The name is what the bar says (cut to kMaxHudNameBytes).
        void actorAdded(openeden::sdk::game::ActorHandle handle, std::string_view name);
        void actorRemoved(openeden::sdk::game::ActorHandle handle);

        /// A hit the pack resolved. The enemy is the other side when exactly one side is the player; a hit between
        /// two actors changes nothing.
        void noteDamage(const openeden::sdk::game::DamageReport& report);
        void noteDeath(openeden::sdk::game::ActorHandle actor);

        /// Once per combatUpdate, inside it (the player's state is only readable there).
        void update(float seconds, openeden::sdk::game::CombatRequestWriter& writer);

        /// True from the first update on: combat is running in this session (the script package says so).
        [[nodiscard]] bool active() const { return mUpdates > 0; }
        [[nodiscard]] const HudSnapshot& snapshot() const { return mSnapshot; }
        [[nodiscard]] std::string_view enemyName() const { return mEnemyName; }
        void values(std::array<double, kHudValueCount>& out) const;

    private:
        struct Entry
        {
            openeden::sdk::game::ActorHandle mHandle;
            std::string mName;
        };
        FighterLookup mLookup;
        std::vector<Entry> mActors;
        HudSnapshot mSnapshot;
        std::string mEnemyName;
        openeden::sdk::game::ActorHandle mTarget;
        float mSince = 0.f; ///< seconds since the target was last hit or died
        std::size_t mUpdates = 0;
    };

    /// Passes every request on and tells the HUD about the reports that went through.
    class HudWriter final : public openeden::sdk::game::CombatRequestWriter
    {
    public:
        HudWriter(openeden::sdk::game::CombatRequestWriter& inner, CombatHud& hud)
            : mInner(inner)
            , mHud(hud)
        {
        }
        bool playClip(openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::ClipRequest& r) override;
        bool stopClip(openeden::sdk::game::CombatSubject subject) override;
        bool requestSweep(
            openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::SweepRequest& r) override;
        bool reportDamage(const openeden::sdk::game::DamageReport& report) override;
        bool faceHeading(openeden::sdk::game::CombatSubject subject, float heading) override;
        bool clipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out) override;
        bool subjectState(openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out) override;
        bool launchProjectile(
            openeden::sdk::game::CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r) override;
        bool setClipSpeed(openeden::sdk::game::CombatSubject subject, float multiplier) override;
        bool clipEventTime(
            std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind, float& out) override;
        bool reportDeath(openeden::sdk::game::ActorHandle actor) override;

    private:
        openeden::sdk::game::CombatRequestWriter& mInner;
        CombatHud& mHud;
    };

    /// The package `core.skyrimse.combat` 1.0: `active() -> bool`, `hud() -> number[10]` (see kHudValueCount) and
    /// `enemy_name() -> string` (empty when no enemy is shown). All Read; the handlers read the CombatHud, which must
    /// outlive the declaration (the pack owns both).
    class CombatHudBindings
    {
    public:
        explicit CombatHudBindings(const CombatHud& hud);
        ~CombatHudBindings();
        CombatHudBindings(const CombatHudBindings&) = delete;
        CombatHudBindings& operator=(const CombatHudBindings&) = delete;

        /// One call, from the pack's start().
        [[nodiscard]] openeden::sdk::DeclareResult declareTo(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
