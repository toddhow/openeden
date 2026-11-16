#ifndef OPENEDEN_GAME_SKYRIMSE_PERKS_H
#define OPENEDEN_GAME_SKYRIMSE_PERKS_H

// The Skyrim SE pack's perks, for combat (task 0179, milestone M-S5 stage S5-7; research note 0056 section 3; decision
// 0018). Pure: no engine, renderer, Jolt, Qt or OSG type, no clock, no random numbers. The same inputs give the same
// bytes.
//
// What the data settles (Skyrim.esm and the four DLC plugins; details and counts in the task's stage log):
//  * PERK: EDID, per perk DATA of 5 bytes, NNAM, the perk's own prerequisite CTDAs before the first PRKE, then per
//    effect PRKE (3 bytes: kind 0 quest / 1 ability / 2 entry point, rank, priority), the effect's DATA (entry point: 3
//    bytes entry point id, function id, tab count; ability: a SPEL form; quest: form and stage), per tab a PRKC (1
//    byte, the tab index) followed by that tab's CTDAs, EPFT (1 byte parameter type), EPFD (4 bytes: a float, or a form
//    for the spell functions; 8 bytes: two floats; text for function 11), and PRKF (empty, ends the effect).
//  * A tab's conditions run on a participant that depends on the entry point (entryPointTabRole): tab 0 is the perk
//    owner, the others a weapon, a target, an attacker, an armour piece or a spell.
//  * Each rank of a perk is its own PERK record; NPC_ PRKR names the record.
// RECALLED-VERIFY (names and rules the data cannot settle): the entry point names (CommonLibSSE-NG), the order effects
// of different priority apply in (higher first), the roles of tabs of the entry points with no conditions in the
// installed data, the archetypes that count as "adds a value" in an ability, and the actor value numbers of
// DamageResist (39) and WeaponSpeedMult (85) (the AVIF form ids of the five masters run consecutively in the engine's
// order, which fixes the numbers; the names are from the editor ids).
//
// Hostile input: every size is checked against the layout, a float that is not finite is refused, a record with more
// effects or conditions than the caps is refused with a reason, nothing throws.

