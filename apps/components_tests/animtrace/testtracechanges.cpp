// Task 0067, stage 1 review fixes: the blend measurements anchored at the frame where a bone group really changes its
// animation (components/animtrace/tracemetrics.hpp), from synthetic traces with answers known by hand. The trace text
// copies what CharacterController::traceAnimation writes: `g<n>=<group>(prev=<group>,trans=<elapsed>/<duration>|none)`
// per bone group, `intent=(...)` and the un-offset target rotations `tgt=[...]` of the traced bones.

#include <components/animtrace/tracemetrics.hpp>
#include <components/animtrace/tracetool.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace AnimTrace;

    constexpr double sPi = 3.14159265358979323846;

    Quat aboutZ(double degrees)
    {
        const double half = degrees * sPi / 360.0;
        return { 0.0, 0.0, std::sin(half), std::cos(half) };
    }

    std::string quatText(const Quat& q)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "(%.6f %.6f %.6f %.6f)", q.mX, q.mY, q.mZ, q.mW);
        return buffer;
    }

    struct Frame
    {
        std::string mGroup0 = "idle";
        std::string mPrev0;
        std::string mTrans0 = "none";
        std::string mGroup1 = "idle";
        double mApplied = 0.0; // degrees about z of the traced bone
        double mTarget = 0.0;
        bool mWithTarget = true;
        std::string mIntent; // empty: no intent field
    };

    std::string line(const Frame& f)
    {
        std::string text = "[00:00:00.000 I] CMTRACE ref=player dt=0.020000 g0=" + f.mGroup0;
        if (!f.mPrev0.empty() || f.mTrans0 != "none")
            text += "(prev=" + f.mPrev0 + ",trans=" + f.mTrans0 + ")";
        text += " g1=" + f.mGroup1 + " g2=idle g3=idle pos=(0.000 0.000 0.000) moved=(0.000 0.000 0.000)"
                                     " loco=(speed 0.0 phase 0) animspeed=1.000 driver=applies";
        if (!f.mIntent.empty())
            text += " intent=(" + f.mIntent + ")";
        text += " bip01 spine1=" + quatText(aboutZ(f.mApplied));
        if (f.mWithTarget)
            text += " tgt=[bip01 spine1=" + quatText(aboutZ(f.mTarget)) + "]";
        return text + "\n";
    }

    Trace parse(const std::string& text)
    {
        std::istringstream in(text);
        return parseLog(in, Options{});
    }

    /// Idle for 10 frames, then walk: the transition lasts `frames` frames of 20 ms, the target stands at 0 and the
    /// applied rotation starts `offset` degrees away and closes linearly.
    std::string blendTrace(double offset, int frames, double target = 0.0)
    {
        std::string text;
        for (int i = 0; i < 10; ++i)
        {
            Frame f;
            f.mApplied = offset;
            f.mTarget = offset;
            text += line(f);
        }
        const double duration = 0.02 * frames;
        for (int i = 0; i <= frames + 5; ++i)
        {
            Frame f;
            f.mGroup0 = "walkforward";
            f.mPrev0 = "idle";
            const double elapsed = 0.02 * i;
            if (i < frames)
            {
                char trans[64];
                std::snprintf(trans, sizeof(trans), "%.6f/%.6f", elapsed, duration);
                f.mTrans0 = trans;
            }
            const double left = i < frames ? offset * (1.0 - static_cast<double>(i) / frames) : 0.0;
            f.mTarget = target;
            f.mApplied = target + left;
            text += line(f);
        }
        return text;
    }
}

