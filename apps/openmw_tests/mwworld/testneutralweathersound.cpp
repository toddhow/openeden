// Task 0127, stage W-S4: the pure rules of a game pack's weather sounds (thunder delay and volume, the file choice,
// wind gusts, loops). Every expected number is written out by hand from the constants in the header's comments.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <string>
#include <vector>

#include "apps/openmw/mwworld/neutralweather.hpp"
#include "apps/openmw/mwworld/neutralweathersound.hpp"

namespace
{
    using namespace MWWorld::NeutralWeatherSound;

    Slot slot(std::initializer_list<const char*> files, float volume = 1.f, bool loop = false)
    {
        Slot s;
        s.name = "Test";
        for (const char* f : files)
            s.files.push_back(f);
        s.volume = volume;
        s.loop = loop;
        return s;
    }

    TEST(NeutralWeatherSound, thunderIsHeardLaterTheFartherTheStrike)
    {
        // 0.4 s plus 1.6 s a step
        EXPECT_FLOAT_EQ(thunderDelaySeconds(0), 0.4f);
        EXPECT_FLOAT_EQ(thunderDelaySeconds(1), 2.0f);
        EXPECT_FLOAT_EQ(thunderDelaySeconds(2), 3.6f);
        EXPECT_FLOAT_EQ(thunderDelaySeconds(3), 5.2f);
        // an index past the last is the last
        EXPECT_FLOAT_EQ(thunderDelaySeconds(9), 5.2f);
    }

    TEST(NeutralWeatherSound, thunderIsQuieterTheFartherTheStrike)
    {
        // a fifth of the volume a step
        EXPECT_FLOAT_EQ(thunderVolume(0.5f, 0), 0.5f);
        EXPECT_FLOAT_EQ(thunderVolume(0.5f, 1), 0.4f);
        EXPECT_FLOAT_EQ(thunderVolume(0.5f, 3), 0.2f);
        EXPECT_FLOAT_EQ(thunderVolume(2.f, 0), 1.f); // never above 1
        EXPECT_FLOAT_EQ(thunderVolume(-1.f, 0), 0.f); // never below 0
        EXPECT_FLOAT_EQ(thunderVolume(std::numeric_limits<float>::quiet_NaN(), 0), 0.f);
    }

    TEST(NeutralWeatherSound, filesComeRoundAndRoundInOrder)
    {
        const Slot s = slot({ "sound/a.wav", "sound/b.wav", "sound/c.wav" });
        EXPECT_EQ(pickFile(s, 0), "sound/a.wav");
        EXPECT_EQ(pickFile(s, 1), "sound/b.wav");
        EXPECT_EQ(pickFile(s, 2), "sound/c.wav");
        EXPECT_EQ(pickFile(s, 3), "sound/a.wav");
        EXPECT_EQ(pickFile(s, 4000000001u), pickFile(s, 4000000001u % 3));
        EXPECT_TRUE(pickFile(Slot{}, 5).empty());
    }

    TEST(NeutralWeatherSound, delayedThunderWaitsForItsTime)
    {
        DelayedThunder q;
        EXPECT_TRUE(q.push(2.0f, 7, 0.4f, 1));
        EXPECT_TRUE(q.push(0.4f, 8, 0.5f, 0));
        EXPECT_EQ(q.size(), 2u);
        DelayedThunder::Due out[4];
        EXPECT_EQ(q.advance(0.3f, out, 4), 0u);
        ASSERT_EQ(q.advance(0.2f, out, 4), 1u); // 0.5 s: the near one
        EXPECT_EQ(out[0].fileIndex, 8u);
        EXPECT_FLOAT_EQ(out[0].volume, 0.5f);
        EXPECT_EQ(out[0].distanceIndex, 0u);
        EXPECT_EQ(q.size(), 1u);
        EXPECT_EQ(q.advance(1.4f, out, 4), 0u); // 1.9 s
        ASSERT_EQ(q.advance(0.2f, out, 4), 1u); // 2.1 s
        EXPECT_EQ(out[0].fileIndex, 7u);
        EXPECT_EQ(out[0].distanceIndex, 1u);
        EXPECT_EQ(q.size(), 0u);
    }

