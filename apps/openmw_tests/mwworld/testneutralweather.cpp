// Task 0127, stage W-S1: a game pack's weather in the engine's weather manager. The conversions from the SDK's plain
// description to the manager's inputs, the chance normalisation, and the region provider over a real neutral CellStore.
// Expectations are written out by hand.
#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <memory>
#include <numeric>

#include <components/esm3/readerscache.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralweather.hpp"

namespace
{
    namespace sdk = openeden::sdk::game;

    sdk::WeatherDescription description()
    {
        sdk::WeatherDescription d;
        d.id = 0x10;
        d.editorId = "SkyrimClear";
        d.cloudTexture = "Sky\\SkyrimClouds01.dds";
        d.sky = { { 0.1f, 0.2f, 0.3f }, { 0.4f, 0.5f, 0.6f }, { 0.7f, 0.8f, 0.9f }, { 0.01f, 0.02f, 0.03f } };
        d.fog.day = { 0.25f, 0.5f, 0.75f };
        d.ambient.night = { 1.f, 0.f, 0.5f };
        d.sun.sunset = { 0.9f, 0.6f, 0.3f };
        d.fogDepth = { 0.3f, 0.35f, 0.4f, 0.1f };
        d.sunDiscSunset = { 1.f, 0.5f, 0.f };
        d.windSpeed = 0.1f;
        d.cloudSpeed = 0.75f;
        d.glareView = 0.6f;
        d.transitionDelta = 0.015f;
        d.cloudsMaximumPercent = 1.f;
        return d;
    }

    TEST(MWWorldNeutralWeather, a_description_becomes_an_entry_with_every_value_carried)
    {
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(description());
        EXPECT_EQ(e.name, "SkyrimClear");
        EXPECT_FLOAT_EQ(e.dlFactor, 1.f);
        EXPECT_FLOAT_EQ(e.dlOffset, 0.f);
        EXPECT_TRUE(e.particleEffect.empty());
        EXPECT_FALSE(e.forceStorm);
        const MWWorld::WeatherParams& p = e.params;
        EXPECT_EQ(p.cloudTexture, "Sky\\SkyrimClouds01.dds");
        EXPECT_EQ(p.sky[0], vsg::vec4(0.1f, 0.2f, 0.3f, 1.f));
        EXPECT_EQ(p.sky[1], vsg::vec4(0.4f, 0.5f, 0.6f, 1.f));
        EXPECT_EQ(p.sky[2], vsg::vec4(0.7f, 0.8f, 0.9f, 1.f));
        EXPECT_EQ(p.sky[3], vsg::vec4(0.01f, 0.02f, 0.03f, 1.f));
        EXPECT_EQ(p.fog[1], vsg::vec4(0.25f, 0.5f, 0.75f, 1.f));
        EXPECT_EQ(p.ambient[3], vsg::vec4(1.f, 0.f, 0.5f, 1.f));
        EXPECT_EQ(p.sun[2], vsg::vec4(0.9f, 0.6f, 0.3f, 1.f));
        EXPECT_EQ(p.landFogDepth, (std::array<float, 4>{ 0.3f, 0.35f, 0.4f, 0.1f }));
        EXPECT_EQ(p.sunDiscSunsetColor, vsg::vec4(1.f, 0.5f, 0.f, 1.f));
        EXPECT_FLOAT_EQ(p.windSpeed, 0.1f);
        EXPECT_FLOAT_EQ(p.cloudSpeed, 0.75f);
        EXPECT_FLOAT_EQ(p.glareView, 0.6f);
        EXPECT_FLOAT_EQ(p.transitionDelta, 0.015f);
        EXPECT_FLOAT_EQ(p.cloudsMaximumPercent, 1.f);
        // Nothing of the later stage is on: no rain, no thunder, no sound.
        EXPECT_FALSE(p.usingPrecip);
        EXPECT_FLOAT_EQ(p.thunderFrequency, 0.f);
        EXPECT_TRUE(p.rainLoopSoundId.empty());
        EXPECT_TRUE(p.ambientLoopSoundId.empty());
    }

    TEST(MWWorldNeutralWeather, a_weather_without_an_editor_id_is_named_by_its_id_and_a_zero_cloud_percent_is_raised)
    {
        sdk::WeatherDescription d = description();
        d.editorId.clear();
        d.id = 0xABC;
        d.cloudsMaximumPercent = 0.f;
        d.storm = true;
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(d);
        EXPECT_EQ(e.name, "weatherabc");
        EXPECT_FLOAT_EQ(e.params.cloudsMaximumPercent, 0.01f);
        EXPECT_TRUE(e.forceStorm);
    }

