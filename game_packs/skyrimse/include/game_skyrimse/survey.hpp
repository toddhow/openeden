#ifndef OPENEDEN_GAME_SKYRIMSE_SURVEY_H
#define OPENEDEN_GAME_SKYRIMSE_SURVEY_H

// The Skyrim SE data survey, part 1: condition functions (task 0116, docs/design/skyrim-se-implementation-plan.md 4.16
// and section 8 item 2). Pure code: it decodes the CTDA subrecords of records handed to it and counts; it reads no
// file, prints nothing and keeps nothing of the records but numbers. The gated tool (tools/skyrimse_survey_check) feeds
// it the player's own plugins and prints aggregates.
//
// Clean-room rule (CLAUDE.md, decision 0014): this file contains no condition function table. A function is known by
// its index, written fn_<index>; what a function reads is shown by the data (the parameter kinds below). Names come
// only from survey_names.cpp (upstream OpenMW's table, cited there), never from xEdit, the Creation Kit wiki or
// CommonLib.
//
// The CTDA layout (32 bytes) is RECALLED-VERIFY until the real-data check has verified it on the installed plugins; the
// checks that do so are listed in the research note of the survey.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse::survey
{
    /// A CTDA subrecord of Skyrim SE has this size (RECALLED-VERIFY; the check counts every size it meets).
    inline constexpr std::size_t kCtdaSize = 32;

    /// The decoded CTDA. Offsets (RECALLED-VERIFY): 0 type byte (compare operator in the top 3 bits, flags in the low
    /// 5), 1..3 unused, 4 comparison value (float, or a global's form id when the use-global flag is set), 8 function
    /// index (u16), 10..11 unused, 12 parameter 1, 16 parameter 2, 20 run-on, 24 reference, 28 parameter 3.
    struct Ctda
    {
        std::uint8_t mTypeByte = 0;
        std::uint8_t mCompare = 0; ///< mTypeByte >> 5 (0 to 5 expected: six comparison operators)
        std::uint8_t mFlags = 0; ///< mTypeByte & 0x1F
        std::uint32_t mComparisonBits = 0;
        float mComparison = 0.0f;
        std::uint16_t mFunction = 0;
        std::uint32_t mParam1 = 0;
        std::uint32_t mParam2 = 0;
        std::uint32_t mRunOn = 0;
        std::uint32_t mReference = 0;
        std::uint32_t mParam3 = 0;
        /// Any of the unused bytes (1 to 3, 10 to 11) non-zero.
        bool mPaddingNonZero = false;
        bool mPaddingAfterTypeNonZero = false; ///< bytes 1 to 3
        bool mPaddingAfterFunctionNonZero = false; ///< bytes 10 to 11
    };

    /// False unless the subrecord is exactly kCtdaSize bytes.
    [[nodiscard]] bool decodeCtda(std::span<const std::byte> data, Ctda& out);

    /// "fn_<index>": the identity of a function here.
    [[nodiscard]] std::string functionName(std::uint16_t index);

    /// A name for the index from the upstream OpenMW table (survey_names.cpp; the Oblivion/FO3/FNV numbering,
    /// RECALLED-VERIFY, given only for indices up to 330 that the data agrees with), or an empty view.
    [[nodiscard]] std::string_view upstreamFunctionName(std::uint16_t index);

    /// "fn_72 (GetIsID?)": the index and, when known, the upstream name with a question mark.
    [[nodiscard]] std::string describeFunction(std::uint16_t index);

    /// One subrecord of a record's data.
    struct SubrecordView
    {
        std::uint32_t mType = 0; ///< FourCC as stored (little endian)
        std::span<const std::byte> mData;
    };

    /// Calls `visit` for each subrecord of a record's (inflated) data, honouring XXXX (a 4 byte size for the next
    /// subrecord). False if the data is malformed (a size past the end): the subrecords before the fault were visited.
    [[nodiscard]] bool forEachSubrecord(
        std::span<const std::byte> data, const std::function<void(const SubrecordView&)>& visit);

    // ---- coverage -----------------------------------------------------------------------------------------------

    struct CoveragePoint
    {
        double mFraction = 0.0; ///< as asked
        std::size_t mItems = 0; ///< how many of the most used items reach it
    };

    /// For counts in any order: the smallest number of the biggest items whose sum reaches fraction * total, per
    /// fraction (clamped to 0..1). No items or a zero total gives 0.
    [[nodiscard]] std::vector<CoveragePoint> coverageCurve(
        std::vector<std::uint64_t> counts, std::span<const double> fractions);

    /// The fractions the research note reports (0.5, 0.8, 0.9, 0.95, 0.99).
    [[nodiscard]] std::span<const double> standardFractions();

    // ---- the condition survey -----------------------------------------------------------------------------------

    /// What the values of one parameter slot looked like across all uses of a function.
    struct ParamStats
    {
        std::uint64_t mZero = 0;
        std::uint64_t mResolved = 0; ///< non-zero and a form id the lookup knows
        std::uint64_t mSmall = 0; ///< non-zero, not resolved, at most 0xFFFF
        std::uint64_t mOther = 0;
        std::map<std::uint32_t, std::uint64_t> mResolvedType; ///< record type (FourCC) of the resolved ids
        /// The values of the "small" kind (at most 64 distinct values are kept; later new ones count in mSmall only).
        std::map<std::uint32_t, std::uint64_t> mSmallValues;

        [[nodiscard]] std::uint64_t total() const { return mZero + mResolved + mSmall + mOther; }
    };

    struct FunctionStats
    {
        std::uint64_t mUses = 0;
        std::map<std::uint32_t, std::uint64_t> mByRecordType;
        std::map<std::uint32_t, std::uint64_t> mRunOn;
        std::map<std::uint8_t, std::uint64_t> mCompare;
        std::uint64_t mUseGlobal = 0; ///< flag 0x04 (RECALLED-VERIFY)
        std::uint64_t mOrFlag = 0; ///< flag 0x01
        ParamStats mParam1, mParam2, mParam3, mReference;
    };

    /// Maps a form id as stored in the current plugin to the FourCC of the record it names, if it is known.
    using FormTypeLookup = std::function<std::optional<std::uint32_t>(std::uint32_t storedFormId)>;

    class ConditionSurvey
    {
    public:
        /// Set before the records of each plugin (the stored id's top byte is the plugin's master index). Optional.
        void setFormTypeLookup(FormTypeLookup lookup) { mLookup = std::move(lookup); }

        /// Counts one record: `data` is its (inflated) bytes after the record header.
        void addRecord(std::uint32_t recordType, std::span<const std::byte> data);

        std::uint64_t mRecords = 0;
        std::uint64_t mRecordsMalformed = 0; ///< subrecord framing failed (the records before the fault are counted)
        std::uint64_t mConditions = 0; ///< decoded CTDA subrecords
        std::uint64_t mConditionsOddSize = 0; ///< CTDA subrecords of another size than kCtdaSize (not decoded)
        std::uint64_t mPaddingNonZero = 0;
        std::uint64_t mPaddingAfterTypeNonZero = 0;
        std::uint64_t mPaddingAfterFunctionNonZero = 0;
        std::uint64_t mNonFiniteComparison = 0;
        std::uint64_t mStringParams = 0; ///< CIS1 and CIS2 subrecords (string parameters of a condition)
        std::uint64_t mCtdtSubrecords = 0; ///< the older CTDT form, if any
        std::map<std::uint32_t, std::uint64_t> mRecordsByType;
        std::map<std::uint32_t, std::uint64_t> mRecordsWithConditions;
        std::map<std::uint32_t, std::uint64_t> mConditionsByRecordType;
        std::map<std::size_t, std::uint64_t> mCtdaSizes;
        std::map<std::uint8_t, std::uint64_t> mCompare;
        std::map<std::uint8_t, std::uint64_t> mFlags;
        std::map<std::uint32_t, std::uint64_t> mRunOn;
        std::map<std::uint16_t, FunctionStats> mFunctions;

        /// (function index, uses), most used first, ties by index.
        [[nodiscard]] std::vector<std::pair<std::uint16_t, std::uint64_t>> functionsByUses() const;
        [[nodiscard]] std::vector<CoveragePoint> functionCoverage(std::span<const double> fractions) const;

    private:
        void classify(ParamStats& stats, std::uint32_t value) const;
        FormTypeLookup mLookup;
    };

    /// FourCC as text ("INFO").
    [[nodiscard]] std::string fourCCText(std::uint32_t type);

    /// Writes aggregate lines, each starting "SKYRIMSE-SURVEY conditions:". `topFunctions` limits the per-function
    /// lines. Never prints a record name, id or path (it holds none).
    void writeConditionReport(const ConditionSurvey& survey, std::ostream& out, std::size_t topFunctions);
}

#endif
