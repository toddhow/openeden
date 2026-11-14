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
// RECALLED-VERIFY (the CK wiki says it, the data cannot): GetInCurrentLoc is also true for a location INSIDE the asked
// one, so the parent chain is walked; LocationHasKeyword reads the location's own keywords only.

#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    struct LocationRecord
    {
        RecordCommon mCommon;
        FormId mParent = kNullFormId; ///< PNAM
        std::vector<FormId> mKeywords; ///< KWDA
    };

    struct LocationLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxKeywords = 4096;
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

    private:
        std::unordered_map<FormId, LocationRecord> mLocations;
    };
}

#endif