TEST(AnimTraceChanges, ReadsPreviousGroupAndTransitionOfEachGroup)
{
    Frame f;
    f.mGroup0 = "walkforward";
    f.mPrev0 = "idle";
    f.mTrans0 = "0.100000/0.375000";
    const Trace trace = parse(line(f));
    ASSERT_EQ(trace.mSamples.size(), 1u);
    const Sample& s = trace.mSamples[0];
    EXPECT_EQ(s.mGroups[0], "walkforward");
    EXPECT_EQ(s.mGroupStates[0].mPrevious, "idle");
    EXPECT_TRUE(s.mGroupStates[0].mHasDriver);
    EXPECT_TRUE(s.mGroupStates[0].mTransitionActive);
    EXPECT_NEAR(s.mGroupStates[0].mElapsed, 0.1, 1e-9);
    EXPECT_NEAR(s.mGroupStates[0].mDuration, 0.375, 1e-9);
    // g1 has no driver text in this line.
    EXPECT_FALSE(s.mGroupStates[1].mHasDriver);
    EXPECT_FALSE(s.mGroupStates[1].mTransitionActive);
}

TEST(AnimTraceChanges, TransNoneIsNoActiveTransition)
{
    Frame f;
    f.mGroup0 = "idle";
    f.mPrev0 = "walkforward";
    f.mTrans0 = "none";
    const Trace trace = parse(line(f));
    ASSERT_EQ(trace.mSamples.size(), 1u);
    EXPECT_TRUE(trace.mSamples[0].mGroupStates[0].mHasDriver);
    EXPECT_FALSE(trace.mSamples[0].mGroupStates[0].mTransitionActive);
    EXPECT_EQ(trace.mSamples[0].mGroupStates[0].mPrevious, "walkforward");
}

TEST(AnimTraceChanges, ReadsTheIntentField)
{
    Frame f;
    f.mIntent = "wants 1 start 0 stop 1 stance 1 turn -1.250";
    const Trace trace = parse(line(f));
    ASSERT_EQ(trace.mSamples.size(), 1u);
    const Sample& s = trace.mSamples[0];
    ASSERT_TRUE(s.mHasIntent);
    EXPECT_TRUE(s.mIntent.mWants);
    EXPECT_FALSE(s.mIntent.mStarting);
    EXPECT_TRUE(s.mIntent.mStopping);
    EXPECT_TRUE(s.mIntent.mStance);
    EXPECT_NEAR(s.mIntent.mTurnRate, -1.25, 1e-9);
}

TEST(AnimTraceChanges, TargetsAreReadAndAreNotBones)
{
    Frame f;
    f.mApplied = 10.0;
    f.mTarget = 4.0;
    const Trace trace = parse(line(f));
    ASSERT_EQ(trace.mSamples.size(), 1u);
    const Sample& s = trace.mSamples[0];
    ASSERT_EQ(s.mBones.size(), 1u);
    EXPECT_EQ(s.mBones[0].mName, "bip01 spine1");
    ASSERT_EQ(s.mTargets.size(), 1u);
    EXPECT_EQ(s.mTargets[0].mName, "bip01 spine1");
    EXPECT_NEAR(angleDegrees(s.mBones[0].mRotation, s.mTargets[0].mRotation), 6.0, 1e-4);
}

TEST(AnimTraceChanges, ALineWithoutTheNewFieldsHasNoIntentAndNoTargets)
{
    Frame f;
    f.mWithTarget = false;
    const Trace trace = parse(line(f));
    ASSERT_EQ(trace.mSamples.size(), 1u);
    EXPECT_FALSE(trace.mSamples[0].mHasIntent);
    EXPECT_TRUE(trace.mSamples[0].mTargets.empty());
    EXPECT_EQ(trace.mSamples[0].mBones.size(), 1u);
}

TEST(AnimTraceChanges, AChangeIsAnchoredWhereTheGroupChangesWithItsDuration)
{
    const Trace trace = parse(blendTrace(10.0, 5));
    const Report report = measure(trace, Options{}, "blend");
    ASSERT_EQ(report.mChanges.size(), 1u);
    const GroupChangeMetrics& c = report.mChanges[0];
    EXPECT_EQ(c.mGroup, 0u);
    EXPECT_EQ(c.mFrame, 10u);
    EXPECT_EQ(c.mFrom, "idle");
    EXPECT_EQ(c.mTo, "walkforward");
    EXPECT_NEAR(c.mDurationS, 0.1, 1e-9);
    EXPECT_EQ(c.mDurationFrames, 5u);
}

