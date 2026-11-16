// Task 0257: a game's modifier (Night Eye) on top of the weather's image space. Render::ImageSpace::combine is pinned
// with numbers worked out by hand. Written before the code.
#include <gtest/gtest.h>

#include <components/render/imagespacemath.hpp>

#include <cmath>
#include <limits>

namespace
{
    using namespace Render::ImageSpace;

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    Modifier nightEye()
    {
        // The last key of Skyrim.esm's Night Eye intro (Update.esm's is milder; docs/tasks/0257-skyrim-night-eye.md).
        Modifier m;
        m.active = true;
        m.saturationMult = 0.5f;
        m.brightnessAdd = 1.1f;
        m.contrastAdd = 0.2f;
        m.tintAmount = 0.502f;
        m.tint = { 0.592f, 0.773f, 0.875f };
        return m;
    }

    TEST(RenderImageSpaceModifier, anInactiveModifierReturnsTheWeatherBitForBit)
    {
        Effective weather;
        weather.on = true;
        weather.stops = 0.37f;
        weather.grade.saturation = 0.8f;
        weather.grade.contrast = 1.1f;
        weather.grade.tintAmount = 0.3f;
        weather.grade.tint = { 0.2f, 0.4f, 0.6f };
        Modifier m = nightEye();
        m.active = false; // every number set, none may count
        const Effective out = combine(weather, m);
        EXPECT_EQ(out.on, weather.on);
        EXPECT_EQ(out.stops, weather.stops);
        EXPECT_EQ(out.grade.saturation, weather.grade.saturation);
        EXPECT_EQ(out.grade.contrast, weather.grade.contrast);
        EXPECT_EQ(out.grade.tintAmount, weather.grade.tintAmount);
        EXPECT_EQ(out.grade.tint, weather.grade.tint);
        const Effective none = combine(Effective{}, Modifier{});
        EXPECT_FALSE(none.on);
        EXPECT_EQ(none.stops, 0.f);
    }

    TEST(RenderImageSpaceModifier, aNeutralActiveModifierSwitchesTheGradeOnAndChangesNoValue)
    {
        Modifier m;
        m.active = true;
        const Effective out = combine(Effective{}, m);
        EXPECT_TRUE(out.on);
        EXPECT_FLOAT_EQ(out.stops, 0.f);
        EXPECT_FLOAT_EQ(out.grade.saturation, 1.f);
        EXPECT_FLOAT_EQ(out.grade.contrast, 1.f);
        EXPECT_FLOAT_EQ(out.grade.tintAmount, 0.f);
    }

    TEST(RenderImageSpaceModifier, nightEyeOnANeutralWeatherIsTheModifiersOwnNumbers)
    {
        const Effective out = combine(Effective{}, nightEye());
        EXPECT_TRUE(out.on);
        EXPECT_NEAR(out.grade.saturation, 0.5f, 1e-6f);
        EXPECT_NEAR(out.grade.contrast, 1.2f, 1e-6f);
        EXPECT_NEAR(out.stops, std::log2(2.1f), 1e-5f); // brightness 1 * 1 + 1.1
        EXPECT_NEAR(out.grade.tintAmount, 0.502f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[0], 0.592f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[1], 0.773f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[2], 0.875f, 1e-6f);
    }

    TEST(RenderImageSpaceModifier, theModifierActsOnWhatTheWeatherAlreadyGives)
    {
        Effective weather;
        weather.on = true;
        weather.stops = 0.5f; // brightness 2^0.5
        weather.grade.saturation = 0.8f;
        weather.grade.contrast = 1.5f;
        const Effective out = combine(weather, nightEye());
        EXPECT_NEAR(out.grade.saturation, 0.4f, 1e-6f); // 0.8 * 0.5 + 0
        EXPECT_NEAR(out.grade.contrast, 1.7f, 1e-6f); // 1.5 * 1 + 0.2
        EXPECT_NEAR(out.stops, std::log2(std::exp2(0.5f) + 1.1f), 1e-5f);
    }

