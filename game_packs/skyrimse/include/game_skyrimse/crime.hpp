#ifndef OPENEDEN_GAME_SKYRIMSE_CRIME_H
#define OPENEDEN_GAME_SKYRIMSE_CRIME_H

// Skyrim crime for the player (task 0204, docs/tasks/0204-skyrim-crime.md): what counts as a crime, who witnesses it,
// what it costs in a crime faction, which items are stolen, and the guard that comes to collect.
//
// What is the data's and what is ours: the bounty numbers (CRVA of the crime faction: murder, assault, trespass,
// pickpocket, the steal multiplier), the guard faction and the NPCs' crime factions (CRIF) come from the records. The
// rules that combine them are OURS, named below and tunable: witness distance and height, the rounding of a theft's
// bounty, the guard's call radius and approach, the attack-on-sight bounty, the jail time.
//
// Standard library and the SDK combat and save chunk types only; no clock (the caller passes seconds), no records.

#include <game_skyrimse/actors.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::crime
{
    enum class Kind : std::uint8_t
    {
        Theft,
        Assault,
        Murder,
        Trespass,
        Pickpocket, ///< a pickpocket try the victim noticed (task 0208)
    };

    [[nodiscard]] std::string_view kindName(Kind kind);

    /// The Player NPC_ (form id 7: the owner of things that are the players own, seen on Skyrim.esm containers).
    inline constexpr FormId kPlayerNpcId = 0x00000007;

    /// A bounty in one faction never exceeds this (a sum that would is clamped).
    inline constexpr std::int32_t kMaxBounty = 1'000'000;
    /// The most items and factions a ledger holds (hostile chunks and long games).
    inline constexpr std::size_t kMaxLedgerEntries = 4096;

    struct Settings
    {
        /// A witness sees a crime within this distance (flat) and this height difference of the player. OURS
        /// (RECALLED-VERIFY: Skyrim uses detection and sight ranges).
        float mWitnessDistance = 2000.f;
        float mWitnessHeight = 200.f;
        /// A guard of the faction within this distance of the player comes. OURS.
        float mGuardCallRadius = 6000.f;
        /// A guard stops and offers within this distance of the player. OURS.
        float mTalkDistance = 220.f;
        /// A guard farther than this runs, else it walks. OURS.
        float mRunDistance = 500.f;
        /// A bounty at or above this makes the guards attack instead of offering. OURS (every hold faction's "attack
        /// on sight" flag is set, so it cannot tell).
        std::int32_t mAttackOnSightBounty = 5000;
        /// Seconds the player is held in jail. OURS (vanilla advances days).
        float mJailSeconds = 6.f;
        /// Seconds before the same faction offers again after a choice or a give-up. OURS.
        float mOfferCooldown = 20.f;
        /// A guard that has not arrived after this long gives up. A waiting offer lapses after kOfferTimeout. OURS.
        float mApproachTimeout = 40.f;
        float mOfferTimeout = 120.f;
        /// The goal of an approaching guard is re-issued this often, and when the player moved this far. OURS.
        float mRetargetPeriod = 0.5f;
        float mRetargetDistance = 48.f;
        /// At most this many witnesses are tested by ray per crime (nearest first). OURS.
        std::size_t mMaxRays = 16;
        /// Trespass (task 0213), all OURS. A trespass is first judged this long after the event (a new cell's actors
        /// need a moment to exist), then every mTrespassRetry until a witness sees it or the watch time passes (a cell
        /// visit and a bed attempt have their own). A bed attempt on the same bed within mTrespassRepeat is not
        /// charged again.
        float mTrespassDelay = 1.5f;
        float mTrespassRetry = 0.5f;
        float mTrespassWatchCell = 30.f;
        float mTrespassWatchBed = 2.f;
        float mTrespassRepeat = 120.f;
    };

    // ---- trespass (task 0213) -------------------------------------------------------------------------------------

    /// What the rules read of a cell the player entered (from the CELL records of the load order).
    struct CellAccess
    {
        bool mKnown = false;
        bool mInterior = false;
        bool mPublic = false; ///< the cell's "public area" flag
        FormId mOwner = kNullFormId; ///< the cell's XOWN (an NPC_ or a FACT), 0 when it has none
    };

    /// Is this owner the player's own (the Player NPC_ or PlayerFaction; the stand-in is in no other faction)?
    [[nodiscard]] bool isPlayersOwner(FormId owner);

    /// Is entering this cell a trespass? An INTERIOR cell with an owner that is not the player's and no public flag.
    /// A public cell, an unowned cell, an exterior cell and an unknown cell are never trespass.
    [[nodiscard]] bool cellEntryIsTrespass(const CellAccess& cell);

    /// What a trespass was done in: a cell visit (cancelled when the player leaves) or at a bed.
    enum class Place : std::uint8_t
    {
        Cell,
        Bed,
    };

    /// The bounty of a crime in a crime faction. Murder, Assault, Trespass: the CRVA numbers. Theft:
    /// ceil(value * count * stealMultiplier), at least 1 for a stolen item of positive value (the multiplier is the
    /// data's, the rounding and the minimum are OURS). A non-finite or negative multiplier, value or count reads as 0
    /// and costs nothing. Clamped to kMaxBounty.
    [[nodiscard]] std::int32_t bountyFor(
        Kind kind, const CrimeData& values, std::int32_t itemValue, std::int32_t count);

    /// Where a jail sends the player (from the JAIL marker's XTEL).
    struct JailDestination
    {
        bool mValid = false;
        std::string mCell; ///< the editor id of the destination cell
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        double mHeading = 0.0;
    };

    // ---- the ledger ---------------------------------------------------------------------------------------------

    /// The player's bounty per crime faction and the count of stolen items per item. Main thread.
    class Ledger
    {
    public:
        /// Adds gold to a faction's bounty (clamped to kMaxBounty). False for a null faction, a non-positive amount or
        /// when the table is full of other factions.
        bool addBounty(FormId faction, std::int32_t gold);
        [[nodiscard]] std::int32_t bounty(FormId faction) const;
        [[nodiscard]] std::int64_t totalBounty() const;
        void clearBounty(FormId faction);
        void clearAllBounties();
        [[nodiscard]] const std::map<FormId, std::int32_t>& bounties() const { return mBounty; }

        /// Marks `count` of an item stolen. False for a null item, a non-positive count or a full table.
        bool markStolen(FormId item, std::int32_t count);
        [[nodiscard]] std::int32_t stolen(FormId item) const;
        [[nodiscard]] std::int64_t stolenTotal() const;
        /// Forgets up to `count` stolen (a sale of a fence, a loss): returns how many were forgotten.
        std::int32_t forgetStolen(FormId item, std::int32_t count);
        void clearStolen();
        [[nodiscard]] const std::map<FormId, std::int32_t>& stolenItems() const { return mStolen; }

        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        void clear();

        /// The save form: u32 faction count, (u32 faction, i32 gold) ascending; u32 item count, (u32 item, i32 count)
        /// ascending. A read that is damaged, over the caps or not canonical changes nothing.
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        std::map<FormId, std::int32_t> mBounty;
        std::map<FormId, std::int32_t> mStolen;
        std::uint32_t mRevision = 0;
    };

    class LedgerChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "crime";
        explicit LedgerChunk(Ledger& ledger)
            : mLedger(ledger)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }
        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mLedger.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        Ledger& mLedger;
        std::function<void(const std::string&)> mSink;
        std::string mRefusal;
    };

    // ---- witnesses ------------------------------------------------------------------------------------------------

    /// What the rules read of a loaded actor.
    struct CrimeActor
    {
        openeden::sdk::game::ActorHandle mHandle;
        FormId mBase = kNullFormId;
        FormId mCrimeFaction = kNullFormId; ///< NPC_ CRIF (0: none, it collects no bounty)
        bool mGuard = false;
        bool mEnemyOfPlayer = false; ///< its factions are enemies of the player's
        bool mEngagedAgainstPlayer = false; ///< fighting the player now
        bool mDead = false;
        std::string mName;
    };

    /// Could this actor have noticed a crime at (x, y, z) done by the player at (px, py, pz) and does it care? Not
    /// dead, not an enemy of the player nor already fighting the player (a hostile reports nothing), within the
    /// witness distance (flat) and height of the player, with `lineClear` (its sight line to the player).
    [[nodiscard]] bool witnessSees(const Settings& settings, const CrimeActor& actor, double ax, double ay, double az,
        double px, double py, double pz, bool lineClear);

    /// Is hurting this actor a crime? Not when it is dead, an enemy of the player's side, or fighting the player.
    [[nodiscard]] bool isCrimeVictim(const CrimeActor& victim);

    // ---- the system -------------------------------------------------------------------------------------------------

    /// What the crime rules need of the game, implemented by the engine side (the cell source over the combat rules and
    /// the inventory) and by a test's fake. Main thread.
    class CrimeWorld
    {
    public:
        virtual ~CrimeWorld() = default;
        /// The loaded actors (the ones the combat rules know), appended.
        virtual void actors(std::vector<CrimeActor>& out) = 0;
        virtual bool actor(openeden::sdk::game::ActorHandle handle, CrimeActor& out) = 0;
        /// The crime data of a faction (null: it is not a crime faction).
        virtual const CrimeData* crimeData(FormId faction) = 0;
        virtual std::string factionName(FormId faction) = 0;
        virtual std::int32_t itemValue(FormId item) = 0;
        virtual std::int32_t playerGold() = 0;
        /// Takes gold from the player; false (nothing taken) when it does not have it.
        virtual bool takeGold(std::int32_t amount) = 0;
        /// Takes up to `count` of an item from the player; returns how many were taken.
        virtual std::int32_t takeItem(FormId item, std::int32_t count) = 0;
        virtual std::int32_t itemCount(FormId item) = 0;
        /// The guard walks to a point, stops (and holds still for the talk), is let go, or turns hostile.
        virtual void guardGoTo(openeden::sdk::game::ActorHandle guard, double x, double y, double z,
            openeden::sdk::game::ActorSpeed speed)
            = 0;
        virtual void guardHold(openeden::sdk::game::ActorHandle guard) = 0;
        virtual void guardRelease(openeden::sdk::game::ActorHandle guard) = 0;
        virtual void guardAttack(openeden::sdk::game::ActorHandle guard) = 0;
        /// One trace and log line (`CRIME ...`).
        virtual void log(const std::string& line) = 0;
    };

    enum class State : std::uint8_t
    {
        None, ///< nothing to collect, or waiting
        Approach, ///< a guard is walking to the player
        Offer, ///< the guard waits for a choice
        Jailed, ///< the player is held; `released()` ends it
        Resisted, ///< the guards fight
    };
    [[nodiscard]] std::string_view stateName(State state);

    enum class Choice : std::uint8_t
    {
        Pay,
        Jail,
        Resist,
    };

    enum class ChoiceStatus : std::uint8_t
    {
        Ok,
        NoOffer, ///< no guard is waiting
        NotEnoughGold,
        Unknown, ///< a choice text that is none of pay, jail, resist
    };

    class CrimeSystem
    {
    public:
        CrimeSystem(Ledger& ledger, CrimeWorld& world, Settings settings = {});

        /// The player took `count` of `item` out of a container somebody else owns. The item is marked stolen at once;
        /// the bounty waits for the next update's witnesses.
        void onTheft(FormId item, std::int32_t count);
        /// A pickpocket try that was caught (task 0208): the victim is a witness; nothing was taken, so no item is
        /// marked stolen. The bounty waits for the next update, like a theft's.
        void onPickpocketCaught(std::uint32_t victimReference);
        /// The player is where he is not welcome (task 0213): `place` is the cell or the bed placement. Judged later,
        /// by the witness rule, and charged once (a cell visit) or once per mTrespassRepeat (a bed).
        void onTrespass(FormId place, Place kind);
        /// The player left the cell he trespassed in: a pending cell visit is dropped.
        void onLeftPlace();
        /// A damage report as it passed through the combat chain, BEFORE the victim is provoked.
        void onDamage(const openeden::sdk::game::DamageReport& report);
        /// An actor died (reportDeath), seen before the report goes on.
        void onDeath(openeden::sdk::game::ActorHandle actor);
        /// Once a frame, inside combatUpdate (rays and positions only work there).
        void update(float seconds, openeden::sdk::game::CombatRequestWriter& writer);
        /// The player's choice at an offer.
        ChoiceStatus choose(std::string_view choice);
        ChoiceStatus choose(Choice choice);
        /// The jail time is over and the player has been taken back (Lua calls this).
        void released();
        /// Replaces the line the window shows (a refusal the service found, such as a faction with no jail).
        void setMessage(std::string text);

        /// Task 0227 (sneaking): with a lookup set, only a witness that has NOTICED the player counts (the lookup answers
        /// per actor reference); without one (the default) a clear sight line is enough, as before.
        void setDetectedLookup(std::function<bool(std::uint32_t reference)> lookup) { mDetected = std::move(lookup); }

        /// Task 0235: called once for each witness (nearest first) of a crime that counts, after its bounty is charged:
        /// the pack makes the witness speak. Unset (the default): nothing is told. Main thread, inside update().
        using WitnessHook = std::function<void(Kind crime, openeden::sdk::game::ActorHandle witness)>;
        void setWitnessHook(WitnessHook hook) { mWitnessHook = std::move(hook); }
        /// Task 0243: during a witness hook call, the victim of the crime being judged (an assault, a murder, a caught
        /// pickpocket); a handle with reference 0 when it has none (a theft, a trespass).
        [[nodiscard]] openeden::sdk::game::ActorHandle witnessVictim() const { return mWitnessVictim; }

        [[nodiscard]] State state() const { return mState; }
        [[nodiscard]] FormId offerFaction() const { return mFaction; }
        [[nodiscard]] const std::string& guardName() const { return mGuardName; }
        [[nodiscard]] const std::string& message() const { return mMessage; }
        [[nodiscard]] std::int32_t offerBounty() const { return mLedger.bounty(mFaction); }
        [[nodiscard]] bool canPay() const;
        [[nodiscard]] const Settings& settings() const { return mSettings; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        /// The faction whose jail the player was sent to (0 when not jailed).
        [[nodiscard]] FormId jailFaction() const { return mJailFaction; }
        [[nodiscard]] std::size_t pendingCount() const { return mPending.size(); }
        [[nodiscard]] std::size_t raysCast() const { return mRays; }
        [[nodiscard]] float clock() const { return mClock; }

    private:
        struct Pending
        {
            Kind mKind = Kind::Theft;
            FormId mItem = kNullFormId;
            std::int32_t mCount = 0;
            openeden::sdk::game::ActorHandle mVictim;
            FormId mPlace = kNullFormId; ///< trespass: the cell or bed
            Place mPlaceKind = Place::Cell;
            float mReadyAt = 0.f; ///< trespass: not judged before this clock time
            float mExpiresAt = 0.f; ///< trespass: dropped, unwitnessed, after this
            float mNextTryAt = 0.f; ///< trespass: not judged again before this
        };
        /// True when a witness was found (whether or not it cost anything).
        bool evaluate(openeden::sdk::game::CombatRequestWriter& writer, const Pending& crime, bool quiet = false);
        void runGuards(openeden::sdk::game::CombatRequestWriter& writer);
        void note(std::string text);
        void finishOffer(float cooldown);
        void confiscate();

        Ledger& mLedger;
        CrimeWorld& mWorld;
        Settings mSettings;
        State mState = State::None;
        FormId mFaction = kNullFormId; ///< the faction of the offer
        openeden::sdk::game::ActorHandle mGuard;
        std::string mGuardName;
        std::string mMessage;
        std::vector<Pending> mPending;
        std::vector<openeden::sdk::game::ActorHandle> mAssaulted; ///< non-hostile victims the player has hit
        std::vector<openeden::sdk::game::ActorHandle> mFoughtBack; ///< NPCs that have hurt the player
        std::map<FormId, float> mCooldownUntil; ///< per faction: the clock before which it does not offer
        std::map<FormId, float> mBedCharged; ///< per bed: the clock at which a bed attempt was last accepted
        std::vector<CrimeActor> mRoster; ///< reused
        std::vector<std::pair<double, std::size_t>> mScratch; ///< reused: (distance, roster index)
        float mClock = 0.f;
        float mStateSince = 0.f;
        float mLastGoalAt = -1.0e9f;
        double mGoalX = 0.0, mGoalY = 0.0;
        FormId mJailFaction = kNullFormId;
        float mJailUntil = 0.f;
        std::size_t mRays = 0;
        std::uint32_t mRevision = 0;
        std::function<bool(std::uint32_t)> mDetected; ///< task 0227
        WitnessHook mWitnessHook; ///< task 0235
        openeden::sdk::game::ActorHandle mWitnessVictim; ///< task 0243: set only while the hook runs
    };

    /// The outermost wrapper of the combat chain (task 0204): it tells the crime system of every damage report and
    /// death BEFORE the report goes on (so before the victim is provoked), and forwards everything. It adds no virtual; every
    /// virtual of the contract is forwarded (a wrapper that does not forward a new one silently gives the pack the
    /// refusing default).
    class CrimeWriter final : public openeden::sdk::game::CombatRequestWriter
    {
    public:
        CrimeWriter(openeden::sdk::game::CombatRequestWriter& inner, CrimeSystem& system)
            : mInner(inner)
            , mSystem(system)
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
            mSystem.onDamage(report);
            return mInner.reportDamage(report);
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
            mSystem.onDeath(actor);
            return mInner.reportDeath(actor);
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
        bool playSound(const openeden::sdk::game::SoundRequest& request) override { return mInner.playSound(request); }

    private:
        openeden::sdk::game::CombatRequestWriter& mInner;
        CrimeSystem& mSystem;
    };
}

#endif
