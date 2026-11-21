// Task 0355, stage k: the weather's directional ambient (WTHR DALC) as the SDK's plain description. Records are built
// by hand; every expected number is written out from the sample bytes.
#include <game_skyrimse/weather_source.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    WeatherRecord weatherRecord()
    {
        WeatherRecord w;
        w.mCommon.mId = 0x20;
        w.mCommon.mEditorId = "CubeTest";
        w.mColourTypes = 17;
        return w;
    }

    // One time of day's DALC: face i gets (base + i, base + 10 + i, base + 20 + i).
    DirectionalAmbient record(std::uint8_t base)
    {
        DirectionalAmbient a;
        for (std::size_t i = 0; i < 6; ++i)
            a.mDirections[i] = Rgba8{ static_cast<std::uint8_t>(base + i), static_cast<std::uint8_t>(base + 10 + i),
                static_cast<std::uint8_t>(base + 20 + i), 0 };
        return a;
    }

    TEST(SkyrimWeatherAmbientCube, fourRecordsFillTheCubeInTimeOfDayAndDirectionOrder)
    {
        WeatherRecord w = weatherRecord();
        w.mDirectionalAmbient = { record(0), record(50), record(100), record(150) };
        const sdk::WeatherDescription d = describeWeather(w);
        ASSERT_TRUE(d.hasAmbientCube);
        EXPECT_FLOAT_EQ(d.ambientCube.sunrise[0].r, 0.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.sunrise[5].b, 25.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.day[0].r, 50.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.day[3].g, 63.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.sunset[2].g, 112.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.night[4].b, 174.f / 255.f);
        EXPECT_FLOAT_EQ(d.ambientCube.night[5].r, 155.f / 255.f);
    }

    TEST(SkyrimWeatherAmbientCube, aWeatherWithoutDalcHasNoCube)
    {
        const sdk::WeatherDescription d = describeWeather(weatherRecord());
        EXPECT_FALSE(d.hasAmbientCube);
        EXPECT_EQ(d.ambientCube.day[0], (sdk::WeatherColour{}));
    }

    TEST(SkyrimWeatherAmbientCube, aHalfTableIsNotUsed)
    {
        WeatherRecord w = weatherRecord();
        w.mDirectionalAmbient = { record(0), record(50), record(100) };
        EXPECT_FALSE(describeWeather(w).hasAmbientCube);
    }

    TEST(SkyrimWeatherAmbientCube, theFlatAmbientColourIsNotTouchedByTheCube)
    {
        WeatherRecord w = weatherRecord();
        w.mColours[SkyColourType::Ambient][WeatherConstants::Day] = Rgba8{ 255, 0, 0, 0 };
        w.mDirectionalAmbient = { record(0), record(50), record(100), record(150) };
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.ambient.day, (sdk::WeatherColour{ 1.f, 0.f, 0.f }));
    }

    TEST(SkyrimWeatherAmbientCube, theWeatherSourceVersionIsZeroPointFive)
    {
        EXPECT_EQ(sdk::kWeatherSourceVersion.minor, 5u);
    }
}