    TEST(MWWorldNeutralWeather, the_sun_hours_become_the_manager_clock)
    {
        sdk::TimeOfDayDescription t;
        t.sunriseBegin = 5.5f;
        t.sunriseEnd = 10.f;
        t.sunsetBegin = 16.f;
        t.sunsetEnd = 20.5f;
        const MWWorld::WeatherManagerParams p = MWWorld::neutralManagerParams(&t);
        EXPECT_FLOAT_EQ(p.sunriseTime, 5.5f);
        EXPECT_FLOAT_EQ(p.sunriseDuration, 4.5f);
        EXPECT_FLOAT_EQ(p.sunsetTime, 16.f);
        EXPECT_FLOAT_EQ(p.sunsetDuration, 4.5f);
        EXPECT_FLOAT_EQ(p.hoursBetweenWeatherChanges, 12.f);
        EXPECT_FLOAT_EQ(p.rainSpeed, 575.f);
        ASSERT_EQ(p.transitions.size(), 4u);
        for (const char* type : { "Sky", "Ambient", "Fog", "Sun" })
            EXPECT_EQ(p.transitions.at(type), (std::array<float, 4>{ 0.f, 0.f, 0.f, 0.f })) << type;
        EXPECT_FLOAT_EQ(p.starsFadingDuration, 2.f);
    }

    TEST(MWWorldNeutralWeather, without_hours_the_clock_is_morrowinds_and_a_zero_length_phase_is_one_minute)
    {
        const MWWorld::WeatherManagerParams d = MWWorld::neutralManagerParams(nullptr);
        EXPECT_FLOAT_EQ(d.sunriseTime, 6.f);
        EXPECT_FLOAT_EQ(d.sunsetTime, 18.f);
        EXPECT_FLOAT_EQ(d.sunriseDuration, 2.f);
        EXPECT_FLOAT_EQ(d.sunsetDuration, 2.f);

        sdk::TimeOfDayDescription t;
        t.sunriseBegin = 6.f;
        t.sunriseEnd = 6.f; // zero length
        t.sunsetBegin = 20.f;
        t.sunsetEnd = 19.f; // backwards
        const MWWorld::WeatherManagerParams p = MWWorld::neutralManagerParams(&t);
        EXPECT_FLOAT_EQ(p.sunriseDuration, 1.f / 60.f);
        EXPECT_FLOAT_EQ(p.sunsetDuration, 1.f / 60.f);
    }

    unsigned sum(const std::vector<std::uint8_t>& v)
    {
        return std::accumulate(v.begin(), v.end(), 0u);
    }

    TEST(MWWorldNeutralWeather, chances_are_scaled_to_exactly_one_hundred)
    {
        sdk::CellWeatherList list;
        list.entries = { { 0, 70 }, { 2, 30 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 3), (std::vector<std::uint8_t>{ 70, 0, 30 }));

        // A list that adds to 85 (one is in the installed data) is scaled: 50/85 -> 58, 35/85 -> 41, remainder 1 to the
        // largest.
        list.entries = { { 0, 50 }, { 1, 35 } };
        const std::vector<std::uint8_t> c = MWWorld::normalizedChances(list, 2);
        EXPECT_EQ(c, (std::vector<std::uint8_t>{ 59, 41 }));
        EXPECT_EQ(sum(c), 100u);

        // Three equal thirds: 33 each, the remainder goes to the lowest index of the largest.
        list.entries = { { 0, 1 }, { 1, 1 }, { 2, 1 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 3), (std::vector<std::uint8_t>{ 34, 33, 33 }));
    }

    TEST(MWWorldNeutralWeather, the_rounding_remainder_goes_to_the_largest_weight_not_the_first_entry)
    {
        // 1/3 -> 33 and 2/3 -> 66 add to 99: the missing one goes to the larger weight (index 1).
        sdk::CellWeatherList list;
        list.entries = { { 0, 1 }, { 1, 2 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 2), (std::vector<std::uint8_t>{ 33, 67 }));
    }

    TEST(MWWorldNeutralWeather, chances_ignore_entries_outside_the_table_and_an_empty_list_is_all_zero)
    {
        sdk::CellWeatherList list;
        list.entries = { { 5, 100 }, { 1, 10 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 2), (std::vector<std::uint8_t>{ 0, 100 }));
        list.entries = { { 0, 0 }, { 9, 5 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 2), (std::vector<std::uint8_t>{ 0, 0 }));
        list.entries.clear();
        EXPECT_EQ(MWWorld::normalizedChances(list, 0), std::vector<std::uint8_t>{});
    }