TEST(AnimTraceChanges, TheOffsetStartsAtItsPeakAndIsGoneAfterTheDuration)
{
    const Trace trace = parse(blendTrace(10.0, 5));
    const Report report = measure(trace, Options{}, "blend");
    ASSERT_EQ(report.mChanges.size(), 1u);
    const GroupChangeMetrics& c = report.mChanges[0];
    ASSERT_TRUE(c.mHasTargets);
    EXPECT_NEAR(c.mOffsetAtChange, 10.0, 1e-3);
    EXPECT_NEAR(c.mOffsetPeak, 10.0, 1e-3);
    EXPECT_NEAR(c.mOffsetLeft, 0.0, 1e-3);
    // Linear decay over 5 frames: under 10 percent of the peak from the fifth frame on (2 degrees at frame 4).
    EXPECT_EQ(c.mDecayFrames, 5u);
    EXPECT_NEAR(c.mDecaySeconds, 0.1, 1e-9);
}

TEST(AnimTraceChanges, ExcessJerkIsZeroWhenTheAppliedPoseIsTheTarget)
{
    const Trace trace = parse(blendTrace(0.0, 5));
    const Report report = measure(trace, Options{}, "noblend");
    ASSERT_EQ(report.mChanges.size(), 1u);
    EXPECT_NEAR(report.mChanges[0].mExcessJerk, 0.0, 1e-6);
    EXPECT_NEAR(report.mChanges[0].mOffsetPeak, 0.0, 1e-6);
}

TEST(AnimTraceChanges, ExcessJerkMeasuresTheBlendNotTheClip)
{
    // The target turns at a constant 3 degrees per frame (no jerk of its own); the applied pose follows it with an
    // offset that closes linearly: the change of slope at the start and at the end of the blend is the excess jerk.
    std::string text;
    for (int i = 0; i < 10; ++i)
    {
        Frame f;
        f.mApplied = 3.0 * i;
        f.mTarget = 3.0 * i;
        text += line(f);
    }
    for (int i = 0; i <= 10; ++i)
    {
        Frame f;
        f.mGroup0 = "walkforward";
        f.mPrev0 = "idle";
        if (i < 5)
        {
            char trans[64];
            std::snprintf(trans, sizeof(trans), "%.6f/%.6f", 0.02 * i, 0.1);
            f.mTrans0 = trans;
        }
        const double target = 3.0 * (10 + i);
        const double left = i < 5 ? 5.0 * (1.0 - i / 5.0) : 0.0;
        f.mTarget = target;
        f.mApplied = target + left;
        text += line(f);
    }
    const Report report = measure(parse(text), Options{}, "excess");
    ASSERT_EQ(report.mChanges.size(), 1u);
    // At the change the applied pose jumps 5 degrees ahead of the target (a step of 8 against 3), then each frame
    // closes 1 degree (steps of 2 against 3): the slope goes from 8 to 2, a second difference of 6 degrees, while
    // the target's own is 0.
    EXPECT_NEAR(report.mChanges[0].mExcessJerk, 6.0, 1e-3);
}

TEST(AnimTraceChanges, WithoutTargetsTheOffsetIsNotMeasured)
{
    std::string text;
    for (int i = 0; i < 4; ++i)
    {
        Frame f;
        f.mWithTarget = false;
        text += line(f);
    }
    for (int i = 0; i < 4; ++i)
    {
        Frame f;
        f.mWithTarget = false;
        f.mGroup0 = "walkforward";
        text += line(f);
    }
    const Report report = measure(parse(text), Options{}, "vanilla");
    ASSERT_EQ(report.mChanges.size(), 1u);
    EXPECT_FALSE(report.mChanges[0].mHasTargets);
    EXPECT_NEAR(report.mChanges[0].mDurationS, 0.0, 1e-12);
    EXPECT_EQ(report.mChanges[0].mFrame, 4u);
}

