// Tests of the pure parts of task 0067, stage 1: the actor's intent (IntentTracker), the transition durations that use
// it, their data (makeTransitionTimes) and the preset that switches the CreativeMotion pieces on together. Written from
// the task spec (docs/tasks/0067-creativemotion-in-game-runtime.md, stage 1) on synthetic movement sequences; no GPU,
// window, network or game data.

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/creativemotionstyle.hpp>

#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace MWRender;

    constexpr float sFrame = 1.f / 60.f;
    constexpr float sWalkMps = 1.5f;

    // Feeds the tracker the movement of an actor whose speed follows a script, one frame at a time.
    struct Walker
    {
        IntentTracker mTracker;
        ActorIntent mLast;
        float mSpeed = 0.f; // what the engine would measure this frame

        ActorIntent step(bool wantsToMove, float measured, float yawDelta = 0.f)
        {
            IntentInput input;
            input.mDt = sFrame;
            input.mMeasuredSpeedMps = measured;
            input.mWantsToMove = wantsToMove;
            input.mIntendedSpeedMps = wantsToMove ? sWalkMps : 0.f;
            input.mYawDeltaRad = yawDelta;
            mLast = mTracker.update(input);
            mSpeed = measured;
            return mLast;
        }

        // The actor holds the keys down for frames frames, speeding up linearly to the walking speed in rampFrames.
        void walk(int frames, int rampFrames)
        {
            for (int i = 0; i < frames; ++i)
            {
                const float t = std::min(1.f, static_cast<float>(i + 1) / static_cast<float>(rampFrames));
                step(true, sWalkMps * t);
            }
        }
    };

    struct SettingsMapsGuard
    {
        decltype(Settings::Manager::mUserSettings) mUser = Settings::Manager::mUserSettings;
        decltype(Settings::Manager::mDefaultSettings) mDefault = Settings::Manager::mDefaultSettings;
        ~SettingsMapsGuard()
        {
            Settings::Manager::mUserSettings = mUser;
            Settings::Manager::mDefaultSettings = mDefault;
        }
    };
}

// --- IntentTracker ---------------------------------------------------------------------------------------------

TEST(CreativeMotionIntentTest, StandingStillHasNoIntent)
{
    Walker run;
    for (int i = 0; i < 120; ++i)
        run.step(false, 0.f);
    EXPECT_FALSE(run.mLast.mStarting);
    EXPECT_FALSE(run.mLast.mStopping);
    EXPECT_FLOAT_EQ(run.mLast.mTargetSpeedMps, 0.f);
    EXPECT_NEAR(run.mLast.mSmoothedSpeedMps, 0.f, 1e-6f);
    EXPECT_NEAR(run.mLast.mAccelerationMps2, 0.f, 1e-6f);
    EXPECT_NEAR(run.mLast.mTurnRate, 0.f, 1e-6f);
}

TEST(CreativeMotionIntentTest, PressingTheKeyStartsAndTheTargetIsTheIntendedSpeed)
{
    Walker run;
    for (int i = 0; i < 20; ++i)
        run.step(false, 0.f);
    const ActorIntent first = run.step(true, 0.2f);
    EXPECT_TRUE(first.mStarting);
    EXPECT_FALSE(first.mStopping);
    EXPECT_FLOAT_EQ(first.mTargetSpeedMps, sWalkMps);
    EXPECT_GT(first.mAccelerationMps2, 0.f);
}

TEST(CreativeMotionIntentTest, StartingEndsWhenTheSpeedIsReached)
{
    Walker run;
    run.walk(10, 10);
    EXPECT_TRUE(run.mLast.mStarting);
    run.walk(240, 1); // steady walking for 4 s
    EXPECT_FALSE(run.mLast.mStarting);
    EXPECT_FALSE(run.mLast.mStopping);
    EXPECT_NEAR(run.mLast.mSmoothedSpeedMps, sWalkMps, 0.02f);
    EXPECT_NEAR(run.mLast.mAccelerationMps2, 0.f, 0.05f);
}

TEST(CreativeMotionIntentTest, LettingGoOfTheKeysIsAStop)
{
    Walker run;
    run.walk(240, 1);
    // Let go: the measured speed falls over a quarter of a second.
    ActorIntent intent;
    bool stoppedSeen = false;
    for (int i = 0; i < 15; ++i)
    {
        intent = run.step(false, sWalkMps * (1.f - static_cast<float>(i + 1) / 15.f));
        stoppedSeen = stoppedSeen || intent.mStopping;
        EXPECT_FALSE(intent.mStarting);
    }
    EXPECT_TRUE(stoppedSeen);
    EXPECT_FLOAT_EQ(intent.mTargetSpeedMps, 0.f);
}

