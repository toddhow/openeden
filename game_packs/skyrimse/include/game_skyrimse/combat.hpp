#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_H

// The Skyrim SE pack's side of the combat hooks (task 0168 stage S5-0, task 0170 stage S5-2, task 0171 stage S5-3;
// decision 0018, sdk/include/openeden/game/combat_rules.hpp).
//
// S5-2 gave the slot table its clips for the one-handed sword and a periodic test swing. S5-3 adds the first rules: on
// a HitFrame event the pack asks for a sweep along the weapon's reach, spends the attacker's stamina, takes the nearest
// valid target's health down by computeMeleeHit (combat_stats.hpp) against that target's armour, plays a hit reaction
// on an NPC, reports the damage and, at 0 health, the death (once). The pack owns every actor's health and stamina;
// for the player the health is the engine's, read and written through the contract (SubjectState, DamageReport).
// The weapon is the WEAP named by the test setting `combat test weapon` (empty: unarmed): inventory weapons wait for
// S5-9. Armour is the NPC's default outfit; inventory armour waits for S5-9.
//
// Test rules, all off by default: `combat test swing` (periodic swings), `combat test hostile` (attacks the player when
// in reach), `combat test player swing` (the player swings every N seconds).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only; the vectors are reserved so that a frame allocates
// nothing (the trace callback, when set, builds strings: tracing is a developer aid).