TEST(AnimTraceChanges, EveryChangeOfEveryGroupIsReportedInFrameOrder)
{
    std::string text;
    const char* groups0[] = { "idle", "idle", "walkforward", "walkforward", "idle", "idle" };
    const char* groups1[] = { "idle", "idle", "idle", "weapononehand", "weapononehand", "idle" };
    for (int i = 0; i < 6; ++i)
    {
        Frame f;
        f.mGroup0 = groups0[i];
        f.mGroup1 = groups1[i];
        text += line(f);
    }
    const Report report = measure(parse(text), Options{}, "many");
    ASSERT_EQ(report.mChanges.size(), 4u);
    EXPECT_EQ(report.mChanges[0].mFrame, 2u);
    EXPECT_EQ(report.mChanges[0].mGroup, 0u);
    EXPECT_EQ(report.mChanges[1].mFrame, 3u);
    EXPECT_EQ(report.mChanges[1].mGroup, 1u);
    EXPECT_EQ(report.mChanges[2].mFrame, 4u);
    EXPECT_EQ(report.mChanges[2].mGroup, 0u);
    EXPECT_EQ(report.mChanges[2].mTo, "idle");
    EXPECT_EQ(report.mChanges[3].mFrame, 5u);
    EXPECT_EQ(report.mChanges[3].mGroup, 1u);
    EXPECT_EQ(report.mChanges[3].mOccurrence, 0u); // the first weapononehand -> idle of g1
}

TEST(AnimTraceChanges, OccurrenceCountsRepeatsOfTheSameChange)
{
    std::string text;
    const char* groups0[] = { "idle", "walkforward", "idle", "walkforward", "idle" };
    for (const char* g : groups0)
    {
        Frame f;
        f.mGroup0 = g;
        text += line(f);
    }
    const Report report = measure(parse(text), Options{}, "repeats");
    ASSERT_EQ(report.mChanges.size(), 4u);
    EXPECT_EQ(report.mChanges[0].mOccurrence, 0u); // idle -> walkforward
    EXPECT_EQ(report.mChanges[1].mOccurrence, 0u); // walkforward -> idle
    EXPECT_EQ(report.mChanges[2].mOccurrence, 1u); // idle -> walkforward again
    EXPECT_EQ(report.mChanges[3].mOccurrence, 1u);
}

TEST(AnimTraceChanges, IntentFramesAreCounted)
{
    std::string text;
    const char* intents[]
        = { "wants 0 start 0 stop 0 stance 0 turn 0.000", "wants 1 start 1 stop 0 stance 0 turn 0.000",
              "wants 1 start 0 stop 0 stance 1 turn 2.000", "wants 0 start 0 stop 1 stance 0 turn 0.000" };
    for (const char* intent : intents)
    {
        Frame f;
        f.mIntent = intent;
        text += line(f);
    }
    Frame plain;
    text += line(plain);
    const Report report = measure(parse(text), Options{}, "intent");
    EXPECT_EQ(report.mIntentFrames, 4u);
    EXPECT_EQ(report.mWantsFrames, 2u);
    EXPECT_EQ(report.mStartingFrames, 1u);
    EXPECT_EQ(report.mStoppingFrames, 1u);
    EXPECT_EQ(report.mStanceFrames, 1u);
}

