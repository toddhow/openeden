#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/survey.hpp>
#include <game_skyrimse/triggers.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

// Task 0328: a box trigger turned about X and Y as well as Z. MQ105SetStage20 is placed with the rotation
// (pi/2, pi/2, 0): hand-worked below, its three bounds lie along world Y, world Z and world X.

namespace
{
    using namespace game_skyrimse;

    constexpr double kPi = 3.14159265358979323846;

    TriggerVolume box(double bx, double by, double bz, double rx, double ry, double rz)
    {
        TriggerVolume v;
        v.mType = kPrimitiveBox;
        v.mBounds[0] = bx;
        v.mBounds[1] = by;
        v.mBounds[2] = bz;
        v.mRotationX = rx;
        v.mRotationY = ry;
        v.mRotationZ = rz;
        v.mAcceptBox = true;
        return v;
    }

    // Local x -> world Y, local y -> world Z, local z -> world X (Skyrim turns about Z first, then Y, then X).
    TEST(SkyrimTriggerBox328, aBoxTurnedAQuarterAboutXAndYHasItsBoundsOnTheOtherAxes)
    {
        const TriggerVolume v = box(375.0, 326.0, 1150.0, kPi / 2, kPi / 2, 0.0);
        EXPECT_TRUE(volumeContains(v, 1100, 0, 0)); // along world X: the long bound
        EXPECT_TRUE(volumeContains(v, -1100, 0, 0));
        EXPECT_FALSE(volumeContains(v, 1200, 0, 0));
        EXPECT_TRUE(volumeContains(v, 0, 370, 0)); // along world Y: the first bound
        EXPECT_FALSE(volumeContains(v, 0, 380, 0));
        EXPECT_TRUE(volumeContains(v, 0, 0, 320)); // along world Z: the second bound
        EXPECT_FALSE(volumeContains(v, 0, 0, 330));
    }

    TEST(SkyrimTriggerBox328, aRotationAboutZAloneIsAsBefore)
    {
        const TriggerVolume v = box(100.0, 10.0, 10.0, 0.0, 0.0, kPi / 2);
        EXPECT_TRUE(volumeContains(v, 0, -90, 0));
        EXPECT_FALSE(volumeContains(v, 90, 0, 0));
    }

    TEST(SkyrimTriggerBox328, aNonFiniteTurnIsNotSupported)
    {
        EXPECT_FALSE(triggerSupported(box(1, 1, 1, std::nan(""), 0.0, 0.0)));
        EXPECT_FALSE(triggerSupported(box(1, 1, 1, 0.0, std::nan(""), 0.0)));
    }

    // MQ105PushTrialScene's phase 1 completes on GetInSameCell(Target = alias 33) run on the Player alias: the
    // condition's flag "use aliases" makes its parameter an alias id, not a reference.
    class SameCellWorld final : public conditions::ConditionWorld
    {
    public:
        conditions::Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 7; }
        bool hasBaseId(conditions::Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0.f; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(conditions::Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0.f; }
        bool inSameCell(conditions::Subject, std::uint32_t reference) const override { return reference == 99; }
        bool inSameCellAlias(conditions::Subject, std::uint32_t alias) const override { return alias == 33; }
    };

    survey::Ctda sameCell(std::uint32_t param1, std::uint8_t flags)
    {
        survey::Ctda c;
        c.mCompare = 0;
        c.mFlags = flags;
        c.mTypeByte = static_cast<std::uint8_t>(flags);
        c.mComparison = 1.f;
        const float one = 1.f;
        std::memcpy(&c.mComparisonBits, &one, 4);
        c.mFunction = 32;
        c.mParam1 = param1;
        c.mRunOn = 5;
        return c;
    }

    TEST(SkyrimConditionSameCell328, theAliasFlagMakesTheParameterAnAliasAndNoFlagKeepsTheReference)
    {
        const SameCellWorld world;
        conditions::Evaluator evaluator;
        EXPECT_TRUE(evaluator.evaluate(std::vector<survey::Ctda>{ sameCell(33, 0x02) }, world));
        EXPECT_FALSE(evaluator.evaluate(std::vector<survey::Ctda>{ sameCell(99, 0x02) }, world));
        EXPECT_TRUE(evaluator.evaluate(std::vector<survey::Ctda>{ sameCell(99, 0x00) }, world));
        EXPECT_FALSE(evaluator.evaluate(std::vector<survey::Ctda>{ sameCell(33, 0x00) }, world));
    }
}