#include <game_skyrimse/combat_stats.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// Weapon animation types (WEAP DNAM +0).
    inline constexpr std::uint8_t kWeaponAnimHandToHand = 0;
    inline constexpr std::uint8_t kWeaponAnim1HSword = 1;

    inline constexpr std::string_view kCombatTestSwingKey = "combat test swing";
    inline constexpr std::size_t kMaxCombatTestSwingName = 128;

    /// Task 0171 test settings, all off by default: the named actors attack the player when it is in reach, the player
    /// swings every N seconds, the editor id of the WEAP all fighters use (empty: unarmed), and the decision trace.
    inline constexpr std::string_view kCombatTestHostileKey = "combat test hostile";
    inline constexpr std::string_view kCombatTestPlayerSwingKey = "combat test player swing";
    inline constexpr std::string_view kCombatTestWeaponKey = "combat test weapon";
    inline constexpr std::string_view kCombatTraceKey = "combat trace";

    /// The sweep's half angle: RACE ATKD +20 "strike angle" is 35 degrees in 1,582 of 2,065 attacks (note 0056 section
    /// 2); ATKD is not decoded per race yet, so every swing uses it (RECALLED-VERIFY, tunable).
    inline constexpr float kStrikeHalfAngle = 0.6108652f; // 35 degrees in radians
    /// The band the sweep covers: from a step below the feet to a head above them (game units).
    inline constexpr float kSweepHeightBelow = 16.f;
    inline constexpr float kSweepHeightAbove = 130.f;
    /// The radius of an actor's physics capsule (MWPhysics::NeutralActor::kRadius, mirrored: no engine include here).
    inline constexpr float kBodyRadius = 22.f;
    /// The player has no clip (no first-person arms), so its swing runs on a hidden clock whose HitFrame time and
    /// length come from the sidecar of the attack clip at run time (CombatRequestWriter::clipTiming, task 0171 stage
    /// S5-3b). There is no fallback number: until the timing is ready the player cannot swing, and a timing that has
    /// not arrived after this many seconds of trying is reported once.
    inline constexpr float kPlayerTimingTimeoutSeconds = 5.f;
    /// Seconds a fighter counts as "in combat" (the regeneration multipliers) after a hit given or taken.
    inline constexpr float kCombatLingerSeconds = 10.f;
    /// Seconds between two attacks of a hostile test actor.
    inline constexpr float kHostileAttackPeriod = 2.5f;
    inline constexpr std::size_t kMaxCombatFighters = 4096;
    /// A sweep asked for and not answered after this many updates is forgotten (the engine answers in the next one; a
    /// result it dropped, for an attacker that had gone for instance, must not hold a table slot for ever).
    inline constexpr std::size_t kSweepAnswerUpdates = 2;

    /// Seconds before the first test swing and between two (the 1HM_AttackRight clip lasts 1.3 s).
    inline constexpr float kTestSwingFirstDelay = 3.f;
    inline constexpr float kTestSwingPeriod = 4.f;

    /// What a fight needs to know about one combatant (an NPC or the player stand-in), from the records through
    /// CombatData; the pack's rules only combine them.
    struct FighterProfile
    {
        float mHealthMax = 0.f;
        float mStaminaMax = 0.f;
        float mHealthRegenPercent = 0.f; ///< percent of the maximum a second (RACE DATA +84)
        float mStaminaRegenPercent = 0.f; ///< (RACE DATA +92)
        float mMitigationPercent = 0.f; ///< of the worn armour, ArmourResult::mMitigationPercent
        float mUnarmedDamage = 0.f;
        float mUnarmedReach = 0.f;
        std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> mSkills{};
    };

    /// Where SkyrimCombat gets its numbers from. SkyrimWorldView implements it; a test supplies a fake. Main thread.
    class CombatData
    {
    public:
        virtual ~CombatData() = default;
        /// The numbers of the NPC with base record `base`. False for an unknown base (that actor never fights).
        virtual bool fighter(openeden::sdk::game::WorldId base, FighterProfile& out) = 0;
        /// The stand-in for the player (level 1, skills 15, no armour: the player has no records).
        virtual FighterProfile playerFighter() = 0;
        /// The WEAP with this editor id (case-insensitive), or null. The pointer stays valid as long as the data.
        virtual const WeaponRecord* weaponByEditorId(std::string_view editorId) = 0;
        virtual CombatSettings settings() = 0;
        /// True when every answer above can be given at once, without reading anything. SkyrimCombat asks this at the
        /// start of every update and does nothing for that update while it is false (no swing, no hit): the data's
        /// first use may need records that a worker is still reading (review fix of task 0171: nothing here may block
        /// the main thread). The first call may start that read. Default: always ready (a test's fake).
        virtual bool combatReady() { return true; }
    };

    /// The table slot (attack, block, ...) x weapon animation type (WEAP DNAM +0, 0 to 9) -> clip file name.
    /// Default-constructed it is empty (the S5-0 skeleton); `humanoid()` is the shipped table. The names it returns
    /// point into static storage.
    class SlotTable
    {
    public:
        struct Entry
        {
            openeden::sdk::game::ClipSlot mSlot;
            std::uint8_t mWeaponAnimType;
            std::string_view mClip; ///< file name under meshes/actors/character/animations, no extension
        };

        SlotTable() = default;

        /// The humanoid table: one-handed sword (animation type 1) from the clips of Skyrim - Animations.bsa. The names
        /// were read from the archive listing (1hm_* files in the humanoid folder, which is not per sex) and the events
        /// from `animationdata/defaultmale.txt` (task 0170). RACE ATKE is not read (it names the event that starts an
        /// attack, which the clips' own sidecars already carry).
        [[nodiscard]] static SlotTable humanoid();

        /// Number of (slot, weapon type) entries.
        [[nodiscard]] std::size_t size() const { return mCount; }

        /// The clip for a slot and weapon animation type, or an empty view when there is none.
        [[nodiscard]] std::string_view clipFor(openeden::sdk::game::ClipSlot slot, std::uint8_t weaponAnimType) const
        {
            for (std::size_t i = 0; i < mCount; ++i)
                if (mEntries[i].mSlot == slot && mEntries[i].mWeaponAnimType == weaponAnimType)
                    return mEntries[i].mClip;
            return {};
        }

    private:
        const Entry* mEntries = nullptr;
        std::size_t mCount = 0;
    };

    /// CombatRules: the clip slots, the melee hit and damage, and the test rules.
    class SkyrimCombat final : public openeden::sdk::game::CombatRules
    {
    public:
        SkyrimCombat();

        void onClipEvent(const openeden::sdk::game::ClipEvent& event) override;
        void onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
            const openeden::sdk::game::SweepHit* hits, std::size_t count) override;
        void combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer) override;
        void onClipFinished(
            openeden::sdk::game::CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how) override;
        void onPlayerAttack(bool power) override;

        /// Names the editor id of the test swingers (case-insensitive); empty switches the rule off. A value longer
        /// than kMaxCombatTestSwingName is refused (false, the rule stays off).
        bool setTestSwing(std::string_view editorId);
        [[nodiscard]] const std::string& testSwing() const { return mTestSwing; }

        /// Task 0171: the numbers (null: no combat rule acts; the counters still count). Not owned.
        void setData(CombatData* data);
        /// Names the actors that attack the player when it is in reach; same rules as setTestSwing.
        bool setTestHostile(std::string_view editorId);
        /// Seconds between the player's automatic swings; 0, negative or not finite is off.
        void setTestPlayerSwing(float seconds);
        /// The WEAP both sides wield (case-insensitive editor id); empty is unarmed. False when too long.
        bool setTestWeapon(std::string_view editorId);
        /// Called with one line per combat decision (`COMBAT ...`), when set. Main thread.
        void setTrace(std::function<void(const std::string&)> trace) { mTrace = std::move(trace); }
        /// Where a warning goes (the host's log); a warning is also traced.
        void setWarning(std::function<void(const std::string&)> warning) { mWarning = std::move(warning); }
        /// How many times the player's swing timing did not arrive within kPlayerTimingTimeoutSeconds (reported once).
        [[nodiscard]] std::size_t playerTimingTimeouts() const { return mPlayerTimingTimeouts; }

        /// What the pack believes about a fighter now; false for an unknown or not yet used one.
        struct FighterView
        {
            float mHealth = 0.f;
            float mHealthMax = 0.f;
            float mStamina = 0.f;
            float mStaminaMax = 0.f;
            bool mDead = false;
        };
        [[nodiscard]] bool fighterView(openeden::sdk::game::CombatSubject subject, FighterView& out) const;

        /// The actor hooks forwarded by the cell source (the combat rules need the handles of the actors).
        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);

        [[nodiscard]] const SlotTable& slots() const { return mSlots; }
        [[nodiscard]] std::size_t updates() const { return mUpdates; }
        [[nodiscard]] std::size_t refusedEvents() const { return mRefusedEvents; }
        [[nodiscard]] std::size_t refusedSweepResults() const { return mRefusedSweepResults; }
        [[nodiscard]] std::size_t swingers() const { return mSwingers.size(); }
        [[nodiscard]] std::size_t hostiles() const { return mHostiles.size(); }
        [[nodiscard]] std::size_t swingsRequested() const { return mSwingsRequested; }
        [[nodiscard]] std::size_t eventsSeen() const { return mEventsSeen; }
        [[nodiscard]] std::size_t clipsFinished() const { return mClipsFinished; }
        [[nodiscard]] std::size_t clipsCompleted() const { return mClipsCompleted; }
        [[nodiscard]] std::size_t hitsResolved() const { return mHitsResolved; }
        /// Sweeps asked for whose result has not come (review fix of task 0171).
        [[nodiscard]] std::size_t pendingSweeps() const { return mAwaiting.size(); }
        /// Updates that did nothing because the data was not ready yet.
        [[nodiscard]] std::size_t updatesWaitingForData() const { return mWaitingForData; }

    private:
        struct Fighter
        {
            openeden::sdk::game::CombatSubject mSubject;
            openeden::sdk::game::WorldId mBase = openeden::sdk::game::kNoWorldId;
            bool mReady = false; ///< the profile was asked for
            bool mHasProfile = false;
            FighterProfile mProfile;
            float mHealth = 0.f;
            float mStamina = 0.f;
            bool mDead = false;
            bool mSwinging = false;
            std::uint32_t mSwingToken = 0;
            float mCombatUntil = -1.f;
            float mNextAttack = 0.f;
        };
        struct PendingHit
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken = 0;
            bool mPower = false;
            std::size_t mAskedAt = 0; ///< mUpdates when the sweep was asked for (mAwaiting only)
        };
        struct SweepDelivery
        {
            PendingHit mWho;
            std::size_t mCount = 0;
            openeden::sdk::game::SweepHit mHits[openeden::sdk::game::kMaxSweepHits];
        };

        Fighter* find(openeden::sdk::game::CombatSubject subject);
        [[nodiscard]] const Fighter* find(openeden::sdk::game::CombatSubject subject) const;
        bool ready(Fighter& fighter);
        void regenerate(float seconds);
        void beginAttack(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter, bool power);
        void startHostiles(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerSwing(openeden::sdk::game::CombatRequestWriter& writer);
        void processHit(openeden::sdk::game::CombatRequestWriter& writer, const PendingHit& hit);
        void resolveSweep(openeden::sdk::game::CombatRequestWriter& writer, const SweepDelivery& delivery);
        void applyHit(openeden::sdk::game::CombatRequestWriter& writer, Fighter& attacker, bool power,
            const openeden::sdk::game::SweepHit& hit, Fighter& victim);
        void trace(const std::string& line) const
        {
            if (mTrace)
                mTrace(line);
        }

        SlotTable mSlots;
        CombatData* mData = nullptr;
        CombatSettings mCombatSettings;
        bool mHaveSettings = false;
        const WeaponRecord* mWeapon = nullptr;
        bool mWeaponLooked = false;
        std::string mTestWeapon; ///< lower case
        std::string mTestHostile; ///< lower case
        std::string mTestSwing; ///< lower case
        std::vector<openeden::sdk::game::ActorHandle> mSwingers;
        std::vector<openeden::sdk::game::ActorHandle> mHostiles;
        float mPlayerAutoPeriod = 0.f;
        float mNextPlayerAuto = 1.f;
        bool mPlayerAttackPending = false;
        bool mPlayerPower = false;
        bool mPlayerSwingActive = false;
        bool mPlayerHitDone = false;
        float mPlayerSwingStart = 0.f;
        std::uint32_t mPlayerSwingToken = 0;
        // Reserved for the usual cell; a cell with more actors makes onActorAdded grow it (an allocation per added
        // actor beyond that, never one per frame). kMaxCombatFighters bounds it.
        std::vector<Fighter> mFighters; ///< index 0 is the player
        std::vector<PendingHit> mPendingHits; ///< Hit events seen since the last combatUpdate
        std::vector<PendingHit> mAwaiting; ///< sweeps asked for, result not here yet
        std::vector<SweepDelivery> mDeliveries; ///< sweep results waiting for the next combatUpdate
        std::function<void(const std::string&)> mTrace;
        std::function<void(const std::string&)> mWarning;
        // The timing of the player's two attack clips, once known: asking the engine again is then never needed.
        openeden::sdk::game::ClipTiming mKnownTiming[2];
        bool mHaveTiming[2] = { false, false };
        std::size_t mWaitingForData = 0;
        bool mWarnedAwaitingFull = false;
        float mPlayerHitTime = 0.f; ///< of the swing that runs: from the sidecar
        float mPlayerSwingLength = 0.f;
        bool mPlayerTimingKicked = false; ///< the read of the player's attack clip sidecars was started
        float mPlayerTimingSince = -1.f; ///< clock of the first refused ask, -1 when none is waiting
        std::size_t mPlayerTimingTimeouts = 0;
        float mClock = 0.f;
        float mNextSwing = kTestSwingFirstDelay;
        std::uint32_t mNextToken = 1;
        std::size_t mUpdates = 0;
        std::size_t mRefusedEvents = 0;
        std::size_t mRefusedSweepResults = 0;
        std::size_t mSwingsRequested = 0;
        std::size_t mEventsSeen = 0;
        std::size_t mClipsFinished = 0;
        std::size_t mClipsCompleted = 0;
        std::size_t mHitsResolved = 0;
    };
}

#endif
