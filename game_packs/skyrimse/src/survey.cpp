#include <game_skyrimse/survey.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <ostream>
#include <type_traits>

namespace game_skyrimse::survey
{
    namespace
    {
        constexpr std::uint32_t fourCC(const char (&n)[5])
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(n[0]))
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(n[1])) << 8)
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(n[2])) << 16)
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(n[3])) << 24);
        }

        template <class T>
        T readAt(std::span<const std::byte> data, std::size_t offset)
        {
            T value{};
            std::memcpy(&value, data.data() + offset, sizeof(T));
            return value;
        }

        constexpr std::array<double, 5> sFractions = { 0.5, 0.8, 0.9, 0.95, 0.99 };
    }

    bool decodeCtda(std::span<const std::byte> data, Ctda& out)
    {
        if (data.size() != kCtdaSize)
            return false;
        Ctda c;
        c.mTypeByte = readAt<std::uint8_t>(data, 0);
        c.mCompare = static_cast<std::uint8_t>(c.mTypeByte >> 5);
        c.mFlags = static_cast<std::uint8_t>(c.mTypeByte & 0x1F);
        c.mComparisonBits = readAt<std::uint32_t>(data, 4);
        std::memcpy(&c.mComparison, &c.mComparisonBits, 4);
        c.mFunction = readAt<std::uint16_t>(data, 8);
        c.mParam1 = readAt<std::uint32_t>(data, 12);
        c.mParam2 = readAt<std::uint32_t>(data, 16);
        c.mRunOn = readAt<std::uint32_t>(data, 20);
        c.mReference = readAt<std::uint32_t>(data, 24);
        c.mParam3 = readAt<std::uint32_t>(data, 28);
        for (std::size_t i : { 1u, 2u, 3u })
            c.mPaddingAfterTypeNonZero = c.mPaddingAfterTypeNonZero || data[i] != std::byte{ 0 };
        for (std::size_t i : { 10u, 11u })
            c.mPaddingAfterFunctionNonZero = c.mPaddingAfterFunctionNonZero || data[i] != std::byte{ 0 };
        c.mPaddingNonZero = c.mPaddingAfterTypeNonZero || c.mPaddingAfterFunctionNonZero;
        out = c;
        return true;
    }

    std::string functionName(std::uint16_t index)
    {
        return "fn_" + std::to_string(index);
    }

    std::string describeFunction(std::uint16_t index)
    {
        const std::string_view name = upstreamFunctionName(index);
        return name.empty() ? functionName(index) : functionName(index) + " (" + std::string(name) + "?)";
    }

    bool forEachSubrecord(std::span<const std::byte> data, const std::function<void(const SubrecordView&)>& visit)
    {
        constexpr std::uint32_t xxxx = fourCC("XXXX");
        std::size_t pos = 0;
        std::optional<std::uint32_t> bigSize;
        while (pos < data.size())
        {
            if (data.size() - pos < 6)
                return false;
            const std::uint32_t type = readAt<std::uint32_t>(data, pos);
            const std::uint16_t smallSize = readAt<std::uint16_t>(data, pos + 4);
            pos += 6;
            if (type == xxxx)
            {
                if (smallSize != 4 || data.size() - pos < 4)
                    return false;
                bigSize = readAt<std::uint32_t>(data, pos);
                pos += 4;
                continue;
            }
            const std::uint64_t size = bigSize.has_value() ? *bigSize : smallSize;
            bigSize.reset();
            if (size > data.size() - pos)
                return false;
            visit(SubrecordView{ type, data.subspan(pos, static_cast<std::size_t>(size)) });
            pos += static_cast<std::size_t>(size);
        }
        return true;
    }

    std::vector<CoveragePoint> coverageCurve(std::vector<std::uint64_t> counts, std::span<const double> fractions)
    {
        std::sort(counts.begin(), counts.end(), std::greater<>());
        std::uint64_t total = 0;
        for (std::uint64_t c : counts)
            total += c;
        std::vector<CoveragePoint> result;
        result.reserve(fractions.size());
        for (double fraction : fractions)
        {
            CoveragePoint point;
            point.mFraction = fraction;
            const double clamped = std::clamp(fraction, 0.0, 1.0);
            if (total != 0 && clamped > 0.0)
            {
                const double need = clamped * static_cast<double>(total);
                double sum = 0;
                std::size_t items = 0;
                for (std::uint64_t c : counts)
                {
                    sum += static_cast<double>(c);
                    ++items;
                    // a small tolerance so that exactly 50 of 100 reaches 0.5 despite rounding
                    if (sum + 1e-9 >= need)
                        break;
                }
                point.mItems = items;
            }
            result.push_back(point);
        }
        return result;
    }

    std::span<const double> standardFractions()
    {
        return sFractions;
    }

    void ConditionSurvey::classify(ParamStats& stats, std::uint32_t value) const
    {
        if (value == 0)
        {
            ++stats.mZero;
            return;
        }
        if (mLookup)
        {
            if (const std::optional<std::uint32_t> type = mLookup(value); type.has_value())
            {
                ++stats.mResolved;
                ++stats.mResolvedType[*type];
                return;
            }
        }
        if (value <= 0xFFFFu)
        {
            ++stats.mSmall;
            if (stats.mSmallValues.size() < 64 || stats.mSmallValues.count(value) != 0)
                ++stats.mSmallValues[value];
        }
        else
            ++stats.mOther;
    }

    void ConditionSurvey::addRecord(std::uint32_t recordType, std::span<const std::byte> data)
    {
        constexpr std::uint32_t tCtda = fourCC("CTDA"), tCis1 = fourCC("CIS1"), tCis2 = fourCC("CIS2"),
                                tCtdt = fourCC("CTDT");
        ++mRecords;
        ++mRecordsByType[recordType];
        bool any = false;
        const bool wellFormed = forEachSubrecord(data, [&](const SubrecordView& sub) {
            if (sub.mType == tCis1 || sub.mType == tCis2)
            {
                ++mStringParams;
            }
            else if (sub.mType == tCtdt)
            {
                ++mCtdtSubrecords;
            }
            else if (sub.mType == tCtda)
            {
                any = true;
                ++mCtdaSizes[sub.mData.size()];
                Ctda c;
                if (!decodeCtda(sub.mData, c))
                {
                    ++mConditionsOddSize;
                    return;
                }
                ++mConditions;
                ++mConditionsByRecordType[recordType];
                ++mCompare[c.mCompare];
                ++mFlags[c.mFlags];
                ++mRunOn[c.mRunOn];
                if (c.mPaddingNonZero)
                    ++mPaddingNonZero;
                if (c.mPaddingAfterTypeNonZero)
                    ++mPaddingAfterTypeNonZero;
                if (c.mPaddingAfterFunctionNonZero)
                    ++mPaddingAfterFunctionNonZero;
                const bool useGlobal = (c.mFlags & 0x04) != 0;
                if (!useGlobal && !std::isfinite(c.mComparison))
                    ++mNonFiniteComparison;
                FunctionStats& f = mFunctions[c.mFunction];
                ++f.mUses;
                ++f.mByRecordType[recordType];
                ++f.mRunOn[c.mRunOn];
                ++f.mCompare[c.mCompare];
                if (useGlobal)
                    ++f.mUseGlobal;
                if ((c.mFlags & 0x01) != 0)
                    ++f.mOrFlag;
                classify(f.mParam1, c.mParam1);
                classify(f.mParam2, c.mParam2);
                classify(f.mParam3, c.mParam3);
                classify(f.mReference, c.mReference);
            }
        });
        if (!wellFormed)
            ++mRecordsMalformed;
        if (any)
            ++mRecordsWithConditions[recordType];
    }

    std::vector<std::pair<std::uint16_t, std::uint64_t>> ConditionSurvey::functionsByUses() const
    {
        std::vector<std::pair<std::uint16_t, std::uint64_t>> result;
        result.reserve(mFunctions.size());
        for (const auto& [index, stats] : mFunctions)
            result.emplace_back(index, stats.mUses);
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        return result;
    }

    std::vector<CoveragePoint> ConditionSurvey::functionCoverage(std::span<const double> fractions) const
    {
        std::vector<std::uint64_t> counts;
        counts.reserve(mFunctions.size());
        for (const auto& entry : mFunctions)
            counts.push_back(entry.second.mUses);
        return coverageCurve(std::move(counts), fractions);
    }

    std::string fourCCText(std::uint32_t type)
    {
        std::string text(4, '?');
        for (int i = 0; i < 4; ++i)
        {
            const unsigned char c = static_cast<unsigned char>((type >> (8 * i)) & 0xFF);
            if (c >= 0x20 && c < 0x7F)
                text[i] = static_cast<char>(c);
        }
        return text;
    }

    namespace
    {
        template <class Map>
        std::string joinCounts(const Map& map, std::size_t limit = 6)
        {
            std::vector<std::pair<std::string, std::uint64_t>> rows;
            for (const auto& [key, count] : map)
            {
                if constexpr (std::is_same_v<typename Map::key_type, std::uint32_t>)
                    rows.emplace_back(fourCCText(key), count);
                else
                    rows.emplace_back(std::to_string(static_cast<std::uint64_t>(key)), count);
            }
            std::stable_sort(
                rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            std::string text;
            for (std::size_t i = 0; i < rows.size() && i < limit; ++i)
                text += (i ? ", " : "") + rows[i].first + " " + std::to_string(rows[i].second);
            return text.empty() ? "-" : text;
        }

        std::string runOnText(const std::map<std::uint32_t, std::uint64_t>& map)
        {
            std::string text;
            for (const auto& [key, count] : map)
                text += (text.empty() ? "" : ", ") + std::to_string(key) + " " + std::to_string(count);
            return text.empty() ? "-" : text;
        }

        std::string paramText(const ParamStats& p)
        {
            std::string text = "zero " + std::to_string(p.mZero) + " form " + std::to_string(p.mResolved);
            if (!p.mResolvedType.empty())
                text += " (" + joinCounts(p.mResolvedType, 3) + ")";
            text += " small " + std::to_string(p.mSmall);
            if (!p.mSmallValues.empty())
            {
                std::vector<std::pair<std::uint32_t, std::uint64_t>> top(p.mSmallValues.begin(), p.mSmallValues.end());
                std::stable_sort(
                    top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                text += " (";
                for (std::size_t i = 0; i < top.size() && i < 3; ++i)
                    text += (i ? ", " : "") + std::to_string(top[i].first) + " x" + std::to_string(top[i].second);
                text += ")";
            }
            text += " other " + std::to_string(p.mOther);
            return text;
        }
    }

    void writeConditionReport(const ConditionSurvey& s, std::ostream& out, std::size_t topFunctions)
    {
        const std::string tag = "SKYRIMSE-SURVEY conditions: ";
        out << tag << "records " << s.mRecords << ", malformed " << s.mRecordsMalformed << ", conditions "
            << s.mConditions << ", odd-size " << s.mConditionsOddSize << ", distinct functions " << s.mFunctions.size()
            << "\n";
        out << tag << "ctda sizes: " << joinCounts(s.mCtdaSizes, 10) << "\n";
        out << tag << "compare operators (type byte >> 5): " << joinCounts(s.mCompare, 10) << "\n";
        out << tag << "flags (type byte & 0x1f): " << joinCounts(s.mFlags, 16) << "\n";
        out << tag << "run-on: " << runOnText(s.mRunOn) << "\n";
        out << tag << "padding non-zero " << s.mPaddingNonZero << " (bytes 1-3: " << s.mPaddingAfterTypeNonZero
            << ", bytes 10-11: " << s.mPaddingAfterFunctionNonZero << "), non-finite comparison "
            << s.mNonFiniteComparison << ", string params (CIS1/CIS2) " << s.mStringParams << ", CTDT "
            << s.mCtdtSubrecords << "\n";
        for (const auto& [type, count] : s.mConditionsByRecordType)
        {
            const auto has = s.mRecordsWithConditions.find(type);
            const auto total = s.mRecordsByType.find(type);
            out << tag << "record type " << fourCCText(type) << ": conditions " << count << ", records with conditions "
                << (has != s.mRecordsWithConditions.end() ? has->second : 0) << " of "
                << (total != s.mRecordsByType.end() ? total->second : 0) << "\n";
        }
        // per record type: how many distinct functions it uses, how many cover 90 percent of its conditions, the top
        // five
        for (const auto& [type, count] : s.mConditionsByRecordType)
        {
            std::vector<std::pair<std::uint16_t, std::uint64_t>> uses;
            for (const auto& [index, stats] : s.mFunctions)
            {
                const auto it = stats.mByRecordType.find(type);
                if (it != stats.mByRecordType.end())
                    uses.emplace_back(index, it->second);
            }
            std::stable_sort(uses.begin(), uses.end(), [](const auto& a, const auto& b) {
                return a.second != b.second ? a.second > b.second : a.first < b.first;
            });
            std::vector<std::uint64_t> counts;
            for (const auto& u : uses)
                counts.push_back(u.second);
            const double ninety[] = { 0.9 };
            out << tag << "record type " << fourCCText(type) << " functions: distinct " << uses.size()
                << ", for 90 percent " << coverageCurve(counts, ninety)[0].mItems << ", top";
            for (std::size_t i = 0; i < uses.size() && i < 5; ++i)
                out << " " << functionName(uses[i].first) << " " << uses[i].second;
            out << "\n";
        }
        for (const CoveragePoint& p : s.functionCoverage(standardFractions()))
            out << tag << "coverage " << static_cast<int>(p.mFraction * 100.0 + 0.5) << " percent: " << p.mItems
                << " functions\n";
        std::size_t rank = 0;
        for (const auto& [index, uses] : s.functionsByUses())
        {
            if (rank++ >= topFunctions)
                break;
            const FunctionStats& f = s.mFunctions.at(index);
            out << tag << "function #" << rank << " " << describeFunction(index) << " uses " << uses << " records ["
                << joinCounts(f.mByRecordType, 4) << "] run-on [" << runOnText(f.mRunOn) << "] compare ["
                << joinCounts(f.mCompare, 6) << "] global " << f.mUseGlobal << " or " << f.mOrFlag << " p1 ["
                << paramText(f.mParam1) << "] p2 [" << paramText(f.mParam2) << "] p3 [" << paramText(f.mParam3)
                << "] ref [" << paramText(f.mReference) << "]\n";
        }
    }
}