#include "game_skyrimse/conditions.hpp"
#include "game_skyrimse/land.hpp"
#include "game_skyrimse/records.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    /// Entry point ids (BGSEntryPoint order; the NUMBERS are the data's, the names RECALLED-VERIFY).
    namespace PerkEntry
    {
        inline constexpr std::uint8_t CalculateWeaponDamage = 0, CalculateMyCriticalHitChance = 1,
                                      CalculateMyCriticalHitDamage = 2, ModBuyPrices = 8, Activate = 14,
                                      ModSneakAttackMult = 18, ModBowZoom = 20, ModRecoverArrowChance = 21,
                                      ModBashingDamage = 26, ModPowerAttackStamina = 27, ModPowerAttackDamage = 28,
                                      ModSpellMagnitude = 29, ModSpellDuration = 30, ModArmorWeight = 32,
                                      ModIncomingStagger = 33, ModTargetStagger = 34, ModAttackDamage = 35,
                                      ModIncomingDamage = 36, ModTargetDamageResistance = 37, ModSpellCost = 38,
                                      ModPercentBlocked = 39, ModShieldDeflectArrowChance = 40, SetSweepAttack = 50,
                                      ApplyCombatHitSpell = 51, ApplyBashingSpell = 52, ModPickpocketChance = 56,
                                      ModDetectionSneakSkill = 57, ModLockpickSweetSpot = 59, ModSellPrices = 60,
                                      ModAlchemyEffectiveness = 66, FilterActivation = 74, ModTemperingHealth = 76,
                                      ModEnchantmentPower = 77, ModArmorRating = 85, ModSpellRange_TargetLoc = 88;
        /// Ids above this do not exist (the data has 0 to 91).
        inline constexpr std::uint8_t kLast = 91;
    }

    /// Function ids of an entry point effect (xEdit's wbPRKEFunctionType; the data uses 1, 2, 3, 9, 10, 11, 14, 15).
    namespace PerkFunction
    {
        inline constexpr std::uint8_t Set = 1, Add = 2, Multiply = 3, AddRange = 4, AddActivateChoice = 9,
                                      SelectSpell = 10, SelectText = 11, MultiplyOnePlusActorValueMult = 14,
                                      SetText = 15;
    }

    /// PRKE type byte.
    namespace PerkEffectKind
    {
        inline constexpr std::uint8_t Quest = 0, Ability = 1, EntryPoint = 2;
    }

    /// The caps of a decoded perk and of one evaluation.
    inline constexpr std::size_t kMaxPerkEffects = 64;
    inline constexpr std::size_t kMaxPerkTabs = 8;
    inline constexpr std::size_t kMaxPerkConditionsPerTab = 256;
    inline constexpr std::size_t kMaxSpellEffects = 64;
    /// Effects of one entry point query that are ordered and applied (more are dropped and counted).
    inline constexpr std::size_t kMaxQueryEffects = 64;

    /// Actor value numbers (the engine's table order; RECALLED-VERIFY names, numbers from the AVIF form id runs).
    namespace ActorValueIndex
    {
        inline constexpr std::uint32_t OneHanded = 6, Marksman = 8, Block = 9, Health = 24, Magicka = 25, Stamina = 26,
                                       DamageResist = 39, WeaponSpeedMult = 85;
    }

    /// One tab of an entry point effect: its index and the conditions that run on that tab's participant.
    struct PerkTab
    {
        std::uint8_t mIndex = 0;
        std::vector<survey::Ctda> mConditions;
    };

    struct PerkEffect
    {
        std::uint8_t mKind = PerkEffectKind::EntryPoint;
        std::uint8_t mRank = 0;
        std::uint8_t mPriority = 0;
        // entry point effects (DATA of 3 bytes)
        std::uint8_t mEntryPoint = 0;
        std::uint8_t mFunction = 0;
        std::uint8_t mTabCount = 0;
        std::uint8_t mParamType = 0; ///< EPFT: 1 float, 2 two floats, 3 to 5 a form, 6 and 7 text
        bool mHasParam = false;
        float mValue = 0.f; ///< EPFD: the float, or the first of two
        float mValue2 = 0.f; ///< the second of two floats
        FormId mForm = kNullFormId; ///< EPFD as a form (select spell), or the ability's spell (kind 1)
        std::vector<PerkTab> mTabs;
    };

    struct PerkRecord
    {
        RecordCommon mCommon;
        bool mHasData = false; ///< DATA, 5 bytes
        std::uint8_t mLevel = 0; ///< the level the perk asks of its owner (prerequisite, not evaluated in combat)
        FormId mNext = kNullFormId; ///< NNAM
        std::vector<survey::Ctda> mPrerequisites; ///< CTDAs before the first PRKE: gate learning, never evaluated
        std::vector<PerkEffect> mEffects;
    };

    /// One effect of a spell: its magic effect and magnitude (EFID, EFIT).
    struct SpellEffect
    {
        FormId mEffect = kNullFormId;
        float mMagnitude = 0.f;
        std::uint32_t mArea = 0;
        std::uint32_t mDuration = 0;
        /// Task 0186: CTDA subrecords after this effect's EFIT (a perk, a race or a state gates the effect). The pack
        /// cannot evaluate them for a spell, so the magic runtime skips an effect that has any (a test setting applies
        /// them anyway).
        std::uint32_t mConditions = 0;
    };

    /// A SPEL, what an ability perk needs: the spell type (SPIT +8: 4 is an ability) and its effects.
    struct SpellRecord
    {
        RecordCommon mCommon;
        bool mHasData = false; ///< SPIT, 36 bytes
        std::uint32_t mType = 0;
        // Task 0186 (spells): SPIT +0 cost, +16 cast type, +20 delivery, +24 cast duration, +28 range (CommonLibSSE-NG
        // member order, RECALLED-VERIFY, checked against the data by SkyrimMagicRealData).
        std::uint32_t mCost = 0;
        std::uint32_t mCastType = 0; ///< 0 constant effect, 1 fire and forget, 2 concentration
        std::uint32_t mDelivery = 0; ///< 0 self, 1 touch, 2 aimed, 3 target actor, 4 target location
        float mCastDuration = 0.f;
        float mRange = 0.f;
        std::vector<SpellEffect> mEffects;
        static constexpr std::uint32_t kTypeAbility = 4;
        [[nodiscard]] bool isAbility() const { return mHasData && mType == kTypeAbility; }
    };

    /// An MGEF, what an ability needs: DATA (152 bytes) flags +0, archetype +64, primary actor value +68.
    struct MagicEffectRecord
    {
        RecordCommon mCommon;
        bool mHasData = false;
        std::uint32_t mFlags = 0;
        std::uint32_t mArchetype = 0;
        std::int32_t mPrimaryValue = -1;
        // Task 0186: +4 base cost, +16 resist actor value, +60 secondary weight, +72 projectile, +80 casting type, +84
        // delivery, +88 secondary actor value.
        float mBaseCost = 0.f;
        std::int32_t mResistValue = -1;
        float mSecondaryWeight = 0.f;
        FormId mProjectile = kNullFormId; ///< PROJ, resolved (0 when none)
        std::uint32_t mCastType = 0;
        std::uint32_t mDelivery = 0;
        std::int32_t mSecondaryValue = -1;
        bool mHasScript = false; ///< the record carries a VMAD (its effect is a script, Firebolt's damage among them)
        /// CTDA subrecords of the record: a perk or a state gates the effect (the Stagger and Demoralize effects of
        /// Firebolt are perk effects with conditions). The magic runtime skips such an effect unless told otherwise.
        std::uint32_t mConditions = 0;
        /// ValueModifier is 0 (VERIFIED: PerkQuickShot); PeakValueModifier is 34 (RECALLED-VERIFY, CommonLibSSE-NG).
        [[nodiscard]] bool modifiesAValue() const { return mHasData && (mArchetype == 0 || mArchetype == 34); }
    };

    [[nodiscard]] bool decodePerk(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, PerkRecord& out, std::string& why);
    [[nodiscard]] bool decodeSpell(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SpellRecord& out, std::string& why);
    [[nodiscard]] bool decodeMagicEffect(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, MagicEffectRecord& out, std::string& why);

    /// The records an evaluation reads.
    class PerkLibrary
    {
    public:
        virtual ~PerkLibrary() = default;
        [[nodiscard]] virtual const PerkRecord* perk(FormId id) const = 0;
        [[nodiscard]] virtual const SpellRecord* spell(FormId) const { return nullptr; }
        [[nodiscard]] virtual const MagicEffectRecord* magicEffect(FormId) const { return nullptr; }
    };

    // ---- participants -----------------------------------------------------------------------------------------------

    /// What a tab of an entry point names.
    enum class PerkRole : std::uint8_t
    {
        Owner,
        Attacker,
        Weapon,
        Target,
        Armour,
        Spell,
        kCount,
    };

    /// One thing a tab's conditions can ask about. Pointers are not owned and must outlive the query; null means
    /// "none".
    struct PerkParticipant
    {
        enum class Kind : std::uint8_t
        {
            None,
            Actor,
            Weapon,
            Armour,
            Spell,
        };
        Kind mKind = Kind::None;
        FormId mForm = kNullFormId; ///< actor: its base record; weapon, armour, spell: the record
        FormId mRace = kNullFormId;
        bool mFemale = false;
        std::uint16_t mLevel = 1;
        const std::vector<FormId>* mPerks = nullptr; ///< actors: PERK forms
        const std::vector<FormId>* mKeywords = nullptr; ///< actors: own and race; others: the record's KWDA
        const std::vector<FormId>* mFactions = nullptr; ///< actors
        const std::vector<FormId>* mWornKeywords = nullptr; ///< actors: keywords of the worn armour
        /// Actor values by index (base values: the 18 skills at 6 to 23, health, magicka, stamina at 24 to 26).
        const std::vector<std::pair<std::uint32_t, float>>* mValues = nullptr;
        std::uint8_t mRightHandType = 0; ///< actors: the WEAP animation type of the right hand (0 nothing)
        std::uint8_t mAnimType = 0; ///< weapons: the WEAP animation type
        bool mSneaking = false;
        bool mBlocking = false;
        bool mPowerAttacking = false;
        bool mSprinting = false;
        bool mUndead = false;
        FormId mAttackType = kNullFormId; ///< the keyword of the attack in progress, 0 when none is known
    };

    /// The participants of one query by role (null: that tab has nobody and its conditions are false).
    struct PerkQuery
    {
        std::array<const PerkParticipant*, static_cast<std::size_t>(PerkRole::kCount)> mRole{};
        void set(PerkRole role, const PerkParticipant* participant)
        {
            mRole[static_cast<std::size_t>(role)] = participant;
        }
    };

    /// The roles of the tabs of an entry point (RECALLED-VERIFY for those with no conditions in the installed data; the
    /// rest corroborated by which condition functions each tab holds, see the task log). `known` is false for an
    /// entry point not in the table: its tab 0 is the owner and the others have nobody.
    [[nodiscard]] PerkRole entryPointTabRole(std::uint8_t entryPoint, std::uint8_t tab, bool& known);

    /// A conditions world over a PerkQuery: subject n+1 is role n. It answers HasPerk, HasKeyword, GetBaseActorValue,
    /// GetActorValue, GetLevel, GetIsRace, GetIsSex, GetInFaction, GetIsID, GetEquippedItemType, IsSneaking and
    /// WornHasKeyword from the participants' data; everything else keeps the default answer (nothing).
    class PerkConditionWorld final : public conditions::ConditionWorld
    {
    public:
        void setQuery(const PerkQuery* query) { mQuery = query; }
        /// The role whose participant "run on subject" (0) means for the conditions being evaluated.
        void setCurrentRole(PerkRole role) { mCurrent = role; }
        [[nodiscard]] static conditions::Subject subjectOf(PerkRole role)
        {
            return static_cast<std::uint64_t>(role) + 1;
        }
        [[nodiscard]] const PerkParticipant* participant(conditions::Subject subject) const;

        conditions::Subject resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const override;
        bool hasBaseId(conditions::Subject subject, std::uint32_t formId) const override;
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(conditions::Subject subject, std::uint32_t faction) const override;
        float globalValue(std::uint32_t) const override { return 0.f; }
        bool hasPerk(conditions::Subject subject, std::uint32_t perk) const override;
        bool hasKeyword(conditions::Subject subject, std::uint32_t keyword) const override;
        float baseActorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const override;
        float actorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const override;
        float levelOf(conditions::Subject subject) const override;
        bool isRace(conditions::Subject subject, std::uint32_t race) const override;
        int sexOf(conditions::Subject subject) const override;
        float equippedItemType(conditions::Subject subject, std::uint32_t hand) const override;
        bool isSneaking(conditions::Subject subject) const override;
        bool wornHasKeyword(conditions::Subject subject, std::uint32_t keyword) const override;

    private:
        const PerkQuery* mQuery = nullptr;
        PerkRole mCurrent = PerkRole::Owner;
    };

    // ---- evaluation -------------------------------------------------------------------------------------------------

    /// What one entry point query did.
    struct EntryOutcome
    {
        float mValue = 0.f; ///< the value after the effects (the input when nothing applied)
        std::uint32_t mApplied = 0; ///< effects whose conditions held and whose function was applied
        std::uint32_t mConsidered = 0; ///< effects for this entry point of the owner's perks
        std::uint32_t mFailedConditions = 0; ///< effects whose conditions did not hold
        std::uint32_t mUnsupported = 0; ///< effects of a function this code does not apply (ignored)
        std::uint32_t mDropped = 0; ///< effects beyond kMaxQueryEffects
    };

    /// Applies the entry points of an owner's perks. Not thread safe (it owns an evaluator and a world); main thread.
    class PerkEngine
    {
    public:
        explicit PerkEngine(const PerkLibrary& library);

        /// True when any of `perks` has an effect for `entryPoint`: the gate that keeps a fight with no perks from
        /// computing anything.
        [[nodiscard]] bool hasEntryPoint(std::span<const FormId> perks, std::uint8_t entryPoint) const;

        /// The effects of `perks` for `entryPoint`, ordered by priority (higher first; ties by perk form id, then the
        /// effect's index), those whose tabs all hold, applied to `value` (Set replaces, Add adds, Multiply multiplies,
        /// MultiplyOnePlusActorValueMult multiplies by 1 + the owner's value of the stored actor value times the
        /// stored coefficient; the others are counted unsupported and ignored).
        [[nodiscard]] EntryOutcome apply(
            std::uint8_t entryPoint, std::span<const FormId> perks, const PerkQuery& query, float value);

        /// For the select-spell functions (51, 52, 53, 67, 69): the spells of the effects that hold, in the same order.
        /// Returns how many were written to `out` (at most out.size()).
        [[nodiscard]] std::size_t selectSpells(
            std::uint8_t entryPoint, std::span<const FormId> perks, const PerkQuery& query, std::span<FormId> out);

        [[nodiscard]] const conditions::Evaluator& evaluator() const { return mEvaluator; }
        [[nodiscard]] std::uint64_t unknownConditionLists() const { return mEvaluator.mListsFailedClosed; }

    private:
        struct Pick
        {
            const PerkRecord* mPerk = nullptr;
            const PerkEffect* mEffect = nullptr;
            std::uint32_t mIndex = 0;
        };
        std::size_t collect(std::uint8_t entryPoint, std::span<const FormId> perks,
            std::array<Pick, kMaxQueryEffects>& out, std::uint32_t& dropped) const;
        bool tabsHold(const PerkEffect& effect, const PerkQuery& query);

        const PerkLibrary& mLibrary;
        conditions::Evaluator mEvaluator;
        PerkConditionWorld mWorld;
    };

    // ---- abilities --------------------------------------------------------------------------------------------------

    /// The ability spells the ability effects (kind 1) of `perks` name, in perk order, appended to `out`.
    void collectPerkAbilities(const PerkLibrary& library, std::span<const FormId> perks, std::vector<FormId>& out);

    /// The sum of the magnitudes of the value-modifying effects of the ability spells on actor value `actorValue`;
    /// `found` is true when at least one effect modified it. A spell that is not an ability counts for nothing.
    [[nodiscard]] float abilityModifier(const PerkLibrary& library, std::span<const FormId> spells,
        std::int32_t actorValue, bool& found);
}

#endif