    TEST(MWWorldNeutralWeather, the_same_weather_twice_in_a_list_adds_up)
    {
        sdk::CellWeatherList list;
        list.entries = { { 1, 20 }, { 0, 60 }, { 1, 20 } };
        EXPECT_EQ(MWWorld::normalizedChances(list, 2), (std::vector<std::uint8_t>{ 60, 40 }));
    }

    // A source with a fixed answer per cell.
    struct FakeSource final : sdk::WeatherSource
    {
        std::map<sdk::WorldId, sdk::CellWeatherList> lists;
        int asked = 0;
        bool describeWeathers(std::vector<sdk::WeatherDescription>& out, std::string&) override
        {
            out = { description(), description() };
            out[1].editorId = "Second";
            return true;
        }
        bool describeTimeOfDay(sdk::TimeOfDayDescription&) override { return false; }
        bool weatherListForCell(sdk::WorldId cell, sdk::CellWeatherList& out) override
        {
            ++asked;
            const auto it = lists.find(cell);
            if (it == lists.end())
                return false;
            out = it->second;
            return true;
        }
    };

    struct ProviderFixture : testing::Test
    {
        void SetUp() override { MWClass::Neutral::registerSelf(); }

        std::unique_ptr<MWWorld::CellStore> neutralCell(sdk::WorldId id, bool interior)
        {
            sdk::WorldCell c;
            c.id = id;
            c.editorId = "Cell" + std::to_string(id);
            c.interior = interior;
            c.hasGrid = !interior;
            c.world = interior ? sdk::kNoWorldId : 20;
            const ESM::Cell record = MWWorld::makeNeutralCellRecord("fakegame", c);
            auto store = std::make_unique<MWWorld::CellStore>(MWWorld::Cell(record), mStore, mReaders);
            EXPECT_TRUE(store->setNeutralCell("fakegame", c));
            return store;
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
    };

    TEST_F(ProviderFixture, cells_with_one_list_share_a_region_and_the_chances_follow_the_table)
    {
        FakeSource source;
        sdk::CellWeatherList list;
        list.key = 0x77;
        list.entries = { { 0, 25 }, { 1, 75 } };
        source.lists[1] = list;
        source.lists[2] = list;
        MWWorld::NeutralRegionProvider provider(source, 2);

        const auto a = neutralCell(1, false);
        const auto b = neutralCell(2, false);
        const ESM::RefId ka = provider.regionOf(*a);
        const ESM::RefId kb = provider.regionOf(*b);
        EXPECT_FALSE(ka.empty());
        EXPECT_EQ(ka, kb);
        std::vector<std::uint8_t> chances;
        ASSERT_TRUE(provider.chancesOf(ka, chances));
        EXPECT_EQ(chances, (std::vector<std::uint8_t>{ 25, 75 }));

        // A second ask for the same cell does not ask the pack again.
        const int before = source.asked;
        EXPECT_EQ(provider.regionOf(*a), ka);
        EXPECT_EQ(source.asked, before);
        EXPECT_FALSE(provider.chancesOf(ESM::RefId::stringRefId("nothing"), chances));
    }

    TEST_F(ProviderFixture, different_keys_are_different_regions)
    {
        FakeSource source;
        sdk::CellWeatherList one, two;
        one.key = 0x77;
        one.entries = { { 0, 100 } };
        two.key = 0x78;
        two.entries = { { 1, 100 } };
        source.lists[1] = one;
        source.lists[2] = two;
        MWWorld::NeutralRegionProvider provider(source, 2);
        EXPECT_NE(provider.regionOf(*neutralCell(1, false)), provider.regionOf(*neutralCell(2, false)));
    }

    TEST_F(ProviderFixture, an_interior_a_cell_without_a_list_and_a_plain_cell_have_no_region)
    {
        FakeSource source;
        sdk::CellWeatherList list;
        list.key = 0x77;
        list.entries = { { 0, 100 } };
        source.lists[1] = list; // an interior with a list is still no region
        MWWorld::NeutralRegionProvider provider(source, 2);
        EXPECT_TRUE(provider.regionOf(*neutralCell(1, true)).empty());
        EXPECT_TRUE(provider.regionOf(*neutralCell(9, false)).empty()); // the pack has no list
        EXPECT_EQ(source.asked, 1); // the interior was not even asked

        // A cell that is not a neutral cell at all.
        sdk::WorldCell wc;
        wc.id = 3;
        wc.hasGrid = true;
        wc.world = 20;
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("fakegame", wc);
        MWWorld::CellStore plain{ MWWorld::Cell(record), mStore, mReaders };
        EXPECT_TRUE(provider.regionOf(plain).empty());
    }

