// Tests for the lightning maths, Render::Lightning (task 0040, stage a). Written from the task spec: the flash curve
// with its double flicker, the photosensitivity limits (the peak cap and the minimum interval), and the seeded flash
// direction. Pure CPU maths: no device, window or game data.

#include <components/render/lightningmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace
{
    using namespace Render::Lightning;

    float length(const vsg::vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    TEST(RenderLightning, envelopeIsZeroBeforeAndAfter)
    {
        EXPECT_FLOAT_EQ(envelope(-1.f), 0.f);
        EXPECT_FLOAT_EQ(envelope(0.f), 0.f);
        EXPECT_FLOAT_EQ(envelope(duration), 0.f);
        EXPECT_FLOAT_EQ(envelope(duration + 5.f), 0.f);
        EXPECT_FLOAT_EQ(envelope(std::numeric_limits<float>::quiet_NaN()), 0.f);
        EXPECT_FLOAT_EQ(envelope(std::numeric_limits<float>::infinity()), 0.f);
    }

    TEST(RenderLightning, envelopeStaysInZeroToOne)
    {
        for (int i = 0; i <= 4000; ++i)
        {
            const float e = envelope(static_cast<float>(i) * 0.00025f);
            EXPECT_GE(e, 0.f);
            EXPECT_LE(e, 1.f);
        }
    }

    TEST(RenderLightning, envelopeIsBrightEarlyAndEndsSmoothly)
    {
        float best = 0.f;
        for (int i = 0; i <= 4000; ++i)
            best = std::max(best, envelope(static_cast<float>(i) * 0.00025f));
        EXPECT_GT(best, 0.6f);
        // Smooth end: no step of more than a few hundredths in a thousandth of a second near the end.
        for (float t = duration - 0.01f; t < duration; t += 0.001f)
            EXPECT_LT(std::fabs(envelope(t + 0.001f) - envelope(t)), 0.02f);
    }

    TEST(RenderLightning, envelopeHasADoubleFlicker)
    {
        // A dip after the first stroke and a second rise: the lowest value between the strokes is lower than the
        // second stroke's peak.
        float dip = 2.f;
        float dipAt = 0.f;
        for (float t = riseTime; t < secondStart; t += 0.001f)
            if (envelope(t) < dip)
            {
                dip = envelope(t);
                dipAt = t;
            }
        float second = 0.f;
        for (float t = secondStart; t < duration; t += 0.001f)
            second = std::max(second, envelope(t));
        EXPECT_GT(dipAt, riseTime);
        EXPECT_GT(second, dip + 0.1f);
    }

    TEST(RenderLightning, peakNeverPassesTheCap)
    {
        const float settings[] = { 0.f, 0.5f, 1.f, 2.f, 10.f, 1e6f, std::numeric_limits<float>::infinity() };
        for (float setting : settings)
            for (unsigned distance = 0; distance <= 5; ++distance)
            {
                const float p = peak(setting, strikeStrength(distance));
                EXPECT_LE(p, peakCap) << setting << " " << distance;
                EXPECT_GE(p, 0.f);
            }
    }

    TEST(RenderLightning, peakScalesWithTheSettingBelowTheCap)
    {
        EXPECT_FLOAT_EQ(peak(0.5f, 0.5f), 0.25f);
        EXPECT_FLOAT_EQ(peak(0.f, 1.f), 0.f);
        EXPECT_GT(peak(1.f, 0.5f), peak(0.5f, 0.5f));
    }

    TEST(RenderLightning, badPeakInputsGiveNothing)
    {
        EXPECT_FLOAT_EQ(peak(std::numeric_limits<float>::quiet_NaN(), 1.f), 0.f);
        EXPECT_FLOAT_EQ(peak(1.f, std::numeric_limits<float>::quiet_NaN()), 0.f);
        EXPECT_FLOAT_EQ(peak(-1.f, 1.f), 0.f);
    }

    TEST(RenderLightning, strikeStrengthFollowsTheVanillaDistance)
    {
        EXPECT_FLOAT_EQ(strikeStrength(0), 1.f);
        EXPECT_FLOAT_EQ(strikeStrength(1), 0.75f);
        EXPECT_FLOAT_EQ(strikeStrength(3), 0.25f);
        EXPECT_FLOAT_EQ(strikeStrength(9), 0.25f);
    }

    TEST(RenderLightning, theCapIsTheClosestVanillaStrike)
    {
        EXPECT_FLOAT_EQ(peakCap, 1.f);
        EXPECT_FLOAT_EQ(peak(1.f, strikeStrength(0)), 1.f);
    }

    TEST(RenderLightning, aFlashNeverExceedsTheCapAtAnyTimeOrSetting)
    {
        for (float setting : { 1.f, 2.f, 1000.f })
        {
            Flasher flasher;
            flasher.strike(0.f, strikeStrength(0));
            for (int i = 0; i <= 3000; ++i)
                EXPECT_LE(flasher.intensity(static_cast<float>(i) * 0.001f, setting), peakCap);
        }
    }

    TEST(RenderLightning, flashesNeverStartCloserThanTheMinimumInterval)
    {
        // A storm of strikes every 0.1 s for a minute: the flashes shown start at least minInterval apart.
        Flasher flasher;
        float lastShown = -1000.f;
        int shown = 0;
        for (int i = 0; i < 600; ++i)
        {
            const float now = static_cast<float>(i) * 0.1f;
            if (flasher.strike(now, 1.f))
            {
                EXPECT_GE(now - lastShown, minInterval - 1e-4f);
                lastShown = now;
                ++shown;
            }
        }
        EXPECT_GT(shown, 0);
        EXPECT_LE(shown, static_cast<int>(60.f / minInterval) + 1);
        EXPECT_EQ(flasher.count(), 600u);
    }

    TEST(RenderLightning, aStrikeAfterTheIntervalStartsAFlash)
    {
        Flasher flasher;
        EXPECT_TRUE(flasher.strike(10.f, 1.f));
        EXPECT_FALSE(flasher.strike(10.f + minInterval * 0.5f, 1.f));
        EXPECT_TRUE(flasher.strike(10.f + minInterval, 1.f));
    }

    TEST(RenderLightning, aRefusedStrikeLeavesTheFlashAlone)
    {
        Flasher flasher;
        flasher.strike(0.f, 1.f);
        const float before = flasher.intensity(0.05f, 1.f);
        flasher.strike(0.5f, 1.f);
        EXPECT_FLOAT_EQ(flasher.intensity(0.05f, 1.f), before);
        EXPECT_FLOAT_EQ(flasher.start(), 0.f);
    }

    TEST(RenderLightning, noStrikeNoLight)
    {
        Flasher flasher;
        EXPECT_FLOAT_EQ(flasher.intensity(3.f, 1.f), 0.f);
        EXPECT_FALSE(flasher.hasFlash());
        EXPECT_FALSE(flasher.strike(1.f, 0.f));
        EXPECT_FLOAT_EQ(flasher.intensity(1.1f, 1.f), 0.f);
    }

    TEST(RenderLightning, anyOneSecondHoldsAtMostThreePeaks)
    {
        // WCAG 2.3.1: no more than three flashes in any one second. Count the stroke peaks in every one-second window of
        // a dense storm (a strike every 0.05 s for a minute), as the limiter lets them through.
        std::vector<float> peaks;
        Flasher flasher;
        for (int i = 0; i < 1200; ++i)
        {
            const float now = static_cast<float>(i) * 0.05f;
            if (flasher.strike(now, 1.f))
            {
                peaks.push_back(now + riseTime);
                peaks.push_back(now + secondStart + riseTime);
            }
        }
        ASSERT_GT(peaks.size(), 10u);
        int most = 0;
        for (float windowStart = 0.f; windowStart < 60.f; windowStart += 0.01f)
        {
            int inside = 0;
            for (float p : peaks)
                if (p >= windowStart && p < windowStart + 1.f)
                    ++inside;
            most = std::max(most, inside);
        }
        EXPECT_LE(most, 3);
        EXPECT_EQ(most, 2);
    }

    TEST(RenderLightning, directionIsAUnitVectorAboveTheHorizonInTheSkyBand)
    {
        for (std::uint32_t i = 0; i < 500; ++i)
        {
            const vsg::vec3 d = direction(defaultSeed, i);
            EXPECT_NEAR(length(d), 1.f, 1e-5f);
            EXPECT_GE(d.z, std::sin(minElevation) - 1e-5f);
            EXPECT_LE(d.z, std::sin(maxElevation) + 1e-5f);
        }
    }

    TEST(RenderLightning, sameSeedAndIndexGiveTheSameDirection)
    {
        for (std::uint32_t i = 0; i < 50; ++i)
        {
            const vsg::vec3 a = direction(7u, i);
            const vsg::vec3 b = direction(7u, i);
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
            EXPECT_EQ(a.z, b.z);
        }
    }

    TEST(RenderLightning, differentIndicesAndSeedsGiveDifferentDirections)
    {
        std::set<float> xs;
        for (std::uint32_t i = 0; i < 100; ++i)
            xs.insert(direction(defaultSeed, i).x);
        EXPECT_GT(xs.size(), 90u);
        const vsg::vec3 a = direction(1u, 3u);
        const vsg::vec3 b = direction(2u, 3u);
        EXPECT_TRUE(a.x != b.x || a.y != b.y);
    }

    TEST(RenderLightning, directionsSpreadAroundTheCompass)
    {
        int quadrants[4] = { 0, 0, 0, 0 };
        for (std::uint32_t i = 0; i < 400; ++i)
        {
            const vsg::vec3 d = direction(defaultSeed, i);
            ++quadrants[(d.x >= 0.f ? 1 : 0) + (d.y >= 0.f ? 2 : 0)];
        }
        for (int q : quadrants)
            EXPECT_GT(q, 60);
    }

    TEST(RenderLightning, theFlashDirectionFollowsTheStrikeCountNotTheLimiter)
    {
        // Two runs with the same strikes give the same directions, whatever was shown.
        Flasher a;
        Flasher b;
        for (int i = 0; i < 5; ++i)
        {
            a.strike(static_cast<float>(i) * 10.f, 1.f);
            b.strike(static_cast<float>(i) * 10.f, 1.f);
        }
        const vsg::vec3 da = a.flashDirection();
        const vsg::vec3 db = b.flashDirection();
        EXPECT_EQ(da.x, db.x);
        EXPECT_EQ(da.z, db.z);
        // The fifth strike is index 4.
        const vsg::vec3 expected = direction(defaultSeed, 4u);
        EXPECT_EQ(da.x, expected.x);
        // A strike the limiter refused still advances the count.
        Flasher c;
        c.strike(0.f, 1.f);
        c.strike(0.1f, 1.f);
        c.strike(5.f, 1.f);
        EXPECT_EQ(c.flashDirection().x, direction(defaultSeed, 2u).x);
    }

    TEST(RenderLightning, colourIsColdAndScalesWithTheAmount)
    {
        EXPECT_GT(colour.z, colour.x);
        const vsg::vec3 add = colourAdd(0.5f);
        EXPECT_FLOAT_EQ(add.z, colour.z * 0.5f);
        EXPECT_FLOAT_EQ(colourAdd(0.f).x, 0.f);
    }

    TEST(RenderLightning, mergedDirectionWithNoFlashIsTheSun)
    {
        const vsg::vec3 sun(3.f, 4.f, 12.f);
        const vsg::vec3 m = mergedDirection(sun, 0.7f, vsg::vec3(0.f, 0.f, 1.f), 0.f);
        EXPECT_EQ(m.x, sun.x);
        EXPECT_EQ(m.y, sun.y);
        EXPECT_EQ(m.z, sun.z);
    }

    TEST(RenderLightning, mergedDirectionKeepsTheLengthAndMovesTowardTheFlash)
    {
        const vsg::vec3 sun(0.f, 0.f, 2.f);
        const vsg::vec3 flash(1.f, 0.f, 0.f);
        const vsg::vec3 m = mergedDirection(sun, 1.f, flash, 1.f);
        EXPECT_NEAR(length(m), 2.f, 1e-5f);
        EXPECT_GT(m.x, 0.f);
        EXPECT_GT(m.z, 0.f);
        const vsg::vec3 mostlyFlash = mergedDirection(sun, 0.01f, flash, 100.f);
        EXPECT_GT(mostlyFlash.x, mostlyFlash.z * 10.f);
    }

    TEST(RenderLightning, mergedDirectionIgnoresBadWeights)
    {
        const vsg::vec3 sun(0.f, 0.f, 1.f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(mergedDirection(sun, 1.f, vsg::vec3(1.f, 0.f, 0.f), nan).z, 1.f);
        EXPECT_EQ(mergedDirection(sun, 1.f, vsg::vec3(1.f, 0.f, 0.f), -3.f).z, 1.f);
        // Opposite directions of equal weight cancel: keep the sun.
        EXPECT_EQ(mergedDirection(sun, 1.f, vsg::vec3(0.f, 0.f, -1.f), 1.f).z, 1.f);
    }

    TEST(RenderLightning, shiftingTheClockKeepsTheFlashAndTheInterval)
    {
        // The weather manager subtracts a whole hour from its seconds now and then, so that a float keeps its precision
        // in a long game. Nothing about the flash changes: its age, brightness and the limiter's interval.
        Flasher a;
        Flasher b;
        a.strike(7200.f, 1.f);
        b.strike(7200.f, 1.f);
        b.shift(3600.f);
        EXPECT_FLOAT_EQ(a.intensity(7200.1f, 1.f), b.intensity(3600.1f, 1.f));
        EXPECT_FALSE(a.strike(7200.5f, 1.f));
        EXPECT_FALSE(b.strike(3600.5f, 1.f));
        EXPECT_TRUE(a.strike(7200.f + minInterval, 1.f));
        EXPECT_TRUE(b.strike(3600.f + minInterval, 1.f));
        EXPECT_EQ(a.count(), b.count());
    }
}
