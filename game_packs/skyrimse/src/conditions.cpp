#include <game_skyrimse/conditions.hpp>

#include <array>

namespace game_skyrimse::conditions
{
    namespace
    {
        // "CTDA" as stored (little endian).
        constexpr std::uint32_t kCtda = 'C' | ('T' << 8) | ('D' << 16) | (static_cast<std::uint32_t>('A') << 24);
        constexpr std::uint8_t kFlagOr = 0x01;
        constexpr std::uint8_t kFlagUseGlobal = 0x04;

        float getIsId(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 && world.hasBaseId(subject, c.mParam1) ? 1.f : 0.f;
        }

        float getStage(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.questStage(c.mParam1);
        }

        float getStageDone(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.questStageDone(c.mParam1, c.mParam2) ? 1.f : 0.f;
        }

        float getInFaction(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 && world.inFaction(subject, c.mParam1) ? 1.f : 0.f;
        }

        float getGlobalValue(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.globalValue(c.mParam1);
        }

        struct Entry
        {
            std::uint16_t mIndex;
            ConditionFunction mFunction;
        };

        // Upstream OpenMW's names (survey_names.cpp): 72 GetIsID, 58 GetStage, 71 GetInFaction, 59 GetStageDone,
        // 74 GetGlobalValue. RECALLED-VERIFY.
        constexpr std::array<Entry, 5> sTable = { {
            { 58, getStage },
            { 59, getStageDone },
            { 71, getInFaction },
            { 72, getIsId },
            { 74, getGlobalValue },
        } };
    }

    bool decodeConditions(std::span<const std::byte> recordData, std::vector<survey::Ctda>& out)
    {
        bool bad = false;
        const bool wellFormed = survey::forEachSubrecord(recordData, [&](const survey::SubrecordView& sub) {
            if (sub.mType != kCtda)
                return;
            survey::Ctda c;
            if (survey::decodeCtda(sub.mData, c))
                out.push_back(c);
            else
                bad = true;
        });
        return wellFormed && !bad;
    }

    ConditionFunction findFunction(std::uint16_t index)
    {
        for (const Entry& e : sTable)
            if (e.mIndex == index)
                return e.mFunction;
        return nullptr;
    }

    bool compareValues(std::uint8_t op, float lhs, float rhs, bool& valid)
    {
        valid = true;
        switch (op)
        {
            case 0:
                return lhs == rhs;
            case 1:
                return lhs != rhs;
            case 2:
                return lhs > rhs;
            case 3:
                return lhs >= rhs;
            case 4:
                return lhs < rhs;
            case 5:
                return lhs <= rhs;
            default:
                valid = false;
                return false;
        }
    }

    bool Evaluator::evaluate(std::span<const survey::Ctda> list, const ConditionWorld& world)
    {
        ++mLists;
        bool failedClosed = false;
        bool allClausesTrue = true;
        bool clauseTrue = false;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const survey::Ctda& c = list[i];
            ++mConditions;
            bool result = false;
            if (const ConditionFunction function = findFunction(c.mFunction))
            {
                const Subject subject = world.resolveRunOn(c.mRunOn, c.mReference);
                const float lhs = function(c, subject, world);
                const float rhs
                    = (c.mFlags & kFlagUseGlobal) != 0 ? world.globalValue(c.mComparisonBits) : c.mComparison;
                bool valid = true;
                result = compareValues(c.mCompare, lhs, rhs, valid);
                if (!valid)
                {
                    ++mConditionsBadCompare;
                    failedClosed = true;
                }
            }
            else
            {
                ++mConditionsUnknown;
                if (mUnknownFunctions[c.mFunction]++ == 0 && mLog)
                    mLog("conditions: unknown function " + survey::functionName(c.mFunction)
                        + ", lists using it evaluate to false");
                failedClosed = true;
            }
            clauseTrue = clauseTrue || result;
            if ((c.mFlags & kFlagOr) == 0 || i + 1 == list.size())
            {
                allClausesTrue = allClausesTrue && clauseTrue;
                clauseTrue = false;
            }
        }
        if (failedClosed)
            ++mListsFailedClosed;
        return !failedClosed && allClausesTrue;
    }
}
