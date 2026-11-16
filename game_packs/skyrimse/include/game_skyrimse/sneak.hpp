#ifndef OPENEDEN_GAME_SKYRIMSE_SNEAK_H
#define OPENEDEN_GAME_SKYRIMSE_SNEAK_H

// Sneaking and detection for the player (task 0227, docs/tasks/0227-skyrim-sneak-detection.md): which NPCs have noticed
// a sneaking player (hidden, caution, detected), the sneak eye the HUD shows, the damage multiplier of an unnoticed
// blow, and the Sneak skill experience. Off unless the pack setting `sneak`.
//
// Where the numbers come from: the GMST values below are Skyrim.esm's (VERIFIED by reading the five masters: only
// Skyrim.esm holds them; there is NO bow multiplier and NO two-handed hammer multiplier in the data). The FORMULA that
// combines them is RECALLED-VERIFY: Skyrim's own detection is a stealth-point meter whose exact arithmetic is not in the
// data, so this is a model of ours built from those names. Every number of ours is a named constant in `Settings`.
//
// Standard library, the SDK combat types and the crime actor roster only (no engine type). Main thread; everything that
// needs the writer (positions, rays) runs inside combatUpdate, as the crime rules do.

#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/perks.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::sneak
{
    /// What one observer has made of the player.
    enum class Alert : std::uint8_t
    {
        Hidden, ///< has not noticed: the eye is closed
        Caution, ///< suspects something: the eye is half open
        Detected, ///< has noticed the player: the eye is open
    };
    [[nodiscard]] std::string_view alertName(Alert alert);

    struct Settings
    {
        // ---- Skyrim.esm's GMSTs (VERIFIED values) ----
        float mMaxDistance = 2500.f; ///< fSneakMaxDistance
        float mSkillMult = 0.5f; ///< fSneakSkillMult
        float mRunningMult = 2.f; ///< fSneakRunningMult
        float mActionMult = 2.f; ///< fSneakActionMult (used here for "standing up, not sneaking")
        float mSoundLosMult = 0.3f; ///< fSneakSoundLosMult (heard but not seen)
        float mSkillUsagePerSecond = 0.625f; ///< fSkillUsageSneakPerSecond
        float mAttackSkillUsage = 30.f; ///< fSneakAttackSkillUsageMelee
        float mMult1HSword = 3.f; ///< fCombatSneak1HSwordMult
        float mMult1HDagger = 3.f; ///< fCombatSneak1HDaggerMult
        float mMult1HAxe = 3.f; ///< fCombatSneak1HAxeMult
        float mMult1HMace = 3.f; ///< fCombatSneak1HMaceMult
        float mMult2HSword = 2.f; ///< fCombatSneak2HSwordMult
        float mMult2HAxe = 2.f; ///< fCombatSneak2HAxeMult (a two-handed hammer has no GMST of its own: it reads this)
        float mMultHand = 2.f; ///< fCombatSneakHandMult (bare hands)
        // ---- OURS (named, tunable; RECALLED-VERIFY against Skyrim's own behaviour) ----
        /// The bow has no GMST in the data: a sneak shot counts this much.
        float mMultBow = 2.f;
        /// The range the GMST's 2500 units shrinks to before the factors below. Chosen so that a walking, sneaking
        /// player at the starting skill is noticed within about 580 units (8 m), not 2300.
        float mRangeScale = 0.25f;
        /// Standing still is quieter than walking.
        float mStillFactor = 0.5f;
        float mStillSpeed = 10.f; ///< units a second: below this the player is "still"
        float mRunSpeed = 200.f; ///< units a second: at or above this the player is running
        /// Each unit of worn armour weight adds this much to the range factor (heavy armour clanks).
        float mArmourNoisePerWeight = 0.01f;
        /// An observer sees the player within this half angle of where it faces (radians; 70 degrees each side).
        float mHalfView = 1.2217305f;
        /// Notice gained a second at point-blank range; it falls linearly to 0 at the edge of the range.
        float mNoticeRate = 1.f;
        float mDecayRate = 0.25f; ///< notice lost a second when nothing is perceived
        float mNoticeMax = 1.5f; ///< notice never rises above this (so a detected observer calms down in a few seconds)
        /// Without a sight line an observer only suspects: its notice stops here.
        float mHeardCap = 0.9f;
        float mCautionEnter = 0.3f; ///< notice at or above this: caution
        float mCautionLeave = 0.2f;
        float mDetectEnter = 1.0f; ///< notice at or above this: detected
        float mDetectLeave = 0.75f;
        float mPeriod = 0.5f; ///< seconds between judgements
        float mConsiderDistance = 3000.f; ///< observers farther than this are not looked at
        std::size_t mMaxRays = 6; ///< sight rays a judgement may cast (nearest observers first)
        float mMessageSeconds = 3.f; ///< how long the sneak attack message is meant to show
    };

    // ---- the pure rules ----------------------------------------------------------------------------------------------

    /// What is known of the player's stealth right now.
    struct PlayerStealth
    {
        bool mSneaking = false;
        float mSkill = 15.f; ///< Sneak, 0 to 100
        float mSpeed = 0.f; ///< units a second
        float mArmourWeight = 0.f;
    };

    /// The distance within which an observer facing the player with a clear line can notice him.
    [[nodiscard]] float stealthRange(const Settings& settings, const PlayerStealth& player);

    /// The new notice value of an observer after `seconds`: it grows when the player is within the range it can
    /// perceive him at (sight: the full range; no sight: the range times mSoundLosMult, and it stops at mHeardCap) and
    /// decays otherwise. Always in [0, mNoticeMax].
    [[nodiscard]] float updateNotice(const Settings& settings, float notice, float seconds, float range, float distance,
        bool sees);

    /// The alert state a notice value gives, with hysteresis from the state before.
    [[nodiscard]] Alert classify(const Settings& settings, float notice, Alert before);

    /// The damage multiplier of an unnoticed blow with a weapon of this animation type (WeaponAnimType; ranged is a
    /// bow, a crossbow counts as one).
    [[nodiscard]] float baseAttackMultiplier(const Settings& settings, std::uint8_t animType);

    /// "Sneak attack for 3x damage!" (the multiplier without trailing zeros).
    [[nodiscard]] std::string attackMessage(float multiplier);

    /// The Sneak experience one judgement of staying hidden earns: `seconds` of mSkillUsagePerSecond, only while the
    /// player sneaks, moves, and somebody near has not noticed him (`hiddenWatchers`).
    [[nodiscard]] float hiddenTraining(
        const Settings& settings, bool sneaking, float speed, std::size_t hiddenWatchers, float seconds);

    /// The perk side of a sneak attack (entry point ModSneakAttackMult, 18): the multiplier after the owner's perks, with
    /// the owner counted as sneaking. `weapon` may be null (bare hands). Unchanged when no perk has the entry point or no
    /// effect's conditions hold. `applied` (optional) counts the effects that applied.
    [[nodiscard]] float applySneakAttackPerks(PerkEngine& engine, std::span<const FormId> perks,
        const PerkParticipant& owner, const PerkParticipant* weapon, const PerkParticipant& target, float multiplier,
        std::size_t* applied = nullptr);

    // ---- the system ----------------------------------------------------------------------------------------------------

    /// What the system needs of the game; a missing hook gives the stand-in value.
    struct Hooks
    {
        /// The loaded actors (appended): handle, dead, engaged against the player, name.
        std::function<void(std::vector<crime::CrimeActor>&)> mActors;
        /// An actor follows the player (never notices him).
        std::function<bool(std::uint32_t reference)> mIsFollower;
        /// The player's Sneak skill (0 to 100); a missing hook is a new character's 15.
        std::function<float()> mSkill;
        /// The summed weight of the armour the player wears.
        std::function<float()> mArmourWeight;
        /// Sneak experience.
        std::function<void(float amount)> mTrain;
        std::function<void(const std::string&)> mLog;
    };

    class SneakSystem
    {
    public:
        explicit SneakSystem(Hooks hooks, Settings settings = Settings{});
        ~SneakSystem();
        SneakSystem(const SneakSystem&) = delete;
        SneakSystem& operator=(const SneakSystem&) = delete;

        /// Once a frame, inside combatUpdate (positions and rays only work there). Judges every mPeriod seconds.
        void update(float seconds, openeden::sdk::game::CombatRequestWriter& writer);

        [[nodiscard]] bool sneaking() const { return mSneaking; }
        /// The worst alert among the observers (Hidden when there are none).
        [[nodiscard]] Alert overall() const { return mOverall; }
        /// The eye: -1 not sneaking (no eye), 0 closed (hidden), 1 half (caution), 2 open (detected).
        [[nodiscard]] int eye() const;
        /// The alert of one observer (Hidden when it is not known).
        [[nodiscard]] Alert alertOf(std::uint32_t reference) const;
        /// True when the observer has noticed the player: crime witnesses and pickpocketing ask this.
        [[nodiscard]] bool detected(std::uint32_t reference) const { return alertOf(reference) == Alert::Detected; }
        /// A pickpocket try or a blow made this observer notice the player at once.
        void markDetected(std::uint32_t reference);

        /// The multiplier of the player's blow on `victim` now: 1 when it is not a sneak attack (the player does not
        /// sneak, or the victim has noticed him). `animType` is the weapon's WEAP animation type (0 bare hands).
        [[nodiscard]] float attackMultiplier(std::uint32_t victim, std::uint8_t animType, bool ranged) const;
        /// A sneak attack landed with this final multiplier (perks included): the message, the experience, and the
        /// victim now knows.
        void onSneakAttack(std::uint32_t victim, float multiplier);

        [[nodiscard]] const std::string& message() const { return mMessage; }
        /// Changes with every sneak attack message (the HUD shows a new message when it moves).
        [[nodiscard]] std::uint32_t messageId() const { return mMessageId; }
        [[nodiscard]] const Settings& settings() const { return mSettings; }
        [[nodiscard]] std::size_t judgements() const { return mJudgements; }
        [[nodiscard]] std::size_t raysCast() const { return mRays; }
        [[nodiscard]] std::size_t observerCount() const { return mObservers.size(); }
        /// The player's speed in units a second: the slower of the last two judgement intervals.
        [[nodiscard]] float speed() const { return mSpeed; }
        [[nodiscard]] float range() const { return mRange; }

    private:
        struct Observer
        {
            std::uint32_t mReference = 0;
            float mNotice = 0.f;
            Alert mAlert = Alert::Hidden;
        };
        struct Candidate
        {
            double mDistance = 0.0;
            std::size_t mIndex = 0;
            openeden::sdk::game::SubjectState mState;
        };
        void judge(float seconds, openeden::sdk::game::CombatRequestWriter& writer);
        Observer* find(std::uint32_t reference);
        const Observer* find(std::uint32_t reference) const;
        void note(const std::string& text);

        Hooks mHooks;
        Settings mSettings;
        std::vector<Observer> mObservers; ///< sorted by reference
        std::vector<crime::CrimeActor> mRoster; ///< reused
        std::vector<Candidate> mScratch; ///< reused
        std::vector<Observer> mNext; ///< reused
        float mSince = 0.f;
        bool mSneaking = false;
        Alert mOverall = Alert::Hidden;
        double mLastX = 0.0, mLastY = 0.0;
        bool mHaveLast = false;
        float mSpeed = 0.f;
        float mPrevSpeed = 0.f; ///< the speed of the interval before
        float mRange = 0.f;
        std::string mMessage;
        std::uint32_t mMessageId = 0;
        std::size_t mJudgements = 0;
        std::size_t mRays = 0;
    };

    // ---- the script package ----------------------------------------------------------------------------------------

    /// The package `core.skyrimse.sneak` 1.0 (all Read): `eye() -> int` (see SneakSystem::eye), `message() -> string`,
    /// `message_id() -> int` and `message_seconds() -> double`. The system must outlive the declaration.
    class SneakBindings
    {
    public:
        explicit SneakBindings(const SneakSystem& system);
        ~SneakBindings();
        SneakBindings(const SneakBindings&) = delete;
        SneakBindings& operator=(const SneakBindings&) = delete;
        [[nodiscard]] openeden::sdk::DeclareResult declareTo(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    inline constexpr std::string_view kNamespace = "skyrimse.sneak";
}

#endif
