// Task 0059 (stage S4d): the hand-over between an object cluster and the live objects, as a pure state machine.
// Written from the specification in components/lod/clusterfade.hpp only. No GPU, window, network or game data.
#include <gtest/gtest.h>

#include <components/lod/clusterfade.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float frameSeconds = 1.0f / 60.0f;
    constexpr float tolerance = 1e-5f;

    TileFade tileAt(FadePhase phase, float fade)
    {
        TileFade tile;
        tile.mPhase = phase;
        tile.mFade = fade;
        return tile;
    }

    // One recorded frame of a simulated life: the state after the step.
    struct Frame
    {
        FadePhase mPhase;
        float mFade;
        bool mCovers;
    };

    using BoolOfFrame = std::function<bool(int, const TileFade&)>;

    // Runs `frames` frames at 60 a second. `wanted` and `liveReady` are told the frame number and the state before
    // the step, so a script can be a timeline or can react to the tile.
    std::vector<Frame> simulate(int frames, float fadeSeconds, const BoolOfFrame& wanted, const BoolOfFrame& liveReady)
    {
        std::vector<Frame> result;
        result.reserve(static_cast<std::size_t>(frames));
        TileFade tile;
        for (int i = 0; i < frames; ++i)
        {
            const bool isWanted = wanted(i, tile);
            const bool isLiveReady = liveReady(i, tile);
            stepTileFade(tile, isWanted, isLiveReady, frameSeconds, fadeSeconds);
            result.push_back({ tile.mPhase, tile.mFade, tileCoversStatics(tile) });
        }
        return result;
    }

    // The invariants that mean the far field never has a hole: while the live objects leave out the statics, the
    // cluster is fully solid; a cluster waiting for the live objects is fully solid; the fade stays in range; a
    // hidden tile has a zero fade.
    void expectNoHole(const std::vector<Frame>& frames)
    {
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const Frame& frame = frames[i];
            SCOPED_TRACE(i);
            ASSERT_FALSE(std::isnan(frame.mFade));
            EXPECT_GE(frame.mFade, 0.f);
            EXPECT_LE(frame.mFade, 1.f);
            if (frame.mCovers)
            {
                EXPECT_EQ(frame.mFade, 1.f) << "live objects left out the statics while the cluster was not solid";
            }
            if (frame.mPhase == FadePhase::WaitingForLive)
            {
                EXPECT_EQ(frame.mFade, 1.f) << "the cluster faded while waiting for the live objects";
            }
            if (frame.mPhase == FadePhase::Hidden)
            {
                EXPECT_EQ(frame.mFade, 0.f);
            }
        }
    }

    // ---- Single transitions ----

    TEST(LodClusterFade, HiddenAndWantedStartsFadingIn)
    {
        TileFade tile;
        stepTileFade(tile, true, false, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_GE(tile.mFade, 0.f);
        EXPECT_LE(tile.mFade, 0.1f + tolerance);
    }

    TEST(LodClusterFade, HiddenAndNotWantedStaysHidden)
    {
        TileFade tile;
        for (int i = 0; i < 10; ++i)
            stepTileFade(tile, false, i % 2 == 0, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Hidden);
        EXPECT_EQ(tile.mFade, 0.f);
    }

    TEST(LodClusterFade, FadingInRisesBySecondsOverFadeSeconds)
    {
        TileFade tile = tileAt(FadePhase::FadingIn, 0.3f);
        stepTileFade(tile, true, false, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_NEAR(tile.mFade, 0.4f, tolerance);

        tile = tileAt(FadePhase::FadingIn, 0.2f);
        stepTileFade(tile, true, false, 0.1f, 0.5f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_NEAR(tile.mFade, 0.4f, tolerance);
    }

    TEST(LodClusterFade, FadingInReachingOneBecomesSolidWithFadeExactlyOne)
    {
        TileFade tile = tileAt(FadePhase::FadingIn, 0.95f);
        stepTileFade(tile, true, false, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Solid);
        EXPECT_EQ(tile.mFade, 1.f);
    }

    TEST(LodClusterFade, FadingInOvershootingIsClampedToOne)
    {
        TileFade tile = tileAt(FadePhase::FadingIn, 0.5f);
        stepTileFade(tile, true, false, 100.f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Solid);
        EXPECT_EQ(tile.mFade, 1.f);
    }

    TEST(LodClusterFade, FadingInThatBecomesUnwantedFadesOutKeepingItsFade)
    {
        TileFade tile = tileAt(FadePhase::FadingIn, 0.4f);
        stepTileFade(tile, false, false, 0.f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_NEAR(tile.mFade, 0.4f, tolerance);
    }

    TEST(LodClusterFade, FadingInThatBecomesUnwantedNeverJumpsUp)
    {
        TileFade tile = tileAt(FadePhase::FadingIn, 0.4f);
        stepTileFade(tile, false, true, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_LE(tile.mFade, 0.4f + tolerance);
        EXPECT_GE(tile.mFade, 0.f);
    }

    TEST(LodClusterFade, SolidThatStaysWantedStaysSolid)
    {
        TileFade tile = tileAt(FadePhase::Solid, 1.f);
        for (int i = 0; i < 10; ++i)
            stepTileFade(tile, true, i % 2 == 0, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Solid);
        EXPECT_EQ(tile.mFade, 1.f);
    }

    TEST(LodClusterFade, SolidThatBecomesUnwantedWaitsForLiveWithFadeStillOne)
    {
        TileFade tile = tileAt(FadePhase::Solid, 1.f);
        stepTileFade(tile, false, false, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::WaitingForLive);
        EXPECT_EQ(tile.mFade, 1.f);
    }

    TEST(LodClusterFade, WaitingForLiveBecomesSolidWhenWantedAgain)
    {
        for (const bool ready : { false, true })
        {
            SCOPED_TRACE(ready);
            TileFade tile = tileAt(FadePhase::WaitingForLive, 1.f);
            stepTileFade(tile, true, ready, 0.1f, 1.f);
            EXPECT_EQ(tile.mPhase, FadePhase::Solid);
            EXPECT_EQ(tile.mFade, 1.f);
        }
    }

    TEST(LodClusterFade, WaitingForLiveStartsFadingOutWhenLiveIsReady)
    {
        TileFade tile = tileAt(FadePhase::WaitingForLive, 1.f);
        stepTileFade(tile, false, true, 0.f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_NEAR(tile.mFade, 1.f, tolerance);
    }

    TEST(LodClusterFade, WaitingForLiveKeepsWaitingWhileLiveIsNotReady)
    {
        TileFade tile = tileAt(FadePhase::WaitingForLive, 1.f);
        for (int i = 0; i < 600; ++i)
        {
            stepTileFade(tile, false, false, frameSeconds, 0.5f);
            ASSERT_EQ(tile.mPhase, FadePhase::WaitingForLive);
            ASSERT_EQ(tile.mFade, 1.f);
        }
    }

    TEST(LodClusterFade, FadingOutFallsBySecondsOverFadeSeconds)
    {
        TileFade tile = tileAt(FadePhase::FadingOut, 0.5f);
        stepTileFade(tile, false, true, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_NEAR(tile.mFade, 0.4f, tolerance);

        tile = tileAt(FadePhase::FadingOut, 0.5f);
        stepTileFade(tile, false, true, 0.1f, 0.5f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_NEAR(tile.mFade, 0.3f, tolerance);
    }

    TEST(LodClusterFade, FadingOutReachingZeroBecomesHiddenWithFadeExactlyZero)
    {
        TileFade tile = tileAt(FadePhase::FadingOut, 0.05f);
        stepTileFade(tile, false, true, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Hidden);
        EXPECT_EQ(tile.mFade, 0.f);
    }

    TEST(LodClusterFade, FadingOutUndershootingIsClampedToZero)
    {
        TileFade tile = tileAt(FadePhase::FadingOut, 0.5f);
        stepTileFade(tile, false, true, 100.f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Hidden);
        EXPECT_EQ(tile.mFade, 0.f);
    }

    TEST(LodClusterFade, FadingOutThatBecomesWantedResumesFadingInFromItsFade)
    {
        TileFade tile = tileAt(FadePhase::FadingOut, 0.6f);
        stepTileFade(tile, true, true, 0.f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_NEAR(tile.mFade, 0.6f, tolerance);

        tile = tileAt(FadePhase::FadingOut, 0.6f);
        stepTileFade(tile, true, false, 0.1f, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_GE(tile.mFade, 0.6f - tolerance);
        EXPECT_LE(tile.mFade, 0.7f + tolerance);
    }

    // ---- No fade at all ----

    TEST(LodClusterFade, ZeroOrNegativeFadeSecondsReachesSolidAtOnce)
    {
        for (const float fadeSeconds : { 0.f, -1.f })
        {
            SCOPED_TRACE(fadeSeconds);
            TileFade tile;
            for (int i = 0; i < 2; ++i)
                stepTileFade(tile, true, false, frameSeconds, fadeSeconds);
            EXPECT_EQ(tile.mFade, 1.f);
            EXPECT_EQ(tile.mPhase, FadePhase::Solid);
        }
    }

    TEST(LodClusterFade, ZeroFadeSecondsWithZeroFrameTimeStaysFinite)
    {
        TileFade tile;
        for (int i = 0; i < 4; ++i)
            stepTileFade(tile, true, false, 0.f, 0.f);
        EXPECT_FALSE(std::isnan(tile.mFade));
        EXPECT_EQ(tile.mFade, 1.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Solid);
    }

    TEST(LodClusterFade, ZeroFadeSecondsStillWaitsForLiveObjects)
    {
        TileFade tile = tileAt(FadePhase::Solid, 1.f);
        for (int i = 0; i < 5; ++i)
        {
            stepTileFade(tile, false, false, frameSeconds, 0.f);
            ASSERT_EQ(tile.mPhase, FadePhase::WaitingForLive);
            ASSERT_EQ(tile.mFade, 1.f);
        }
        for (int i = 0; i < 4; ++i)
            stepTileFade(tile, false, true, frameSeconds, 0.f);
        EXPECT_EQ(tile.mPhase, FadePhase::Hidden);
        EXPECT_EQ(tile.mFade, 0.f);
    }

    // ---- Predicates ----

    TEST(LodClusterFade, TileCoversStaticsOnlyWhenSolid)
    {
        EXPECT_FALSE(tileCoversStatics(tileAt(FadePhase::Hidden, 0.f)));
        EXPECT_FALSE(tileCoversStatics(tileAt(FadePhase::FadingIn, 0.5f)));
        EXPECT_TRUE(tileCoversStatics(tileAt(FadePhase::Solid, 1.f)));
        EXPECT_FALSE(tileCoversStatics(tileAt(FadePhase::WaitingForLive, 1.f)));
        EXPECT_FALSE(tileCoversStatics(tileAt(FadePhase::FadingOut, 0.5f)));
    }

    TEST(LodClusterFade, TileIsDrawnInEveryPhaseButHidden)
    {
        EXPECT_FALSE(tileIsDrawn(tileAt(FadePhase::Hidden, 0.f)));
        EXPECT_TRUE(tileIsDrawn(tileAt(FadePhase::FadingIn, 0.5f)));
        EXPECT_TRUE(tileIsDrawn(tileAt(FadePhase::Solid, 1.f)));
        EXPECT_TRUE(tileIsDrawn(tileAt(FadePhase::WaitingForLive, 1.f)));
        EXPECT_TRUE(tileIsDrawn(tileAt(FadePhase::FadingOut, 0.5f)));
    }

    // ---- Frame-time independence ----

    TEST(LodClusterFade, RisingFadeDoesNotDependOnTheStepSize)
    {
        TileFade oneStep = tileAt(FadePhase::FadingIn, 0.f);
        stepTileFade(oneStep, true, false, 0.6f, 1.f);

        TileFade manySteps = tileAt(FadePhase::FadingIn, 0.f);
        for (int i = 0; i < 60; ++i)
            stepTileFade(manySteps, true, false, 0.01f, 1.f);

        EXPECT_EQ(oneStep.mPhase, FadePhase::FadingIn);
        EXPECT_EQ(manySteps.mPhase, oneStep.mPhase);
        EXPECT_NEAR(oneStep.mFade, 0.6f, tolerance);
        EXPECT_NEAR(manySteps.mFade, oneStep.mFade, tolerance);
    }

    TEST(LodClusterFade, FallingFadeDoesNotDependOnTheStepSize)
    {
        TileFade oneStep = tileAt(FadePhase::FadingOut, 0.9f);
        stepTileFade(oneStep, false, true, 0.5f, 1.f);

        TileFade manySteps = tileAt(FadePhase::FadingOut, 0.9f);
        for (int i = 0; i < 50; ++i)
            stepTileFade(manySteps, false, true, 0.01f, 1.f);

        EXPECT_EQ(oneStep.mPhase, FadePhase::FadingOut);
        EXPECT_EQ(manySteps.mPhase, oneStep.mPhase);
        EXPECT_NEAR(oneStep.mFade, 0.4f, tolerance);
        EXPECT_NEAR(manySteps.mFade, oneStep.mFade, tolerance);
    }

    TEST(LodClusterFade, FullFadeInTakesTheSameTimeWhateverTheStepSize)
    {
        // 1 s fade: after 0.99 s it is not solid yet, after 1.01 s it is.
        for (const int steps : { 1, 7, 99, 990 })
        {
            SCOPED_TRACE(steps);
            TileFade tile = tileAt(FadePhase::FadingIn, 0.f);
            for (int i = 0; i < steps; ++i)
                stepTileFade(tile, true, false, 0.99f / static_cast<float>(steps), 1.f);
            EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
            for (int i = 0; i < steps; ++i)
                stepTileFade(tile, true, false, 0.02f / static_cast<float>(steps), 1.f);
            EXPECT_EQ(tile.mPhase, FadePhase::Solid);
            EXPECT_EQ(tile.mFade, 1.f);
        }
    }

    // ---- Whole lives: there is never a hole ----

    TEST(LodClusterFade, ScriptedLifeNeverLeavesAHoleAndWaitsForLive)
    {
        // Wanted for 2 s, then unwanted; the live objects are ready 1.3 s after the tile turned unwanted.
        const int wantedFrames = 120;
        const int readyFrame = wantedFrames + 78;
        const auto frames = simulate(
            420, 0.5f, [&](int i, const TileFade&) { return i < wantedFrames; },
            [&](int i, const TileFade&) { return i >= readyFrame; });

        expectNoHole(frames);

        // The phases come in the documented order, each once.
        std::vector<FadePhase> order;
        for (const Frame& frame : frames)
            if (order.empty() || order.back() != frame.mPhase)
                order.push_back(frame.mPhase);
        const std::vector<FadePhase> expected = { FadePhase::FadingIn, FadePhase::Solid, FadePhase::WaitingForLive,
            FadePhase::FadingOut, FadePhase::Hidden };
        EXPECT_EQ(order, expected);

        // It waits for about the 1.3 s the live objects needed, and holds the fade at one the whole time.
        int waitingFrames = 0;
        for (const Frame& frame : frames)
            if (frame.mPhase == FadePhase::WaitingForLive)
                ++waitingFrames;
        EXPECT_GE(waitingFrames, 70);
        EXPECT_LE(waitingFrames, 85);

        EXPECT_EQ(frames.back().mPhase, FadePhase::Hidden);
        EXPECT_EQ(frames.back().mFade, 0.f);
    }

    TEST(LodClusterFade, ScriptedLifeCoversStaticsOnlyWhileSolid)
    {
        const auto frames = simulate(
            420, 0.5f, [](int i, const TileFade&) { return i < 120; }, [](int i, const TileFade&) { return i >= 198; });
        bool sawCovering = false;
        for (const Frame& frame : frames)
        {
            EXPECT_EQ(frame.mCovers, frame.mPhase == FadePhase::Solid);
            sawCovering = sawCovering || frame.mCovers;
        }
        EXPECT_TRUE(sawCovering);
    }

    TEST(LodClusterFade, ClusterStaysSolidForTheWholeWaitWhileLiveIsNotReady)
    {
        // Solid, then unwanted for ten seconds without the live objects ever being ready.
        TileFade tile = tileAt(FadePhase::Solid, 1.f);
        for (int i = 0; i < 600; ++i)
        {
            stepTileFade(tile, false, false, frameSeconds, 0.5f);
            ASSERT_EQ(tile.mPhase, FadePhase::WaitingForLive) << "frame " << i;
            ASSERT_EQ(tile.mFade, 1.f) << "frame " << i;
            ASSERT_TRUE(tileIsDrawn(tile));
        }
    }

    TEST(LodClusterFade, LiveReadyFromTheStartNeverLeavesAHole)
    {
        const auto frames = simulate(
            300, 0.5f, [](int i, const TileFade&) { return i < 100; }, [](int, const TileFade&) { return true; });
        expectNoHole(frames);
        EXPECT_EQ(frames.back().mPhase, FadePhase::Hidden);
    }

    TEST(LodClusterFade, LiveReadyOnlyOnceTheTileNoLongerCoversNeverLeavesAHole)
    {
        // Closer to the game: the live objects get their statics back the frame after the tile stops covering them.
        const auto frames = simulate(
            400, 0.4f, [](int i, const TileFade&) { return i < 90; },
            [](int, const TileFade& tile) { return !tileCoversStatics(tile); });
        expectNoHole(frames);
        EXPECT_EQ(frames.back().mPhase, FadePhase::Hidden);
    }

    TEST(LodClusterFade, FlickeringWantedNeverLeavesAHole)
    {
        // The selection toggles the tile every three frames; fades from instant to slower than the toggling.
        for (const float fadeSeconds : { 0.f, 0.05f, 0.25f, 1.f })
        {
            SCOPED_TRACE(fadeSeconds);
            const auto frames = simulate(
                1200, fadeSeconds, [](int i, const TileFade&) { return (i / 3) % 2 == 0; },
                [](int i, const TileFade&) { return (i / 7) % 2 == 0; });
            expectNoHole(frames);
        }
    }

    TEST(LodClusterFade, FlickeringWantedWithReactiveLiveObjectsNeverLeavesAHole)
    {
        for (const float fadeSeconds : { 0.f, 0.05f, 0.25f, 1.f })
        {
            SCOPED_TRACE(fadeSeconds);
            const auto frames = simulate(
                1200, fadeSeconds, [](int i, const TileFade&) { return (i / 3) % 2 == 0; },
                [](int, const TileFade& tile) { return !tileCoversStatics(tile); });
            expectNoHole(frames);
        }
    }

    TEST(LodClusterFade, FlickeringWantedWithLiveObjectsNeverReadyNeverLeavesAHole)
    {
        for (const float fadeSeconds : { 0.f, 0.05f, 0.25f, 1.f })
        {
            SCOPED_TRACE(fadeSeconds);
            const auto frames = simulate(
                1200, fadeSeconds, [](int i, const TileFade&) { return (i / 3) % 2 == 0; },
                [](int, const TileFade&) { return false; });
            expectNoHole(frames);
        }
    }

    TEST(LodClusterFade, TheSameScriptGivesIdenticalStatesTwice)
    {
        const auto wanted = [](int i, const TileFade&) { return (i / 5) % 3 != 0; };
        const auto ready = [](int i, const TileFade&) { return (i / 11) % 2 == 1; };
        const auto first = simulate(2000, 0.3f, wanted, ready);
        const auto second = simulate(2000, 0.3f, wanted, ready);
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            ASSERT_EQ(first[i].mPhase, second[i].mPhase) << "frame " << i;
            ASSERT_EQ(first[i].mFade, second[i].mFade) << "frame " << i;
            ASSERT_EQ(first[i].mCovers, second[i].mCovers) << "frame " << i;
        }
    }
}