    TEST(RenderImageSpaceModifier, exposureIsClampedToTwoStopsAndTheBrightnessFloor)
    {
        Modifier bright;
        bright.active = true;
        bright.brightnessAdd = 10.f;
        EXPECT_FLOAT_EQ(combine(Effective{}, bright).stops, 2.f);
        Modifier dark;
        dark.active = true;
        dark.brightnessMult = 0.f; // brightness 0: the floor 0.25 is -2 stops
        EXPECT_FLOAT_EQ(combine(Effective{}, dark).stops, -2.f);
    }

    TEST(RenderImageSpaceModifier, saturationAndContrastAreKeptInZeroToFour)
    {
        Modifier m;
        m.active = true;
        m.saturationAdd = -5.f;
        m.contrastAdd = 9.f;
        const Effective out = combine(Effective{}, m);
        EXPECT_FLOAT_EQ(out.grade.saturation, 0.f);
        EXPECT_FLOAT_EQ(out.grade.contrast, 4.f);
    }

    TEST(RenderImageSpaceModifier, tintsMixByTheirAmountsAndTheModifierCoversWhatItLeavesOfTheWeather)
    {
        Effective weather;
        weather.on = true;
        weather.grade.tintAmount = 0.4f;
        weather.grade.tint = { 1.f, 0.f, 0.f };
        Modifier m;
        m.active = true;
        m.tintAmount = 0.5f;
        m.tint = { 0.f, 0.f, 1.f };
        const Effective out = combine(weather, m);
        // amount 0.5 + 0.4 * 0.5 = 0.7; colour (0,0,1) * 0.5 + (1,0,0) * 0.4 * 0.5, over 0.7
        EXPECT_NEAR(out.grade.tintAmount, 0.7f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[0], 0.2f / 0.7f, 1e-5f);
        EXPECT_NEAR(out.grade.tint[1], 0.f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[2], 0.5f / 0.7f, 1e-5f);
    }

    TEST(RenderImageSpaceModifier, aModifierWithNoTintKeepsTheWeathersTint)
    {
        Effective weather;
        weather.on = true;
        weather.grade.tintAmount = 0.4f;
        weather.grade.tint = { 1.f, 0.f, 0.f };
        Modifier m;
        m.active = true;
        m.saturationMult = 0.5f;
        const Effective out = combine(weather, m);
        EXPECT_NEAR(out.grade.tintAmount, 0.4f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[0], 1.f, 1e-6f);
        EXPECT_NEAR(out.grade.tint[2], 0.f, 1e-6f);
    }

    TEST(RenderImageSpaceModifier, aValueThatIsNotANumberIsTheNeutralOne)
    {
        Modifier m;
        m.active = true;
        m.saturationMult = kNan;
        m.saturationAdd = kNan;
        m.brightnessMult = kNan;
        m.brightnessAdd = kNan;
        m.contrastMult = kNan;
        m.contrastAdd = kNan;
        m.tintAmount = kNan;
        m.tint = { kNan, kNan, kNan };
        const Effective out = combine(Effective{}, m);
        EXPECT_TRUE(out.on);
        EXPECT_FLOAT_EQ(out.grade.saturation, 1.f);
        EXPECT_FLOAT_EQ(out.grade.contrast, 1.f);
        EXPECT_FLOAT_EQ(out.stops, 0.f);
        EXPECT_FLOAT_EQ(out.grade.tintAmount, 0.f);
        for (float c : out.grade.tint)
            EXPECT_TRUE(std::isfinite(c));
    }

    TEST(RenderImageSpaceModifier, theTintAmountOfAModifierIsKeptInZeroToOne)
    {
        Modifier m;
        m.active = true;
        m.tintAmount = 3.f;
        m.tint = { 0.1f, 0.2f, 0.3f };
        const Effective out = combine(Effective{}, m);
        EXPECT_FLOAT_EQ(out.grade.tintAmount, 1.f);
        EXPECT_NEAR(out.grade.tint[1], 0.2f, 1e-6f);
    }
}
