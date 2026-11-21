#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// Task 0349: MQ106IntroScene's phase 6 waits for GetOpenState(RiverwoodInnDelphineRoomDoor) == 3 (the door is closed)
// AND the bedroom trigger's variable. GetOpenState is function 157: 1 open, 3 closed (task 0289's values), run on a
// reference.

namespace
{
    using namespace game_skyrimse;

    class DoorWorld final : public conditions::ConditionWorld
    {
    public:
        conditions::Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 7; }
        bool hasBaseId(conditions::Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(conditions::Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
        int openState(conditions::Subject subject) const override { return subject == 7 ? mState : 0; }
        int mState = 3;
    };

    survey::Ctda openStateIs(float value)
    {
        survey::Ctda c;
        c.mCompare = 0; // ==
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, 4);
        c.mFunction = 157;
        c.mRunOn = 2;
        return c;
    }

    TEST(SkyrimOpenState349, aClosedDoorIsState3AndAnOpenOneState1)
    {
        DoorWorld world;
        conditions::Evaluator evaluator;
        EXPECT_TRUE(evaluator.evaluate(std::vector<survey::Ctda>{ openStateIs(3.f) }, world));
        EXPECT_FALSE(evaluator.evaluate(std::vector<survey::Ctda>{ openStateIs(1.f) }, world));
        world.mState = 1;
        EXPECT_TRUE(evaluator.evaluate(std::vector<survey::Ctda>{ openStateIs(1.f) }, world));
        EXPECT_FALSE(evaluator.evaluate(std::vector<survey::Ctda>{ openStateIs(3.f) }, world));
    }
}
