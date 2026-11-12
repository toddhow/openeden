// Task 0127, stage W-S1: Weather and WeatherManager now read a plain description (mwworld/weatherparams.hpp) instead of
// the INI keys. The Morrowind path fills it from Fallback::Map. These tests pin that path FIELD BY FIELD against the
// numbers the old constructors read: every Fallback value below is distinct, every expectation is written out by hand
// from the old constructor text (apps/openmw/mwworld/weather.cpp at the parent commit), none computed by the code under
// test.
#include <gtest/gtest.h>

#include <map>
#include <string>

#include <components/fallback/fallback.hpp>

#include "apps/openmw/mwworld/weather.hpp"
#include "apps/openmw/mwworld/weatherparams.hpp"

namespace
{
    // Fallback::Map::init only adds keys (emplace), and no other test of this binary fills it, so the first call wins.
    struct FallbackFixture : testing::Test
    {
        static void SetUpTestSuite()
        {
            std::map<std::string, std::string> m;
            const auto colours = [&](const char* group, int base)
            {
                const char* times[] = { "Sunrise", "Day", "Sunset", "Night" };
                for (int i = 0; i < 4; ++i)
                    m[std::string("Weather_Rain_") + group + "_" + times[i] + "_Color"]
                        = std::to_string(base + i) + "," + std::to_string(base + i + 10) + ","
                        + std::to_string(base + i + 20);
            };
            colours("Sky", 10);
            colours("Fog", 40);
            colours("Ambient", 70);
            colours("Sun", 100);
            m["Weather_Rain_Sun_Disc_Sunset_Color"] = "255,128,0";
            m["Weather_Rain_Cloud_Texture"] = "Tx_Sky_Clear.tga";
            m["Weather_Rain_Land_Fog_Day_Depth"] = "0.69";
            m["Weather_Rain_Land_Fog_Night_Depth"] = "0.4";
            m["Weather_Rain_Wind_Speed"] = "0.5";
            m["Weather_Rain_Cloud_Speed"] = "1.25";
            m["Weather_Rain_Glare_View"] = "0.75";
            m["Weather_Rain_Rain_Entrance_Speed"] = "7.5";
            m["Weather_Rain_Max_Raindrops"] = "33";
            m["Weather_Rain_Rain_Diameter"] = "1024";
            m["Weather_Rain_Rain_Threshold"] = "0.6";
            m["Weather_Rain_Rain_Height_Min"] = "100";
            m["Weather_Rain_Rain_Height_Max"] = "300";
            m["Weather_Rain_Using_Precip"] = "1";
            m["Weather_Rain_Clouds_Maximum_Percent"] = "0.9";
            m["Weather_Rain_Transition_Delta"] = "0.015";
            m["Weather_Rain_Thunder_Frequency"] = "0.3";
            m["Weather_Rain_Thunder_Threshold"] = "0.2";
            m["Weather_Rain_Flash_Decrement"] = "0.7";
            m["Weather_Rain_Thunder_Sound_ID_0"] = "Thunder0";
            m["Weather_Rain_Thunder_Sound_ID_1"] = "Thunder1";
            m["Weather_Rain_Thunder_Sound_ID_2"] = "Thunder2";
            m["Weather_Rain_Thunder_Sound_ID_3"] = "Thunder3";
            m["Weather_Rain_Rain_Loop_Sound_ID"] = "Rain Heavy";
            m["Weather_Rain_Ambient_Loop_Sound_ID"] = "None";

            m["Weather_Sunrise_Time"] = "5.5";
            m["Weather_Sunset_Time"] = "19.5";
            m["Weather_Sunrise_Duration"] = "2.5";
            m["Weather_Sunset_Duration"] = "3.5";
            m["Weather_Hours_Between_Weather_Changes"] = "18";
            m["Weather_Precip_Gravity"] = "575";
            m["Water_UnderwaterSunriseFog"] = "3.1";
            m["Water_UnderwaterDayFog"] = "2.6";
            m["Water_UnderwaterSunsetFog"] = "3.2";
            m["Water_UnderwaterNightFog"] = "4.1";
            int k = 1;
            for (const char* type : { "Sky", "Ambient", "Fog", "Sun" })
                for (const char* w : { "Pre-Sunrise", "Post-Sunrise", "Pre-Sunset", "Post-Sunset" })
                    m[std::string("Weather_") + type + "_" + w + "_Time"] = std::to_string(k++) + ".5";
            m["Weather_Stars_Post-Sunset_Start"] = "1.1";
            m["Weather_Stars_Pre-Sunrise_Finish"] = "2.2";
            m["Weather_Stars_Fading_Duration"] = "3.3";
            Fallback::Map::init(m);
        }
    };