    TEST_F(ProviderFixture, a_list_of_only_unknown_weathers_gives_no_region)
    {
        FakeSource source;
        sdk::CellWeatherList list;
        list.key = 0x77;
        list.entries = { { 7, 100 } }; // outside a table of 2
        source.lists[1] = list;
        MWWorld::NeutralRegionProvider provider(source, 2);
        EXPECT_TRUE(provider.regionOf(*neutralCell(1, false)).empty());
    }

    TEST(MWWorldNeutralWeather, a_setup_is_made_from_a_source_and_refused_without_weather)
    {
        FakeSource source;
        MWWorld::NeutralWeatherSetup setup;
        std::string error;
        ASSERT_TRUE(MWWorld::makeNeutralWeatherSetup(source, setup, error));
        ASSERT_EQ(setup.weathers.size(), 2u);
        EXPECT_EQ(setup.weathers[1].name, "Second");
        EXPECT_NE(setup.regions, nullptr);
        EXPECT_FLOAT_EQ(setup.manager.sunriseTime, 6.f); // the fake gives no hours

        struct Empty final : sdk::WeatherSource
        {
            bool describeWeathers(std::vector<sdk::WeatherDescription>& out, std::string&) override
            {
                out.clear();
                return true;
            }
            bool describeTimeOfDay(sdk::TimeOfDayDescription&) override { return false; }
            bool weatherListForCell(sdk::WorldId, sdk::CellWeatherList&) override { return false; }
        } empty;
        EXPECT_FALSE(MWWorld::makeNeutralWeatherSetup(empty, setup, error));
        EXPECT_FALSE(error.empty());
    }

    // ---- Stage W-S2 (precipitation, snow and thunder). The numbers are Morrowind's own INI values for the weathers the
    // engine's code was written for (files/openmw.cfg: Weather_Rain_*, Weather_Thunderstorm_*), written out by hand.

    sdk::WeatherDescription rainy(float threshold = 0.6f)
    {
        sdk::WeatherDescription d = description();
        d.precipitation = sdk::Precipitation::Rain;
        d.precipitationThreshold = threshold;
        return d;
    }

    TEST(MWWorldNeutralWeather, rain_uses_the_engines_rain_with_morrowinds_rain_numbers_and_no_loop_sound)
    {
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(rainy());
        const MWWorld::WeatherParams& p = e.params;
        EXPECT_TRUE(p.usingPrecip);
        EXPECT_FLOAT_EQ(p.rainEntranceSpeed, 7.f);
        EXPECT_EQ(p.rainMaxRaindrops, 450);
        EXPECT_FLOAT_EQ(p.rainDiameter, 600.f);
        EXPECT_FLOAT_EQ(p.rainMinHeight, 200.f);
        EXPECT_FLOAT_EQ(p.rainMaxHeight, 700.f);
        EXPECT_FLOAT_EQ(p.rainThreshold, 0.6f);
        // The game's own rain sound is not wired yet: "None" is how the INI says no loop.
        EXPECT_EQ(p.rainLoopSoundId, "None");
        EXPECT_TRUE(e.particleEffect.empty());
        EXPECT_FLOAT_EQ(p.thunderFrequency, 0.f);
    }

    TEST(MWWorldNeutralWeather, the_precipitation_threshold_is_kept_between_a_tenth_and_nine_tenths)
    {
        EXPECT_FLOAT_EQ(MWWorld::weatherEntryFromDescription(rainy(0.f)).params.rainThreshold, 0.1f);
        EXPECT_FLOAT_EQ(MWWorld::weatherEntryFromDescription(rainy(1.f)).params.rainThreshold, 0.9f);
        EXPECT_FLOAT_EQ(MWWorld::weatherEntryFromDescription(rainy(0.3f)).params.rainThreshold, 0.3f);
        EXPECT_FLOAT_EQ(MWWorld::weatherEntryFromDescription(rainy(std::numeric_limits<float>::quiet_NaN())).params.rainThreshold,
            0.5f);
    }

