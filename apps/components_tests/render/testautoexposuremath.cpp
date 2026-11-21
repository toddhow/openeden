// Tests for the pure maths of automatic exposure, Render::AutoExposure (task 0364a). Every expected number is worked out
// by hand in a comment. Pure CPU maths: no device, window or game data.

#include <components/render/autoexposuremath.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using namespace Render::AutoExposure;

    std::array<std::uint32_t, bins> emptyHistogram()
    {
        return {};
    }

    TEST(RenderAutoExposure, aSampleThatIsNotABrightnessIsBinZero)
    {
        EXPECT_EQ(binOf(0.f), 0);
        EXPECT_EQ(binOf(-1.f), 0);
        EXPECT_EQ(binOf(std::nanf("")), 0);
        // Below 2^-8.
        EXPECT_EQ(binOf(0.0039f), 0);
    }

    TEST(RenderAutoExposure, binsSplitSixteenStopsIntoSixtyThreeSteps)
    {
        // log2 = -8 is the bottom of bin 1; the width is 16 / 63 stops.
        EXPECT_EQ(binOf(std::exp2(-8.f)), 1);
        // log2 = 0: position (0 + 8) / (16 / 63) = 31.5, floor 31, bin 32.
        EXPECT_EQ(binOf(1.f), 32);
        // Above the range joins the last bin: 2^20.
        EXPECT_EQ(binOf(std::exp2(20.f)), 63);
        // The upper edge itself is position 63, clamped to the last of the 63 bins.
        EXPECT_EQ(binOf(std::exp2(8.f)), 63);
    }

    TEST(RenderAutoExposure, aBinCentreLiesInsideItsBin)
    {
        for (int bin = firstBin; bin < bins; ++bin)
            EXPECT_EQ(binOf(std::exp2(binCentreLog2(bin))), bin) << bin;
    }

    TEST(RenderAutoExposure, theMeanOfOneBinIsItsCentre)
    {
        auto h = emptyHistogram();
        h[32] = 1000;
        const Measurement m = trimmedMean(h, 0, 1000, 0.1f, 0.9f);
        EXPECT_TRUE(m.valid());
        EXPECT_NEAR(m.meanLog2, binCentreLog2(32), 1e-4f);
        EXPECT_NEAR(m.weight, 800.f, 1e-2f); // 10 percent cut at each end of 1000
    }

    TEST(RenderAutoExposure, blackSamplesDoNotCount)
    {
        auto h = emptyHistogram();
        h[0] = 100000; // a black border
        h[40] = 10;
        const Measurement m = trimmedMean(h, 0, 100010, 0.f, 1.f);
        EXPECT_NEAR(m.meanLog2, binCentreLog2(40), 1e-4f);
        EXPECT_NEAR(m.weight, 10.f, 1e-3f);
    }

    TEST(RenderAutoExposure, anEmptyOrBlackHistogramIsNotValid)
    {
        auto h = emptyHistogram();
        EXPECT_FALSE(trimmedMean(h, 0, 0, 0.1f, 0.9f).valid());
        h[0] = 500;
        EXPECT_FALSE(trimmedMean(h, 0, 500, 0.1f, 0.9f).valid());
    }

    TEST(RenderAutoExposure, theTrimLeavesOutTheDarkestAndBrightestShares)
    {
        // 100 dark samples in bin 10, 800 in bin 30, 100 bright in bin 60. Trimming 10 percent each side removes both
        // outliers exactly (100 of 1000): the mean is bin 30's centre.
        auto h = emptyHistogram();
        h[10] = 100;
        h[30] = 800;
        h[60] = 100;
        const Measurement trimmed = trimmedMean(h, 0, 1000, 0.1f, 0.9f);
        EXPECT_NEAR(trimmed.meanLog2, binCentreLog2(30), 1e-4f);
        EXPECT_NEAR(trimmed.weight, 800.f, 1e-2f);
        // Untrimmed: (100 c10 + 800 c30 + 100 c60) / 1000.
        const Measurement full = trimmedMean(h, 0, 1000, 0.f, 1.f);
        const float expected
            = (100.f * binCentreLog2(10) + 800.f * binCentreLog2(30) + 100.f * binCentreLog2(60)) / 1000.f;
        EXPECT_NEAR(full.meanLog2, expected, 1e-3f);
    }

    TEST(RenderAutoExposure, aCutInsideABinCountsThePartThatRemains)
    {
        // 100 samples in bin 20 and 100 in bin 40, trim the lowest 25 percent: 50 of the 200 are cut from bin 20, so the
        // kept weights are 50 and 100 over 150.
        auto h = emptyHistogram();
        h[20] = 100;
        h[40] = 100;
        const Measurement m = trimmedMean(h, 0, 200, 0.25f, 1.f);
        EXPECT_NEAR(m.weight, 150.f, 1e-3f);
        EXPECT_NEAR(m.meanLog2, (50.f * binCentreLog2(20) + 100.f * binCentreLog2(40)) / 150.f, 1e-3f);
    }

    TEST(RenderAutoExposure, theArithmeticMeanComesFromTheFixedPointSum)
    {
        auto h = emptyHistogram();
        h[32] = 4;
        // Four samples of luminance 0.5: sum 4 * 0.5 * 256 = 512; mean 512 / 256 / 4.
        EXPECT_NEAR(trimmedMean(h, 512, 4, 0.f, 1.f).meanLuma, 0.5f, 1e-6f);
    }

    TEST(RenderAutoExposure, theTargetBringsTheMeanToTheKey)
    {
        Settings settings;
        settings.key = 0.18f;
        settings.minStops = -10.f;
        settings.maxStops = 10.f;
        // A scene at the key needs no change.
        EXPECT_NEAR(targetStops(std::log2(0.18f), settings), 0.f, 1e-5f);
        // Half the key (one stop darker) needs +1; four times (two stops brighter) needs -2.
        EXPECT_NEAR(targetStops(std::log2(0.09f), settings), 1.f, 1e-5f);
        EXPECT_NEAR(targetStops(std::log2(0.72f), settings), -2.f, 1e-5f);
    }

    TEST(RenderAutoExposure, aPercentileIsInterpolatedInsideItsBin)
    {
        auto h = emptyHistogram();
        h[10] = 100;
        h[30] = 100;
        // The 25th percentile: 50 of 200 samples, inside bin 10 at half: its lower edge plus half a width.
        EXPECT_NEAR(percentileLog2(h, 0.25f), minLog2 + (9.f + 0.5f) * binWidth, 1e-4f);
        // The 75th: 150 of 200: 50 samples into bin 30 of 100.
        EXPECT_NEAR(percentileLog2(h, 0.75f), minLog2 + (29.f + 0.5f) * binWidth, 1e-4f);
        // Nothing counted: the bottom of the range.
        EXPECT_FLOAT_EQ(percentileLog2(emptyHistogram(), 0.98f), minLog2);
    }

    TEST(RenderAutoExposure, theHighlightsLimitHowFarTheExposureMayRise)
    {
        Settings settings;
        settings.minStops = -10.f;
        settings.maxStops = 10.f;
        settings.highlightPercentile = 0.98f;
        settings.highlightWhite = 1.f;
        settings.key = 0.11f;
        // The key asks for +3 stops, but the highlights lie at 0.5 (log2 -1): at most +1 before they pass the white.
        EXPECT_NEAR(targetStops(std::log2(0.11f) - 3.f, -1.f, settings), 1.f, 1e-4f);
    }

    TEST(RenderAutoExposure, highlightsNeverMakeASceneDarkerThanTheKeyAsksFor)
    {
        Settings settings;
        settings.minStops = -10.f;
        settings.maxStops = 10.f;
        settings.highlightPercentile = 0.98f;
        settings.key = 0.11f;
        // A dark room with a lamp at 20 (log2 4.3): the headroom is negative, the target stays 0 at the lowest (never
        // below it because of the lamp) ...
        EXPECT_NEAR(targetStops(std::log2(0.11f) - 2.f, 4.3f, settings), 0.f, 1e-4f);
        // ... and a bright scene is still darkened by the key.
        EXPECT_NEAR(targetStops(std::log2(0.44f), 0.f, settings), -2.f, 1e-4f);
        // Off with a percentile of 0: the key alone.
        settings.highlightPercentile = 0.f;
        EXPECT_NEAR(targetStops(std::log2(0.11f) - 2.f, 4.3f, settings), 2.f, 1e-4f);
    }

    TEST(RenderAutoExposure, theTargetIsClampedToTheStopLimits)
    {
        Settings settings;
        settings.minStops = -2.f;
        settings.maxStops = 3.f;
        EXPECT_FLOAT_EQ(targetStops(-8.f, settings), 3.f);
        EXPECT_FLOAT_EQ(targetStops(8.f, settings), -2.f);
        // Swapped limits are tolerated.
        std::swap(settings.minStops, settings.maxStops);
        EXPECT_FLOAT_EQ(targetStops(8.f, settings), -2.f);
    }

    TEST(RenderAutoExposure, adaptationIsExponentialAndIndependentOfTheFrameRate)
    {
        Settings settings;
        settings.speedUp = 2.f;
        settings.speedDown = 2.f;
        // One step of 0.5 s (the longest step): 1 - exp(-2 * 0.5) = 0.632 of the way.
        EXPECT_NEAR(adapt(0.f, 1.f, 0.5f, settings), 1.f - std::exp(-1.f), 1e-5f);
        // Two steps of 0.25 s equal one of 0.5 s.
        const float once = adapt(0.f, 1.f, 0.5f, settings);
        const float half = adapt(0.f, 1.f, 0.25f, settings);
        EXPECT_NEAR(adapt(half, 1.f, 0.25f, settings), once, 1e-5f);
    }

    TEST(RenderAutoExposure, theTwoDirectionsHaveTheirOwnSpeeds)
    {
        Settings settings;
        settings.speedUp = 1.f;
        settings.speedDown = 4.f;
        EXPECT_NEAR(adapt(0.f, 1.f, 0.5f, settings), 1.f - std::exp(-0.5f), 1e-5f); // rising: up
        EXPECT_NEAR(adapt(0.f, -1.f, 0.5f, settings), -(1.f - std::exp(-2.f)), 1e-5f); // falling: down
    }

    TEST(RenderAutoExposure, aLongFrameAndANegativeOneAreBounded)
    {
        Settings settings;
        settings.speedUp = 1.f;
        settings.speedDown = 1.f;
        EXPECT_FLOAT_EQ(adapt(0.f, 1.f, 100.f, settings), adapt(0.f, 1.f, maxStep, settings));
        EXPECT_FLOAT_EQ(adapt(0.f, 1.f, -1.f, settings), 0.f);
        settings.speedUp = 0.f;
        EXPECT_FLOAT_EQ(adapt(0.f, 1.f, 1.f, settings), 0.f);
    }

    TEST(RenderAutoExposure, theFirstMeasurementIsTakenAsItIsAndAnInvalidOneChangesNothing)
    {
        Settings settings;
        settings.key = 0.18f;
        settings.highlightPercentile = 0.f; // the key alone
        settings.minStops = -10.f;
        settings.maxStops = 10.f;
        State state;
        Measurement dark;
        dark.meanLog2 = std::log2(0.045f); // two stops under the key
        dark.weight = 100.f;
        EXPECT_NEAR(state.update(dark, 0.016f, settings), 2.f, 1e-4f);
        EXPECT_TRUE(state.started);
        // The scene turns bright: it moves towards -2 gradually, not at once.
        Measurement bright;
        bright.meanLog2 = std::log2(0.72f);
        bright.weight = 100.f;
        const float next = state.update(bright, 0.1f, settings);
        EXPECT_LT(next, 2.f);
        EXPECT_GT(next, -2.f);
        // Nothing to measure: the value stays.
        EXPECT_FLOAT_EQ(state.update(Measurement(), 1.f, settings), next);
        state.reset();
        EXPECT_FALSE(state.started);
        EXPECT_FLOAT_EQ(state.stops, 0.f);
    }

    TEST(RenderAutoExposure, theRingGivesTheNewestSlotThatIsOldEnough)
    {
        std::array<std::uint32_t, slots> serials{};
        EXPECT_FALSE(pickSlot(serials, 100).has_value()); // nothing written
        serials[0] = 96; // age 4: old enough
        serials[1] = 97; // age 3: too young
        serials[2] = 90; // age 10
        const auto slot = pickSlot(serials, 100);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(*slot, 0);
        // Only young slots: none.
        serials = {};
        serials[3] = 99;
        EXPECT_FALSE(pickSlot(serials, 100).has_value());
    }

    TEST(RenderAutoExposure, theRingHandlesTheSerialWrappingAround)
    {
        std::array<std::uint32_t, slots> serials{};
        // Serials count 1 .. 2^24; counter 2 just after a wrap, slot written at serial 2^24 - 1: age 3 (ring), too young;
        // serial 2^24 - 3 is age 5.
        // Ages: serial 2^24 - 1 is 3 (too young); 2^24 - 4 is 6.
        serials[0] = serialMask;
        serials[1] = serialMask - 4;
        const auto slot = pickSlot(serials, 2);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(*slot, 1);
    }

    TEST(RenderAutoExposure, aSerialFromTheFutureIsIgnored)
    {
        std::array<std::uint32_t, slots> serials{};
        serials[0] = 200; // written by a counter the CPU has not reached (a stale ring after a counter reset)
        EXPECT_FALSE(pickSlot(serials, 100).has_value());
    }

    TEST(RenderAutoExposure, serialsNeverAreZero)
    {
        EXPECT_EQ(serialOf(0), 1u);
        EXPECT_EQ(serialOf(1), 2u);
        for (std::uint64_t frame : { std::uint64_t(serialMask - 1), std::uint64_t(serialMask),
                 std::uint64_t(serialMask) + 1, std::uint64_t(1) << 40 })
            EXPECT_NE(serialOf(frame), 0u);
        EXPECT_EQ(serialOf(serialMask), 1u);
    }

    TEST(RenderAutoExposure, thePivotIsTheTonemappedEncodedGreyOfTheMean)
    {
        using Render::Hdr::Tonemap;
        // Vanilla curve, no exposure change, encoded: mean 0.5 -> 0.5^(1/2.2) = 0.7297 (above 0.8? no: 0.7297).
        EXPECT_NEAR(pivotFromMeanLuma(0.5f, 0.f, Tonemap::Vanilla, true), std::pow(0.5f, 1.f / 2.2f), 1e-5f);
        // The result is clamped to 0.02 .. 0.8.
        EXPECT_FLOAT_EQ(pivotFromMeanLuma(0.0001f, 0.f, Tonemap::Vanilla, true), 0.02f);
        EXPECT_FLOAT_EQ(pivotFromMeanLuma(10.f, 0.f, Tonemap::Vanilla, true), 0.8f);
        // Exposure: one stop doubles the mean (0.1 -> 0.2, not encoded).
        EXPECT_NEAR(pivotFromMeanLuma(0.1f, 1.f, Tonemap::Vanilla, false), 0.2f, 1e-5f);
        // Not measured: 0.
        EXPECT_FLOAT_EQ(pivotFromMeanLuma(0.f, 0.f, Tonemap::Vanilla, true), 0.f);
    }

    TEST(RenderAutoExposure, theCurveIsAppliedLikeTheShadersPivot)
    {
        using Render::Hdr::Tonemap;
        // Reinhard of 0.5: 0.5 * (1 + 0.5 / 16) / 1.5 = 0.34375.
        EXPECT_NEAR(pivotFromMeanLuma(0.5f, 0.f, Tonemap::Reinhard, false), 0.34375f, 1e-5f);
    }

    // --- The shader twin -------------------------------------------------------------------------------------------

    std::string readShader()
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "post" / "autoexposure.slang";
        std::ifstream in(source);
        if (!in)
            return {};
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::optional<double> slangNumber(const std::string& text, const std::string& name)
    {
        const std::regex re("static\\s+const\\s+\\w+\\s+" + name + "\\s*=\\s*([-+0-9.eE]+)[uUfF]?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    TEST(RenderAutoExposureShader, theConstantsMatchTheCppConstants)
    {
        const std::string slang = readShader();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const std::pair<const char*, double> expected[] = {
            { "BINS", bins },
            { "FIRST_BIN", firstBin },
            { "MEASURED_BINS", measuredBins },
            { "MIN_LOG2", minLog2 },
            { "MAX_LOG2", maxLog2 },
            { "LUMA_SUM_INDEX", lumaSumIndex },
            { "SAMPLE_COUNT_INDEX", sampleCountIndex },
            { "MEAN_LUMA_CLAMP", meanLumaClamp },
            { "MEAN_LUMA_SCALE", meanLumaScale },
            { "SLOTS", slots },
            { "SERIAL_MASK", serialMask },
        };
        for (const auto& [name, value] : expected)
        {
            const auto found = slangNumber(slang, name);
            ASSERT_TRUE(found.has_value()) << name << " is not declared in autoexposure.slang";
            EXPECT_NEAR(*found, value, std::max(1e-9, std::fabs(value) * 1e-6)) << name;
        }
    }

    TEST(RenderAutoExposureShader, theLuminanceWeightsAreRec709)
    {
        const std::string slang = readShader();
        if (slang.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        EXPECT_NE(slang.find("float3(0.2126, 0.7152, 0.0722)"), std::string::npos);
    }
}