    vsg::vec4 rgb(float r, float g, float b)
    {
        return vsg::vec4(r / 255.f, g / 255.f, b / 255.f, 1.f);
    }

    void expectColour(const vsg::vec4& got, float r, float g, float b)
    {
        const vsg::vec4 want = rgb(r, g, b);
        EXPECT_FLOAT_EQ(got.x, want.x);
        EXPECT_FLOAT_EQ(got.y, want.y);
        EXPECT_FLOAT_EQ(got.z, want.z);
        EXPECT_FLOAT_EQ(got.w, 1.f);
    }

    TEST_F(FallbackFixture, the_fallback_params_hold_every_key_the_old_constructor_read)
    {
        const MWWorld::WeatherParams p = MWWorld::weatherParamsFromFallback("Rain");
        EXPECT_EQ(p.cloudTexture, "Tx_Sky_Clear.tga");
        for (int i = 0; i < 4; ++i)
        {
            expectColour(p.sky[i], 10.f + i, 20.f + i, 30.f + i);
            expectColour(p.fog[i], 40.f + i, 50.f + i, 60.f + i);
            expectColour(p.ambient[i], 70.f + i, 80.f + i, 90.f + i);
            expectColour(p.sun[i], 100.f + i, 110.f + i, 120.f + i);
        }
        // The old constructor used the DAY depth for sunrise, day and sunset and the NIGHT depth for night.
        EXPECT_FLOAT_EQ(p.landFogDepth[0], 0.69f);
        EXPECT_FLOAT_EQ(p.landFogDepth[1], 0.69f);
        EXPECT_FLOAT_EQ(p.landFogDepth[2], 0.69f);
        EXPECT_FLOAT_EQ(p.landFogDepth[3], 0.4f);
        expectColour(p.sunDiscSunsetColor, 255, 128, 0);
        EXPECT_FLOAT_EQ(p.windSpeed, 0.5f);
        EXPECT_FLOAT_EQ(p.cloudSpeed, 1.25f);
        EXPECT_FLOAT_EQ(p.glareView, 0.75f);
        EXPECT_FLOAT_EQ(p.rainEntranceSpeed, 7.5f);
        EXPECT_EQ(p.rainMaxRaindrops, 33);
        EXPECT_FLOAT_EQ(p.rainDiameter, 1024.f);
        EXPECT_FLOAT_EQ(p.rainThreshold, 0.6f);
        EXPECT_FLOAT_EQ(p.rainMinHeight, 100.f);
        EXPECT_FLOAT_EQ(p.rainMaxHeight, 300.f);
        EXPECT_TRUE(p.usingPrecip);
        EXPECT_FLOAT_EQ(p.cloudsMaximumPercent, 0.9f);
        EXPECT_FLOAT_EQ(p.transitionDelta, 0.015f);
        EXPECT_FLOAT_EQ(p.thunderFrequency, 0.3f);
        EXPECT_FLOAT_EQ(p.thunderThreshold, 0.2f);
        EXPECT_FLOAT_EQ(p.flashDecrement, 0.7f);
        EXPECT_EQ(p.thunderSoundId[0], "Thunder0");
        EXPECT_EQ(p.thunderSoundId[1], "Thunder1");
        EXPECT_EQ(p.thunderSoundId[2], "Thunder2");
        EXPECT_EQ(p.thunderSoundId[3], "Thunder3");
        EXPECT_EQ(p.rainLoopSoundId, "Rain Heavy");
        EXPECT_EQ(p.ambientLoopSoundId, "None");
    }

