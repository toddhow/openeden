#ifndef OPENEDEN_GAME_SKYRIMSE_ACTORS_H
#define OPENEDEN_GAME_SKYRIMSE_ACTORS_H

// Actor and appearance records of Skyrim SE (task 0118, stage N5-0 of decision 0017): NPC_ in full, RACE, ARMA, ARMO's
// armature list and slot mask, OTFT, HDPT, CLAS, and resolution helpers for NPC templates (TPLT and the template use
// flags) and leveled lists. Plain structs only: no engine, renderer, Jolt, Qt or OSG type. Nothing here is wired into
// the engine (stage N5-0 changes no engine, SDK or core file).
//
// Same hostile-input discipline as records.hpp: every size is checked against the bytes that remain, every list and
// string is capped (ActorLimits), nothing throws, a bad record becomes a located Content::Error and the walk goes on.
// A subrecord of a size the layout does not allow is counted in ActorRecords::mOddBy and decodes to nothing.
//
// Layout status is marked in actors.cpp: VERIFIED (sizes and references checked on the five installed masters by
// tools/skyrimse_actors_check, aggregates in the task 0118 stage log) or RECALLED-VERIFY (meaning from memory of the
// UESP / TES5Edit documentation, not settled by the data). Upstream OpenMW components/esm4 may corroborate, never
// source.

