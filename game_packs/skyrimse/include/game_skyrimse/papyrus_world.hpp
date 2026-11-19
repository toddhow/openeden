#ifndef OPENEDEN_GAME_SKYRIMSE_PAPYRUS_WORLD_H
#define OPENEDEN_GAME_SKYRIMSE_PAPYRUS_WORLD_H

// The Papyrus host's reach into the pack's systems (task 0198, milestone M-S6). Two parts, both plain and testable:
//
//  * `PackWorldHooks` implements `PapyrusWorldHooks` over the pack's inventory model and a few function objects (actor
//    values, health, death, notifications). A reference is a form id; 0x14 is the player, who is the inventory's
//    `kPlayerOwner`, every other reference is its own owner id (the placement id).
//  * `ScriptCombatBridge` and `ScriptEventWriter` carry what combat reports to the host: a wrapper around the combat
//    request writer tells the bridge of each damage and death report that went through (combat.cpp is not changed), and
//    the bridge starts the deaths scripts asked for (Actor.Kill) inside the combat update, where the writer is valid.
//
// Threading: everything runs on the main thread. No heap allocation per call except in the std::function objects the
// pack supplies.

#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/papyrus_host.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/combat_rules.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    inline constexpr FormId kPapyrusPlayerReference = 0x14;

    /// What PackWorldHooks needs besides the inventory model. Every function may be empty (the hook then says
    /// "cannot").
    struct PackWorldEnv
    {
        /// The model, or null while the inventory is off.
        std::function<inventory::InventoryModel*()> model;
        /// Task 0318: the item a leveled item list resolves to for a quest alias that creates it in an inventory (0: none).
        std::function<FormId(FormId)> resolveItem;
        /// Task 0335: the item table is built (empty: ready).
        std::function<bool()> itemsReady;
        /// Registers the owner of a reference in the model (from its base record) so that an item can go in; true when
        /// the owner exists afterwards.
        std::function<bool(std::uint64_t owner, FormId reference)> ensureOwner;
        std::function<bool(FormId actor, std::string_view lowerName, double& current, double& maximum)> actorValue;
        std::function<bool(FormId actor, double health)> setHealth;
        std::function<bool(FormId actor)> isDead;
        std::function<bool(FormId actor)> queueKill;
        std::function<bool(FormId form)> isItem;
        std::function<std::int32_t(FormId form)> goldValue; ///< task 0285: the item's base value, -1 unknown
        std::function<bool(FormId word)> teachWord; ///< task 0205
        std::function<bool(FormId word)> unlockWord;
        std::function<bool(FormId word)> isWordUnlocked;
        std::function<bool(FormId reference)> addToMap; ///< task 0296
        /// Task 0258: the arrest dialogue's fragments (see PapyrusWorldHooks).
        std::function<FormId(FormId actor)> crimeFactionOf;
        std::function<bool(FormId faction, bool removeStolenItems, bool goToJail)> payCrimeGold;
        std::function<bool(FormId faction, bool removeInventory, bool realJail)> sendToJail;
        std::function<bool(FormId actor)> resistArrest;
        std::function<std::int32_t(FormId faction)> crimeGold;
        std::function<void(const std::string&)> notification;
        /// Task 0202: enable or disable with a fade, move or turn, and create (PlaceAtMe) through the world.
        std::function<bool(FormId reference, bool enabled, float fadeSeconds)> setObjectEnabled;
        std::function<bool(FormId reference, const ObjectMoveRequest& request)> moveObject;
        /// Task 0286: a move into another cell (see PapyrusWorldHooks::moveToCell).
        std::function<bool(FormId reference, FormId destinationCell, const ObjectMoveRequest& request)> moveToCell;
        std::function<ObjectPlaceResult(const ObjectPlaceRequest& request)> placeObject;
        std::function<bool(FormId cell, FormId owner)> setCellOwner; ///< task 0261
        std::function<bool(FormId reference)> referenceLoaded; ///< task 0268
        std::function<bool(FormId cart, FormId horse, const double (&position)[3], double heading)>
            tetherCart; ///< task 0274
        std::function<bool(FormId rider, FormId vehicle)> setVehicle; ///< task 0274
        std::function<bool(FormId reference, const std::string& event, double& seconds)>
            playObjectAnimation; ///< task 0289
        std::function<bool(FormId actor, FormId idle, std::string& animationEvent, double& seconds)>
            playIdle; ///< task 0274
        std::function<ObjectPlaceResult(FormId reference, FormId base, const ObjectMoveRequest& request, FormId cell)>
            relocateObject; ///< task 0268
        std::function<bool(const PapyrusWorldHooks::WeaponFireRequest& request)> fireWeapon; ///< task 0299
        std::function<bool(FormId actor)> startCombat; ///< task 0264
        std::function<bool(FormId actor)> stopCombat;
        std::function<bool(FormId actor)> resetHealth;
        std::function<bool(FormId actor)> inCombat;
        std::function<void(FormId actor, std::string_view lowerName, double value)> actorValueSet; ///< task 0302
        std::function<bool(FormId reference, double& x, double& y, double& z)> livePosition; ///< task 0313
    };

    class PackWorldHooks final : public PapyrusWorldHooks
    {
    public:
        explicit PackWorldHooks(PackWorldEnv env)
            : mEnv(std::move(env))
        {
        }

        /// The inventory owner of a reference, and back (the player is the owner 0 and the reference 0x14).
        [[nodiscard]] static std::uint64_t ownerOf(FormId reference)
        {
            return reference == kPapyrusPlayerReference ? inventory::kPlayerOwner : reference;
        }
        [[nodiscard]] static FormId referenceOf(std::uint64_t owner)
        {
            if (owner == inventory::InventoryModel::kNoOwner)
                return 0;
            return owner == inventory::kPlayerOwner ? kPapyrusPlayerReference : static_cast<FormId>(owner);
        }

        FormId resolveLeveledItem(FormId base) override;
        bool itemsReady() override; // task 0335
        bool addItem(FormId owner, FormId item, std::int32_t count) override;
        std::int32_t removeItem(FormId owner, FormId item, std::int32_t count) override;
        std::int32_t itemCount(FormId owner, FormId item) override;
        std::int32_t goldValue(FormId item) override;
        bool transferItem(FormId from, FormId to, FormId item, std::int32_t count) override;
        std::int64_t removeAllItems(FormId owner, FormId target) override;
        bool actorValue(FormId actor, std::string_view lowerName, double& current, double& maximum) override;
        bool setHealth(FormId actor, double health) override;
        bool isDead(FormId actor) override;
        bool killActor(FormId actor) override;
        bool isItem(FormId form) override;
        bool fireWeapon(const WeaponFireRequest& request) override
        {
            return mEnv.fireWeapon && mEnv.fireWeapon(request);
        }
        void notification(const std::string& text) override;
        bool setObjectEnabled(FormId reference, bool enabled, float fadeSeconds) override
        {
            return mEnv.setObjectEnabled && mEnv.setObjectEnabled(reference, enabled, fadeSeconds);
        }
        bool moveObject(FormId reference, const ObjectMoveRequest& request) override
        {
            return mEnv.moveObject && mEnv.moveObject(reference, request);
        }
        bool moveToCell(FormId reference, FormId destinationCell, const ObjectMoveRequest& request) override
        {
            return mEnv.moveToCell && mEnv.moveToCell(reference, destinationCell, request);
        }
        ObjectPlaceResult placeObject(const ObjectPlaceRequest& request) override
        {
            return mEnv.placeObject ? mEnv.placeObject(request) : ObjectPlaceResult{};
        }
        bool tetherCart(FormId cart, FormId horse, const double (&position)[3], double heading) override
        {
            return mEnv.tetherCart && mEnv.tetherCart(cart, horse, position, heading);
        }
        bool playIdle(FormId actor, FormId idle, std::string& animationEvent, double& seconds) override
        {
            return mEnv.playIdle && mEnv.playIdle(actor, idle, animationEvent, seconds);
        }
        bool playObjectAnimation(FormId reference, const std::string& event, double& seconds) override
        {
            return mEnv.playObjectAnimation && mEnv.playObjectAnimation(reference, event, seconds);
        }
        bool setVehicle(FormId rider, FormId vehicle) override
        {
            return mEnv.setVehicle && mEnv.setVehicle(rider, vehicle);
        }
        bool referenceLoaded(FormId reference) override
        {
            return !mEnv.referenceLoaded || mEnv.referenceLoaded(reference);
        }
        ObjectPlaceResult relocateObject(
            FormId reference, FormId base, const ObjectMoveRequest& request, FormId destinationCell) override
        {
            return mEnv.relocateObject ? mEnv.relocateObject(reference, base, request, destinationCell)
                                       : ObjectPlaceResult{};
        }
        bool setCellOwner(FormId cell, FormId owner) override
        {
            return mEnv.setCellOwner && mEnv.setCellOwner(cell, owner);
        }
        bool startCombat(FormId actor) override { return mEnv.startCombat && mEnv.startCombat(actor); }
        bool stopCombat(FormId actor) override { return mEnv.stopCombat && mEnv.stopCombat(actor); }
        bool resetHealth(FormId actor) override { return mEnv.resetHealth && mEnv.resetHealth(actor); }
        bool inCombat(FormId actor) override { return mEnv.inCombat && mEnv.inCombat(actor); }
        void actorValueSet(FormId actor, std::string_view lowerName, double value) override
        {
            if (mEnv.actorValueSet)
                mEnv.actorValueSet(actor, lowerName, value); // task 0302
        }
        bool livePosition(FormId reference, double& x, double& y, double& z) override
        {
            return mEnv.livePosition && mEnv.livePosition(reference, x, y, z); // task 0313
        }
        bool addToMap(FormId reference) override { return mEnv.addToMap && mEnv.addToMap(reference); }
        bool teachWord(FormId word) override { return mEnv.teachWord && mEnv.teachWord(word); }
        bool unlockWord(FormId word) override { return mEnv.unlockWord && mEnv.unlockWord(word); }
        bool isWordUnlocked(FormId word) override { return mEnv.isWordUnlocked && mEnv.isWordUnlocked(word); }
        FormId crimeFactionOf(FormId actor) override { return mEnv.crimeFactionOf ? mEnv.crimeFactionOf(actor) : 0; }
        bool payCrimeGold(FormId faction, bool removeStolenItems, bool goToJail) override
        {
            return mEnv.payCrimeGold && mEnv.payCrimeGold(faction, removeStolenItems, goToJail);
        }
        bool sendToJail(FormId faction, bool removeInventory, bool realJail) override
        {
            return mEnv.sendToJail && mEnv.sendToJail(faction, removeInventory, realJail);
        }
        bool resistArrest(FormId actor) override { return mEnv.resistArrest && mEnv.resistArrest(actor); }
        std::int32_t crimeGold(FormId faction) override { return mEnv.crimeGold ? mEnv.crimeGold(faction) : -1; }

    private:
        inventory::InventoryModel* model() const { return mEnv.model ? mEnv.model() : nullptr; }
        bool ready(FormId reference, inventory::InventoryModel*& model) const;
        PackWorldEnv mEnv;
    };

    /// The script namespace `skyrimse.papyrus` 1.0 (Read): what Debug.Notification said, for the HUD script
    /// (`files/data/scripts/openeden/skyrimse/papyrus_player.lua`).
    ///   notification_count() -> int     how many notifications there have been (a script shows the new ones)
    ///   notification(n) -> string       the n-th (1-based), empty when it left the ring of
    ///   PapyrusHost::kMaxNotifications
    class PapyrusNotificationBindings
    {
    public:
        explicit PapyrusNotificationBindings(PapyrusHost& host);
        ~PapyrusNotificationBindings();
        PapyrusNotificationBindings(const PapyrusNotificationBindings&) = delete;
        PapyrusNotificationBindings& operator=(const PapyrusNotificationBindings&) = delete;
        [[nodiscard]] openeden::sdk::DeclareResult declareTo(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    /// Carries the combat reports to the host and the host's kills to the combat update.
    class ScriptCombatBridge
    {
    public:
        using HitFn = std::function<void(FormId victim, FormId attacker, bool blocked, bool bash)>;
        using DeathFn = std::function<void(FormId victim, FormId killer)>;
        /// Starts the death of an actor in the combat rules' own terms (the pack gives it the handle and combat).
        using KillFn = std::function<void(FormId actor, openeden::sdk::game::CombatRequestWriter& writer)>;

        static constexpr std::size_t kMaxKills = 256;
        static constexpr std::size_t kMaxRememberedAttackers = 256;

        /// Task 0299: a script's Weapon.Fire waits here until the next combat update, which fires it through the
        /// combat rules (the shot function gets the combat's own writer).
        struct QueuedShot
        {
            FormId mWeapon = 0;
            FormId mAmmo = 0;
            double x = 0.0, y = 0.0, z = 0.0;
            float heading = 0.f, pitch = 0.f;
        };
        using ShotFn = std::function<void(const QueuedShot&, openeden::sdk::game::CombatRequestWriter& writer)>;
        static constexpr std::size_t kMaxShots = 64;
        void setShotSink(ShotFn fire) { mShot = std::move(fire); }
        bool queueShot(const QueuedShot& shot)
        {
            if (!mShot || mShots.size() >= kMaxShots)
                return false;
            mShots.push_back(shot);
            return true;
        }

        void setSinks(HitFn onHit, DeathFn onDeath, KillFn kill)
        {
            mHit = std::move(onHit);
            mDeath = std::move(onDeath);
            mKill = std::move(kill);
        }
        [[nodiscard]] bool active() const { return static_cast<bool>(mHit) || static_cast<bool>(mDeath); }

        /// Actor.Kill: queued until the next combat update. False when the queue is full.
        bool queueKill(FormId actor);
        [[nodiscard]] std::size_t pendingKills() const { return mKills.size(); }

        /// A damage report that went through. `reference` of a handle is the placement id.
        void noteDamage(const openeden::sdk::game::DamageReport& report);
        /// A death report that went through.
        void noteDeath(openeden::sdk::game::ActorHandle actor);
        /// Starts the queued deaths on `writer` (the inner writer of the combat update); reports them to the host.
        void drain(openeden::sdk::game::CombatRequestWriter& writer);

    private:
        HitFn mHit;
        DeathFn mDeath;
        KillFn mKill;
        std::vector<FormId> mKills;
        ShotFn mShot;
        std::vector<QueuedShot> mShots;
        std::map<FormId, FormId> mLastAttacker; ///< victim -> the attacker of the last damage (0: the player is 0x14)
    };

    /// Passes every request on and tells the bridge about each damage and death report that went through. A wrapper
    /// that does not forward a virtual gives the pack the refusing default, so every function of the contract is here.
    class ScriptEventWriter final : public openeden::sdk::game::CombatRequestWriter
    {
    public:
        ScriptEventWriter(openeden::sdk::game::CombatRequestWriter& inner, ScriptCombatBridge& bridge)
            : mInner(inner)
            , mBridge(bridge)
        {
        }
        bool playClip(openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::ClipRequest& r) override
        {
            return mInner.playClip(subject, r);
        }
        bool stopClip(openeden::sdk::game::CombatSubject subject) override { return mInner.stopClip(subject); }
        bool requestSweep(
            openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::SweepRequest& r) override
        {
            return mInner.requestSweep(subject, r);
        }
        bool reportDamage(const openeden::sdk::game::DamageReport& report) override
        {
            const bool ok = mInner.reportDamage(report);
            if (ok)
                mBridge.noteDamage(report);
            return ok;
        }
        bool faceHeading(openeden::sdk::game::CombatSubject subject, float heading) override
        {
            return mInner.faceHeading(subject, heading);
        }
        bool clipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out) override
        {
            return mInner.clipTiming(clip, perSex, out);
        }
        bool subjectState(openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out) override
        {
            return mInner.subjectState(subject, out);
        }
        bool launchProjectile(
            openeden::sdk::game::CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r) override
        {
            return mInner.launchProjectile(owner, r);
        }
        bool setClipSpeed(openeden::sdk::game::CombatSubject subject, float multiplier) override
        {
            return mInner.setClipSpeed(subject, multiplier);
        }
        bool clipEventTime(
            std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind, float& out) override
        {
            return mInner.clipEventTime(clip, perSex, kind, out);
        }
        bool reportDeath(openeden::sdk::game::ActorHandle actor) override
        {
            const bool ok = mInner.reportDeath(actor);
            if (ok)
                mBridge.noteDeath(actor);
            return ok;
        }
        bool castRay(openeden::sdk::game::CombatSubject from, const openeden::sdk::game::RayRequest& request,
            openeden::sdk::game::RayHit& out) override
        {
            return mInner.castRay(from, request, out);
        }
        bool showEffect(
            openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::EffectRequest& request) override
        {
            return mInner.showEffect(subject, request);
        }
        bool hideEffect(std::uint32_t token) override { return mInner.hideEffect(token); }
        bool cancelProjectile(std::uint32_t token) override { return mInner.cancelProjectile(token); } // task 0262
        bool playSound(const openeden::sdk::game::SoundRequest& request) override { return mInner.playSound(request); }

    private:
        openeden::sdk::game::CombatRequestWriter& mInner;
        ScriptCombatBridge& mBridge;
    };
}

#endif