    TEST_F(FallbackFixture, a_weather_built_the_old_way_has_every_value_of_the_fallback)
    {
        // stormWindSpeed 0.4 is below the wind speed 0.5, so it is a storm.
        const MWWorld::Weather w(ESM::RefId::stringRefId("Rain"), 3, "Rain", 0.4f, 575.f, 0.9f, 12.f, "particle.nif");
        EXPECT_EQ(w.mScriptId, 3);
        EXPECT_EQ(w.mName, "Rain");
        EXPECT_EQ(w.mCloudTexture, "Tx_Sky_Clear.tga");
        expectColour(w.mSkyColor.getSunriseValue(), 10, 20, 30);
        expectColour(w.mSkyColor.getDayValue(), 11, 21, 31);
        expectColour(w.mSkyColor.getSunsetValue(), 12, 22, 32);
        expectColour(w.mSkyColor.getNightValue(), 13, 23, 33);
        expectColour(w.mFogColor.getNightValue(), 43, 53, 63);
        expectColour(w.mAmbientColor.getSunsetValue(), 72, 82, 92);
        expectColour(w.mSunColor.getDayValue(), 101, 111, 121);
        EXPECT_FLOAT_EQ(w.mLandFogDepth.getSunriseValue(), 0.69f);
        EXPECT_FLOAT_EQ(w.mLandFogDepth.getSunsetValue(), 0.69f);
        EXPECT_FLOAT_EQ(w.mLandFogDepth.getNightValue(), 0.4f);
        expectColour(w.mSunDiscSunsetColor, 255, 128, 0);
        EXPECT_FLOAT_EQ(w.mWindSpeed, 0.5f);
        EXPECT_FLOAT_EQ(w.mCloudSpeed, 1.25f);
        EXPECT_FLOAT_EQ(w.mGlareView, 0.75f);
        EXPECT_TRUE(w.mIsStorm);
        EXPECT_FLOAT_EQ(w.mRainSpeed, 575.f);
        EXPECT_FLOAT_EQ(w.mRainEntranceSpeed, 7.5f);
        EXPECT_EQ(w.mRainMaxRaindrops, 33);
        EXPECT_FLOAT_EQ(w.mRainDiameter, 1024.f);
        EXPECT_FLOAT_EQ(w.mRainThreshold, 0.6f);
        EXPECT_FLOAT_EQ(w.mRainMinHeight, 100.f);
        EXPECT_FLOAT_EQ(w.mRainMaxHeight, 300.f);
        EXPECT_EQ(w.mParticleEffect, "particle.nif");
        EXPECT_EQ(w.mRainEffect, "raindrop.nif"); // Using_Precip is set
        EXPECT_FLOAT_EQ(w.mCloudsMaximumPercent, 0.9f);
        EXPECT_FLOAT_EQ(w.transitionDelta(), 0.015f);
        EXPECT_FLOAT_EQ(w.mDL.FogFactor, 0.9f);
        EXPECT_FLOAT_EQ(w.mDL.FogOffset, 12.f);
        EXPECT_EQ(w.mThunderSoundID[0], ESM::RefId::stringRefId("Thunder0"));
        EXPECT_EQ(w.mThunderSoundID[3], ESM::RefId::stringRefId("Thunder3"));
        // A rain weather takes its loop sound from the key, and "None" for the ambient loop means no sound.
        EXPECT_EQ(w.mRainLoopSoundID, ESM::RefId::stringRefId("Rain Heavy"));
        EXPECT_TRUE(w.mAmbientLoopSoundID.empty());
        EXPECT_EQ(w.mStormDirection, MWWorld::Weather::defaultDirection());
    }

    TEST_F(FallbackFixture, a_weather_with_a_storm_wind_limit_above_its_wind_is_no_storm)
    {
        const MWWorld::Weather w(ESM::RefId::stringRefId("Rain"), 0, "Rain", 0.8f, 575.f, 1.f, 0.f, "");
        EXPECT_FALSE(w.mIsStorm);
    }