    TEST(MWWorldNeutralWeather, rain_with_thunder_takes_the_thunderstorms_numbers)
    {
        sdk::WeatherDescription d = rainy();
        d.thunderFrequency = 0.9f;
        d.thunderThreshold = 0.25f;
        const MWWorld::WeatherParams p = MWWorld::weatherEntryFromDescription(d).params;
        EXPECT_TRUE(p.usingPrecip);
        EXPECT_FLOAT_EQ(p.rainEntranceSpeed, 5.f);
        EXPECT_EQ(p.rainMaxRaindrops, 650);
        EXPECT_FLOAT_EQ(p.rainDiameter, 600.f);
        EXPECT_FLOAT_EQ(p.thunderFrequency, 0.9f);
        EXPECT_FLOAT_EQ(p.thunderThreshold, 0.25f);
        EXPECT_FLOAT_EQ(p.flashDecrement, 4.f);
        // The sound ids stay empty: the engine's sound lookup finds nothing for them and plays nothing.
        for (const std::string& id : p.thunderSoundId)
            EXPECT_TRUE(id.empty());
    }

    TEST(MWWorldNeutralWeather, thunder_without_rain_still_flashes_and_the_threshold_stays_below_one)
    {
        sdk::WeatherDescription d = description();
        d.thunderFrequency = 2.4f;
        d.thunderThreshold = 1.f; // would divide by zero in the strike chance
        const MWWorld::WeatherParams p = MWWorld::weatherEntryFromDescription(d).params;
        EXPECT_FALSE(p.usingPrecip);
        EXPECT_FLOAT_EQ(p.thunderFrequency, 2.4f);
        EXPECT_FLOAT_EQ(p.thunderThreshold, 0.9f);
        EXPECT_FLOAT_EQ(p.flashDecrement, 4.f);
    }

    TEST(MWWorldNeutralWeather, a_weather_without_thunder_has_no_flash)
    {
        sdk::WeatherDescription d = description();
        d.thunderThreshold = 0.5f; // a threshold alone does nothing
        const MWWorld::WeatherParams p = MWWorld::weatherEntryFromDescription(d).params;
        EXPECT_FLOAT_EQ(p.thunderFrequency, 0.f);
        EXPECT_FLOAT_EQ(p.flashDecrement, 0.f);
        EXPECT_FLOAT_EQ(p.thunderThreshold, 0.f);
    }

    TEST(MWWorldNeutralWeather, snow_is_the_snow_effect_and_a_hard_wind_makes_it_the_blizzard)
    {
        sdk::WeatherDescription d = description();
        d.precipitation = sdk::Precipitation::Snow;
        d.windSpeed = 0.3f;
        const MWWorld::WeatherEntry snow = MWWorld::weatherEntryFromDescription(d);
        EXPECT_EQ(snow.particleEffect, Settings::models().mWeathersnow.get().value());
        EXPECT_FALSE(snow.params.usingPrecip);
        EXPECT_TRUE(snow.params.rainLoopSoundId.empty());

        d.windSpeed = 0.5f;
        EXPECT_EQ(MWWorld::weatherEntryFromDescription(d).particleEffect, Settings::models().mWeatherblizzard.get().value());
        d.windSpeed = 0.7f;
        EXPECT_EQ(MWWorld::weatherEntryFromDescription(d).particleEffect, Settings::models().mWeatherblizzard.get().value());
        // Its threshold is the same value the rain uses.
        d.precipitationThreshold = 0.8f;
        EXPECT_FLOAT_EQ(MWWorld::weatherEntryFromDescription(d).params.rainThreshold, 0.8f);
    }

    TEST(MWWorldNeutralWeather, a_forced_weather_is_found_by_its_name_whatever_the_case)
    {
        std::vector<MWWorld::WeatherEntry> table(3);
        table[0].name = "SkyrimClear";
        table[1].name = "SkyrimOvercastSnow";
        table[2].name = "SkyrimStormRain";
        EXPECT_EQ(MWWorld::findWeatherByName(table, "SkyrimOvercastSnow"), 1);
        EXPECT_EQ(MWWorld::findWeatherByName(table, "skyrimstormrain"), 2);
        EXPECT_EQ(MWWorld::findWeatherByName(table, "SkyrimNothing"), -1);
        EXPECT_EQ(MWWorld::findWeatherByName(table, ""), -1);
        EXPECT_EQ(MWWorld::findWeatherByName({}, "SkyrimClear"), -1);
    }

    TEST(MWWorldNeutralWeather, no_precipitation_leaves_every_precipitation_field_at_none)
    {
        const MWWorld::WeatherEntry e = MWWorld::weatherEntryFromDescription(description());
        EXPECT_FALSE(e.params.usingPrecip);
        EXPECT_TRUE(e.particleEffect.empty());
        EXPECT_EQ(e.params.rainMaxRaindrops, 0);
        EXPECT_FLOAT_EQ(e.params.rainEntranceSpeed, 0.f);
        EXPECT_FLOAT_EQ(e.params.rainThreshold, 0.f);
    }
}
