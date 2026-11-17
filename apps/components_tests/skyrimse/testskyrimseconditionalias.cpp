// Task 0276: GetDistance with the "use aliases" flag (0x02): the parameter is a quest alias id, not a form id (the
// wedding scene's first phase: the player within 1000 units of the Altar alias).
#include <game_skyrimse/conditions.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>

namespace
{
    using namespace game_skyrimse;
    using conditions::ConditionWorld;
    using conditions::Evaluator;
    using conditions::Subject;
    using survey::Ctda;

    constexpr Subject kPlayerSubject = 7;

    class AliasWorld final : public ConditionWorld
    {
    public:
        Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return kPlayerSubject; }
        bool hasBaseId(Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
        float distanceTo(Subject, std::uint32_t) const override { return 5000.f; } // the form id reading
        float distanceToAlias(Subject s, std::uint32_t alias) const override
        {
            return s == kPlayerSubject && alias == 15 ? 411.f : std::numeric_limits<float>::max();
        }
    };

    Ctda distanceCondition(std::uint8_t compare, float value, std::uint32_t param1, std::uint8_t flags)
    {
        Ctda c;
        c.mCompare = compare;
        c.mFlags = flags;
        c.mTypeByte = static_cast<std::uint8_t>((compare << 5) | flags);
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, 4);
        c.mFunction = 1;
        c.mParam1 = param1;
        return c;
    }

    bool run(const Ctda& c)
    {
        AliasWorld world;
        Evaluator evaluator;
        const Ctda list[] = { c };
        return evaluator.evaluate(list, world);
    }

    constexpr std::uint8_t kLe = 5;

    TEST(SkyrimConditionAlias, GetDistance_with_the_alias_flag_asks_the_alias_not_the_form)
    {
        EXPECT_TRUE(run(distanceCondition(kLe, 1000.f, 15, 0x02))) << "the alias is 411 away";
        EXPECT_FALSE(run(distanceCondition(kLe, 1000.f, 14, 0x02))) << "another alias is far";
    }

    TEST(SkyrimConditionAlias, GetDistance_without_the_flag_still_reads_a_form)
    {
        EXPECT_FALSE(run(distanceCondition(kLe, 1000.f, 15, 0))) << "the form id reading says 5000";
        EXPECT_TRUE(run(distanceCondition(kLe, 6000.f, 15, 0)));
    }
}
