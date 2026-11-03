// Task 0067, stage 0: reading the game's CMTRACE and CMPHASE log lines (components/animtrace/tracemetrics.hpp).
// The sample lines are copies of what CharacterController::traceAnimation writes.

#include <components/animtrace/tracemetrics.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace
{
    using namespace AnimTrace;

    const std::string sBones
        = " bip01 pelvis=(0.0000 0.0000 0.0000 1.0000) bip01 l thigh=(0.1000 0.2000 -0.3000 0.9000)"
          " bip01 r thigh=(0.0000 0.0000 0.7071 0.7071) bip01 spine1=(0.0000 0.0000 0.0000 1.0000)"
          " bip01 l upperarm=(0.0000 0.0000 0.0000 1.0000) bip01 head=(0.0000 0.0000 0.0000 1.0000)";

    std::string traceLine(const std::string& ref, const std::string& extra = "", const std::string& dt = "0.016667")
    {
        return "[21:22:11.245 I] CMTRACE ref=" + ref + " dt=" + dt
            + " g0=walkforward(prev=idle,trans=0.100000/0.250000) g1=walkforward g2=idle g3=idle"
              " pos=(1.000 2.000 3.000) moved=(0.500 -1.500 0.250) loco=(speed 80.5 phase 2) animspeed=0.625"
              " driver=applies"
            + extra + sBones + "\n";
    }

    Trace parse(const std::string& text, const Options& options = Options{})
    {
        std::istringstream in(text);
        return parseLog(in, options);
    }

    TEST(AnimTraceParse, ReadsEveryFieldOfATraceLine)
    {
        const Trace trace = parse(traceLine("player"));
        ASSERT_EQ(trace.mSamples.size(), 1u);
        const Sample& sample = trace.mSamples[0];
        EXPECT_NEAR(sample.mDt, 0.016667, 1e-9);
        EXPECT_EQ(sample.mGroups[0], "walkforward");
        EXPECT_EQ(sample.mGroups[1], "walkforward");
        EXPECT_EQ(sample.mGroups[2], "idle");
        EXPECT_EQ(sample.mGroups[3], "idle");
        EXPECT_NEAR(sample.mMoved[0], 0.5, 1e-9);
        EXPECT_NEAR(sample.mMoved[1], -1.5, 1e-9);
        EXPECT_NEAR(sample.mMoved[2], 0.25, 1e-9);
        EXPECT_NEAR(sample.mLocoSpeed, 80.5, 1e-9);
        EXPECT_NEAR(sample.mAnimSpeed, 0.625, 1e-9);
        ASSERT_EQ(sample.mBones.size(), 6u);
        // Bone names have spaces in them.
        EXPECT_EQ(sample.mBones[0].mName, "bip01 pelvis");
        EXPECT_EQ(sample.mBones[1].mName, "bip01 l thigh");
        EXPECT_EQ(sample.mBones[4].mName, "bip01 l upperarm");
        EXPECT_NEAR(sample.mBones[1].mRotation.mX, 0.1, 1e-9);
        EXPECT_NEAR(sample.mBones[1].mRotation.mY, 0.2, 1e-9);
        EXPECT_NEAR(sample.mBones[1].mRotation.mZ, -0.3, 1e-9);
        EXPECT_NEAR(sample.mBones[1].mRotation.mW, 0.9, 1e-9);
    }

    TEST(AnimTraceParse, TheStyleTextBeforeTheBonesIsNotABone)
    {
        // With the style layer on, the line has " style=cm_default(on) w=(move 0.50 idle 1.00 posture 0.00)
        // cadence=1.00" before the bones; its `w=(` must not be read as a bone.
        const Trace trace
            = parse(traceLine("player", " style=cm_default(on) w=(move 0.50 idle 1.00 posture 0.00) cadence=1.00"));
        ASSERT_EQ(trace.mSamples.size(), 1u);
        EXPECT_EQ(trace.mSamples[0].mBones.size(), 6u);
        EXPECT_EQ(trace.mSamples[0].mBones[0].mName, "bip01 pelvis");
    }

    TEST(AnimTraceParse, ALineWithoutADriverHasGroupsWithoutTransitionText)
    {
        std::string line
            = "CMTRACE ref=player dt=0.020000 g0=idle g1=idle g2=idle g3=idle pos=(0.000 0.000 0.000)"
              " moved=(0.000 0.000 0.000) loco=(speed 0.0 phase 0) animspeed=1.000 driver=off"
            + sBones + "\n";
        const Trace trace = parse(line);
        ASSERT_EQ(trace.mSamples.size(), 1u);
        EXPECT_EQ(trace.mSamples[0].mGroups[0], "idle");
        EXPECT_EQ(trace.mSamples[0].mGroups[3], "idle");
    }

    TEST(AnimTraceParse, OnlyTheChosenActorIsRead)
    {
        const std::string text = traceLine("player") + traceLine("fargoth") + traceLine("player");
        EXPECT_EQ(parse(text).mSamples.size(), 2u);
        Options fargoth;
        fargoth.mRef = "Fargoth";
        EXPECT_EQ(parse(text, fargoth).mSamples.size(), 1u);
    }

    TEST(AnimTraceParse, TheActorNameIgnoresCaseAndQuotationMarks)
    {
        const std::string text = traceLine("\"Player\"") + traceLine("PLAYER");
        EXPECT_EQ(parse(text).mSamples.size(), 2u);
    }

    TEST(AnimTraceParse, PhaseLinesBecomeEventsAtTheNextFrame)
    {
        const std::string text = traceLine("player") + traceLine("player") + "[21:22:11.300 I] CMPHASE walk t=2.000\n"
            + traceLine("player") + "[21:22:11.400 I] CMPHASE run t=4.500\n" + traceLine("player");
        const Trace trace = parse(text);
        ASSERT_EQ(trace.mSamples.size(), 4u);
        ASSERT_EQ(trace.mEvents.size(), 2u);
        EXPECT_EQ(trace.mEvents[0].mName, "walk");
        EXPECT_NEAR(trace.mEvents[0].mTime, 2.0, 1e-9);
        EXPECT_EQ(trace.mEvents[0].mFrame, 2u);
        EXPECT_EQ(trace.mEvents[1].mName, "run");
        EXPECT_NEAR(trace.mEvents[1].mTime, 4.5, 1e-9);
        EXPECT_EQ(trace.mEvents[1].mFrame, 3u);
    }

    TEST(AnimTraceParse, OtherActorsLinesDoNotAdvanceTheFrameNumber)
    {
        const std::string text = traceLine("player") + traceLine("fargoth") + traceLine("fargoth")
            + "CMPHASE walk t=1.000\n" + traceLine("player");
        const Trace trace = parse(text);
        ASSERT_EQ(trace.mEvents.size(), 1u);
        EXPECT_EQ(trace.mEvents[0].mFrame, 1u);
    }

    TEST(AnimTraceParse, DamagedLinesAreCountedAndSkipped)
    {
        const std::string damaged = "[21:22:11.245 I] CMTRACE ref=player dt=0.016667 g0=idle\n";
        const std::string text = traceLine("player") + damaged + traceLine("player") + "CMPHASE  t=1.0\n"
            + "CMPHASE walk\n" + "unrelated text\n";
        const Trace trace = parse(text);
        EXPECT_EQ(trace.mSamples.size(), 2u);
        EXPECT_EQ(trace.mSkippedLines, 1u);
        EXPECT_TRUE(trace.mEvents.empty());
    }

    TEST(AnimTraceParse, WindowsLineEndingsAreFine)
    {
        std::string text = traceLine("player") + "CMPHASE stop t=3.250\n";
        std::string crlf;
        for (const char c : text)
        {
            if (c == '\n')
            {
                crlf += "\r\n";
            }
            else
            {
                crlf.push_back(c);
            }
        }
        const Trace trace = parse(crlf);
        ASSERT_EQ(trace.mSamples.size(), 1u);
        ASSERT_EQ(trace.mEvents.size(), 1u);
        EXPECT_EQ(trace.mEvents[0].mName, "stop");
        EXPECT_NEAR(trace.mEvents[0].mTime, 3.25, 1e-9);
    }

    TEST(AnimTraceParse, AnEmptyOrTraceFreeLogGivesAnEmptyTrace)
    {
        EXPECT_TRUE(parse("").mSamples.empty());
        EXPECT_TRUE(parse("[21:22:11.245 I] Loading settings\n").mSamples.empty());
    }

    TEST(AnimTraceParse, ParsedLinesFeedTheMeasurement)
    {
        // Three player frames with the head turning 3 degrees about z each frame, the event in the middle.
        std::string text;
        for (int i = 0; i < 12; ++i)
        {
            const double half = (3.0 * i) * 3.14159265358979323846 / 360.0;
            char head[96];
            std::snprintf(head, sizeof(head), " bip01 head=(0.0000 0.0000 %.6f %.6f)", std::sin(half), std::cos(half));
            text += "CMTRACE ref=player dt=0.020000 g0=idle g1=idle g2=idle g3=idle pos=(0 0 0) moved=(0 0 0)"
                    " loco=(speed 0.0 phase 0) animspeed=1.000 driver=off"
                + std::string(head) + "\n";
            if (i == 5)
            {
                text += "CMPHASE turn t=1.0\n";
            }
        }
        const Report report = measure(parse(text), Options{}, "parsed");
        ASSERT_EQ(report.mEvents.size(), 1u);
        EXPECT_EQ(report.mEvents[0].mFrame, 6u);
        EXPECT_NEAR(report.mEvents[0].mStepAtEvent, 3.0, 1e-4);
        EXPECT_EQ(report.mEvents[0].mStepAtEventBone, "bip01 head");
    }
}
