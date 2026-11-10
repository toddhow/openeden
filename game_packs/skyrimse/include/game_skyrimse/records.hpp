#ifndef OPENEDEN_GAME_SKYRIMSE_RECORDS_H
#define OPENEDEN_GAME_SKYRIMSE_RECORDS_H

// A game-neutral, engine-type-free MODEL of Skyrim SE records (task 0086, "record loading", first slice). It is built
// from the raw records of Content::Plugin (components/contentsource) and holds plain structs only: no ESMStore, no
// MWWorld, no renderer type, no Jolt/Qt/OSG type. Nothing here is wired into the engine; which world model the
// engine would load these into is an open question (task 0086 stage log).
//
// Hostile input: every size read from a file is checked against the bytes that remain before it is used, every list
// and string is capped (ParseLimits), nothing throws, and a bad record becomes a located Content::Error while the walk
// goes on with the next record. The same plugin parsed twice gives the same ParsedPlugin.
//
// Layouts are from recall of the UESP / TES5Edit documentation and a cross-reading of the cc9cii readers in
// components/esm4 (loadcell.cpp, loadrefr.cpp, loadnpc.cpp, loadarmo.cpp, loadlvli.cpp ...), which are stream-based and
// throw, so they are not called here. Each layout detail is marked in records.cpp as VERIFIED (checked against the
// installed plugins by tools/skyrimse_records_check) or RECALLED-VERIFY (not checked).

