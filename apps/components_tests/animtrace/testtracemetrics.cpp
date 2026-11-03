// Task 0067, stage 0: the maths of the CreativeMotion transition measurement, from synthetic data with answers known
// by hand (components/animtrace/tracemetrics.hpp).

#include <components/animtrace/tracemetrics.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace AnimTrace;

    constexpr double sPi = 3.14159265358979323846;

    Quat about(double x, double y, double z, double degrees)
    {
        const double half = degrees * sPi / 360.0;
        const double length = std::sqrt(x * x + y * y + z * z);
        const double s = std::sin(half) / length;
        return { x * s, y * s, z * s, std::cos(half) };
    }

    Quat aboutZ(double degrees)
    {
        return about(0.0, 0.0, 1.0, degrees);
    }

    /// First `a`, then `b`, as a single rotation (the product b * a).
    Quat then(const Quat& a, const Quat& b)
    {
        return { b.mW * a.mX + b.mX * a.mW + b.mY * a.mZ - b.mZ * a.mY,
            b.mW * a.mY - b.mX * a.mZ + b.mY * a.mW + b.mZ * a.mX,
            b.mW * a.mZ + b.mX * a.mY - b.mY * a.mX + b.mZ * a.mW,
            b.mW * a.mW - b.mX * a.mX - b.mY * a.mY - b.mZ * a.mZ };
    }

    /// A trace of one bone whose z rotation is `angles[i]` degrees at frame i, 20 ms frames, standing still.
    Trace traceOfAngles(const std::vector<double>& angles, const std::string& bone = "bip01 spine1")
    {
        Trace trace;
        for (const double angle : angles)
        {
            Sample sample;
            sample.mDt = 0.02;
            sample.mBones.push_back({ bone, aboutZ(angle) });
            trace.mSamples.push_back(sample);
        }
        return trace;
    }

    /// Angles that stand at 0 until `start`, rise by `perFrame` for `moving` frames, then stay.
    std::vector<double> rampAngles(std::size_t count, std::size_t start, std::size_t moving, double perFrame)
    {
        std::vector<double> angles(count, 0.0);
        double angle = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i >= start && i < start + moving)
            {
                angle += perFrame;
            }
            angles[i] = angle;
        }
        return angles;
    }

    std::vector<Quat> rotationsOf(const std::vector<double>& angles)
    {
        std::vector<Quat> rotations;
        for (const double angle : angles)
        {
            rotations.push_back(aboutZ(angle));
        }
        return rotations;
    }

    TEST(AnimTraceAngle, IdentityIsZero)
    {
        EXPECT_DOUBLE_EQ(angleDegrees(Quat{}, Quat{}), 0.0);
    }

    TEST(AnimTraceAngle, RotationAboutAnAxisIsItsAngle)
    {
        EXPECT_NEAR(angleDegrees(Quat{}, aboutZ(30.0)), 30.0, 1e-9);
        EXPECT_NEAR(angleDegrees(about(1, 0, 0, 10.0), about(1, 0, 0, 55.0)), 45.0, 1e-9);
        EXPECT_NEAR(angleDegrees(Quat{}, about(1, 1, 1, 120.0)), 120.0, 1e-9);
    }

    TEST(AnimTraceAngle, AQuaternionAndItsNegativeAreTheSameRotation)
    {
        const Quat q = about(0.3, -0.5, 0.8, 70.0);
        const Quat negative{ -q.mX, -q.mY, -q.mZ, -q.mW };
        EXPECT_NEAR(angleDegrees(q, negative), 0.0, 1e-9);
        EXPECT_NEAR(angleDegrees(Quat{}, negative), angleDegrees(Quat{}, q), 1e-9);
    }

    TEST(AnimTraceAngle, TheShortWayRoundIsUsed)
    {
        // 350 degrees about z is 10 degrees the other way.
        EXPECT_NEAR(angleDegrees(Quat{}, aboutZ(350.0)), 10.0, 1e-9);
    }

    TEST(AnimTraceAngle, TinyAnglesKeepTheirPrecision)
    {
        // acos of a dot product would read these as 0 or about 0.03; atan2 of the parts does not.
        EXPECT_NEAR(angleDegrees(Quat{}, aboutZ(0.001)), 0.001, 1e-9);
        EXPECT_NEAR(angleDegrees(aboutZ(40.0), aboutZ(40.0001)), 0.0001, 1e-9);
    }

    TEST(AnimTraceAngle, InputsNeedNotBeNormalised)
    {
        const Quat q = aboutZ(25.0);
        const Quat scaled{ q.mX * 3.0, q.mY * 3.0, q.mZ * 3.0, q.mW * 3.0 };
        EXPECT_NEAR(angleDegrees(Quat{}, scaled), 25.0, 1e-9);
    }

    TEST(AnimTraceSeries, ConstantTurnRateGivesEqualStepsAndNoJerk)
    {
        std::vector<double> angles;
        for (int i = 0; i < 12; ++i)
        {
            angles.push_back(2.0 * i);
        }
        const std::vector<double> steps = stepSeries(rotationsOf(angles));
        const std::vector<double> jerk = jerkSeries(rotationsOf(angles));
        ASSERT_EQ(steps.size(), angles.size());
        ASSERT_EQ(jerk.size(), angles.size());
        EXPECT_DOUBLE_EQ(steps[0], 0.0);
        for (std::size_t i = 1; i < steps.size(); ++i)
        {
            EXPECT_NEAR(steps[i], 2.0, 1e-9) << "frame " << i;
        }
        for (std::size_t i = 0; i < jerk.size(); ++i)
        {
            EXPECT_NEAR(jerk[i], 0.0, 1e-9) << "frame " << i;
        }
    }

    TEST(AnimTraceSeries, ASpeedChangeOfOneDegreePerFrameIsAJerkOfOne)
    {
        // Steps are 2, 2, 3: only the third step differs from the one before it, by 1 degree.
        const std::vector<double> jerk = jerkSeries(rotationsOf({ 0.0, 2.0, 4.0, 7.0 }));
        EXPECT_NEAR(jerk[2], 0.0, 1e-9);
        EXPECT_NEAR(jerk[3], 1.0, 1e-9);
    }

    TEST(AnimTraceSeries, AReversalOfTwoDegreesPerFrameIsAJerkOfFour)
    {
        const std::vector<double> jerk = jerkSeries(rotationsOf({ 0.0, 2.0, 0.0 }));
        EXPECT_NEAR(jerk[2], 4.0, 1e-9);
    }

    TEST(AnimTraceSeries, ABendOfTheTurnAxisIsAJerk)
    {
        // Two steps of 10 degrees: one about x, then one about y. The two step rotations are different, so the
        // second difference is positive and (here) larger than 1 degree and smaller than a reversal.
        const Quat first = about(1, 0, 0, 10.0);
        const Quat second = then(first, about(0, 1, 0, 10.0));
        const std::vector<double> jerk = jerkSeries({ Quat{}, first, second });
        EXPECT_GT(jerk[2], 1.0);
        EXPECT_LT(jerk[2], 20.0);
    }

    TEST(AnimTraceSeries, ShortSeriesAreAllZeroAndDoNotCrash)
    {
        EXPECT_TRUE(stepSeries({}).empty());
        EXPECT_TRUE(jerkSeries({}).empty());
        EXPECT_EQ(stepSeries({ Quat{} }), std::vector<double>{ 0.0 });
        EXPECT_EQ(jerkSeries({ Quat{}, aboutZ(5.0) }), (std::vector<double>{ 0.0, 0.0 }));
    }

    TEST(AnimTraceSeries, GroundSpeedIsTheLengthInXAndYOverDt)
    {
        std::vector<Sample> samples(3);
        samples[0].mDt = 0.5;
        samples[0].mMoved[0] = 3.0;
        samples[0].mMoved[1] = 4.0;
        samples[0].mMoved[2] = 99.0; // up is not ground speed
        samples[1].mDt = 0.0; // a frame without time repeats the previous speed
        samples[2].mDt = 0.25;
        samples[2].mMoved[0] = 0.0;
        samples[2].mMoved[1] = -2.0;
        const std::vector<double> speeds = groundSpeedSeries(samples);
        EXPECT_NEAR(speeds[0], 10.0, 1e-12);
        EXPECT_NEAR(speeds[1], 10.0, 1e-12);
        EXPECT_NEAR(speeds[2], 8.0, 1e-12);
    }

    // The measurements around an event. The scene: still for 10 frames, five frames of 2 degrees each, still again.
    // The angle is 2 * (i - 9) at frame i in 10..14 and 10 degrees from frame 14 on.
    Trace rampTrace()
    {
        Trace trace = traceOfAngles(rampAngles(40, 10, 5, 2.0));
        trace.mEvents.push_back({ "walk", 3.0, 10 });
        return trace;
    }

    TEST(AnimTraceMeasure, StepAtTheEventAndAfterIt)
    {
        const Report report = measure(rampTrace(), Options{}, "ramp");
        ASSERT_EQ(report.mEvents.size(), 1u);
        const EventMetrics& walk = report.mEvents[0];
        EXPECT_EQ(walk.mName, "walk");
        EXPECT_EQ(walk.mFrame, 10u);
        EXPECT_NEAR(walk.mStepAtEvent, 2.0, 1e-9);
        EXPECT_EQ(walk.mStepAtEventBone, "bip01 spine1");
        EXPECT_NEAR(walk.mStepAfter, 2.0, 1e-9);
    }

    TEST(AnimTraceMeasure, JerkIsTheLargestSecondDifferenceInTheWindow)
    {
        // The step goes from 0 to 2 degrees at frame 10 (jerk 2) and back to 0 at frame 15 (jerk 2).
        const Report report = measure(rampTrace(), Options{}, "ramp");
        EXPECT_NEAR(report.mEvents[0].mJerk, 2.0, 1e-9);
    }

    TEST(AnimTraceMeasure, AMotionOutsideTheWindowIsNotCounted)
    {
        Trace trace = traceOfAngles(rampAngles(40, 25, 5, 2.0));
        trace.mEvents.push_back({ "stop", 3.0, 10 });
        const Report report = measure(trace, Options{}, "late");
        EXPECT_DOUBLE_EQ(report.mEvents[0].mStepAtEvent, 0.0);
        EXPECT_DOUBLE_EQ(report.mEvents[0].mStepAfter, 0.0);
        EXPECT_DOUBLE_EQ(report.mEvents[0].mJerk, 0.0);
    }

    TEST(AnimTraceMeasure, TheWindowIsAnOption)
    {
        Trace trace = traceOfAngles(rampAngles(40, 14, 2, 2.0));
        trace.mEvents.push_back({ "walk", 3.0, 10 });
        Options narrow;
        narrow.mWindowFrames = 3; // frames 11..13: nothing moves yet
        EXPECT_DOUBLE_EQ(measure(trace, narrow, "").mEvents[0].mStepAfter, 0.0);
        Options wide;
        wide.mWindowFrames = 8;
        EXPECT_NEAR(measure(trace, wide, "").mEvents[0].mStepAfter, 2.0, 1e-9);
    }

    TEST(AnimTraceMeasure, SettlesAtTheFirstRunOfStillFrames)
    {
        // Steps are 2 degrees at frames 10..14 and 0 from frame 15: the run of 8 still frames starts at frame 15,
        // 5 frames after the event, 0.1 s at 20 ms a frame.
        const Report report = measure(rampTrace(), Options{}, "ramp");
        const EventMetrics& walk = report.mEvents[0];
        EXPECT_TRUE(walk.mSettled);
        EXPECT_EQ(walk.mSettleFrames, 5u);
        EXPECT_NEAR(walk.mSettleSeconds, 0.1, 1e-12);
    }

    TEST(AnimTraceMeasure, ASteadyMotionNeverSettles)
    {
        Trace trace = traceOfAngles(rampAngles(60, 10, 50, 2.0));
        trace.mEvents.push_back({ "walk", 3.0, 10 });
        EXPECT_FALSE(measure(trace, Options{}, "").mEvents[0].mSettled);
    }

    TEST(AnimTraceMeasure, TheSettleLimitAndCountAreOptions)
    {
        const Trace trace = rampTrace();
        Options loose;
        loose.mSettleStepDegrees = 2.5; // 2 degree steps already count as still
        const Report first = measure(trace, loose, "");
        EXPECT_TRUE(first.mEvents[0].mSettled);
        EXPECT_EQ(first.mEvents[0].mSettleFrames, 1u);
        Options longRun;
        longRun.mSettleFrames = 100; // more still frames than the trace has
        EXPECT_FALSE(measure(trace, longRun, "").mEvents[0].mSettled);
    }

    TEST(AnimTraceMeasure, ASettleRunStopsAtTheNextEvent)
    {
        // Still frames from 15 on would settle with 3 frames (15, 16, 17), but a run of 4 would reach frame 18,
        // which belongs to the next event.
        Trace trace = rampTrace();
        trace.mEvents.push_back({ "run", 3.5, 18 });
        Options options;
        options.mSettleFrames = 3;
        const Report within = measure(trace, options, "");
        EXPECT_TRUE(within.mEvents[0].mSettled);
        EXPECT_EQ(within.mEvents[0].mSettleFrames, 5u);
        EXPECT_EQ(within.mEvents[0].mWindowEnd, 18u);
        options.mSettleFrames = 4;
        EXPECT_FALSE(measure(trace, options, "").mEvents[0].mSettled);
    }

    TEST(AnimTraceMeasure, TheWorstBoneAtTheEventIsNamed)
    {
        Trace trace = traceOfAngles(rampAngles(30, 10, 5, 2.0));
        const std::vector<double> big = rampAngles(30, 10, 5, 5.0);
        for (std::size_t i = 0; i < trace.mSamples.size(); ++i)
        {
            trace.mSamples[i].mBones.push_back({ "bip01 l thigh", aboutZ(big[i]) });
        }
        trace.mEvents.push_back({ "walk", 3.0, 10 });
        const Report report = measure(trace, Options{}, "");
        EXPECT_NEAR(report.mEvents[0].mStepAtEvent, 5.0, 1e-9);
        EXPECT_EQ(report.mEvents[0].mStepAtEventBone, "bip01 l thigh");
        ASSERT_EQ(report.mBoneNames.size(), 2u);
        EXPECT_EQ(report.mBoneNames[0], "bip01 spine1");
    }

    TEST(AnimTraceMeasure, SpeedStepIsTheLargestChangeOfGroundSpeed)
    {
        Trace trace = traceOfAngles(std::vector<double>(30, 0.0));
        for (std::size_t i = 0; i < trace.mSamples.size(); ++i)
        {
            // 0 u/s before frame 12, then 20 u/s, then 70 u/s from frame 14 (dt is 0.02 s, moved is per frame).
            const double speed = i < 12 ? 0.0 : (i < 14 ? 20.0 : 70.0);
            trace.mSamples[i].mMoved[0] = speed * 0.02;
        }
        trace.mEvents.push_back({ "walk", 3.0, 10 });
        EXPECT_NEAR(measure(trace, Options{}, "").mEvents[0].mSpeedStep, 50.0, 1e-9);
    }

    TEST(AnimTraceMeasure, EventsPastTheEndAndEmptyTracesGiveZeros)
    {
        Trace empty;
        empty.mEvents.push_back({ "walk", 1.0, 0 });
        const Report none = measure(empty, Options{}, "none");
        EXPECT_EQ(none.mSampleCount, 0u);
        EXPECT_TRUE(none.mEvents.empty());

        Trace trace = traceOfAngles(std::vector<double>(5, 0.0));
        trace.mEvents.push_back({ "late", 9.0, 50 });
        const Report late = measure(trace, Options{}, "late");
        ASSERT_EQ(late.mEvents.size(), 1u);
        EXPECT_DOUBLE_EQ(late.mEvents[0].mStepAtEvent, 0.0);
        EXPECT_FALSE(late.mEvents[0].mSettled);
    }

    TEST(AnimTraceMeasure, TheReportCountsFramesTimeAndTheLongestFrame)
    {
        Trace trace = traceOfAngles(std::vector<double>(10, 0.0));
        trace.mSamples[4].mDt = 0.1;
        const Report report = measure(trace, Options{}, "t");
        EXPECT_EQ(report.mSampleCount, 10u);
        EXPECT_NEAR(report.mSeconds, 9 * 0.02 + 0.1, 1e-12);
        EXPECT_NEAR(report.mLongestFrame, 0.1, 1e-12);
    }

    TEST(AnimTraceFormat, TheSameTraceGivesTheSameBytes)
    {
        const Report a = measure(rampTrace(), Options{}, "x");
        const Report b = measure(rampTrace(), Options{}, "x");
        EXPECT_EQ(formatJson(a), formatJson(b));
        EXPECT_EQ(formatText(a), formatText(b));
    }

    TEST(AnimTraceFormat, JsonIsCanonicalAndEscapesTheLabel)
    {
        const Report report = measure(rampTrace(), Options{}, "a \"b\"\\c");
        const std::string json = formatJson(report);
        EXPECT_EQ(json.front(), '{');
        EXPECT_EQ(json.back(), '\n');
        EXPECT_NE(json.find("\"label\": \"a \\\"b\\\"\\\\c\""), std::string::npos);
        EXPECT_NE(json.find("\"name\": \"walk\", \"frame\": 10"), std::string::npos);
        EXPECT_NE(json.find("\"stepAtEvent\": 2.000"), std::string::npos);
        EXPECT_NE(json.find("\"settled\": true, \"settleFrames\": 5, \"settleSeconds\": 0.100"), std::string::npos);
    }

    TEST(AnimTraceFormat, ANegativeZeroIsNotPrinted)
    {
        Trace trace = traceOfAngles(std::vector<double>(12, 0.0));
        trace.mEvents.push_back({ "idle", -0.0, 3 });
        const std::string json = formatJson(measure(trace, Options{}, ""));
        EXPECT_EQ(json.find("-0.000"), std::string::npos);
    }

    TEST(AnimTraceFormat, TextNamesEveryEvent)
    {
        Trace trace = rampTrace();
        trace.mEvents.push_back({ "run", 3.5, 20 });
        const std::string text = formatText(measure(trace, Options{}, "scenario"));
        EXPECT_NE(text.find("scenario"), std::string::npos);
        EXPECT_NE(text.find("walk"), std::string::npos);
        EXPECT_NE(text.find("run"), std::string::npos);
    }
}