TEST(CreativeMotionIntentTest, AccelerationIsNegativeWhileStopping)
{
    Walker run;
    run.walk(240, 1);
    const ActorIntent intent = run.step(false, 0.f);
    EXPECT_LT(intent.mAccelerationMps2, 0.f);
    EXPECT_TRUE(intent.mStopping);
}

TEST(CreativeMotionIntentTest, StoppingEndsOnceTheActorStandsStill)
{
    Walker run;
    run.walk(240, 1);
    for (int i = 0; i < 180; ++i)
        run.step(false, 0.f);
    EXPECT_FALSE(run.mLast.mStopping);
    EXPECT_NEAR(run.mLast.mSmoothedSpeedMps, 0.f, 0.02f);
}

TEST(CreativeMotionIntentTest, BeingBlockedIsNotAStop)
{
    // The keys stay down but a wall stops the actor: the speed falls, the intent is still to move.
    Walker run;
    run.walk(240, 1);
    bool stoppingSeen = false;
    for (int i = 0; i < 30; ++i)
    {
        const ActorIntent intent = run.step(true, 0.f);
        stoppingSeen = stoppingSeen || intent.mStopping;
    }
    EXPECT_FALSE(stoppingSeen);
    EXPECT_FLOAT_EQ(run.mLast.mTargetSpeedMps, sWalkMps);
}

TEST(CreativeMotionIntentTest, TurnRateIsYawChangeOverTime)
{
    Walker run;
    const ActorIntent left = run.step(false, 0.f, 0.05f);
    EXPECT_NEAR(left.mTurnRate, 0.05f / sFrame, 1e-3f);
    const ActorIntent right = run.step(false, 0.f, -0.02f);
    EXPECT_NEAR(right.mTurnRate, -0.02f / sFrame, 1e-3f);
}

TEST(CreativeMotionIntentTest, ZeroFrameTimeChangesNothingAndDividesByNothing)
{
    IntentTracker tracker;
    IntentInput input;
    input.mDt = 0.f;
    input.mMeasuredSpeedMps = 1.f;
    input.mWantsToMove = true;
    input.mIntendedSpeedMps = sWalkMps;
    input.mYawDeltaRad = 0.5f;
    const ActorIntent intent = tracker.update(input);
    EXPECT_TRUE(std::isfinite(intent.mAccelerationMps2));
    EXPECT_TRUE(std::isfinite(intent.mTurnRate));
    EXPECT_NEAR(intent.mSmoothedSpeedMps, 0.f, 1e-6f);
}

TEST(CreativeMotionIntentTest, BadInputsStayFinite)
{
    IntentTracker tracker;
    IntentInput input;
    input.mDt = sFrame;
    input.mMeasuredSpeedMps = std::nanf("");
    input.mIntendedSpeedMps = INFINITY;
    input.mWantsToMove = true;
    input.mYawDeltaRad = std::nanf("");
    const ActorIntent intent = tracker.update(input);
    EXPECT_TRUE(std::isfinite(intent.mSmoothedSpeedMps));
    EXPECT_TRUE(std::isfinite(intent.mAccelerationMps2));
    EXPECT_TRUE(std::isfinite(intent.mTargetSpeedMps));
    EXPECT_TRUE(std::isfinite(intent.mTurnRate));
}

TEST(CreativeMotionIntentTest, SameSequenceGivesSameIntent)
{
    Walker a;
    Walker b;
    for (int i = 0; i < 300; ++i)
    {
        const bool wants = (i / 90) % 2 == 0;
        const float measured = wants ? std::min(sWalkMps, 0.05f * static_cast<float>(i % 90)) : 0.f;
        const ActorIntent x = a.step(wants, measured, 0.001f * static_cast<float>(i % 7));
        const ActorIntent y = b.step(wants, measured, 0.001f * static_cast<float>(i % 7));
        EXPECT_EQ(x.mSmoothedSpeedMps, y.mSmoothedSpeedMps);
        EXPECT_EQ(x.mStarting, y.mStarting);
        EXPECT_EQ(x.mStopping, y.mStopping);
    }
}

TEST(CreativeMotionIntentTest, ResetForgetsTheSpeed)
{
    Walker run;
    run.walk(240, 1);
    run.mTracker.reset();
    const ActorIntent intent = run.step(false, 0.f);
    EXPECT_NEAR(intent.mSmoothedSpeedMps, 0.f, 1e-6f);
    EXPECT_FALSE(intent.mStopping);
}

// --- transitionSeconds with intent ----------------------------------------------------------------------------

TEST(CreativeMotionIntentTest, StopWithIntentTakesTheStoppingTime)
{
    const TransitionTimes times;
    TransitionIntent intent;
    intent.mStopping = true;
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Walk, MotionKind::Idle, 0.25f, intent, times), 0.25f * 1.5f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Run, MotionKind::Idle, 0.25f, intent, times), 0.25f * 1.5f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Sneak, MotionKind::Idle, 0.25f, intent, times), 0.25f * 1.5f);
}

