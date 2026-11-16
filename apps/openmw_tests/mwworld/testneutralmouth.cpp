// Task 0235: the mouth of a speaking neutral actor (NeutralMouthDriver::step, the pure frame step shared by the
// conversation and the scene speech path) and NeutralSceneSpeech::busy. A fake loader stands for the VFS; no game data.
// Written before the code and run red against a stub (see the stage log of the task).
#include <apps/openmw/mwworld/neutralmouth.hpp>
#include <apps/openmw/mwworld/neutralscenespeech.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace
{
    using MWWorld::NeutralMouthDriver;

    // Two rows: Aah (slot 6) 0 then 1; the track starts at row 0, 30 rows a second.
    Lip::Track makeTrack()
    {
        Lip::Track t;
        t.mFrames = 2;
        t.mFirst = 0;
        t.mTimingKnown = true;
        t.mGrid.assign(2 * Lip::kSlots, 0.f);
        t.mGrid[1 * Lip::kSlots + Lip::kVisemeBase + 0] = 1.f; // row 1: Aah full
        t.mGrid[1 * Lip::kSlots + Lip::kVisemeBase + 2] = 0.5f; // row 1: BMP half
        return t;
    }

    struct MouthRig
    {
        NeutralMouthDriver mDriver;
        std::map<std::string, int> mLoads;
        bool mHasLip = true;
        NeutralMouthDriver::LipLoader mLoader = [this](const std::string& file) -> std::optional<Lip::Track> {
            ++mLoads[file];
            if (!mHasLip)
                return std::nullopt;
            return makeTrack();
        };
        TriMorph::VisemeWeights mWeights{};
        bool step(bool saying, const std::string& file, float seconds, float loudness)
        {
            return mDriver.step(saying, file, seconds, loudness, mLoader, mWeights);
        }
        static bool anyOpen(const TriMorph::VisemeWeights& w)
        {
            return std::any_of(w.begin(), w.end(), [](float x) { return x > 0.f; });
        }
    };

    TEST(NeutralMouth, aClosedMouthThatStaysClosedSendsNothing)
    {
        MouthRig rig;
        EXPECT_FALSE(rig.step(false, "", 0.f, 0.f));
        EXPECT_FALSE(rig.mDriver.open());
        EXPECT_TRUE(rig.mLoads.empty());
    }

    TEST(NeutralMouth, aLineWithLipDataFollowsTheTrackAtThePlaybackPosition)
    {
        MouthRig rig;
        EXPECT_TRUE(rig.step(true, "a.fuz", 1.f / 30.f, 0.f)); // on row 1
        Lip::Track t = makeTrack();
        std::array<float, Lip::kVisemes> expected{};
        ASSERT_TRUE(Lip::sampleVisemes(t, 1.f / 30.f, expected));
        EXPECT_FLOAT_EQ(rig.mWeights[0], expected[0]);
        EXPECT_FLOAT_EQ(rig.mWeights[2], expected[2]);
        EXPECT_GT(rig.mWeights[0], 0.f);
        EXPECT_TRUE(rig.mDriver.open());
    }

    TEST(NeutralMouth, theLipDataOfAFileIsLoadedOnceEvenWhenItFails)
    {
        MouthRig rig;
        rig.step(true, "a.fuz", 0.f, 0.f);
        rig.step(true, "a.fuz", 0.02f, 0.f);
        rig.step(true, "a.fuz", 0.03f, 0.f);
        EXPECT_EQ(rig.mLoads["a.fuz"], 1);
        rig.mHasLip = false;
        rig.step(true, "b.fuz", 0.f, 0.f);
        rig.step(true, "b.fuz", 0.1f, 0.f);
        EXPECT_EQ(rig.mLoads["b.fuz"], 1);
        rig.step(true, "a.fuz", 0.f, 0.f); // another file in between: loaded again
        EXPECT_EQ(rig.mLoads["a.fuz"], 2);
    }

    TEST(NeutralMouth, aLineWithNoLipDataFollowsTheLoudness)
    {
        MouthRig rig;
        rig.mHasLip = false;
        EXPECT_TRUE(rig.step(true, "b.fuz", 0.5f, 0.5f));
        TriMorph::MouthDrive reference;
        const TriMorph::VisemeWeights expected = TriMorph::toVisemes(reference.step(0.5f));
        EXPECT_EQ(rig.mWeights, expected);
        EXPECT_TRUE(MouthRig::anyOpen(rig.mWeights));
    }

    TEST(NeutralMouth, aSilentLineWithNoLipDataStaysClosedAndSendsNothing)
    {
        MouthRig rig;
        rig.mHasLip = false;
        EXPECT_FALSE(rig.step(true, "b.fuz", 0.5f, 0.f));
        EXPECT_FALSE(rig.mDriver.open());
    }

    TEST(NeutralMouth, whenTheLineEndsTheMouthClosesOnceAndThenSendsNothing)
    {
        MouthRig rig;
        ASSERT_TRUE(rig.step(true, "a.fuz", 1.f / 30.f, 0.f));
        ASSERT_TRUE(rig.mDriver.open());
        // the line is over: the loudness drive lets the jaw fall over a few frames, then it is closed and told so once
        int sends = 0;
        bool lastClosed = false;
        for (int i = 0; i < 60; ++i)
        {
            if (rig.step(false, "", 0.f, 0.f))
            {
                ++sends;
                lastClosed = !MouthRig::anyOpen(rig.mWeights);
            }
        }
        EXPECT_GE(sends, 1);
        EXPECT_TRUE(lastClosed) << "the last thing sent is a closed mouth";
        EXPECT_FALSE(rig.mDriver.open());
        EXPECT_FALSE(rig.step(false, "", 0.f, 0.f));
    }

    TEST(NeutralMouth, aNewLineAfterTheOldOneUsesItsOwnLipData)
    {
        MouthRig rig;
        rig.step(true, "a.fuz", 0.f, 0.f);
        for (int i = 0; i < 60; ++i)
            rig.step(false, "", 0.f, 0.f);
        rig.step(true, "a.fuz", 0.f, 0.f);
        EXPECT_EQ(rig.mLoads["a.fuz"], 2) << "the cache is dropped when the line ends";
    }

    TEST(NeutralMouth, badTimeAndLoudnessAreHarmless)
    {
        MouthRig rig;
        rig.mHasLip = false;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_NO_THROW((void)rig.step(true, "b.fuz", nan, nan));
        EXPECT_NO_THROW((void)rig.step(true, "b.fuz", -3.f, -1.f));
        EXPECT_FALSE(rig.mDriver.open());
    }

    TEST(NeutralMouth, resetClosesTheStateWithoutSending)
    {
        MouthRig rig;
        rig.step(true, "a.fuz", 1.f / 30.f, 0.f);
        ASSERT_TRUE(rig.mDriver.open());
        rig.mDriver.reset();
        EXPECT_FALSE(rig.mDriver.open());
        EXPECT_FALSE(rig.step(false, "", 0.f, 0.f));
    }

    // ---- NeutralSceneSpeech::busy ---------------------------------------------------------------------------------

    struct BusySink final : MWWorld::NeutralSceneSpeech::Sink
    {
        bool active(const void*) override { return mPlaying; }
        void say(const void*, const std::string&) override { mPlaying = true; }
        void subtitle(const void*, const std::string&) override {}
        bool mPlaying = false;
    };

    TEST(NeutralSceneSpeechBusy, anActorWithALineQueuedOrPlayingIsBusy)
    {
        const int key = 0;
        MWWorld::NeutralSceneSpeech speech;
        BusySink sink;
        EXPECT_FALSE(speech.busy(&key));
        ASSERT_TRUE(speech.add(&key, "hello", { "x/1.fuz" }));
        EXPECT_TRUE(speech.busy(&key)) << "queued";
        speech.update(sink);
        EXPECT_TRUE(speech.busy(&key)) << "playing";
        sink.mPlaying = false;
        speech.update(sink);
        EXPECT_FALSE(speech.busy(&key)) << "over";
    }

    TEST(NeutralSceneSpeechBusy, anotherActorIsNotBusy)
    {
        const int a = 0;
        const int b = 0;
        MWWorld::NeutralSceneSpeech speech;
        ASSERT_TRUE(speech.add(&a, "hello", { "x/1.fuz" }));
        EXPECT_FALSE(speech.busy(&b));
    }
}
