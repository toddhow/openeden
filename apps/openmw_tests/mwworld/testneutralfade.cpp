// Task 0202: the pure schedule of the fades of neutral objects (Papyrus Enable(abFadeIn) and Disable(abFadeOut)).
// Written
// first and run red against a stub that refuses every fade.
#include <apps/openmw/mwworld/neutralfade.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    using MWWorld::NeutralFades;

    TEST(NeutralFade, aFadeOutFallsInAStraightLineAndFinishesAtZero)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(7, false, 2.f));
        EXPECT_TRUE(fades.fading(7));
        std::vector<NeutralFades::Update> out;
        fades.advance(0.5f, out);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mPlacement, 7u);
        EXPECT_NEAR(out[0].mAlpha, 0.75f, 1e-6f);
        EXPECT_FALSE(out[0].mFinished);
        EXPECT_FALSE(out[0].mFadeIn);
        out.clear();
        fades.advance(1.f, out);
        EXPECT_NEAR(out[0].mAlpha, 0.25f, 1e-6f);
        out.clear();
        fades.advance(0.5f, out);
        EXPECT_EQ(out[0].mAlpha, 0.f);
        EXPECT_TRUE(out[0].mFinished);
        EXPECT_FALSE(fades.fading(7));
        EXPECT_EQ(fades.size(), 0u);
    }

    TEST(NeutralFade, aFadeInRisesFromZeroAndFinishesAtOne)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(9, true, 4.f));
        std::vector<NeutralFades::Update> out;
        fades.advance(1.f, out);
        EXPECT_NEAR(out[0].mAlpha, 0.25f, 1e-6f);
        EXPECT_TRUE(out[0].mFadeIn);
        out.clear();
        fades.advance(100.f, out);
        EXPECT_EQ(out[0].mAlpha, 1.f);
        EXPECT_TRUE(out[0].mFinished);
        EXPECT_EQ(fades.size(), 0u);
    }

    TEST(NeutralFade, aReplacingFadeStartsFromTheCurrentAlphaSoThePictureDoesNotJump)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(3, false, 2.f));
        std::vector<NeutralFades::Update> out;
        fades.advance(1.f, out);
        ASSERT_NEAR(out[0].mAlpha, 0.5f, 1e-6f);
        // Enable asked while it fades out: it turns round from 0.5
        ASSERT_TRUE(fades.start(3, true, 1.f));
        EXPECT_EQ(fades.size(), 1u) << "replaced, not added";
        out.clear();
        fades.advance(0.5f, out);
        EXPECT_NEAR(out[0].mAlpha, 0.75f, 1e-6f);
        out.clear();
        fades.advance(0.5f, out);
        EXPECT_EQ(out[0].mAlpha, 1.f);
        EXPECT_TRUE(out[0].mFinished);
    }

    TEST(NeutralFade, aFadeThatHasNotRunYetStartsAtTheEndOfTheScale)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(3, false, 2.f));
        ASSERT_TRUE(fades.start(3, true, 2.f)); // no time passed: from 1 (out start) to 1 (in end)
        std::vector<NeutralFades::Update> out;
        fades.advance(1.f, out);
        EXPECT_EQ(out[0].mAlpha, 1.f);
    }

    TEST(NeutralFade, updatesComeInTheOrderTheFadesWereStarted)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(30, false, 1.f));
        ASSERT_TRUE(fades.start(10, true, 1.f));
        ASSERT_TRUE(fades.start(20, false, 1.f));
        std::vector<NeutralFades::Update> out;
        fades.advance(0.1f, out);
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].mPlacement, 30u);
        EXPECT_EQ(out[1].mPlacement, 10u);
        EXPECT_EQ(out[2].mPlacement, 20u);
    }

    TEST(NeutralFade, aTimeThatIsNotPositiveAndFiniteIsRefused)
    {
        NeutralFades fades;
        EXPECT_FALSE(fades.start(1, false, 0.f));
        EXPECT_FALSE(fades.start(1, false, -1.f));
        EXPECT_FALSE(fades.start(1, false, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(fades.start(1, false, std::numeric_limits<float>::infinity()));
        EXPECT_EQ(fades.size(), 0u);
    }

    TEST(NeutralFade, aTooLongTimeIsCutToTheLongestFade)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(1, false, 1e9f));
        std::vector<NeutralFades::Update> out;
        fades.advance(NeutralFades::kMaxSeconds / 2.f, out);
        EXPECT_NEAR(out[0].mAlpha, 0.5f, 1e-5f);
    }

    TEST(NeutralFade, aStepThatIsNegativeOrNotFiniteMovesNothing)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(1, false, 2.f));
        std::vector<NeutralFades::Update> out;
        fades.advance(-1.f, out);
        fades.advance(std::numeric_limits<float>::quiet_NaN(), out);
        fades.advance(std::numeric_limits<float>::infinity(), out);
        EXPECT_TRUE(out.empty());
        fades.advance(1.f, out);
        EXPECT_NEAR(out[0].mAlpha, 0.5f, 1e-6f);
    }

    TEST(NeutralFade, theNumberOfFadesAtOnceIsBounded)
    {
        NeutralFades fades;
        for (std::uint32_t i = 0; i < NeutralFades::kMaxFades; ++i)
            ASSERT_TRUE(fades.start(i + 1, false, 1.f));
        EXPECT_FALSE(fades.start(NeutralFades::kMaxFades + 5, false, 1.f));
        EXPECT_TRUE(fades.start(1, true, 1.f)) << "an object that already fades may change its fade";
        EXPECT_EQ(fades.size(), NeutralFades::kMaxFades);
    }

    TEST(NeutralFade, cancelDropsOneFadeWithoutAFinalUpdate)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(1, false, 1.f));
        ASSERT_TRUE(fades.start(2, false, 1.f));
        EXPECT_TRUE(fades.cancel(1));
        EXPECT_FALSE(fades.cancel(1)) << "it was not fading any more";
        EXPECT_FALSE(fades.fading(1));
        std::vector<NeutralFades::Update> out;
        fades.advance(0.5f, out);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mPlacement, 2u);
    }

    TEST(NeutralFade, clearForgetsEveryFade)
    {
        NeutralFades fades;
        ASSERT_TRUE(fades.start(1, false, 1.f));
        fades.clear();
        EXPECT_EQ(fades.size(), 0u);
        std::vector<NeutralFades::Update> out;
        fades.advance(1.f, out);
        EXPECT_TRUE(out.empty());
    }

    TEST(NeutralFade, twoRunsOfTheSameStepsGiveTheSameAlphas)
    {
        std::vector<float> runs[2];
        for (int run = 0; run < 2; ++run)
        {
            NeutralFades fades;
            (void)fades.start(5, false, 1.7f);
            (void)fades.start(6, true, 0.9f);
            std::vector<NeutralFades::Update> out;
            for (int i = 0; i < 20; ++i)
            {
                out.clear();
                fades.advance(1.f / 60.f, out);
                for (const NeutralFades::Update& u : out)
                    runs[run].push_back(u.mAlpha);
            }
        }
        EXPECT_EQ(runs[0], runs[1]);
        EXPECT_FALSE(runs[0].empty());
    }
}