TEST(CreativeMotionIntentTest, StopWithoutIntentIsTheBaseTime)
{
    const TransitionTimes times;
    EXPECT_FLOAT_EQ(
        transitionSeconds(MotionKind::Walk, MotionKind::Idle, 0.25f, TransitionIntent(), times), 0.25f * times.mFlip);
    EXPECT_FLOAT_EQ(times.mFlip, 1.f);
}

TEST(CreativeMotionIntentTest, StartWithIntentTakesTheStartingTime)
{
    const TransitionTimes times;
    TransitionIntent intent;
    intent.mStarting = true;
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Idle, MotionKind::Walk, 0.25f, intent, times), 0.25f * 0.8f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Idle, MotionKind::Run, 0.25f, intent, times), 0.25f * 0.8f);
    // The same change without the intent is the base time.
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Idle, MotionKind::Walk, 0.25f, TransitionIntent(), times), 0.25f);
}

TEST(CreativeMotionIntentTest, TurnInPlaceFollowsTheTurnRate)
{
    const TransitionTimes times;
    TransitionIntent fast;
    fast.mTurnRate = 2.5f;
    TransitionIntent slow;
    slow.mTurnRate = 0.4f;
    TransitionIntent fastLeft;
    fastLeft.mTurnRate = -2.5f; // the sign does not matter
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Idle, MotionKind::Turn, 0.25f, fast, times), 0.25f * 0.6f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Turn, MotionKind::Idle, 0.25f, fastLeft, times), 0.25f * 0.6f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Idle, MotionKind::Turn, 0.25f, slow, times), 0.25f * 1.0f);
}

TEST(CreativeMotionIntentTest, OtherChangesAreAsTheOldRulesSay)
{
    const TransitionTimes times;
    TransitionIntent intent;
    intent.mStopping = true;
    intent.mStarting = true;
    intent.mTurnRate = 3.f;
    const MotionKind kinds[] = { MotionKind::Idle, MotionKind::Ready, MotionKind::Walk, MotionKind::Run,
        MotionKind::Sneak, MotionKind::Strafe, MotionKind::Swim, MotionKind::Jump, MotionKind::Attack, MotionKind::Hit,
        MotionKind::Death, MotionKind::Other };
    for (const MotionKind from : kinds)
    {
        for (const MotionKind to : kinds)
        {
            const bool stop
                = (from == MotionKind::Walk || from == MotionKind::Run || from == MotionKind::Sneak)
                && to == MotionKind::Idle;
            const bool start = from == MotionKind::Idle
                && (to == MotionKind::Walk || to == MotionKind::Run || to == MotionKind::Sneak);
            if (stop || start || from == MotionKind::Idle || to == MotionKind::Idle)
                continue;
            EXPECT_FLOAT_EQ(transitionSeconds(from, to, 0.25f, intent, times), transitionSeconds(from, to, 0.25f))
                << static_cast<int>(from) << " -> " << static_cast<int>(to);
        }
    }
}

TEST(CreativeMotionIntentTest, AZeroOrNegativeBaseIsZero)
{
    const TransitionTimes times;
    TransitionIntent intent;
    intent.mStopping = true;
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Walk, MotionKind::Idle, 0.f, intent, times), 0.f);
    EXPECT_FLOAT_EQ(transitionSeconds(MotionKind::Walk, MotionKind::Idle, -1.f, intent, times), 0.f);
}

// --- TransitionTimes as data -----------------------------------------------------------------------------------

TEST(CreativeMotionIntentTest, TransitionTimesDefaultsMatchTheSpec)
{
    const TransitionTimes times = makeTransitionTimes({});
    EXPECT_FLOAT_EQ(times.mStopping, 1.5f);
    EXPECT_FLOAT_EQ(times.mFlip, 1.0f);
    EXPECT_FLOAT_EQ(times.mStarting, 0.8f);
    EXPECT_FLOAT_EQ(times.mFastTurn, 0.6f);
    EXPECT_FLOAT_EQ(times.mSlowTurn, 1.0f);
    EXPECT_FLOAT_EQ(times.mFastTurnRate, 1.5f);
}

TEST(CreativeMotionIntentTest, TransitionTimesReadTheKnownKeys)
{
    const TransitionTimes times = makeTransitionTimes({ { "stopping", "2.5" }, { "flip", "0.5" }, { "starting", "0.4" },
        { "fast turn", "0.3" }, { "slow turn", "1.2" }, { "fast turn rate", "2" } });
    EXPECT_FLOAT_EQ(times.mStopping, 2.5f);
    EXPECT_FLOAT_EQ(times.mFlip, 0.5f);
    EXPECT_FLOAT_EQ(times.mStarting, 0.4f);
    EXPECT_FLOAT_EQ(times.mFastTurn, 0.3f);
    EXPECT_FLOAT_EQ(times.mSlowTurn, 1.2f);
    EXPECT_FLOAT_EQ(times.mFastTurnRate, 2.f);
}