    TEST_F(FallbackFixture, the_new_constructor_with_the_fallback_params_equals_the_old_one)
    {
        const MWWorld::Weather a(ESM::RefId::stringRefId("Rain"), 1, "Rain", 0.4f, 575.f, 0.9f, 12.f, "x.nif");
        const MWWorld::Weather b(ESM::RefId::stringRefId("Rain"), 1, "Rain", 0.4f, 575.f, 0.9f, 12.f, "x.nif",
            MWWorld::weatherParamsFromFallback("Rain"));
        EXPECT_EQ(a.mCloudTexture, b.mCloudTexture);
        EXPECT_EQ(a.mSkyColor.getDayValue(), b.mSkyColor.getDayValue());
        EXPECT_EQ(a.mLandFogDepth.getNightValue(), b.mLandFogDepth.getNightValue());
        EXPECT_EQ(a.mWindSpeed, b.mWindSpeed);
        EXPECT_EQ(a.mIsStorm, b.mIsStorm);
        EXPECT_EQ(a.mRainEffect, b.mRainEffect);
        EXPECT_EQ(a.mThunderSoundID, b.mThunderSoundID);
        EXPECT_EQ(a.transitionDelta(), b.transitionDelta());
    }

    TEST_F(FallbackFixture, a_weather_without_precipitation_never_asks_for_the_rain_loop_key)
    {
        // Fallback::Map throws for "Weather_Cloudy_Rain_Loop_Sound_ID" (only rain and thunderstorm have the key): the
        // old constructor read it only for a weather with precipitation, and so must the params.
        MWWorld::WeatherParams p;
        ASSERT_NO_THROW(p = MWWorld::weatherParamsFromFallback("Cloudy"));
        EXPECT_FALSE(p.usingPrecip);
        EXPECT_TRUE(p.rainLoopSoundId.empty());
        const MWWorld::Weather w(ESM::RefId::stringRefId("Cloudy"), 1, "Cloudy", 0.8f, 575.f, 0.9f, 0.f, "");
        EXPECT_TRUE(w.mRainEffect.empty());
        EXPECT_TRUE(w.mRainLoopSoundID.empty());
    }

    TEST_F(FallbackFixture, the_manager_params_hold_every_key_the_old_constructor_read)
    {
        const MWWorld::WeatherManagerParams p = MWWorld::weatherManagerParamsFromFallback();
        EXPECT_FLOAT_EQ(p.sunriseTime, 5.5f);
        EXPECT_FLOAT_EQ(p.sunsetTime, 19.5f);
        EXPECT_FLOAT_EQ(p.sunriseDuration, 2.5f);
        EXPECT_FLOAT_EQ(p.sunsetDuration, 3.5f);
        // The same INI key as the Sun window's pre-sunset time (the sixteen values below): 15.5.
        EXPECT_FLOAT_EQ(p.sunPreSunsetTime, 15.5f);
        EXPECT_FLOAT_EQ(p.hoursBetweenWeatherChanges, 18.f);
        EXPECT_FLOAT_EQ(p.rainSpeed, 575.f);
        EXPECT_FLOAT_EQ(p.underwaterFog[0], 3.1f);
        EXPECT_FLOAT_EQ(p.underwaterFog[1], 2.6f);
        EXPECT_FLOAT_EQ(p.underwaterFog[2], 3.2f);
        EXPECT_FLOAT_EQ(p.underwaterFog[3], 4.1f);
        // The sixteen windows were given 1.5, 2.5 ... 16.5 in the order Sky, Ambient, Fog, Sun x pre-sunrise,
        // post-sunrise, pre-sunset, post-sunset.
        ASSERT_EQ(p.transitions.size(), 4u);
        EXPECT_EQ(p.transitions.at("Sky"), (std::array<float, 4>{ 1.5f, 2.5f, 3.5f, 4.5f }));
        EXPECT_EQ(p.transitions.at("Ambient"), (std::array<float, 4>{ 5.5f, 6.5f, 7.5f, 8.5f }));
        EXPECT_EQ(p.transitions.at("Fog"), (std::array<float, 4>{ 9.5f, 10.5f, 11.5f, 12.5f }));
        EXPECT_EQ(p.transitions.at("Sun"), (std::array<float, 4>{ 13.5f, 14.5f, 15.5f, 16.5f }));
        EXPECT_FLOAT_EQ(p.starsPostSunsetStart, 1.1f);
        EXPECT_FLOAT_EQ(p.starsPreSunriseFinish, 2.2f);
        EXPECT_FLOAT_EQ(p.starsFadingDuration, 3.3f);
    }
}