#include "game_skyrimse/records.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    enum class Sex
    {
        Male,
        Female,
    };

    /// ACBS template use flags (NPC_ ACBS offset 18, u16). VERIFIED: the OR of all 6,525 NPC_ records of the five
    /// masters is 0x1FFF (13 bits, none above). The names are RECALLED-VERIFY; the "Base Data" bit is supported by the
    /// data (3,512 of 3,512 templated NPCs with the bit set have no FULL of their own).
    namespace TemplateFlags
    {
        inline constexpr std::uint16_t Traits = 0x0001;
        inline constexpr std::uint16_t Stats = 0x0002;
        inline constexpr std::uint16_t Factions = 0x0004;
        inline constexpr std::uint16_t SpellList = 0x0008;
        inline constexpr std::uint16_t AiData = 0x0010;
        inline constexpr std::uint16_t AiPackages = 0x0020;
        inline constexpr std::uint16_t ModelAnimation = 0x0040;
        inline constexpr std::uint16_t BaseData = 0x0080;
        inline constexpr std::uint16_t Inventory = 0x0100;
        inline constexpr std::uint16_t Script = 0x0200;
        inline constexpr std::uint16_t DefPackList = 0x0400;
        inline constexpr std::uint16_t AttackData = 0x0800;
        inline constexpr std::uint16_t Keywords = 0x1000;
        inline constexpr std::uint16_t All = 0x1FFF;
    }

    struct FactionMembership
    {
        FormId mFaction = kNullFormId; ///< SNAM, an 8 byte entry: form id, rank i8, three zero bytes (VERIFIED)
        std::int8_t mRank = 0; ///< 255 (-1) in the installed data means "no rank"
    };

    struct InventoryEntry
    {
        FormId mItem = kNullFormId; ///< CNTO, 8 bytes: form id, count i32 (VERIFIED: ARMO, LVLI and other item types)
        std::int32_t mCount = 0;
    };

    /// AIDT, 20 bytes (VERIFIED size). Byte meanings RECALLED-VERIFY; value ranges agree with them (aggression 0..3,
    /// confidence 0..4, energy 0..100, assistance 0..2).
    struct AiData
    {
        std::uint8_t mAggression = 0;
        std::uint8_t mConfidence = 0;
        std::uint8_t mEnergy = 0;
        std::uint8_t mResponsibility = 0;
        std::uint8_t mMood = 0;
        std::uint8_t mAssistance = 0;
        std::uint8_t mAggroBehaviour = 0; ///< byte 6, 0 or 1 in the installed data
        std::uint8_t mUnknown7 = 0; ///< byte 7, left raw
        std::array<std::uint32_t, 3> mRadii{}; ///< u32 at 8, 12, 16, left raw
    };

    /// NPC_ in full.
    struct ActorNpc
    {
        RecordCommon mCommon;
        LString mName;
        FormId mRace = kNullFormId; ///< RNAM
        FormId mClass = kNullFormId; ///< CNAM
        FormId mVoiceType = kNullFormId; ///< VTCK
        FormId mTemplate = kNullFormId; ///< TPLT: an NPC_ or an LVLN (VERIFIED: 3,957 and 658 of 4,615 uses)
        FormId mDefaultOutfit = kNullFormId; ///< DOFT: an OTFT
        FormId mSleepOutfit = kNullFormId; ///< SOFT: an OTFT
        FormId mSkin = kNullFormId; ///< WNAM: an ARMO worn as the body skin
        FormId mFarAwaySkin = kNullFormId; ///< ANAM: an ARMO (RECALLED-VERIFY meaning: the distant model's skin)
        FormId mDeathItem = kNullFormId; ///< INAM: an LVLI
        FormId mCrimeFaction = kNullFormId; ///< CRIF
        FormId mCombatStyle = kNullFormId; ///< ZNAM
        FormId mHeadTextureSet = kNullFormId; ///< FTST: a TXST
        FormId mHairColor = kNullFormId; ///< HCLF: a CLFM

        bool mHasConfig = false; ///< ACBS present
        std::uint32_t mFlags = 0; ///< ACBS flags, see NpcFlags
        std::uint16_t mLevel = 0;
        std::uint16_t mCalcMin = 0;
        std::uint16_t mCalcMax = 0;
        std::uint16_t mTemplateFlags = 0; ///< ACBS offset 18
        bool mHasStats = false; ///< DNAM
        std::array<std::uint8_t, 18> mSkills{};
        std::uint16_t mHealth = 0;
        std::uint16_t mMagicka = 0;
        std::uint16_t mStamina = 0;

        std::optional<AiData> mAi;
        std::vector<FactionMembership> mFactions; ///< SNAM
        std::vector<InventoryEntry> mInventory; ///< CNTO
        std::vector<FormId> mPackages; ///< PKID, in order
        FormId mDefaultPackageList = kNullFormId; ///< DPLT (an FLST in the installed data)
        std::vector<FormId> mKeywords; ///< KWDA
        std::vector<FormId> mSpells; ///< SPLO
        std::vector<FormId> mHeadParts; ///< PNAM: HDPT forms (VERIFIED: 14,382 of 14,382)
        std::optional<float> mHeight; ///< NAM6, 0.5 to 3.3 in the installed data
        std::optional<float> mWeight; ///< NAM7, 0 to 100
        /// QNAM, the skin tone as three floats in 0 to 1 (VERIFIED: 6,362 of 6,362).
        std::optional<std::array<float, 3>> mSkinTone;
        /// NAM9, 19 face morph floats (VERIFIED size 76); FLT_MAX in some slots means "unused".
        std::optional<std::array<float, 19>> mFaceMorphs;
        /// NAMA, four u32: nose, unknown (-1 or 0), eyes, mouth (RECALLED-VERIFY meanings; sizes VERIFIED).
        std::optional<std::array<std::uint32_t, 4>> mFaceParts;
        std::uint32_t mTintLayerCount = 0; ///< TINI entries; the layers themselves are not decoded

        [[nodiscard]] bool female() const { return (mFlags & NpcFlags::Female) != 0; }
        [[nodiscard]] Sex sex() const { return female() ? Sex::Female : Sex::Male; }
    };

    /// A model path that belongs to one sex.
    struct RaceModel
    {
        Sex mSex = Sex::Male;
        std::uint32_t mIndex = 0; ///< INDX where the section has one, else 0
        std::string mPath;
    };

    struct RaceHeadPart
    {
        Sex mSex = Sex::Male;
        std::uint32_t mIndex = 0; ///< INDX: the head part slot (0 to 3 or 4 in the installed data)
        FormId mHeadPart = kNullFormId; ///< HEAD: an HDPT (VERIFIED: 282 of 282)
    };

    /// RACE. The skeleton models are the ANAM that follows the MNAM / FNAM markers before the body-data section
    /// (VERIFIED on the installed data: 399 ANAM, two per race, all paths ending in .nif and none containing "hkx").
    struct RaceRecord
    {
        RecordCommon mCommon;
        LString mName;
        std::string mSkeleton[2]; ///< indexed by Sex
        std::vector<RaceModel> mBodyModels; ///< NAM1 section: INDX + MODL (.egt morph files in the installed data)
        std::vector<RaceModel> mBehaviourGraphs; ///< NAM3 section: MODL (.hkx)
        std::vector<RaceHeadPart> mHeadParts; ///< NAM0 section: INDX + HEAD
        FormId mSkin = kNullFormId; ///< WNAM: an ARMO (VERIFIED: 158 of 158)
        std::array<FormId, 2> mVoiceTypes{}; ///< VTCK male, female
        std::array<FormId, 2> mDecapitateArmor{}; ///< DNAM male, female
        std::array<FormId, 2> mDefaultFaceTexture{}; ///< DFTM, DFTF
        std::vector<FormId> mAvailableHairColors[2]; ///< AHCM, AHCF
        std::vector<FormId> mFaceTextureSets[2]; ///< FTSM, FTSF
        std::uint32_t mSlotMask = 0; ///< BOD2 biped slots
        std::uint32_t mArmorType = 0; ///< BOD2
        bool mHasData = false; ///< DATA (164 bytes, VERIFIED)
        std::array<float, 2> mHeight{ 1.0f, 1.0f }; ///< DATA at 16 (male), 20 (female)
        std::array<float, 2> mWeight{ 1.0f, 1.0f }; ///< DATA at 24, 28
        std::uint32_t mFlags = 0; ///< DATA at 32, raw (bit 0 is "playable": set on 10 of 161 races, RECALLED-VERIFY)
        std::vector<FormId> mKeywords;
        std::vector<FormId> mSpells;

        [[nodiscard]] const std::string& skeleton(Sex sex) const { return mSkeleton[sex == Sex::Male ? 0 : 1]; }
        [[nodiscard]] bool playable() const { return (mFlags & 1u) != 0; }
    };

    /// ARMA, an armor addon. The BOD2 / BODT slot mask says which biped slots it covers.
    struct ArmatureRecord
    {
        RecordCommon mCommon;
        std::uint32_t mSlotMask = 0; ///< BOD2 or BODT first u32 (VERIFIED: 187 BOD2 of 8 bytes, 926 BODT of 12)
        std::uint32_t mArmorType = 0;
        FormId mRace = kNullFormId; ///< RNAM (a RACE, VERIFIED 1,076 of 1,076)
        std::vector<FormId> mAdditionalRaces; ///< MODL entries (VERIFIED: 13,443 RACE)
        std::string mMaleModel; ///< MOD2
        std::string mFemaleModel; ///< MOD3
        std::string mMaleFirstPerson; ///< MOD4
        std::string mFemaleFirstPerson; ///< MOD5
        bool mHasDnam = false; ///< DNAM (12 bytes, VERIFIED)
        std::uint8_t mMalePriority = 0;
        std::uint8_t mFemalePriority = 0;
        std::uint8_t mMaleWeightSlider = 0; ///< byte 2 (RECALLED-VERIFY: 0 or 2 in the installed data)
        std::uint8_t mFemaleWeightSlider = 0; ///< byte 3
        float mWeaponAdjust = 0.0f; ///< f32 at 8

        [[nodiscard]] const std::string& model(Sex sex, bool firstPerson = false) const
        {
            return sex == Sex::Male ? (firstPerson ? mMaleFirstPerson : mMaleModel)
                                    : (firstPerson ? mFemaleFirstPerson : mFemaleModel);
        }
        /// True when this addon is for `race`: RNAM or one of the additional races.
        [[nodiscard]] bool appliesTo(FormId race) const;
    };

    /// What an ARMO adds to the item model of records.hpp: the slot mask, the armor type, the addon list in order and
    /// the template armor (TNAM, an ARMO, VERIFIED 2,620 of 2,620).
    struct ArmorExtra
    {
        RecordCommon mCommon;
        std::uint32_t mSlotMask = 0; ///< BOD2 (VERIFIED 3,665 of 3,675 ARMO) or BODT (10 of 3,675)
        std::uint32_t mArmorType = 0; ///< 0 light, 1 heavy, 2 clothing (RECALLED-VERIFY names; values 0..2 VERIFIED)
        FormId mRace = kNullFormId; ///< RNAM (VERIFIED: a RACE)
        std::vector<FormId> mArmatures; ///< MODL (VERIFIED: all ARMA)
        FormId mTemplate = kNullFormId; ///< TNAM
        std::uint32_t mRating = 0; ///< DNAM, 100 times the armor rating (RECALLED-VERIFY)
    };

    struct OutfitRecord
    {
        RecordCommon mCommon;
        std::vector<FormId> mItems; ///< INAM (VERIFIED: ARMO and LVLI)
    };

    namespace HeadPartType
    {
        inline constexpr std::uint32_t Misc = 0, Face = 1, Eyes = 2, Hair = 3, FacialHair = 4, Scar = 5, Brows = 6;
    }

    struct HeadPartRecord
    {
        RecordCommon mCommon;
        LString mName;
        std::uint8_t mFlags = 0; ///< DATA, one byte (VERIFIED size 1)
        std::uint32_t mType = 0; ///< PNAM, 0 to 6 (VERIFIED)
        std::string mModel; ///< MODL
        std::vector<FormId> mExtraParts; ///< HNAM (HDPT, VERIFIED)
        FormId mTextureSet = kNullFormId; ///< TNAM (TXST, VERIFIED)
        FormId mColor = kNullFormId; ///< CNAM
        FormId mValidRaces = kNullFormId; ///< RNAM (an FLST in the installed data; the list is not read)
    };

    struct ClassRecord
    {
        RecordCommon mCommon;
        LString mName;
        bool mHasData = false; ///< DATA, 36 bytes (VERIFIED: 159 of 159)
        std::uint8_t mTrainedSkill = 0; ///< byte 4 (<= 18 or 255, VERIFIED); RECALLED-VERIFY meaning
        std::uint8_t mMaxTrainingLevel = 0; ///< byte 5 (<= 100, VERIFIED)
        std::array<std::uint8_t, 18> mSkillWeights{}; ///< bytes 6 to 23 (largest value 9, VERIFIED)
        float mBleedoutDefault = 0.0f; ///< f32 at 24 (0.1 on most classes)
        std::uint32_t mVoicePoints = 0; ///< u32 at 28 (0, 50, 100, 300 or 1000)
        std::uint8_t mHealthWeight = 0; ///< bytes 32 to 34 (RECALLED-VERIFY meaning)
        std::uint8_t mMagickaWeight = 0;
        std::uint8_t mStaminaWeight = 0;
    };

    // ---- the parse -----------------------------------------------------------------------------------------------

    struct ActorLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxEntriesPerList = 4096; ///< inventory, factions, packages, head parts, outfit items ...
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    struct ActorRecords
    {
        std::string mName;
        std::vector<ActorNpc> mNpcs;
        std::vector<RaceRecord> mRaces;
        std::vector<ArmatureRecord> mArmatures;
        std::vector<ArmorExtra> mArmors;
        std::vector<OutfitRecord> mOutfits;
        std::vector<HeadPartRecord> mHeadParts;
        std::vector<ClassRecord> mClasses;

        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy; ///< "TYPE.SUB" -> count
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
        std::uint64_t mRecordsWalked = 0;
    };

    /// Parses NPC_, RACE, ARMA, ARMO (the extra fields), OTFT, HDPT and CLAS. `order` must hold the plugin and its
    /// masters for ids to resolve. False only when the walk itself failed (mWalkError); records before are kept.
    [[nodiscard]] bool parseActorRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ActorLimits& limits, ActorRecords& out);

    [[nodiscard]] bool parseActorRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ActorLimits& limits, ActorRecords& out, Content::Error& error);

    // ---- resolution ----------------------------------------------------------------------------------------------

    /// The records of every plugin of a load order, later plugins overriding earlier ones by form id (whole-record
    /// replacement, as the data does it). Holds copies.
    class ActorIndex
    {
    public:
        void add(const ActorRecords& records);
        /// Adds leveled lists (from ParsedPlugin::mLeveledLists) for template resolution.
        void addLeveledLists(const std::vector<LeveledList>& lists);

        [[nodiscard]] const ActorNpc* npc(FormId id) const { return find(mNpcs, id); }
        [[nodiscard]] const RaceRecord* race(FormId id) const { return find(mRaces, id); }
        [[nodiscard]] const ArmatureRecord* armature(FormId id) const { return find(mArmatures, id); }
        [[nodiscard]] const ArmorExtra* armor(FormId id) const { return find(mArmors, id); }
        [[nodiscard]] const OutfitRecord* outfit(FormId id) const { return find(mOutfits, id); }
        [[nodiscard]] const HeadPartRecord* headPart(FormId id) const { return find(mHeadParts, id); }
        [[nodiscard]] const ClassRecord* classRecord(FormId id) const { return find(mClasses, id); }
        [[nodiscard]] const LeveledList* leveledList(FormId id) const { return find(mLeveled, id); }

        [[nodiscard]] std::size_t npcCount() const { return mNpcs.size(); }

        /// All NPC_ ids, sorted (deterministic).
        [[nodiscard]] std::vector<FormId> npcIds() const;

        /// The entry of a leveled list a pick chooses. Deterministic, no random numbers: the caller supplies `pick`
        /// (a number the caller draws, for example from its seeded generator). Entries above `level` are not eligible;
        /// unless the list has CalcFromAllLevelsLessThanPlayer, only the entries of the highest eligible level are (the
        /// rule is RECALLED-VERIFY). nullptr when nothing is eligible or the list is unknown.
        [[nodiscard]] const LeveledEntry* pickLeveled(FormId list, std::uint16_t level, std::uint32_t pick) const;

        struct Resolved
        {
            /// The NPC_ record each category comes from, after following TPLT and the template use flags.
            FormId mTraitsFrom = kNullFormId;
            FormId mStatsFrom = kNullFormId;
            FormId mFactionsFrom = kNullFormId;
            FormId mSpellsFrom = kNullFormId;
            FormId mAiFrom = kNullFormId;
            FormId mPackagesFrom = kNullFormId;
            FormId mBaseDataFrom = kNullFormId;
            FormId mInventoryFrom = kNullFormId; ///< items and outfits
            FormId mKeywordsFrom = kNullFormId;
            /// The effective values.
            FormId mRace = kNullFormId;
            FormId mClass = kNullFormId;
            FormId mDefaultOutfit = kNullFormId;
            FormId mSkin = kNullFormId;
            std::vector<FormId> mHeadParts;
            std::vector<InventoryEntry> mInventory;
            std::vector<FactionMembership> mFactions;
            std::vector<FormId> mPackages;
            LString mName;
            std::uint32_t mDepth = 0; ///< how many TPLT links were followed
        };

        enum class ResolveStatus
        {
            Ok,
            UnknownNpc, ///< the id is not an NPC_
            Cycle, ///< a TPLT chain leads back to itself
            TooDeep, ///< longer than kMaxTemplateDepth
            BrokenLink, ///< a TPLT names something that is neither an NPC_ nor a leveled list with a pick
        };
        static constexpr std::uint32_t kMaxTemplateDepth = 16;

        /// Follows TPLT. A template that is an LVLN is resolved with `level` and `pick`. For each category the NPC
        /// takes the values of its template when its own template flag for the category is set (RECALLED-VERIFY: which
        /// record fields each flag covers is in the definitions below, they are not settled by the data).
        [[nodiscard]] ResolveStatus resolve(FormId npc, std::uint16_t level, std::uint32_t pick, Resolved& out) const;

        /// The models an NPC wears by default, in outfit order: the ARMO items of the effective default outfit (leveled
        /// list items are resolved with `level` / `pick`), each with the addon (ARMA) models that apply to `race`.
        struct WornModel
        {
            FormId mArmor = kNullFormId;
            FormId mArmature = kNullFormId;
            std::uint32_t mSlotMask = 0;
            std::string mModel; ///< the sex's world model of the addon
            std::uint8_t mPriority = 0;
        };
        [[nodiscard]] std::vector<WornModel> wornModels(
            FormId outfit, FormId race, Sex sex, std::uint16_t level, std::uint32_t pick) const;

    private:
        template <class T>
        static const T* find(const std::unordered_map<FormId, T>& map, FormId id)
        {
            const auto it = map.find(id);
            return it == map.end() ? nullptr : &it->second;
        }

        std::unordered_map<FormId, ActorNpc> mNpcs;
        std::unordered_map<FormId, RaceRecord> mRaces;
        std::unordered_map<FormId, ArmatureRecord> mArmatures;
        std::unordered_map<FormId, ArmorExtra> mArmors;
        std::unordered_map<FormId, OutfitRecord> mOutfits;
        std::unordered_map<FormId, HeadPartRecord> mHeadParts;
        std::unordered_map<FormId, ClassRecord> mClasses;
        std::unordered_map<FormId, LeveledList> mLeveled;
    };
}

#endif
