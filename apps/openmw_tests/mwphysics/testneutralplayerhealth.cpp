// Task 0206: the engine player's health after a pack report (apps/openmw/mwphysics/neutralplayerhealth.hpp). The
// maximum follows the pack's (a level up raises it), the current value stays inside it, and a report with no maximum
// behaves as it did before. Written first, run red against a stub that never changes the maximum.
#include "apps/openmw/mwphysics/neutralplayerhealth.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using MWPhysics::neutralPlayerHealthWrite;

    TEST(NeutralPlayerHealth, aRaisedMaximumRaisesTheBaseAndTheCurrentValueFollows)
    {
        const auto w = neutralPlayerHealthWrite(100.f, 0.f, 100.f, 110.f, 110.f);
        EXPECT_FLOAT_EQ(w.mBase, 110.f);
        EXPECT_FLOAT_EQ(w.mCurrent, 110.f);
    }

    TEST(NeutralPlayerHealth, theModifierIsKeptSoTheModifiedValueEqualsTheMaximum)
    {
        const auto w = neutralPlayerHealthWrite(100.f, 20.f, 90.f, 90.f, 130.f);
        EXPECT_FLOAT_EQ(w.mBase + 20.f, 130.f);
        EXPECT_FLOAT_EQ(w.mCurrent, 90.f);
    }

    TEST(NeutralPlayerHealth, aLowerMaximumLowersTheBaseAndClampsTheCurrentValue)
    {
        const auto w = neutralPlayerHealthWrite(110.f, 0.f, 110.f, 110.f, 100.f);
        EXPECT_FLOAT_EQ(w.mBase, 100.f);
        EXPECT_FLOAT_EQ(w.mCurrent, 100.f);
    }

    TEST(NeutralPlayerHealth, theBaseNeverGoesBelowZeroEvenForAHugeModifier)
    {
        const auto w = neutralPlayerHealthWrite(100.f, 500.f, 100.f, 50.f, 80.f);
        EXPECT_FLOAT_EQ(w.mBase, 0.f); // the modified value is then the modifier, 500: the maximum cannot go lower
        EXPECT_FLOAT_EQ(w.mCurrent, 50.f);
    }

    TEST(NeutralPlayerHealth, aReportWithTheSameMaximumChangesOnlyTheCurrentValue)
    {
        const auto w = neutralPlayerHealthWrite(100.f, 0.f, 100.f, 83.f, 100.f);
        EXPECT_FLOAT_EQ(w.mBase, 100.f);
        EXPECT_FLOAT_EQ(w.mCurrent, 83.f);
        const auto close = neutralPlayerHealthWrite(100.f, 0.f, 100.f, 83.f, 100.0004f);
        EXPECT_FLOAT_EQ(close.mBase, 100.f); // a rounding difference does not rewrite the stat
    }

    TEST(NeutralPlayerHealth, aReportWithNoMaximumLeavesTheMaximumAlone)
    {
        for (const float none :
            { 0.f, -5.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            const auto w = neutralPlayerHealthWrite(100.f, 0.f, 100.f, 150.f, none);
            EXPECT_FLOAT_EQ(w.mBase, 100.f);
            EXPECT_FLOAT_EQ(w.mCurrent, 100.f); // clamped to the engine's own maximum, as before
        }
    }

    TEST(NeutralPlayerHealth, theCurrentValueStaysInsideZeroAndTheMaximum)
    {
        EXPECT_FLOAT_EQ(neutralPlayerHealthWrite(100.f, 0.f, 50.f, -4.f, 110.f).mCurrent, 0.f);
        EXPECT_FLOAT_EQ(neutralPlayerHealthWrite(100.f, 0.f, 50.f, 400.f, 110.f).mCurrent, 110.f);
    }

    TEST(NeutralPlayerHealth, aHealthThatIsNotANumberLeavesTheCurrentValue)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto w = neutralPlayerHealthWrite(100.f, 0.f, 42.f, nan, 110.f);
        EXPECT_FLOAT_EQ(w.mCurrent, 42.f);
        EXPECT_FLOAT_EQ(w.mBase, 110.f); // the maximum was still reported
    }
}
