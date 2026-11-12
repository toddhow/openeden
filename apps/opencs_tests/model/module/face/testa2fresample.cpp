// Task 0147 slice 2: resampling a voice line to the 16 kHz the Audio2Face networks take, and the deterministic noise.
#include "editor_modules/face_editor/a2f_generate.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace
{
    using namespace face_editor;

    std::vector<float> tone(double hz, std::uint32_t rate, double seconds, double amplitude = 0.5)
    {
        std::vector<float> v(static_cast<std::size_t>(seconds * rate));
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
        return v;
    }

    double rms(const std::vector<float>& v, std::size_t from, std::size_t to)
    {
        double s = 0;
        for (std::size_t i = from; i < to; ++i)
            s += static_cast<double>(v[i]) * v[i];
        return std::sqrt(s / static_cast<double>(to - from));
    }

    TEST(A2FResample, sixteenKilohertzInputComesBackUnchanged)
    {
        const auto in = tone(440, 16000, 0.25);
        std::string error;
        EXPECT_EQ(resampleMono(in, 16000, 16000, error), in);
    }

    TEST(A2FResample, outputLengthFollowsTheRatioForCommonRates)
    {
        std::string error;
        EXPECT_EQ(resampleMono(std::vector<float>(44100, 0.f), 44100, 16000, error).size(), 16000u);
        EXPECT_EQ(resampleMono(std::vector<float>(48000, 0.f), 48000, 16000, error).size(), 16000u);
        EXPECT_EQ(resampleMono(std::vector<float>(22050, 0.f), 22050, 16000, error).size(), 16000u);
        EXPECT_EQ(resampleMono(std::vector<float>(8000, 0.f), 8000, 16000, error).size(), 16000u);
        EXPECT_EQ(resampleMono(std::vector<float>(44101, 0.f), 44100, 16000, error).size(), 16000u); // round(16000.36)
    }

    TEST(A2FResample, aMidBandToneKeepsItsLevelAndItsFrequency)
    {
        std::string error;
        const auto out = resampleMono(tone(1000, 44100, 1.0), 44100, 16000, error);
        ASSERT_EQ(out.size(), 16000u);
        // Away from the filter's ends the level is the tone's: 0.5 / sqrt 2.
        EXPECT_NEAR(rms(out, 1000, 15000), 0.5 / std::sqrt(2.0), 0.01);
        // Zero crossings: 2 per period, 1000 Hz over (15000 - 1000) / 16000 s.
        int crossings = 0;
        for (std::size_t i = 1001; i < 15000; ++i)
            if ((out[i - 1] < 0) != (out[i] < 0))
                ++crossings;
        EXPECT_NEAR(crossings, 2 * 1000 * 14000.0 / 16000.0, 2.0);
    }

    TEST(A2FResample, aToneAboveTheNewNyquistRateIsRemovedNotFolded)
    {
        std::string error;
        const auto out = resampleMono(tone(12000, 44100, 1.0), 44100, 16000, error);
        ASSERT_EQ(out.size(), 16000u);
        EXPECT_LT(rms(out, 1000, 15000), 0.02); // would fold to 4000 Hz at 0.35 without the low-pass
    }

    TEST(A2FResample, upsamplingKeepsALowToneToo)
    {
        std::string error;
        const auto out = resampleMono(tone(300, 8000, 1.0), 8000, 16000, error);
        ASSERT_EQ(out.size(), 16000u);
        EXPECT_NEAR(rms(out, 1000, 15000), 0.5 / std::sqrt(2.0), 0.01);
    }

    TEST(A2FResample, emptyInputAndBadRatesGiveNothingWithAnError)
    {
        std::string error;
        EXPECT_TRUE(resampleMono({}, 44100, 16000, error).empty());
        EXPECT_TRUE(resampleMono(std::vector<float>(10, 0.f), 0, 16000, error).empty());
        EXPECT_FALSE(error.empty());
        error.clear();
        EXPECT_TRUE(resampleMono(std::vector<float>(10, 0.f), 44100, 0, error).empty());
        EXPECT_FALSE(error.empty());
        error.clear();
        EXPECT_TRUE(resampleMono(std::vector<float>(10, 0.f), 1000000, 16000, error).empty());
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FResample, theSameInputGivesTheSameBytes)
    {
        std::string error;
        const auto in = tone(700, 44100, 0.3);
        EXPECT_EQ(resampleMono(in, 44100, 16000, error), resampleMono(in, 44100, 16000, error));
    }

    TEST(A2FNoise, theSameSeedGivesTheSameBytesAndAnotherSeedDoesNot)
    {
        std::vector<float> a(5000), b(5000), c(5000);
        fillNormalNoise(a.data(), a.size(), 7);
        fillNormalNoise(b.data(), b.size(), 7);
        fillNormalNoise(c.data(), c.size(), 8);
        EXPECT_EQ(a, b);
        EXPECT_NE(a, c);
    }

    TEST(A2FNoise, isStandardNormalWithinSampleError)
    {
        std::vector<float> a(200000);
        fillNormalNoise(a.data(), a.size(), 12345);
        double mean = 0, var = 0, tail = 0;
        for (float v : a)
        {
            EXPECT_TRUE(std::isfinite(v));
            mean += v;
            if (std::fabs(v) > 2.0)
                tail += 1;
        }
        mean /= static_cast<double>(a.size());
        for (float v : a)
            var += (v - mean) * (v - mean);
        var /= static_cast<double>(a.size());
        EXPECT_NEAR(mean, 0.0, 0.01);
        EXPECT_NEAR(var, 1.0, 0.02);
        EXPECT_NEAR(tail / static_cast<double>(a.size()), 0.0455, 0.004); // P(|z| > 2)
    }

    TEST(A2FNoise, anOddCountFillsEveryEntry)
    {
        std::vector<float> a(7, 99.f);
        fillNormalNoise(a.data(), a.size(), 1);
        for (float v : a)
            EXPECT_NE(v, 99.f);
    }
}