#include <components/contentsource/plugin.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// A form id in the load order's namespace: the plugin index in the top byte (0x00 to 0xFD), or 0xFE, a light
    /// plugin's index in the next 12 bits and the record's 12 bit id in the low 12 bits. 0 is "no form".
    using FormId = std::uint32_t;
    inline constexpr FormId kNullFormId = 0;
    /// A reference to a plugin the load order does not hold. Not a legal id (0xFFFFFFFF would be plugin 0xFF).
    inline constexpr FormId kUnresolvedFormId = 0xFFFFFFFFu;

    struct Vec3
    {
        float x = 0;
        float y = 0;
        float z = 0;
    };

    // ---- load order and form id resolution -----------------------------------------------------------------------

    /// The plugins of a session in load order. Normal plugins take indices 0 to 253 (0xFD), light plugins (ESL flag)
    /// take their own index of 0 to 4095 (the "FE xxx" space). Names compare case-insensitively.
    class LoadOrder
    {
    public:
        static constexpr std::size_t kMaxNormal = 254;
        static constexpr std::size_t kMaxLight = 4096;

        /// False with `error` when the plugin is a duplicate or its space is full.
        [[nodiscard]] bool add(std::string name, bool light, std::string& error);
        [[nodiscard]] bool contains(std::string_view name) const;
        /// The id prefix of a plugin's records: (index << 24), or 0xFE000000 | (light index << 12).
        [[nodiscard]] std::optional<FormId> prefixOf(std::string_view name) const;
        [[nodiscard]] bool isLight(std::string_view name) const;
        [[nodiscard]] std::size_t normalCount() const { return mNormal; }
        [[nodiscard]] std::size_t lightCount() const { return mLight; }

    private:
        struct Entry
        {
            FormId mPrefix = 0;
            bool mLight = false;
        };
        std::map<std::string, Entry> mEntries; // lower-cased name
        std::size_t mNormal = 0;
        std::size_t mLight = 0;
    };

    /// Turns the form ids stored in one plugin into load order ids. The top byte of a stored id indexes the plugin's
    /// master list; a value at or past the list's size means the plugin itself.
    class FormIdResolver
    {
    public:
        /// `selfName` must be in `order` for ids of the plugin itself to resolve.
        FormIdResolver(const Content::Header& header, std::string selfName, const LoadOrder& order);

        /// 0 stays 0. Nullopt when the plugin the id points into is not in the load order.
        [[nodiscard]] std::optional<FormId> resolve(std::uint32_t stored) const;

    private:
        std::vector<std::optional<FormId>> mMasterPrefixes; // by master index; nullopt: not in the load order
        std::vector<bool> mMasterLight;
        std::optional<FormId> mSelfPrefix;
        bool mSelfLight = false;
    };

    // ---- strings -------------------------------------------------------------------------------------------------

    /// A name field. In a localised plugin the record holds a 4 byte id into the plugin's string tables and `text`
    /// stays empty until the id is looked up (StringTable); otherwise `text` is the zero-terminated string stored.
    struct LString
    {
        bool mPresent = false;
        bool mLocalized = false;
        std::uint32_t mId = 0;
        std::string mText;
    };

    enum class StringTableKind
    {
        Strings, ///< .STRINGS: names (FULL), zero-terminated entries
        DlStrings, ///< .DLSTRINGS: descriptions, length-prefixed entries
        IlStrings, ///< .ILSTRINGS: dialogue lines, length-prefixed entries
    };

    /// One of a localised plugin's string tables, parsed from the file's bytes. Strings are Windows-1252 bytes as
    /// stored (not converted).
    class StringTable
    {
    public:
        /// False with `error` filled for a table that is cut short, whose directory points outside its data, or that
        /// asks for more than the caps allow. `name` is what errors call the file.
        [[nodiscard]] static bool parse(std::span<const std::byte> bytes, StringTableKind kind, const std::string& name,
            StringTable& out, Content::Error& error);

        [[nodiscard]] const std::string* find(std::uint32_t id) const;
        [[nodiscard]] std::size_t size() const { return mStrings.size(); }

    private:
        std::map<std::uint32_t, std::string> mStrings;
    };

    /// "strings\<plugin base name>_<language>.<ext>", lower case, with backslashes as the archives name files.
    [[nodiscard]] std::string stringTablePath(
        std::string_view pluginName, std::string_view language, StringTableKind kind);

    /// Fills `text` of a localised, present name from the table (false: not localised, not present, or id unknown).
    [[nodiscard]] bool lookupLString(LString& value, const StringTable& table);

    // ---- records -------------------------------------------------------------------------------------------------

    namespace RecordFlags
    {
        /// Header flags of a record (UESP): RECALLED-VERIFY except 0x20, 0x400 and 0x800, which are consistent with
        /// the data (see the check tool).
        inline constexpr std::uint32_t Deleted = 0x20;
        inline constexpr std::uint32_t Persistent = 0x400;
        inline constexpr std::uint32_t InitiallyDisabled = 0x800;
        inline constexpr std::uint32_t Ignored = 0x1000;
        inline constexpr std::uint32_t VisibleWhenDistant = 0x8000;
    }

    /// What every record has.
    struct RecordCommon
    {
        FormId mId = kNullFormId;
        std::uint32_t mStoredId = 0; ///< as stored in the file
        std::uint32_t mFlags = 0; ///< the record header's flags
        std::string mEditorId;
        std::uint64_t mOffset = 0; ///< in the file, for messages

        [[nodiscard]] bool deleted() const { return (mFlags & RecordFlags::Deleted) != 0; }
    };

    struct WorldRecord
    {
        RecordCommon mCommon;
        LString mName;
        FormId mParent = kNullFormId; ///< the parent worldspace (WNAM), if any
    };

    enum class RefKind
    {
        Placed, ///< REFR
        Actor, ///< ACHR
    };

    /// Which child group of its cell a placed reference was in.
    enum class RefGroup
    {
        Persistent, ///< group type 8
        Temporary, ///< group type 9
        VisibleDistant, ///< group type 10
        Other, ///< outside the three (a damaged file)
    };

    struct EnableParent
    {
        FormId mParent = kNullFormId;
        std::uint8_t mFlags = 0; ///< bit 0: opposite of the parent's state; bit 1: pop in (RECALLED-VERIFY)
    };

    struct PlacedRef
    {
        RefKind mKind = RefKind::Placed;
        RefGroup mGroup = RefGroup::Other;
        RecordCommon mCommon;
        FormId mBase = kNullFormId; ///< NAME
        bool mHasTransform = false;
        Vec3 mPosition;
        Vec3 mRotation; ///< radians, as stored
        float mScale = 1.0f; ///< XSCL
        std::optional<EnableParent> mEnableParent; ///< XESP

        [[nodiscard]] bool persistent() const { return (mCommon.mFlags & RecordFlags::Persistent) != 0; }
        [[nodiscard]] bool initiallyDisabled() const { return (mCommon.mFlags & RecordFlags::InitiallyDisabled) != 0; }
    };

    namespace CellFlags
    {
        inline constexpr std::uint16_t Interior = 0x0001;
        inline constexpr std::uint16_t HasWater = 0x0002;
        inline constexpr std::uint16_t CantTravelFromHere = 0x0004;
        inline constexpr std::uint16_t NoLodWater = 0x0008;
        inline constexpr std::uint16_t PublicArea = 0x0020;
        inline constexpr std::uint16_t HandChanged = 0x0040;
        inline constexpr std::uint16_t ShowSky = 0x0080;
        inline constexpr std::uint16_t UseSkyLighting = 0x0100;
    }

    struct CellGrid
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };

    struct CellRecord
    {
        RecordCommon mCommon;
        LString mName;
        std::uint16_t mFlags = 0; ///< DATA, one or two bytes
        std::optional<CellGrid> mGrid; ///< XCLC, exterior cells
        std::optional<float> mWaterHeight; ///< XCLW
        FormId mLocation = kNullFormId; ///< XLCN
        /// The world whose children group held the cell, or 0 for an interior cell.
        FormId mWorld = kNullFormId;
        /// Interior cells: the labels of the block (type 2) and sub-block (type 3) groups, otherwise -1.
        std::int64_t mInteriorBlock = -1;
        std::int64_t mInteriorSubBlock = -1;
        /// Exterior cells: the raw labels of the block (type 4) and sub-block (type 5) groups, otherwise 0. VERIFIED on
        /// the installed plugins: a label is (x << 16 | y & 0xFFFF) of the block (grid / 32) or sub-block (grid / 8).
        std::uint32_t mExteriorBlockLabel = 0;
        std::uint32_t mExteriorSubBlockLabel = 0;
        /// An exterior cell that sits directly in its world's children group (the persistent cell of the world).
        bool mWorldPersistentCell = false;
        std::vector<PlacedRef> mPersistent;
        std::vector<PlacedRef> mTemporary;
        std::vector<PlacedRef> mVisibleDistant;
        std::vector<PlacedRef> mOther;
        /// Where the cell sits in its file, so that its references can be read later without walking the whole plugin
        /// (world store, lazy cells): the cell record's offset is mCommon.mOffset; this is the record's size with its
        /// header, and the offset and size (header included) of the cell-children group (type 6) that follows it.
        std::uint64_t mRecordSize = 0;
        bool mHasChildren = false;
        std::uint64_t mChildrenOffset = 0;
        std::uint64_t mChildrenSize = 0;

        [[nodiscard]] bool interior() const { return (mFlags & CellFlags::Interior) != 0; }
        [[nodiscard]] std::size_t refCount() const
        {
            return mPersistent.size() + mTemporary.size() + mVisibleDistant.size() + mOther.size();
        }
    };

    struct StaticRecord
    {
        RecordCommon mCommon;
        std::optional<std::array<std::int16_t, 6>> mBounds; ///< OBND: x1 y1 z1 x2 y2 z2
        std::string mModel; ///< MODL
    };

    /// A base form that is placed in the world: ACTI, CONT, DOOR, FLOR, FURN, LIGH, MSTT, TREE, TACT; the carried
    /// things ALCH, AMMO, APPA, BOOK, INGR, KEYM, SCRL, SLGM; and the markers IDLM, SOUN, ASPC, SPEL, TXST (no model).
    /// The same fields as StaticRecord, plus the type.
    struct ModelRecord
    {
        std::uint32_t mType = 0; ///< The record's FourCC
        RecordCommon mCommon;
        std::optional<std::array<std::int16_t, 6>> mBounds; ///< OBND
        std::string mModel; ///< MODL
    };

    namespace NpcFlags
    {
        /// ACBS flags (UESP, RECALLED-VERIFY).
        inline constexpr std::uint32_t Female = 0x00000001;
        inline constexpr std::uint32_t Essential = 0x00000002;
        inline constexpr std::uint32_t Respawn = 0x00000008;
        inline constexpr std::uint32_t AutoCalcStats = 0x00000010;
        inline constexpr std::uint32_t Unique = 0x00000020;
        inline constexpr std::uint32_t PlayerLevelMult = 0x00000080;
        inline constexpr std::uint32_t Protected = 0x00000800;
        inline constexpr std::uint32_t Summonable = 0x00004000;
        inline constexpr std::uint32_t Invulnerable = 0x80000000u;
    }

    struct NpcRecord
    {
        RecordCommon mCommon;
        LString mName;
        FormId mRace = kNullFormId; ///< RNAM
        FormId mClass = kNullFormId; ///< CNAM
        bool mHasConfig = false; ///< ACBS present
        std::uint32_t mFlags = 0; ///< ACBS flags
        /// ACBS level: the level, or (with PlayerLevelMult) the multiplier times 1000.
        std::uint16_t mLevel = 0;
        std::uint16_t mCalcMin = 0;
        std::uint16_t mCalcMax = 0;
        /// DNAM: the 18 skill values and the three base attributes (RECALLED-VERIFY).
        bool mHasStats = false;
        std::array<std::uint8_t, 18> mSkills{};
        std::uint16_t mHealth = 0;
        std::uint16_t mMagicka = 0;
        std::uint16_t mStamina = 0;

        [[nodiscard]] bool female() const { return (mFlags & NpcFlags::Female) != 0; }
        [[nodiscard]] bool essential() const { return (mFlags & NpcFlags::Essential) != 0; }
        [[nodiscard]] bool unique() const { return (mFlags & NpcFlags::Unique) != 0; }
        [[nodiscard]] bool isProtected() const { return (mFlags & NpcFlags::Protected) != 0; }
    };

    enum class ItemKind
    {
        Armor, ///< ARMO
        Weapon, ///< WEAP
        Misc, ///< MISC
    };

    /// ARMO, WEAP and MISC, names, models, value, weight and keywords only.
    struct ItemRecord
    {
        ItemKind mKind = ItemKind::Misc;
        RecordCommon mCommon;
        LString mName;
        /// WEAP and MISC: MODL. ARMO: MOD2, the male world model (MODL there lists armature forms instead).
        std::string mModel;
        std::string mFemaleModel; ///< ARMO: MOD4
        std::vector<FormId> mArmatures; ///< ARMO: the MODL entries that are 4 bytes (ARMA forms)
        bool mHasData = false;
        std::int32_t mValue = 0;
        float mWeight = 0.0f;
        std::uint16_t mDamage = 0; ///< WEAP only (DATA is 10 bytes)
        std::vector<FormId> mKeywords; ///< KWDA
    };

    namespace LeveledFlags
    {
        inline constexpr std::uint8_t CalcFromAllLevelsLessThanPlayer = 0x01;
        inline constexpr std::uint8_t CalcForEachItemInCount = 0x02;
        inline constexpr std::uint8_t UseAll = 0x04;
        inline constexpr std::uint8_t SpecialLoot = 0x08;
    }

    struct LeveledEntry
    {
        std::uint16_t mLevel = 0;
        FormId mReference = kNullFormId;
        std::uint16_t mCount = 0;
    };

    struct LeveledList
    {
        bool mIsNpcList = false; ///< LVLN, else LVLI
        RecordCommon mCommon;
        std::uint8_t mChanceNone = 0; ///< LVLD, percent
        std::uint8_t mFlags = 0; ///< LVLF
        FormId mGlobal = kNullFormId; ///< LVLG, the global that replaces the chance
        std::vector<LeveledEntry> mEntries; ///< LVLO
    };

    // ---- the parse -----------------------------------------------------------------------------------------------

    /// Bounds on what a file may make the parse allocate.
    struct ParseLimits
    {
        /// One record's inflated bytes (a record over this is refused with an error).
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxEntriesPerList = 4096; ///< leveled entries, keywords, armatures
        std::uint32_t mMaxStringBytes = 64 * 1024; ///< one zstring or file path
        /// Errors kept in ParsedPlugin::mErrors; further ones are only counted.
        std::uint32_t mMaxErrors = 256;
        /// Records kept per type. A plugin has 2^24 form ids at most, but a hostile one can repeat records.
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
        /// Walk the groups and count REFR and ACHR but do not decode them (the world store's index pass; cells are
        /// still decoded, with their children group's place in the file).
        bool mSkipReferences = false;
    };

    /// Per record type: what the walk saw and what became a struct.
    struct TypeCount
    {
        std::uint64_t mSeen = 0;
        std::uint64_t mParsed = 0;
        std::uint64_t mFailed = 0;
        std::uint64_t mDeleted = 0;
    };

    struct ParsedPlugin
    {
        std::string mName;
        Content::Header mHeader;
        std::vector<WorldRecord> mWorlds;
        std::vector<CellRecord> mCells;
        std::vector<StaticRecord> mStatics;
        std::vector<ModelRecord> mModels;
        std::vector<NpcRecord> mNpcs;
        std::vector<ItemRecord> mItems;
        std::vector<LeveledList> mLeveledLists;

        /// Types the model decodes, with what happened to them.
        std::map<std::uint32_t, TypeCount> mCounts;
        /// References whose own record could not be placed (outside any cell group, or no matching cell).
        std::uint64_t mRefsOutsideCells = 0;
        /// Form ids that pointed into a plugin the load order does not hold (stored as kUnresolvedFormId).
        std::uint64_t mUnresolvedFormIds = 0;
        /// Subrecords the model decodes whose size or content did not fit the layout (the record is still kept).
        std::uint64_t mOddSubrecords = 0;
        /// An odd subrecord by "TYPE.SUB" for the tools.
        std::map<std::string, std::uint64_t> mOddBy;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        /// The error that ended the walk, if the file is damaged past a record boundary.
        std::optional<Content::Error> mWalkError;
        std::uint64_t mRecordsWalked = 0;

        [[nodiscard]] const CellRecord* findCell(FormId id) const;
    };

    /// Parses an opened plugin. `order` must hold the plugin (by its name) and its masters for ids to resolve; ids into
    /// plugins it lacks become kUnresolvedFormId and are counted. Returns false only when the walk itself failed
    /// (mWalkError); the records before the failure are kept.
    [[nodiscard]] bool parseRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ParseLimits& limits, ParsedPlugin& out);

    /// Same on a file or on bytes already in memory (the tests use this one). False with `error` for a file that
    /// cannot be opened or is not a Skyrim SE plugin; a walk failure is in out.mWalkError and also returns false.
    [[nodiscard]] bool parseRecordsFile(const std::filesystem::path& path, const LoadOrder& order,
        const ParseLimits& limits, ParsedPlugin& out, Content::Error& error);
    [[nodiscard]] bool parseRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ParseLimits& limits, ParsedPlugin& out, Content::Error& error);
}

#endif
