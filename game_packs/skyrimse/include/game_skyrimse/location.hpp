#ifndef OPENEDEN_GAME_SKYRIMSE_LOCATION_H
#define OPENEDEN_GAME_SKYRIMSE_LOCATION_H

// Skyrim SE locations (task 0156): the LCTN records, only what the conditions need: the parent location and the
// keywords. A cell names its location with XLCN (records.hpp, CellRecord::mLocation); GetInCurrentLoc and
// LocationHasKeyword ask about that location. Plain structs, no engine type.
//
// Layout (VERIFIED on the five installed masters, 831 LCTN): EDID; PNAM 4 bytes, the parent location (801 of 831 have
// one); KSIZ u32 count followed by KWDA, that many 4 byte keyword ids (698 records, the count equals the array in all
// of them). The other subrecords (FULL, LCSR, LCEC, LCPR, LCID, LCEP, LCUN, RNAM, MNAM, CNAM, the A* actor
// versions ...) are left out.
//
// Task 0211 adds the lists radiant quests and the story manager need (xEdit's definitions, cited in the task; sizes
// VERIFIED on the five masters by divisibility and by resolving the ids, see SkyrimRadiantSurvey):
//  - LCEC / ACEC / RCEC "exterior cells": a world id (4) then one (Y i16, X i16) pair (4 bytes) per cell of that world
//  in
//    this location (403 of 831 records have one: this is how most exterior cells belong to a location, only 1,907 of
//    75,776 exterior CELL records have an XLCN);
//  - LCSR / ACSR "special references": 16 bytes each: location reference type (LCRT), reference, world or cell, grid;
//  - LCPR / ACPR / RCPR "persistent references": 12 bytes each: reference, world or cell, grid;
//  - LCUN / ACUN "unique NPCs": 12 bytes each: actor (NPC_), reference, location.
//  The three families (L*, A*, R*) are merged into one list per kind, in the order they occur: what each family means
//  in the game (location's own, actor-enabled, reference-enabled) is not needed by anything here (RECALLED-VERIFY).
//
// RECALLED-VERIFY (the CK wiki says it, the data cannot): GetInCurrentLoc is also true for a location INSIDE the asked
// one, so the parent chain is walked; LocationHasKeyword reads the location's own keywords only.

