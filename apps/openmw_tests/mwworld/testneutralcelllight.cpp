// Task 0355 stages f and j: the interior sun, the clip distance and the light fade a game pack's interior asks for
// (apps/openmw/mwworld/neutralcelllight.hpp). Written before the code; the first run was against stubs. Every expected
// number is written out by hand.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "apps/openmw/mwworld/neutralcelllight.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::neutralClipDistance;
    using MWWorld::neutralInteriorSun;
    using MWWorld::neutralLightFade;

    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

    CellLightSettings settings(double xy, double z, double fade)
    {
        CellLightSettings s;
        s.rotationXY = xy;
        s.rotationZ = z;
        s.directionalFade = fade;
        return s;
    }

    void expectDirection(const MWWorld::NeutralInteriorSun& sun, float x, float y, float z)
    {
        EXPECT_NEAR(sun.towardLight[0], x, 1e-6f);
        EXPECT_NEAR(sun.towardLight[1], y, 1e-6f);
        EXPECT_NEAR(sun.towardLight[2], z, 1e-6f);
    }

    // ---- the sun -----------------------------------------------------------------------------------------------

    TEST(NeutralInteriorSun, nothingWhenOffOrWithNoSettings)
    {
        const CellLightSettings s = settings(0, 90, 1);
        EXPECT_FALSE(neutralInteriorSun(&s, false).has_value());
        EXPECT_FALSE(neutralInteriorSun(nullptr, true).has_value());
    }

    TEST(NeutralInteriorSun, theTemplatesDefaultRotationIsTheLightStraightAbove)
    {
        // The lighting templates' own default is XY 0, Z 90 (28 + 12 + ... of 126 LGTM records of the masters): above.
        const CellLightSettings s = settings(0, 90, 1);
        const auto sun = neutralInteriorSun(&s, true);
        ASSERT_TRUE(sun.has_value());
        expectDirection(*sun, 0.f, 0.f, 1.f);
        EXPECT_FLOAT_EQ(sun->intensity, 1.f);
    }

    TEST(NeutralInteriorSun, rotationZeroZeroIsNotSetAndKeepsTheEnginesOldDirectionFromAbove)
    {
        // toddhow, 2026-11-19: (0, 0) is the "not set" case (349 cells): the old fixed (-0.15, 0.15, 1), normalised.
        const float length = std::sqrt(0.15f * 0.15f + 0.15f * 0.15f + 1.f);
        const CellLightSettings unset = settings(0, 0, 1);
        expectDirection(*neutralInteriorSun(&unset, true), -0.15f / length, 0.15f / length, 1.f / length);
        const CellLightSettings unsetFade = settings(0, 0, 0.5);
        const auto sun = neutralInteriorSun(&unsetFade, true);
        expectDirection(*sun, -0.15f / length, 0.15f / length, 1.f / length);
        EXPECT_FLOAT_EQ(sun->intensity, 0.5f);
        // Any non-zero angle is the cell's own.
        const CellLightSettings headingOnly = settings(90, 0, 1);
        expectDirection(*neutralInteriorSun(&headingOnly, true), 1.f, 0.f, 0.f);
        const CellLightSettings elevationOnly = settings(0, 30, 1);
        expectDirection(*neutralInteriorSun(&elevationOnly, true), 0.f, 0.8660254f, 0.5f);
    }

    TEST(NeutralInteriorSun, zeroXyAtElevation90IsAboveAndZ270IsFromBelow)
    {
        const CellLightSettings below = settings(0, 270, 1);
        expectDirection(*neutralInteriorSun(&below, true), 0.f, 0.f, -1.f);
        const CellLightSettings glow = settings(270, 240, 1); // the lava and glow caves
        expectDirection(*neutralInteriorSun(&glow, true), 0.5f, 0.f, -0.8660254f);
    }

    TEST(NeutralInteriorSun, theHeadingTurnsClockwiseFromNorth)
    {
        const CellLightSettings east = settings(90, 0, 1);
        expectDirection(*neutralInteriorSun(&east, true), 1.f, 0.f, 0.f);
        const CellLightSettings south = settings(180, 0, 1);
        expectDirection(*neutralInteriorSun(&south, true), 0.f, -1.f, 0.f);
        const CellLightSettings tilted = settings(45, 30, 1);
        expectDirection(*neutralInteriorSun(&tilted, true), 0.6123724f, 0.6123724f, 0.5f);
    }

    TEST(NeutralInteriorSun, theDirectionIsAUnitVector)
    {
        for (double xy : { 0.0, 45.0, 200.0, 270.0, 359.0 })
            for (double z : { 0.0, 30.0, 90.0, 150.0, 240.0, 270.0 })
            {
                const CellLightSettings s = settings(xy, z, 1);
                const auto sun = neutralInteriorSun(&s, true);
                ASSERT_TRUE(sun.has_value());
                const float length = std::sqrt(sun->towardLight[0] * sun->towardLight[0]
                    + sun->towardLight[1] * sun->towardLight[1] + sun->towardLight[2] * sun->towardLight[2]);
                EXPECT_NEAR(length, 1.f, 1e-5f);
            }
    }

    TEST(NeutralInteriorSun, aFadeOfZeroIsTheFullLightAndOnlyBetweenZeroAndOneDimsIt)
    {
        const CellLightSettings none = settings(0, 90, 0);
        EXPECT_FLOAT_EQ(neutralInteriorSun(&none, true)->intensity, 1.f); // toddhow, 2026-11-19: fade 0 is the full light
        const CellLightSettings fifth = settings(0, 90, 0.2);
        EXPECT_FLOAT_EQ(neutralInteriorSun(&fifth, true)->intensity, 0.2f);
        const CellLightSettings five = settings(0, 90, 5);
        EXPECT_FLOAT_EQ(neutralInteriorSun(&five, true)->intensity, 1.f);
        const CellLightSettings negative = settings(0, 90, -2);
        EXPECT_FLOAT_EQ(neutralInteriorSun(&negative, true)->intensity, 0.f);
    }

    TEST(NeutralInteriorSun, numbersThatAreNotNumbersGiveNothing)
    {
        const CellLightSettings a = settings(kNan, 90, 1);
        EXPECT_FALSE(neutralInteriorSun(&a, true).has_value());
        const CellLightSettings b = settings(0, kNan, 1);
        EXPECT_FALSE(neutralInteriorSun(&b, true).has_value());
        const CellLightSettings c = settings(0, 90, kNan);
        EXPECT_FALSE(neutralInteriorSun(&c, true).has_value());
        const CellLightSettings d = settings(std::numeric_limits<double>::infinity(), 90, 1);
        EXPECT_FALSE(neutralInteriorSun(&d, true).has_value());
    }

    // ---- the clip distance -------------------------------------------------------------------------------------

    TEST(NeutralClipDistance, theCellsDistanceWhenItIsUsable)
    {
        CellLightSettings s;
        s.clipDistance = 12000.0;
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 12000.f);
        s.clipDistance = 1800.0; // the smallest of the five masters
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 1800.f);
    }

    TEST(NeutralClipDistance, noneWhenOffZeroTinyNegativeOrNotANumber)
    {
        CellLightSettings s;
        s.clipDistance = 12000.0;
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, false), 0.f);
        EXPECT_FLOAT_EQ(neutralClipDistance(nullptr, true), 0.f);
        s.clipDistance = 0.0;
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 0.f);
        s.clipDistance = -5.0;
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 0.f);
        s.clipDistance = 100.0; // under kMinNeutralClipDistance: the room would vanish
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 0.f);
        s.clipDistance = kNan;
        EXPECT_FLOAT_EQ(neutralClipDistance(&s, true), 0.f);
    }

    // ---- the light fade ----------------------------------------------------------------------------------------

    TEST(NeutralLightFade, theCellsTwoDistances)
    {
        CellLightSettings s;
        s.lightFadeBegin = 4000.0;
        s.lightFadeEnd = 5000.0;
        EXPECT_EQ(neutralLightFade(&s, true), (MWWorld::NeutralLightFade{ 4000.f, 5000.f }));
        s.lightFadeBegin = 100.0;
        s.lightFadeEnd = 50000.0;
        EXPECT_EQ(neutralLightFade(&s, true), (MWWorld::NeutralLightFade{ 100.f, 50000.f }));
    }

    TEST(NeutralLightFade, noneWhenOffOrEndIsNotPositiveOrNotANumber)
    {
        CellLightSettings s;
        s.lightFadeBegin = 4000.0;
        s.lightFadeEnd = 5000.0;
        EXPECT_EQ(neutralLightFade(&s, false), MWWorld::NeutralLightFade{});
        EXPECT_EQ(neutralLightFade(nullptr, true), MWWorld::NeutralLightFade{});
        s.lightFadeEnd = 0.0;
        EXPECT_EQ(neutralLightFade(&s, true), MWWorld::NeutralLightFade{});
        s.lightFadeEnd = -1.0;
        EXPECT_EQ(neutralLightFade(&s, true), MWWorld::NeutralLightFade{});
        s.lightFadeEnd = kNan;
        EXPECT_EQ(neutralLightFade(&s, true), MWWorld::NeutralLightFade{});
        s.lightFadeBegin = kNan;
        s.lightFadeEnd = 5000.0;
        EXPECT_EQ(neutralLightFade(&s, true), MWWorld::NeutralLightFade{});
    }

    TEST(NeutralLightFade, aNegativeBeginCountsAsZero)
    {
        CellLightSettings s;
        s.lightFadeBegin = -10.0;
        s.lightFadeEnd = 1000.0;
        EXPECT_EQ(neutralLightFade(&s, true), (MWWorld::NeutralLightFade{ 0.f, 1000.f }));
    }
}