    TEST(NeutralWeatherSound, twoDueAtOnceComeOutEarliestDueFirst)
    {
        DelayedThunder q;
        EXPECT_TRUE(q.push(1.0f, 1, 1.f, 0));
        EXPECT_TRUE(q.push(0.5f, 2, 1.f, 0));
        DelayedThunder::Due out[4];
        // both due in one long step: the earlier due time first
        ASSERT_EQ(q.advance(3.f, out, 4), 2u);
        EXPECT_EQ(out[0].fileIndex, 2u);
        EXPECT_EQ(out[1].fileIndex, 1u);
    }

    TEST(NeutralWeatherSound, theQueueHoldsEightAndDropsTheNinth)
    {
        DelayedThunder q;
        for (std::uint32_t i = 0; i < kMaxPendingThunder; ++i)
            EXPECT_TRUE(q.push(1.f, i, 1.f, 0));
        EXPECT_FALSE(q.push(1.f, 99, 1.f, 0));
        EXPECT_EQ(q.size(), kMaxPendingThunder);
        q.clear();
        EXPECT_EQ(q.size(), 0u);
        EXPECT_TRUE(q.push(1.f, 1, 1.f, 0));
    }

    TEST(NeutralWeatherSound, outputIsCappedAndTheRestStaysQueued)
    {
        DelayedThunder q;
        for (std::uint32_t i = 0; i < 3; ++i)
            EXPECT_TRUE(q.push(0.1f, i, 1.f, 0));
        DelayedThunder::Due out[2];
        EXPECT_EQ(q.advance(1.f, out, 2), 2u);
        EXPECT_EQ(q.size(), 1u);
        EXPECT_EQ(q.advance(0.f, out, 2), 1u);
    }

    TEST(NeutralWeatherSound, aBadDelayOrTimeIsHandledQuietly)
    {
        DelayedThunder q;
        EXPECT_TRUE(q.push(-5.f, 1, 1.f, 0)); // due at once
        EXPECT_TRUE(q.push(std::numeric_limits<float>::quiet_NaN(), 2, 1.f, 0)); // treated as due at once too
        DelayedThunder::Due out[4];
        EXPECT_EQ(q.advance(std::numeric_limits<float>::quiet_NaN(), out, 4), 2u);
        EXPECT_EQ(q.size(), 0u);
    }

    TEST(NeutralWeatherSound, gustsComeSoonerInStrongerWind)
    {
        EXPECT_FLOAT_EQ(gustIntervalSeconds(0.f), 30.f);
        EXPECT_FLOAT_EQ(gustIntervalSeconds(1.f), 6.f);
        EXPECT_FLOAT_EQ(gustIntervalSeconds(0.5f), 18.f);
        EXPECT_FLOAT_EQ(gustIntervalSeconds(2.f), 6.f);
        EXPECT_FLOAT_EQ(gustIntervalSeconds(-1.f), 30.f);
        EXPECT_FLOAT_EQ(gustIntervalSeconds(std::numeric_limits<float>::quiet_NaN()), 30.f);
    }

    TEST(NeutralWeatherSound, theGustTimerFiresOncePerIntervalAndCounts)
    {
        GustTimer t;
        // wind 1: every 6 s
        EXPECT_FALSE(t.advance(5.f, 1.f));
        EXPECT_TRUE(t.advance(1.f, 1.f));
        EXPECT_EQ(t.count(), 1u);
        EXPECT_FALSE(t.advance(5.9f, 1.f));
        EXPECT_TRUE(t.advance(0.2f, 1.f));
        EXPECT_EQ(t.count(), 2u);
        t.reset();
        EXPECT_EQ(t.count(), 0u);
        EXPECT_FALSE(t.advance(5.f, 1.f));
    }

    TEST(NeutralWeatherSound, aLongFrameFiresOnceNotManyTimes)
    {
        GustTimer t;
        EXPECT_TRUE(t.advance(100.f, 1.f));
        EXPECT_EQ(t.count(), 1u);
        EXPECT_FALSE(t.advance(0.1f, 1.f));
    }

    TEST(NeutralWeatherSound, aLoopPlaysItsFirstFileAtItsVolumeTimesTheBlend)
    {
        const Slot rain = slot({ "sound/fx/rain.wav", "sound/fx/rain2.wav" }, 0.5f, true);
        const LoopWish w = loopWish(rain, 0.5f);
        EXPECT_EQ(w.file, "sound/fx/rain.wav");
        EXPECT_FLOAT_EQ(w.volume, 0.25f);
    }