#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    /// A cell of a worldspace as a location record stores it: Y then X (xEdit's order; checked against the CELL
    /// records).
    struct LocationGrid
    {
        std::int16_t mY = 0;
        std::int16_t mX = 0;
    };

    struct LocationCells
    {
        FormId mWorld = kNullFormId;
        std::vector<LocationGrid> mGrids;
    };

    struct LocationSpecialRef
    {
        FormId mRefType = kNullFormId; ///< LCRT record
        FormId mRef = kNullFormId;
        FormId mWorld = kNullFormId; ///< a worldspace or a cell
        LocationGrid mGrid;
    };

    struct LocationPersistentRef
    {
        FormId mRef = kNullFormId;
        FormId mWorld = kNullFormId;
        LocationGrid mGrid;
    };

    struct LocationUniqueNpc
    {
        FormId mActor = kNullFormId; ///< NPC_
        FormId mRef = kNullFormId;
        FormId mLocation = kNullFormId;
    };

    struct LocationRecord
    {
        RecordCommon mCommon;
        FormId mParent = kNullFormId; ///< PNAM
        LString mName; ///< FULL (task 0217: the journal names a location a quest alias holds)
        std::uint32_t mPlugin = 0; ///< the index of the plugin that gave the record (LocationIndex::add numbers them)
        std::vector<FormId> mKeywords; ///< KWDA
        std::vector<LocationCells> mCells; ///< LCEC, ACEC, RCEC (task 0211)
        std::vector<LocationSpecialRef> mSpecialRefs; ///< LCSR, ACSR
        std::vector<LocationPersistentRef> mPersistentRefs; ///< LCPR, ACPR, RCPR
        std::vector<LocationUniqueNpc> mUniqueNpcs; ///< LCUN, ACUN
    };

    struct LocationLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxKeywords = 4096;
        std::uint32_t mMaxEntries = 65536; ///< per list (task 0211)
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecords = 4ull * 1024 * 1024;
    };

    struct LocationRecords
    {
        std::string mName;
        std::vector<LocationRecord> mLocations;
        TypeCount mCount;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseLocationRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const LocationLimits& limits, LocationRecords& out);

    /// The locations of a load order: a later record replaces an earlier one with the same id, a deleted one removes
    /// it.
    class LocationIndex
    {
    public:
        void add(LocationRecords records);

        [[nodiscard]] const LocationRecord* location(FormId id) const;
        [[nodiscard]] std::size_t count() const { return mLocations.size(); }

        /// Is `current` the location `ancestor` or inside it (its parent chain, at most 64 steps, cycles stop)?
        [[nodiscard]] bool isWithin(FormId current, FormId ancestor) const;
        /// Does the location itself carry this keyword?
        [[nodiscard]] bool hasKeyword(FormId location, FormId keyword) const;

        // ---- task 0211 -----------------------------------------------------------------------------------------
        /// The location an exterior cell belongs to by the locations' own cell lists: the location that lists the grid
        /// square of `world` with the FEWEST cells (the most specific), ties to the lower form id; kNullFormId when
        /// none lists it (RECALLED-VERIFY: the game's rule is not in the data). The XLCN of the cell, when it has one,
        /// wins and is the caller's business.
        [[nodiscard]] FormId locationOfGrid(FormId world, std::int32_t x, std::int32_t y) const;
        /// A cell of the location (the first listed): where to look for it. False when it lists none.
        [[nodiscard]] bool gridOf(FormId location, FormId& world, std::int32_t& x, std::int32_t& y) const;
        /// All location ids, ascending.
        [[nodiscard]] std::vector<FormId> ids() const;
        /// The special references of the location with this reference type, in stored order.
        [[nodiscard]] std::vector<LocationSpecialRef> specialRefsOfType(FormId location, FormId refType) const;
        /// Does the location (its own lists only) have a special reference of this type?
        [[nodiscard]] bool hasRefType(FormId location, FormId refType) const;
        /// Task 0298: how many special references of the location with this type `counts` says yes to (GetRefTypeAliveCount:
        /// the caller says whether a reference is alive). Zero for an unknown location or type.
        [[nodiscard]] std::size_t countSpecialRefs(
            FormId location, FormId refType, const std::function<bool(FormId)>& counts) const;

        // ---- task 0226 (bounty quests) -----------------------------------------------------------------------------
        /// The location itself or the first ancestor that carries `keyword` (the parent chain, at most 64 steps);
        /// kNullFormId when none does.
        [[nodiscard]] FormId ancestorWithKeyword(FormId location, FormId keyword) const;
        /// The location below `location` (its descendants, not itself) that carries `keyword` and is nearest in the
        /// tree (fewest parent steps), ties to the lower form id; kNullFormId when none.
        [[nodiscard]] FormId descendantWithKeyword(FormId location, FormId keyword) const;
        /// The "editor location" of a placed reference: the location whose special or persistent reference list names
        /// it; the most deeply nested one when several do, ties to the lower form id (RECALLED-VERIFY: the reference's
        /// own persistent-location field is not decoded). kNullFormId when no location lists it.
        [[nodiscard]] FormId editorLocationOf(FormId reference) const;
        // ---- task 0230 ---------------------------------------------------------------------------------------------
        /// The location a unique-NPC list (LCUN, ACUN) gives an actor: the location the entry names (the owner of the
        /// list when it names none), from the entry that names this placed reference, else from the one that names this
        /// base (a null reference matches by base only); kNullFormId when no list names it. Several entries: the most
        /// deeply nested location, ties to the lower form id.
        [[nodiscard]] FormId uniqueNpcLocation(FormId actor, FormId reference) const;

    private:
        void rebuildGrids() const;
        std::unordered_map<FormId, LocationRecord> mLocations;
        std::uint32_t mAdded = 0; ///< how many plugins were added: the next plugin's index
        mutable bool mGridsDirty = true;
        mutable std::map<std::tuple<FormId, std::int32_t, std::int32_t>, FormId> mGridOwner;
        void rebuildRefOwners() const;
        mutable bool mRefOwnersDirty = true;
        mutable std::unordered_map<FormId, FormId> mRefOwner;
        void rebuildUnique() const;
        mutable bool mUniqueDirty = true;
        mutable std::unordered_map<FormId, FormId> mUniqueByRef; ///< task 0230
        mutable std::unordered_map<FormId, FormId> mUniqueByActor;
    };
}

#endif