TEST(CreativeMotionIntentTest, TransitionTimesIgnoreBadEntries)
{
    const TransitionTimes times = makeTransitionTimes({ { "stopping", "-1" }, { "starting", "fast" }, { "flip", "inf" },
        { "fast turn", "nan" }, { "unknown key", "9" }, { "slow turn", "" } });
    const TransitionTimes defaults;
    EXPECT_FLOAT_EQ(times.mStopping, defaults.mStopping);
    EXPECT_FLOAT_EQ(times.mStarting, defaults.mStarting);
    EXPECT_FLOAT_EQ(times.mFlip, defaults.mFlip);
    EXPECT_FLOAT_EQ(times.mFastTurn, defaults.mFastTurn);
    EXPECT_FLOAT_EQ(times.mSlowTurn, defaults.mSlowTurn);
}

TEST(CreativeMotionIntentTest, TransitionTimesLoadFromTheSettingsCategory)
{
    SettingsMapsGuard guard;
    Settings::Manager::mDefaultSettings[std::make_pair("CreativeMotion Transitions", "stopping")] = "2";
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Transitions", "stopping")] = "3";
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Transitions", "starting")] = "0.5";
    const TransitionTimes times = loadTransitionTimesFromSettings();
    EXPECT_FLOAT_EQ(times.mStopping, 3.f); // the user's entry wins over the default entry
    EXPECT_FLOAT_EQ(times.mStarting, 0.5f);
    EXPECT_FLOAT_EQ(times.mFlip, TransitionTimes().mFlip);
}

// --- The preset ------------------------------------------------------------------------------------------------

TEST(CreativeMotionIntentTest, PresetNames)
{
    bool recognised = false;
    EXPECT_EQ(parseCreativeMotionPreset("off", &recognised), CreativeMotionPreset::Off);
    EXPECT_TRUE(recognised);
    EXPECT_EQ(parseCreativeMotionPreset("natural", &recognised), CreativeMotionPreset::Natural);
    EXPECT_TRUE(recognised);
    EXPECT_EQ(parseCreativeMotionPreset("NaTuRaL", &recognised), CreativeMotionPreset::Natural);
    EXPECT_TRUE(recognised);
    EXPECT_EQ(parseCreativeMotionPreset("vanilla", &recognised), CreativeMotionPreset::Off);
    EXPECT_FALSE(recognised);
    EXPECT_EQ(parseCreativeMotionPreset("", &recognised), CreativeMotionPreset::Off);
    EXPECT_FALSE(recognised);
    EXPECT_EQ(parseCreativeMotionPreset("natural ", &recognised), CreativeMotionPreset::Off);
    EXPECT_FALSE(recognised);
}

TEST(CreativeMotionIntentTest, PresetOffChangesNothing)
{
    for (int bits = 0; bits < 8; ++bits)
    {
        const bool animation = (bits & 1) != 0;
        const bool rules = (bits & 2) != 0;
        const bool style = (bits & 4) != 0;
        const CreativeMotionFeatures features
            = resolveCreativeMotionFeatures(CreativeMotionPreset::Off, animation, rules, style);
        EXPECT_EQ(features.mAnimation, animation);
        EXPECT_EQ(features.mTransitionRules, rules);
        EXPECT_EQ(features.mStyleLayer, style);
        EXPECT_FALSE(features.mIntentTransitions);
    }
}

TEST(CreativeMotionIntentTest, PresetNaturalSwitchesTheFeaturesOnTogether)
{
    const CreativeMotionFeatures features
        = resolveCreativeMotionFeatures(CreativeMotionPreset::Natural, false, false, false);
    EXPECT_TRUE(features.mAnimation);
    EXPECT_TRUE(features.mTransitionRules);
    EXPECT_TRUE(features.mStyleLayer);
    EXPECT_TRUE(features.mIntentTransitions);
}

TEST(CreativeMotionIntentTest, PresetNaturalKeepsWhatIsAlreadyOn)
{
    const CreativeMotionFeatures features
        = resolveCreativeMotionFeatures(CreativeMotionPreset::Natural, true, true, true);
    EXPECT_TRUE(features.mAnimation);
    EXPECT_TRUE(features.mTransitionRules);
    EXPECT_TRUE(features.mStyleLayer);
}

TEST(CreativeMotionIntentTest, PresetSettingDefaultsToOff)
{
    EXPECT_EQ(parseCreativeMotionPreset(Settings::game().mCreativeMotionPreset.get()), CreativeMotionPreset::Off);
    EXPECT_EQ(Settings::game().mCreativeMotionPreset.get(), "off");
}
