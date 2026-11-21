// Task 0311 (Hearthfire): the condition debug aid. A sink and a quest id: every condition of a list that names the
// quest is reported with its value, its compared value and its answer; other lists and a cleared sink report nothing.
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/houses.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::conditions;
    using game_skyrimse::survey::Ctda;

    Ctda stageCond(std::uint32_t quest, float value)
    {
        Ctda c;
        c.mCompare = 0;
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, 4);
        c.mFunction = 58; // GetStage
        c.mParam1 = quest;
        return c;
    }

    class StageWorld final : public ConditionWorld
    {
    public:
        Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 1; }
        bool hasBaseId(Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 30.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
    };

    TEST(SkyrimConditionDebug, aListThatNamesTheQuestIsReportedConditionByCondition)
    {
        std::vector<std::string> lines;
        setConditionDebug(0x0300305E, [&lines](const std::string& line) { lines.push_back(line); });
        StageWorld world;
        Evaluator eval;
        // the same quest through another plugin index (the low 24 bits name it) and a list that does not
        const std::vector<Ctda> named{ stageCond(0x0100305E, 30.f), stageCond(0x0100305E, 40.f) };
        const std::vector<Ctda> other{ stageCond(0x0100999, 30.f) };
        EXPECT_FALSE(eval.evaluate(named, world));
        EXPECT_TRUE(eval.evaluate(other, world));
        ASSERT_EQ(lines.size(), 3u); // a line for the list, then one for each condition
        EXPECT_NE(lines[0].find("LIST of 2"), std::string::npos) << lines[0];
        EXPECT_NE(lines[1].find("true"), std::string::npos) << lines[1];
        EXPECT_NE(lines[2].find("false"), std::string::npos) << lines[2];
        EXPECT_NE(lines[1].find("value 30"), std::string::npos) << lines[1];
        setConditionDebug(0, {});
        lines.clear();
        EXPECT_FALSE(eval.evaluate(named, world));
        EXPECT_TRUE(lines.empty());
    }

    TEST(SkyrimHearthfireData, theTestAidBefriendsTheFalkreathJarlAndSteward)
    {
        const std::vector<game_skyrimse::FormId> bases = game_skyrimse::houses::testPermissionHearthfireBases();
        ASSERT_EQ(bases.size(), 2u);
        EXPECT_EQ(bases[0], 0x13653u); // Siddgeir
        EXPECT_EQ(bases[1], 0x13659u); // Nenya
    }
}