    TEST(NeutralWeatherSound, noLoopWhenTheSlotIsEmptyNotALoopOrSilent)
    {
        EXPECT_TRUE(loopWish(Slot{}, 1.f).file.empty());
        EXPECT_TRUE(loopWish(slot({ "sound/a.wav" }, 1.f, false), 1.f).file.empty());
        EXPECT_TRUE(loopWish(slot({ "sound/a.wav" }, 1.f, true), 0.f).file.empty());
        EXPECT_TRUE(loopWish(slot({ "sound/a.wav" }, 1.f, true), -1.f).file.empty());
        EXPECT_TRUE(loopWish(slot({ "sound/a.wav" }, 0.f, true), 1.f).file.empty());
        EXPECT_TRUE(loopWish(slot({ "sound/a.wav" }, 1.f, true), std::numeric_limits<float>::quiet_NaN()).file.empty());
    }

    TEST(NeutralWeatherSound, aSetIsAnyWhenOneSlotHasAFile)
    {
        Set s;
        EXPECT_FALSE(s.any());
        s.wind = slot({ "sound/w.wav" });
        EXPECT_TRUE(s.any());
    }

    // ---- the description to the weather's slots
    // ------------------------------------------------------------------------

    namespace sdk = openeden::sdk::game;

    TEST(NeutralWeatherSoundConversion, theFourSoundsAreCarriedIntoTheParams)
    {
        sdk::WeatherDescription d;
        d.editorId = "SkyrimStormRain";
        d.ambientSound = { "Wind", { "sound/fx/a.wav" }, 0.5f, true };
        d.precipitationSound = { "Rain", { "sound/fx/r.wav", "sound/fx/r2.wav" }, 0.25f, true };
        d.windSound = { "Gust", { "sound/fx/g.wav" }, 1.f, false };
        d.thunderSound = { "Thunder", { "sound/fx/t1.wav", "sound/fx/t2.wav" }, 0.66f, false };
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(d);
        const Set& s = e.params.sounds;
        EXPECT_EQ(s.ambient.name, "Wind");
        EXPECT_EQ(s.ambient.files, std::vector<std::string>{ "sound/fx/a.wav" });
        EXPECT_FLOAT_EQ(s.ambient.volume, 0.5f);
        EXPECT_TRUE(s.ambient.loop);
        EXPECT_EQ(s.precipitation.files.size(), 2u);
        EXPECT_FALSE(s.wind.loop);
        EXPECT_EQ(s.thunder.files[1], "sound/fx/t2.wav");
        EXPECT_FLOAT_EQ(s.thunder.volume, 0.66f);
        EXPECT_TRUE(s.any());
    }

    TEST(NeutralWeatherSoundConversion, noSoundsMeansAnEmptySet)
    {
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(sdk::WeatherDescription{});
        EXPECT_FALSE(e.params.sounds.any());
    }

    TEST(NeutralWeatherSoundConversion, emptyNamesAreDroppedTheCountIsCappedAndTheVolumeIsCleaned)
    {
        sdk::WeatherDescription d;
        d.thunderSound.name = "Many";
        d.thunderSound.files.push_back("");
        for (int i = 0; i < 12; ++i)
            d.thunderSound.files.push_back("sound/fx/t" + std::to_string(i) + ".wav");
        d.thunderSound.volume = 7.f;
        d.windSound = { "Gust", { "sound/fx/g.wav" }, std::numeric_limits<float>::quiet_NaN(), false };
        d.ambientSound = { "Neg", { "sound/fx/n.wav" }, -2.f, true };
        const Set s = MWWorld::weatherEntryFromDescription(d).params.sounds;
        ASSERT_EQ(s.thunder.files.size(), sdk::kMaxWeatherSoundFiles);
        EXPECT_EQ(s.thunder.files.front(), "sound/fx/t0.wav");
        EXPECT_FLOAT_EQ(s.thunder.volume, 1.f);
        EXPECT_FLOAT_EQ(s.wind.volume, 1.f);
        EXPECT_FLOAT_EQ(s.ambient.volume, 0.f);
    }

    TEST(NeutralWeatherSoundConversion, aSlotWithOnlyEmptyFileNamesIsEmpty)
    {
        sdk::WeatherDescription d;
        d.thunderSound = { "Nothing", { "", "" }, 0.5f, false };
        const Set s = MWWorld::weatherEntryFromDescription(d).params.sounds;
        EXPECT_TRUE(s.thunder.empty());
        EXPECT_TRUE(s.thunder.name.empty());
        EXPECT_FLOAT_EQ(s.thunder.volume, 1.f);
    }
}
