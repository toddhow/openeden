#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_STATS_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_STATS_H

// Skyrim SE combat numbers, pure (task 0169, milestone M-S5 stage S5-1 "actor values and derived stats", subset for the
// first slice; research note 0056 sections 2, 5 and 8). Decoders for the records a melee hit needs (WEAP, the race's
// unarmed numbers and regeneration, GMST) and plain functions that turn them into an armour rating, a melee hit and
// regeneration. No engine, renderer, Jolt, Qt or OSG type, no clock, no random numbers: the same inputs give the same
// bytes. Nothing in the engine calls it yet (S5-3 does).
//
// STATUS OF EVERYTHING HERE: the record layouts are marked VERIFIED where the research note's census of the five
// official masters settled them (note 0056 section 5) and RECALLED-VERIFY otherwise. The FORMULAS are not in the data
// (note 0056 section 4): they are plausible and tunable (toddhow, 2026-11-12), every one is marked RECALLED-VERIFY, and
// the GMST names are the stored Skyrim ones. A GMST missing from the load order falls back to the named constant in
// CombatSettings (the value the vanilla Skyrim.esm stores, from note 0056 section 4).
//
// Hostile input: sizes are checked against the bytes that remain; a subrecord of another size than the layout allows is
// counted in DecodeNotes::mOddBy and decodes to nothing; a float that is not finite is refused; nothing throws.

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/actors.hpp"
#include "game_skyrimse/land.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    // ---- records -------------------------------------------------------------------------------------------------

    /// The engine's actor value order has the skills at 6 to 23 in the same order as ActorStats::skills (6 one-handed,
    /// 7 two-handed, 8 archery; RECALLED-VERIFY, the data agrees for 6, 7 and 8 by usage). WEAP DNAM +76 holds one.
    inline constexpr std::uint32_t kFirstSkillActorValue = 6;

    /// The weapon animation types of WEAP DNAM +0 (0 to 9; names RECALLED-VERIFY, values VERIFIED in the data).
    namespace WeaponAnimType
    {
        inline constexpr std::uint8_t HandToHand = 0, OneHandSword = 1, OneHandDagger = 2, OneHandAxe = 3,
                                      OneHandMace = 4, TwoHandSword = 5, TwoHandAxe = 6, Bow = 7, Staff = 8,
                                      Crossbow = 9;
    }

    /// A WEAP, the numbers a melee hit reads.
    struct WeaponRecord
    {
        RecordCommon mCommon;
        bool mHasData = false; ///< DATA, 10 bytes: value u32, weight f32, damage u16 (VERIFIED size 10)
        std::uint32_t mValue = 0;
        float mWeight = 0.f;
        std::uint16_t mDamage = 0;
        bool mHasDnam = false; ///< DNAM, 100 bytes (VERIFIED)
        std::uint8_t mAnimType = 0; ///< +0
        float mSpeed = 1.f; ///< +4 (0.5 to 10 in the data)
        float mReach = 1.f; ///< +8 (0.7, 1.0, 1.3 in the data)
        std::uint16_t mFlags = 0; ///< +12
        std::uint32_t mSkillValue = kFirstSkillActorValue; ///< +76, an actor value index (6, 7 or 8 in the data)
        float mStagger = 0.f; ///< +96 (0 to 1.5 in the data)
        bool mHasCrit = false; ///< CRDT, 24 bytes (VERIFIED size)
        std::uint16_t mCritDamage = 0; ///< +0
        float mCritMultiplier = 1.f; ///< +4 (1.0 for 3,160 of 3,262)

        [[nodiscard]] bool isMelee() const
        {
            return mAnimType != WeaponAnimType::Bow && mAnimType != WeaponAnimType::Crossbow
                && mAnimType != WeaponAnimType::Staff;
        }
        /// The index into ActorStats::skills the weapon is used with; 255 when the stored actor value is not a skill.
        [[nodiscard]] std::uint8_t skillSlot() const
        {
            if (mSkillValue < kFirstSkillActorValue)
                return 255;
            const std::uint32_t slot = mSkillValue - kFirstSkillActorValue;
            return slot < openeden::sdk::game::kActorSkillCount ? static_cast<std::uint8_t>(slot)
                                                                : static_cast<std::uint8_t>(255);
        }
    };

    /// The combat numbers of a RACE DATA (164 bytes, offsets from note 0056 section 2, RECALLED-VERIFY names): the
    /// regeneration rates and the unarmed damage and reach.
    struct RaceCombatRecord
    {
        RecordCommon mCommon;
        bool mHasData = false;
        float mHealthRegen = 0.f; ///< +84, percent of the maximum a second (Nord 0.7)
        float mMagickaRegen = 0.f; ///< +88 (Nord 3)
        float mStaminaRegen = 0.f; ///< +92 (Nord 5)
        float mUnarmedDamage = 0.f; ///< +96 (Nord 4, dragon 25)
        float mUnarmedReach = 0.f; ///< +100 (Nord 96)
    };

    /// A numeric GMST: the first letter of the editor id says the type (f float, i signed integer, b boolean as 0 or
    /// 1); the others (s strings) are not numeric and are not kept.
    struct GameSetting
    {
        RecordCommon mCommon;
        bool mNumeric = false;
        float mValue = 0.f;
    };

    [[nodiscard]] bool decodeWeapon(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, WeaponRecord& out, std::string& why);
    [[nodiscard]] bool decodeRaceCombat(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, RaceCombatRecord& out, std::string& why);
    [[nodiscard]] bool decodeGameSetting(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, GameSetting& out, std::string& why);

    /// What one plugin's walk found.
    struct CombatRecords
    {
        std::string mName;
        std::vector<WeaponRecord> mWeapons;
        std::vector<RaceCombatRecord> mRaces;
        std::vector<GameSetting> mSettings;
        DecodeNotes mNotes;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    /// Walks one plugin and decodes WEAP, RACE (the combat numbers only) and GMST. False only when the walk itself
    /// failed (mWalkError); records before are kept.
    [[nodiscard]] bool parseCombatRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ExteriorLimits& limits, CombatRecords& out);

    // ---- settings ------------------------------------------------------------------------------------------------

    /// The GMSTs the formulas read. The defaults are the values Skyrim.esm stores (note 0056 section 4), used when the
    /// load order has no such GMST. Every name is RECALLED-VERIFY as to how the engine USES it; the values are VERIFIED
    /// by the data census.
    struct CombatSettings
    {
        float mMaxArmorRating = 80.f; ///< fMaxArmorRating: the cap of the percent of damage armour takes off
        float mArmorScalingFactor = 0.12f; ///< fArmorScalingFactor: percent a point of armour rating takes off
        float mArmorBaseFactor = 0.03f; ///< fArmorBaseFactor: times 100 = percent each worn main piece takes off
        float mClothingArmorScale = 1.f; ///< fClothingArmorScale
        float mDamagePCSkillMax = 1.5f; ///< fDamagePCSkillMax: the most a skill raises weapon damage
        float mPowerAttackDefaultBonus = 1.f; ///< fPowerAttackDefaultBonus
        float mPowerAttackStaminaPenalty = 2.f; ///< fPowerAttackStaminaPenalty
        float mStaminaAttackWeaponBase = 20.f; ///< fStaminaAttackWeaponBase
        float mStaminaAttackWeaponMult = 1.f; ///< fStaminaAttackWeaponMult
        float mActionPointsAttackUnarmed = 22.f; ///< fActionPointsAttackUnarmed
        float mCombatDistance = 141.f; ///< fCombatDistance: units a weapon reach of 1.0 spans
        float mCombatHealthRegenRateMult = 0.f; ///< fCombatHealthRegenRateMult
        float mCombatMagickaRegenRateMult = 0.33f; ///< fCombatMagickaRegenRateMult
        float mCombatStaminaRegenRateMult = 0.35f; ///< fCombatStaminaRegenRateMult
    };

    /// Task 0172 (stage S5-4): the numbers of blocking, bashing and staggering. The GMST values are VERIFIED by the data
    /// census (note 0056 section 2); how they combine (computeBlock, causesStagger) is RECALLED-VERIFY and tunable. The
    /// last two are not GMSTs: they are ours.
    struct BlockSettings
    {
        float mBlockMax = 0.7f; ///< fBlockMax: the most damage a block takes off (a fraction)
        float mBlockWeaponBase = 0.3f; ///< fBlockWeaponBase
        float mBlockWeaponScaling = 0.2f; ///< fBlockWeaponScaling
        float mBlockSkillMult = 2.0f; ///< fBlockSkillMult
        float mBlockPowerAttackMult = 0.66f; ///< fBlockPowerAttackMult: a power attack is blocked this much as well
        float mStaminaBlockDmgMult = 0.25f; ///< fStaminaBlockDmgMult: stamina paid per point of damage blocked
        float mStaminaBlockBase = 0.f; ///< fStaminaBlockBase
        float mStaminaBashBase = 35.f; ///< fStaminaBashBase: what a bash costs
        float mStaggerThreshold = 1.f; ///< OURS: the stagger magnitude at which a blow staggers its target
        float mPowerAttackStaggerBonus = 1.f; ///< OURS: a power attack adds this to the weapon's stagger value
    };

    /// The GMSTs of a load order by editor id (later plugins win).
    class SettingsTable
    {
    public:
        void add(const CombatRecords& records);
        void set(const std::string& name, float value);
        [[nodiscard]] bool has(const std::string& name) const;
        [[nodiscard]] float get(const std::string& name, float fallback) const;
        [[nodiscard]] std::size_t size() const { return mValues.size(); }

        /// The settings the formulas read: a GMST the table has replaces the default; `missing` (if given) gets the
        /// names of those that stayed the default, in a fixed order.
        [[nodiscard]] CombatSettings combatSettings(std::vector<std::string>* missing = nullptr) const;
        /// Task 0172: the block, bash and stagger numbers (a GMST the table has replaces the default).
        [[nodiscard]] BlockSettings blockSettings() const;

    private:
        std::map<std::string, float> mValues;
    };

    /// WEAP and RACE combat numbers of a load order by form id (later plugins override, whole records).
    class CombatIndex
    {
    public:
        void add(const CombatRecords& records);
        [[nodiscard]] const WeaponRecord* weapon(FormId id) const;
        [[nodiscard]] const RaceCombatRecord* race(FormId id) const;
        [[nodiscard]] std::size_t weaponCount() const { return mWeapons.size(); }
        [[nodiscard]] std::size_t raceCount() const { return mRaces.size(); }
        [[nodiscard]] std::vector<FormId> weaponIds() const; ///< sorted
        [[nodiscard]] const SettingsTable& settings() const { return mSettings; }

    private:
        std::unordered_map<FormId, WeaponRecord> mWeapons;
        std::unordered_map<FormId, RaceCombatRecord> mRaces;
        SettingsTable mSettings;
    };

    // ---- armour --------------------------------------------------------------------------------------------------

    /// The biped slot bits (BOD2) that count as the four main armour pieces and the shield.
    namespace ArmourSlot
    {
        inline constexpr std::uint32_t Head = 0x1, Body = 0x4, Hands = 0x8, Feet = 0x80, Shield = 0x200;
        inline constexpr std::uint32_t MainPieces = Head | Body | Hands | Feet;
    }

    /// One worn armour: its base rating (the stored DNAM / 100), slot mask and type (0 light, 1 heavy, 2 clothing).
    struct ArmourPiece
    {
        float mRating = 0.f;
        std::uint32_t mSlotMask = 0;
        std::uint32_t mType = 0;
    };

    /// The armour of an actor: the summed rating (skill-scaled), how many main pieces count and the percent of damage
    /// it takes off (0 to mMaxArmorRating).
    struct ArmourResult
    {
        float mRating = 0.f;
        std::uint8_t mMainPieces = 0;
        float mMitigationPercent = 0.f;
    };

    /// RECALLED-VERIFY formula (UESP "Armor Rating"; the 80 percent cap and 567 as the rating that reaches it agree:
    /// 567 * 0.12 + 3 * 4 = 80.04):
    ///   piece = base * (1 + skill / 200) for light and heavy (skill = Light Armor or Heavy Armor), base * clothing
    ///   scale for clothing; rating = sum of pieces; percent = min(mMaxArmorRating, rating * mArmorScalingFactor + 100
    ///   * mArmorBaseFactor * main pieces worn).
    /// A negative or non-finite rating counts as 0; the skill is clamped to 0 to 100. A main piece counts when its
    /// rating is above 0 (one piece for each of head, body, hands and feet at most).
    [[nodiscard]] ArmourResult armourOf(const std::vector<ArmourPiece>& pieces,
        const std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount>& skills, const CombatSettings& settings);

    /// The pieces an NPC wears by its default outfit (the ARMO of the effective outfit, leveled items resolved, one
    /// entry per armour, not per addon). Items in the NPC's inventory it would equip are not read (a limit).
    [[nodiscard]] bool wornArmour(const ActorIndex& index, FormId npc, std::uint32_t variation,
        std::uint16_t playerLevel, std::vector<ArmourPiece>& out, std::string& error);

    /// wornArmour + armourOf with the NPC's own skills (computeActorStats).
    [[nodiscard]] bool computeActorArmour(const ActorIndex& index, const CombatSettings& settings, FormId npc,
        std::uint32_t variation, std::uint16_t playerLevel, ArmourResult& out, std::string& error);

    // ---- a melee hit ---------------------------------------------------------------------------------------------

    /// The numbers of one swing, gathered by the caller.
    struct MeleeInput
    {
        const WeaponRecord* mWeapon = nullptr; ///< null: unarmed
        float mUnarmedDamage = 0.f; ///< the attacker race's unarmed damage (used when no weapon)
        float mAttackerSkill = 0.f; ///< the attacker's skill the weapon uses (0 to 100; unarmed: ignored)
        bool mPowerAttack = false;
        float mTargetMitigationPercent = 0.f; ///< ArmourResult::mMitigationPercent of the target
    };

    struct MeleeHit
    {
        float mDamageBefore = 0.f; ///< weapon damage with skill and power attack, before armour
        float mDamageAfter = 0.f; ///< after the target's armour
        float mStaminaCost = 0.f; ///< what the attacker's swing costs
        float mStagger = 0.f; ///< the weapon's stagger value (0 unarmed)
        float mCriticalDamage = 0.f; ///< the extra a critical adds (CRDT damage * multiplier), 0 if none
    };

    /// RECALLED-VERIFY formulas, all plausible and tunable:
    ///   skill multiplier = min(mDamagePCSkillMax, 1 + skill / 200) (skill clamped to 0 to 100; 100 gives 1.5);
    ///   before = weapon damage * skill multiplier (unarmed: the race's unarmed damage, no skill), times
    ///            (1 + mPowerAttackDefaultBonus) for a power attack;
    ///   after = before * (1 - mitigation / 100), mitigation clamped to 0 to mMaxArmorRating;
    ///   stamina = mStaminaAttackWeaponBase + weight * mStaminaAttackWeaponMult (unarmed mActionPointsAttackUnarmed),
    ///             times mPowerAttackStaminaPenalty for a power attack.
    /// Negative and non-finite inputs count as 0. Pure: no random numbers (a critical's chance is the caller's).
    [[nodiscard]] MeleeHit computeMeleeHit(const MeleeInput& input, const CombatSettings& settings);

    /// One blow meeting a block (task 0172). `mDamage` is the damage after armour.
    struct BlockInput
    {
        float mDamage = 0.f;
        float mBlockerSkill = 0.f; ///< the blocker's Block skill, 0 to 100
        bool mPowerAttack = false;
        float mBlockerStamina = 0.f;
    };

    struct BlockResult
    {
        bool mBlocked = false; ///< the block held
        bool mBroken = false; ///< the blocker lacked the stamina: full damage, stamina gone, and it staggers
        float mFraction = 0.f; ///< the fraction of the damage the block takes off (0 when broken)
        float mDamageTaken = 0.f; ///< what reaches the blocker's health
        float mStaminaDrain = 0.f; ///< what the blocker pays
    };

    /// RECALLED-VERIFY, tunable: fraction = min(fBlockMax, fBlockWeaponBase + fBlockWeaponScaling x skill x
    /// fBlockSkillMult / 100) with the skill clamped to 0 to 100 (skill 100 gives fBlockMax exactly), times
    /// fBlockPowerAttackMult for a power attack; blocked = damage x fraction; the blocker pays blocked x
    /// fStaminaBlockDmgMult + fStaminaBlockBase. With less stamina than that the block is broken: the whole damage
    /// is taken and the blocker's stamina goes to 0. Negative and non-finite inputs count as 0.
    [[nodiscard]] BlockResult computeBlock(const BlockInput& input, const BlockSettings& settings);

    /// The stagger a blow carries: the weapon's stagger value, plus mPowerAttackStaggerBonus for a power attack (so a
    /// power attack staggers by default, as in Skyrim, and a normal blow only with a weapon of stagger 1 or more).
    [[nodiscard]] float staggerMagnitude(float weaponStagger, bool powerAttack, const BlockSettings& settings);

    /// True when that magnitude reaches mStaggerThreshold (RECALLED-VERIFY: Skyrim also looks at the target's mass).
    [[nodiscard]] bool causesStagger(float weaponStagger, bool powerAttack, const BlockSettings& settings);

    /// What a bash costs the basher: fStaminaBashBase.
    [[nodiscard]] float bashStaminaCost(const BlockSettings& settings);

    /// How far a swing reaches, in units: the weapon's reach times mCombatDistance (RECALLED-VERIFY); unarmed uses
    /// `unarmedReach` as it is (the race's stored 96, already units).
    [[nodiscard]] float meleeReachUnits(const WeaponRecord* weapon, float unarmedReach, const CombatSettings& settings);

    /// Regeneration of one value for `seconds`: max * rate / 100 * seconds, times the in-combat multiplier while in
    /// combat (RECALLED-VERIFY: the race's rate is percent of the maximum a second; Nord health 0.7). 0 for a negative
    /// or non-finite input.
    [[nodiscard]] float regenAmount(
        float maximum, float ratePercentPerSecond, float combatMultiplier, bool inCombat, float seconds);
}

#endif