TEST(AnimTraceChanges, SummaryGivesMinMaxAndMeanOverRuns)
{
    std::vector<Report> reports;
    for (const double offset : { 10.0, 11.0, 12.0 })
        reports.push_back(measure(parse(blendTrace(offset, 5)), Options{}, "run"));
    const Summary summary = summarise(reports, "natural");
    EXPECT_EQ(summary.mRuns, 3u);
    ASSERT_EQ(summary.mChanges.size(), 1u);
    const ChangeSummary& c = summary.mChanges[0];
    EXPECT_EQ(c.mRuns, 3u);
    EXPECT_EQ(c.mFrom, "idle");
    EXPECT_EQ(c.mTo, "walkforward");
    EXPECT_NEAR(c.mOffsetPeak.mMin, 10.0, 1e-3);
    EXPECT_NEAR(c.mOffsetPeak.mMax, 12.0, 1e-3);
    EXPECT_NEAR(c.mOffsetPeak.mMean, 11.0, 1e-3);
    EXPECT_NEAR(c.mDurationS.mMin, 0.1, 1e-9);
    EXPECT_NEAR(c.mDurationS.mMax, 0.1, 1e-9);
}

TEST(AnimTraceChanges, SummaryMatchesChangesByGroupKindsAndOccurrence)
{
    std::vector<Report> reports;
    reports.push_back(measure(parse(blendTrace(10.0, 5)), Options{}, "a"));
    // The second run has no change at all.
    std::string still;
    for (int i = 0; i < 5; ++i)
        still += line(Frame{});
    reports.push_back(measure(parse(still), Options{}, "b"));
    const Summary summary = summarise(reports, "mixed");
    EXPECT_EQ(summary.mRuns, 2u);
    ASSERT_EQ(summary.mChanges.size(), 1u);
    EXPECT_EQ(summary.mChanges[0].mRuns, 1u);
}

TEST(AnimTraceChanges, JsonListsTheChangesAndTheMeta)
{
    Report report = measure(parse(blendTrace(10.0, 5)), Options{}, "json");
    report.mMeta.emplace_back("commit", "abc123");
    report.mMeta.emplace_back("extra", "[CreativeMotion Transitions]\nstopping = 2");
    const std::string json = formatJson(report);
    EXPECT_NE(json.find("\"changes\": ["), std::string::npos);
    EXPECT_NE(json.find("\"from\": \"idle\""), std::string::npos);
    EXPECT_NE(json.find("\"durationSeconds\": 0.100"), std::string::npos);
    EXPECT_NE(
        json.find("\"meta\": {\"commit\": \"abc123\", \"extra\": \"[CreativeMotion Transitions]\\nstopping = 2\"}"),
        std::string::npos);
    EXPECT_EQ(json, formatJson(report)); // deterministic
}

TEST(AnimTraceChanges, ToolTakesMetaAndSummarisesSeveralLogs)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openeden_animtrace_changes";
    std::filesystem::create_directories(dir);
    std::vector<std::string> paths;
    for (const double offset : { 10.0, 12.0 })
    {
        const std::filesystem::path path = dir / ("run" + std::to_string(static_cast<int>(offset)) + ".log");
        std::ofstream(path, std::ios::binary) << blendTrace(offset, 5);
        paths.push_back(path.string());
    }
    {
        std::ostringstream out;
        std::ostringstream err;
        const int status = runTraceTool({ "--json", "--meta", "commit=abc123", "--label", "one", paths[0] }, out, err);
        EXPECT_EQ(status, 0) << err.str();
        EXPECT_NE(out.str().find("\"commit\": \"abc123\""), std::string::npos);
    }
    {
        std::ostringstream out;
        std::ostringstream err;
        const int status = runTraceTool({ "--summary", "--label", "two", paths[0], paths[1] }, out, err);
        EXPECT_EQ(status, 0) << err.str();
        EXPECT_NE(out.str().find("runs 2"), std::string::npos) << out.str();
        EXPECT_NE(out.str().find("idle -> walkforward"), std::string::npos) << out.str();
    }
    {
        std::ostringstream out;
        std::ostringstream err;
        EXPECT_EQ(runTraceTool({ "--meta", "nokeyvalue", paths[0] }, out, err), 2);
    }
    std::filesystem::remove_all(dir);
}
