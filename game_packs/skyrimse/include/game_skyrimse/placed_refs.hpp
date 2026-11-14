#ifndef OPENEDEN_GAME_SKYRIMSE_PLACED_REFS_H
#define OPENEDEN_GAME_SKYRIMSE_PLACED_REFS_H

// Skyrim SE placed references, containers and relationships for the conditions world (task 0160).
//
// Conditions can run on a reference other than the speaker (run-on 2, a quest alias): "is this chest's owner dead",
// "does that guard carry a key". A fresh game answers them from the data: the placed reference's base, the cell it
// stands in, whether it starts disabled or dead, where it stands, and (through the actor index) its base's factions,
// inventory and sex. This table keeps, for every REFR and ACHR of a load order, exactly that (32 bytes each), plus the
// inventories of the CONT records and the RELA relationship records.
//
// Layouts: REFR/ACHR NAME (4 bytes, the base), DATA (24 bytes, position then rotation, only the position is read),
// XESP (8 bytes: enable parent, flags; bit 1 means "opposite of the parent": CONFIRMED by upstream loadrefr.cpp in
// records.cpp); record flags 0x400 persistent, 0x800 initially disabled (records.hpp RecordFlags), and for ACHR
// 0x200 "starts dead" (VERIFIED by data: the 1,507 ACHR with the bit are bases named TreasCorpse... and the like).
// RELA DATA is 16 bytes (VERIFIED on 673 of 673): parent NPC_, child NPC_, rank u8, one unused byte, flags u8 (all 0),
// one byte (0 or 128), association type form id. The rank enum is RECALLED-VERIFY (xEdit): 0 lover, 1 ally, 2
// confidant, 3 friend, 4 acquaintance, 5 rival, 6 foe, 7 enemy, 8 archnemesis, which GetRelationshipRank reports as
// 4, 3, 2, 1, 0, -1, -2, -3, -4 (CK wiki: -4 archnemesis to 4 lover).

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace PlacedFlags
    {
        inline constexpr std::uint8_t Disabled = 0x01; ///< record flag "initially disabled"
        inline constexpr std::uint8_t Dead = 0x02; ///< ACHR record flag "starts dead"
        inline constexpr std::uint8_t Persistent = 0x04;
        inline constexpr std::uint8_t IsActor = 0x08; ///< an ACHR
        inline constexpr std::uint8_t EnableOpposite = 0x10; ///< XESP flag: opposite of the parent's state
    }

    struct PlacedEntry
    {
        FormId mId = kNullFormId;
        FormId mBase = kNullFormId; ///< NAME (0 when a record has none; the merge inherits the earlier one)
        FormId mCell = kNullFormId; ///< the CELL whose children group holds it
        FormId mEnableParent = kNullFormId; ///< XESP
        float mX = 0.f, mY = 0.f, mZ = 0.f;
        std::uint8_t mFlags = 0; ///< PlacedFlags
        bool mDeleted = false; ///< the record is flagged deleted (the merge removes the reference)
    };

    struct RelationshipRecord
    {
        FormId mId = kNullFormId;
        FormId mParent = kNullFormId;
        FormId mChild = kNullFormId;
        std::uint8_t mRank = 4; ///< the stored enum, 0 lover to 8 archnemesis
        FormId mAssociation = kNullFormId;
        bool mDeleted = false;
    };

    struct ContainerRecord
    {
        FormId mId = kNullFormId;
        std::vector<InventoryEntry> mItems; ///< CNTO
        bool mDeleted = false;
    };

    struct PlacedRefLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint64_t mMaxRefs = 8ull * 1024 * 1024;
        std::uint32_t mMaxItems = 65536;
        std::uint32_t mMaxErrors = 256;
    };

    struct PlacedRefRecords
    {
        std::string mName;
        std::vector<PlacedEntry> mRefs;
        std::vector<RelationshipRecord> mRelations;
        std::vector<ContainerRecord> mContainers;
        std::uint64_t mFailed = 0;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::vector<Content::Error> mErrors;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parsePlacedRefs(
        const Content::Plugin& plugin, const LoadOrder& order, const PlacedRefLimits& limits, PlacedRefRecords& out);

    /// GetRelationshipRank's number for a stored RELA rank (see above); out of range gives 0.
    [[nodiscard]] float relationshipRankValue(std::uint8_t storedRank);

    /// The merged table of a load order: a later plugin's record with the same id replaces an earlier one (fields it
    /// lacks, the base of a reference, are inherited), a deleted record removes it. Add plugins in load order, then
    /// call finish() once; queries are valid only after that. Read only afterwards (any thread).
    class PlacedRefIndex
    {
    public:
        void add(PlacedRefRecords&& records);
        void finish();

        [[nodiscard]] const PlacedEntry* find(FormId reference) const;
        /// Every reference of a base, in id order (a view into the table; empty for none).
        struct Range
        {
            const PlacedEntry* const* mBegin = nullptr;
            const PlacedEntry* const* mEnd = nullptr;
            [[nodiscard]] std::size_t size() const { return static_cast<std::size_t>(mEnd - mBegin); }
        };
        [[nodiscard]] Range byBase(FormId base) const;
        [[nodiscard]] const ContainerRecord* container(FormId base) const;
        /// The relationship between two NPC_ bases: (parent a, child b) first, else (parent b, child a). Nullopt when
        /// the data holds none.
        [[nodiscard]] std::optional<std::uint8_t> relationRank(FormId a, FormId b) const;

        /// Is the reference initially disabled, following its enable parent (at most 4 steps; a parent that is not in
        /// the table, the player's 0x14 included, counts as enabled)? RECALLED-VERIFY: the state follows the parent's.
        [[nodiscard]] bool initiallyDisabled(const PlacedEntry& ref) const;

        /// Every reference, sorted by id (valid after finish()).
        [[nodiscard]] const std::vector<PlacedEntry>& all() const { return mRefs; }
        [[nodiscard]] std::size_t size() const { return mRefs.size(); }
        [[nodiscard]] std::size_t relationCount() const { return mRelations.size(); }
        [[nodiscard]] std::size_t containerCount() const { return mContainers.size(); }
        /// The bytes the table holds (vectors' capacity times element size, the by-base list; not the allocator's
        /// overhead).
        [[nodiscard]] std::uint64_t memoryBytes() const;

    private:
        std::vector<PlacedEntry> mRefs; ///< sorted by id after finish()
        std::vector<const PlacedEntry*> mByBase; ///< sorted by (base, id)
        std::vector<RelationshipRecord> mRelations;
        std::unordered_map<FormId, ContainerRecord> mContainers;
        bool mFinished = false;
    };
}

#endif
