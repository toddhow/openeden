// Task 0127, stage W-S1: Skyrim SE weather as the SDK's plain description (weather_source.hpp). Records are built by
// hand; every expected number is written out from the constants of WeatherScale and the sample values, not computed by
// the code under test.
#include <game_skyrimse/weather_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    WeatherRecord weatherRecord(FormId id, const char* editorId)
    {
        WeatherRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = editorId;
        w.mColourTypes = 17;
        return w;
    }

    void setColour(
        WeatherRecord& w, std::uint32_t type, std::uint32_t time, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        w.mColours[type][time] = Rgba8{ r, g, b, 0 };
    }

    TEST(SkyrimWeatherSource, colourTablesComeFromTheirNam0TypesAndTimesOfDay)
    {
        WeatherRecord w = weatherRecord(0x10, "Test");
        setColour(w, SkyColourType::SkyUpper, WeatherConstants::Sunrise, 255, 0, 0);
        setColour(w, SkyColourType::SkyUpper, WeatherConstants::Day, 0, 255, 0);
        setColour(w, SkyColourType::SkyUpper, WeatherConstants::Sunset, 0, 0, 255);
        setColour(w, SkyColourType::SkyUpper, WeatherConstants::Night, 51, 102, 153);
        setColour(w, SkyColourType::FogFar, WeatherConstants::Day, 10, 20, 30);
        setColour(w, SkyColourType::Ambient, WeatherConstants::Night, 255, 255, 255);
        setColour(w, SkyColourType::Sunlight, WeatherConstants::Sunrise, 0, 255, 255);
        setColour(w, SkyColourType::Sun, WeatherConstants::Sunset, 255, 255, 0);
        // A type the other tables must not take.
        setColour(w, SkyColourType::FogNear, WeatherConstants::Day, 200, 200, 200);

        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.id, 0x10u);
        EXPECT_EQ(d.editorId, "Test");
        EXPECT_EQ(d.sky.sunrise, (sdk::WeatherColour{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(d.sky.day, (sdk::WeatherColour{ 0.f, 1.f, 0.f }));
        EXPECT_EQ(d.sky.sunset, (sdk::WeatherColour{ 0.f, 0.f, 1.f }));
        EXPECT_FLOAT_EQ(d.sky.night.g, 102.f / 255.f);
        EXPECT_FLOAT_EQ(d.fog.day.b, 30.f / 255.f);
        EXPECT_EQ(d.ambient.night, (sdk::WeatherColour{ 1.f, 1.f, 1.f }));
        EXPECT_EQ(d.sun.sunrise, (sdk::WeatherColour{ 0.f, 1.f, 1.f }));
        EXPECT_EQ(d.sunDiscSunset, (sdk::WeatherColour{ 1.f, 1.f, 0.f }));
        EXPECT_EQ(d.sun.day, (sdk::WeatherColour{}));
    }

    TEST(SkyrimWeatherSource, aRecordWithFewerColourTypesGivesBlackForTheMissingOnes)
    {
        WeatherRecord w = weatherRecord(0x11, "Short");
        w.mColourTypes = 4; // SkyUpper, FogNear, Unknown2, Ambient exist; Sunlight (4) and FogFar (12) do not
        setColour(w, SkyColourType::Ambient, WeatherConstants::Day, 255, 0, 0);
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.ambient.day, (sdk::WeatherColour{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(d.sun.day, (sdk::WeatherColour{}));
        EXPECT_EQ(d.fog.day, (sdk::WeatherColour{}));
    }

    TEST(SkyrimWeatherSource, fogDepthIsTheSkyrimFogAmountAtTheReferenceDistance)
    {
        WeatherRecord w = weatherRecord(0x12, "Fog");
        WeatherFog f;
        f.mDayNear = 0.f;
        f.mDayFar = 80000.f;
        f.mDayPower = 0.4f;
        f.mDayMax = 0.85f;
        // night: far inside the reference distance, so the maximum applies
        f.mNightNear = 0.f;
        f.mNightFar = 3584.f;
        f.mNightPower = 0.4f;
        f.mNightMax = 0.6f;
        w.mFog = f;
        const sdk::WeatherDescription d = describeWeather(w);
        // (7168 / 80000) ^ 0.4 = exp(0.4 * ln 0.0896) = 0.3814
        EXPECT_NEAR(d.fogDepth.day, 0.3814f, 0.001f);
        EXPECT_FLOAT_EQ(d.fogDepth.sunrise, d.fogDepth.day);
        EXPECT_FLOAT_EQ(d.fogDepth.sunset, d.fogDepth.day);
        EXPECT_FLOAT_EQ(d.fogDepth.night, 0.6f);
    }

    TEST(SkyrimWeatherSource, noFogDistanceOrABrokenOneMeansNoFog)
    {
        WeatherRecord w = weatherRecord(0x13, "NoFog");
        WeatherFog f; // all zero
        w.mFog = f;
        EXPECT_FLOAT_EQ(describeWeather(w).fogDepth.day, 0.f);
        f.mDayFar = std::numeric_limits<float>::quiet_NaN();
        w.mFog = f;
        EXPECT_FLOAT_EQ(describeWeather(w).fogDepth.day, 0.f);
        f.mDayNear = 5000.f;
        f.mDayFar = 4000.f; // far before near
        w.mFog = f;
        EXPECT_FLOAT_EQ(describeWeather(w).fogDepth.day, 0.f);
        WeatherRecord none = weatherRecord(0x14, "NoFnam"); // no FNAM at all
        EXPECT_FLOAT_EQ(describeWeather(none).fogDepth.night, 0.f);
    }

    TEST(SkyrimWeatherSource, windGlareAndTransitionAreScaledByTheirConstants)
    {
        WeatherRecord w = weatherRecord(0x15, "Data");
        WeatherData data;
        data.mWindSpeed = 102;
        data.mSunGlare = 153;
        data.mTransitionDelta = 125;
        w.mData = data;
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_FLOAT_EQ(d.windSpeed, 0.4f); // 102 / 255
        EXPECT_FLOAT_EQ(d.glareView, 0.6f); // 153 / 255
        EXPECT_NEAR(d.transitionDelta, 0.015f, 1e-6f); // 125 * 0.00012
        EXPECT_FLOAT_EQ(d.cloudsMaximumPercent, 1.f);
        EXPECT_FALSE(d.storm);
    }

    TEST(SkyrimWeatherSource, theCloudPictureIsTheFirstLayerWithATextureAndItsSpeedFollowsItsBytes)
    {
        WeatherRecord w = weatherRecord(0x16, "Clouds");
        w.mCloudLayers.resize(3);
        w.mCloudLayers[1].mHasTexture = true;
        w.mCloudLayers[1].mTexture = "Sky\\SkyrimClouds02.dds";
        w.mCloudLayers[1].mSpeedQ = 127; // still
        w.mCloudLayers[1].mSpeedR = 127;
        w.mCloudLayers[2].mHasTexture = true;
        w.mCloudLayers[2].mTexture = "Sky\\Other.dds";
        sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.cloudTexture, "Sky\\SkyrimClouds02.dds");
        EXPECT_FLOAT_EQ(d.cloudSpeed, 0.25f); // the floor

        w.mCloudLayers[1].mSpeedQ = 0; // 127 away: 127 / 127 * 2
        EXPECT_FLOAT_EQ(describeWeather(w).cloudSpeed, 2.f);
        w.mCloudLayers[1].mSpeedQ = 127;
        w.mCloudLayers[1].mSpeedR = 190; // 63 away: 63 / 127 * 2 = 0.992
        EXPECT_NEAR(describeWeather(w).cloudSpeed, 0.992f, 0.001f);
    }

    TEST(SkyrimWeatherSource, aLayerWhoseTextureDoesNotExistIsSkipped)
    {
        WeatherRecord w = weatherRecord(0x18, "Leftover");
        w.mCloudLayers.resize(3);
        w.mCloudLayers[0].mHasTexture = true;
        w.mCloudLayers[0].mTexture = "Sky\\OblivionCloudCloudyUpper01.dds"; // not shipped
        w.mCloudLayers[1].mHasTexture = true;
        w.mCloudLayers[1].mTexture = "Sky\\SkyrimClouds04.dds";
        w.mCloudLayers[1].mSpeedQ = 0;
        w.mCloudLayers[1].mSpeedR = 127;
        const auto exists = [](const std::string& path) { return path != "Sky\\OblivionCloudCloudyUpper01.dds"; };
        const sdk::WeatherDescription d = describeWeather(w, exists);
        EXPECT_EQ(d.cloudTexture, "Sky\\SkyrimClouds04.dds");
        EXPECT_FLOAT_EQ(d.cloudSpeed, 2.f); // the speed is the chosen layer's own
        // No probe: the first textured layer.
        EXPECT_EQ(describeWeather(w).cloudTexture, "Sky\\OblivionCloudCloudyUpper01.dds");
        // Nothing exists: no picture.
        EXPECT_TRUE(describeWeather(w, [](const std::string&) { return false; }).cloudTexture.empty());
    }

    TEST(SkyrimWeatherSource, aWeatherWithNoCloudTextureHasNoPicture)
    {
        WeatherRecord w = weatherRecord(0x17, "Clear");
        w.mCloudLayers.resize(2);
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_TRUE(d.cloudTexture.empty());
        EXPECT_FLOAT_EQ(d.cloudSpeed, 0.25f);
    }

    ClimateRecord climate(FormId id, int a, int b, int c, int dd)
    {
        ClimateRecord cl;
        cl.mCommon.mId = id;
        ClimateTiming t;
        t.mSunriseBegin = static_cast<std::uint8_t>(a);
        t.mSunriseEnd = static_cast<std::uint8_t>(b);
        t.mSunsetBegin = static_cast<std::uint8_t>(c);
        t.mSunsetEnd = static_cast<std::uint8_t>(dd);
        cl.mTiming = t;
        return cl;
    }

    TEST(SkyrimWeatherSource, theSunHoursAreTheTimingMostClimatesShare)
    {
        WeatherRecords r;
        r.mClimates = { climate(1, 33, 60, 96, 123), climate(2, 36, 42, 100, 110), climate(3, 33, 60, 96, 123),
            climate(4, 0, 0, 0, 0) };
        WeatherIndex index;
        index.add(r);
        sdk::TimeOfDayDescription out;
        ASSERT_TRUE(describeTimeOfDay(index, out));
        EXPECT_FLOAT_EQ(out.sunriseBegin, 5.5f);
        EXPECT_FLOAT_EQ(out.sunriseEnd, 10.f);
        EXPECT_FLOAT_EQ(out.sunsetBegin, 16.f);
        EXPECT_FLOAT_EQ(out.sunsetEnd, 20.5f);
    }

    TEST(SkyrimWeatherSource, aTieInTheTimingGoesToTheLowestClimateId)
    {
        WeatherRecords r;
        r.mClimates = { climate(9, 36, 42, 100, 110), climate(2, 33, 60, 96, 123) };
        WeatherIndex index;
        index.add(r);
        sdk::TimeOfDayDescription out;
        ASSERT_TRUE(describeTimeOfDay(index, out));
        EXPECT_FLOAT_EQ(out.sunriseBegin, 5.5f); // climate 2's
    }

    TEST(SkyrimWeatherSource, noClimateTimingGivesNothing)
    {
        WeatherRecords r;
        r.mClimates = { climate(4, 0, 0, 0, 0) };
        ClimateRecord none;
        none.mCommon.mId = 5;
        r.mClimates.push_back(none);
        WeatherIndex index;
        index.add(r);
        sdk::TimeOfDayDescription out;
        EXPECT_FALSE(describeTimeOfDay(index, out));
    }

    struct World
    {
        WeatherRecords records;
        WeatherIndex index;
        void build() { index.add(records); }
    };

    CellWeatherLinks exteriorCell(FormId id, FormId world, std::vector<FormId> regions)
    {
        CellWeatherLinks c;
        c.mCommon.mId = id;
        c.mWorld = world;
        c.mRegions = std::move(regions);
        return c;
    }

    TEST(SkyrimWeatherSource, theTableIsInAscendingFormIdOrderAndListsPointIntoIt)
    {
        World w;
        for (const FormId id : { FormId(0x30), FormId(0x10), FormId(0x20) })
            w.records.mWeathers.push_back(weatherRecord(id, "W"));
        RegionWeatherRecord region;
        region.mCommon.mId = 0x100;
        region.mHasWeather = true;
        region.mPriority = 60;
        region.mWeathers = { { 0x30, 70, 0 }, { 0x10, 30, 0 } };
        w.records.mRegions.push_back(region);
        w.records.mCells.push_back(exteriorCell(0x200, 0x300, { 0x100 }));
        w.build();

        const WeatherTable table(w.index);
        ASSERT_EQ(table.weathers().size(), 3u);
        EXPECT_EQ(table.weathers()[0].id, 0x10u);
        EXPECT_EQ(table.weathers()[1].id, 0x20u);
        EXPECT_EQ(table.weathers()[2].id, 0x30u);
        EXPECT_EQ(table.indexOf(0x20), 1);
        EXPECT_EQ(table.indexOf(0x99), -1);

        sdk::CellWeatherList list;
        ASSERT_TRUE(table.listForCell(0x200, list));
        EXPECT_EQ(list.key, 0x100u);
        ASSERT_EQ(list.entries.size(), 2u);
        EXPECT_EQ(list.entries[0], (sdk::WeatherChanceEntry{ 2, 70 })); // 0x30 is index 2
        EXPECT_EQ(list.entries[1], (sdk::WeatherChanceEntry{ 0, 30 }));
    }

    TEST(SkyrimWeatherSource, aCellWithoutRegionsUsesItsWorldsClimateAndKeysItByTheClimate)
    {
        World w;
        w.records.mWeathers.push_back(weatherRecord(0x10, "A"));
        ClimateRecord cl;
        cl.mCommon.mId = 0x400;
        cl.mWeathers = { { 0x10, 100, 0 } };
        w.records.mClimates.push_back(cl);
        WorldSpaceRecord world;
        world.mCommon.mId = 0x300;
        world.mClimate = 0x400;
        w.records.mWorlds.push_back(world);
        w.records.mCells.push_back(exteriorCell(0x200, 0x300, {}));
        w.build();

        const WeatherTable table(w.index);
        sdk::CellWeatherList list;
        ASSERT_TRUE(table.listForCell(0x200, list));
        EXPECT_EQ(list.key, 0x400u);
        ASSERT_EQ(list.entries.size(), 1u);
        EXPECT_EQ(list.entries[0], (sdk::WeatherChanceEntry{ 0, 100 }));
    }

    TEST(SkyrimWeatherSource, listedWeathersThatAreNotInTheTableAreDroppedAndNegativeChancesAreZero)
    {
        World w;
        w.records.mWeathers.push_back(weatherRecord(0x10, "A"));
        RegionWeatherRecord region;
        region.mCommon.mId = 0x100;
        region.mHasWeather = true;
        region.mWeathers = { { 0x77, 50, 0 }, { 0x10, -5, 0 } };
        w.records.mRegions.push_back(region);
        w.records.mCells.push_back(exteriorCell(0x200, 0x300, { 0x100 }));
        w.build();

        const WeatherTable table(w.index);
        sdk::CellWeatherList list;
        ASSERT_TRUE(table.listForCell(0x200, list));
        ASSERT_EQ(list.entries.size(), 1u);
        EXPECT_EQ(list.entries[0], (sdk::WeatherChanceEntry{ 0, 0 }));
    }

    TEST(SkyrimWeatherSource, nothingApplyingOrNoKnownWeatherGivesNoList)
    {
        World w;
        w.records.mWeathers.push_back(weatherRecord(0x10, "A"));
        RegionWeatherRecord region;
        region.mCommon.mId = 0x100;
        region.mHasWeather = true;
        region.mWeathers = { { 0x77, 50, 0 } }; // not a known weather
        w.records.mRegions.push_back(region);
        w.records.mCells.push_back(exteriorCell(0x200, 0x300, { 0x100 }));
        w.records.mCells.push_back(exteriorCell(0x201, 0x300, {})); // no region, world without a climate
        w.build();

        const WeatherTable table(w.index);
        sdk::CellWeatherList list;
        list.key = 5;
        EXPECT_FALSE(table.listForCell(0x200, list));
        EXPECT_EQ(list.key, sdk::kNoWorldId); // cleared
        EXPECT_FALSE(table.listForCell(0x201, list));
        EXPECT_FALSE(table.listForCell(0x999, list)); // unknown cell
    }

    TEST(SkyrimWeatherSource, theTableIsTheSameWhateverOrderThePluginsAreAdded)
    {
        WeatherRecords a, b;
        a.mWeathers = { weatherRecord(0x10, "A"), weatherRecord(0x30, "C") };
        b.mWeathers = { weatherRecord(0x20, "B") };
        WeatherIndex one, two;
        one.add(a);
        one.add(b);
        two.add(b);
        two.add(a);
        const WeatherTable t1(one), t2(two);
        ASSERT_EQ(t1.weathers().size(), 3u);
        EXPECT_EQ(t1.weathers(), t2.weathers());
    }

    // ---- Stage W-S2: precipitation and thunder (task 0127). Expected values are written out by hand from the DATA
    // bytes: flags 0x04 rainy, 0x08 snow (RECALLED-VERIFY, supported by the installed data), byte 6 / 255 the share of
    // a transition after which the precipitation has changed over, byte 10 = 255 no thunder, (255 - byte) / 10 the
    // engine's frequency.

    WeatherRecord precipitating(std::uint8_t flags, bool particles, std::uint8_t begin = 153)
    {
        WeatherRecord w = weatherRecord(0x30, "Falling");
        WeatherData d;
        d.mFlags = flags;
        d.mPrecipitationBegin = begin;
        d.mThunderFrequency = 255;
        w.mData = d;
        w.mPrecipitation = particles ? 0x00ABCDEF : kNullFormId;
        return w;
    }

    TEST(SkyrimWeatherSource, rainySnowyAndNeitherAreToldApartByTheFlagsWhenParticlesExist)
    {
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Rainy, true)).precipitation, sdk::Precipitation::Rain);
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Snow, true)).precipitation, sdk::Precipitation::Snow);
        // Both bits: snow wins (a snowy storm is also flagged as stormy).
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Rainy | WeatherFlags::Snow, true)).precipitation,
            sdk::Precipitation::Snow);
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Pleasant, true)).precipitation, sdk::Precipitation::None);
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Cloudy, true)).precipitation, sdk::Precipitation::None);
        // Other bits do not matter: aurora with rain is rain.
        EXPECT_EQ(
            describeWeather(precipitating(WeatherFlags::Rainy | WeatherFlags::AuroraPermanent, true)).precipitation,
            sdk::Precipitation::Rain);
    }

    TEST(SkyrimWeatherSource, aRainyFlagWithoutPrecipitationParticlesFallsNothing)
    {
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Rainy, false)).precipitation, sdk::Precipitation::None);
        EXPECT_EQ(describeWeather(precipitating(WeatherFlags::Snow, false)).precipitation, sdk::Precipitation::None);
        WeatherRecord noData = weatherRecord(0x31, "NoData");
        noData.mPrecipitation = 0x1234;
        EXPECT_EQ(describeWeather(noData).precipitation, sdk::Precipitation::None);
    }

    TEST(SkyrimWeatherSource, thePrecipitationThresholdIsTheBeginByteOver255)
    {
        EXPECT_FLOAT_EQ(describeWeather(precipitating(WeatherFlags::Rainy, true, 153)).precipitationThreshold, 0.6f);
        EXPECT_FLOAT_EQ(describeWeather(precipitating(WeatherFlags::Rainy, true, 0)).precipitationThreshold, 0.f);
        EXPECT_FLOAT_EQ(describeWeather(precipitating(WeatherFlags::Rainy, true, 255)).precipitationThreshold, 1.f);
        // With no DATA the default stays.
        EXPECT_FLOAT_EQ(describeWeather(weatherRecord(0x32, "None")).precipitationThreshold, 0.5f);
    }

    WeatherRecord thundering(std::uint8_t frequency, std::uint8_t begin, std::uint8_t end)
    {
        WeatherRecord w = weatherRecord(0x33, "Thunder");
        WeatherData d;
        d.mThunderFrequency = frequency;
        d.mThunderBegin = begin;
        d.mThunderEnd = end;
        w.mData = d;
        return w;
    }

    TEST(SkyrimWeatherSource, thunderFrequencyCountsDownFrom255AndNeedsAnEndFade)
    {
        // 246 (the stormy rain): nine steps below "none" give 9 * 0.05 = 0.45, about four and a half strikes a minute.
        const sdk::WeatherDescription storm = describeWeather(thundering(246, 0, 255));
        EXPECT_NEAR(storm.thunderFrequency, 0.45f, 1e-5f);
        EXPECT_FLOAT_EQ(storm.thunderThreshold, 0.f);
        // The magic storm: 240 steps would give 12, held at the maximum 1; its begin 128 gives 128 / 255.
        const sdk::WeatherDescription magic = describeWeather(thundering(15, 128, 255));
        EXPECT_FLOAT_EQ(magic.thunderFrequency, 1.f);
        EXPECT_NEAR(magic.thunderThreshold, 128.f / 255.f, 1e-6f);
        // The maximum is reached at 20 steps and not rounded up before it.
        EXPECT_NEAR(describeWeather(thundering(235, 0, 255)).thunderFrequency, 1.f, 1e-5f);
        EXPECT_NEAR(describeWeather(thundering(236, 0, 255)).thunderFrequency, 0.95f, 1e-5f);
        // 255 is none whatever the fades say.
        EXPECT_FLOAT_EQ(describeWeather(thundering(255, 0, 255)).thunderFrequency, 0.f);
        // Frequency 0 with no end fade (the default weather): none.
        EXPECT_FLOAT_EQ(describeWeather(thundering(0, 0, 0)).thunderFrequency, 0.f);
        // No DATA: none.
        EXPECT_FLOAT_EQ(describeWeather(weatherRecord(0x34, "NoData")).thunderFrequency, 0.f);
    }

    // ---- stage W-S3: extra cloud layers, the image space, the climate's sky pictures --------------------------------

    void addLayer(WeatherRecord& w, std::size_t index, const char* texture,
        std::optional<std::array<float, 4>> alpha = std::array<float, 4>{ 1.f, 1.f, 1.f, 1.f })
    {
        if (w.mCloudLayers.size() <= index)
            w.mCloudLayers.resize(index + 1);
        w.mCloudLayers[index].mHasTexture = true;
        w.mCloudLayers[index].mTexture = texture;
        w.mCloudLayers[index].mAlpha = alpha;
        w.mCloudLayers[index].mSpeedQ = 127;
        w.mCloudLayers[index].mSpeedR = 127;
    }

    TEST(SkyrimWeatherLayers, theOtherTexturedLayersFollowThePictureWithTheirAlphaTintAndSpeed)
    {
        WeatherRecord w = weatherRecord(0x40, "Layers");
        addLayer(w, 0, "Sky\\A.dds");
        addLayer(w, 1, "Sky\\B.dds", std::array<float, 4>{ 0.2f, 1.f, 0.5f, 0.8f });
        w.mCloudLayers[1].mColour = std::array<Rgba8, 4>{ Rgba8{ 255, 0, 0, 0 }, Rgba8{ 0, 255, 0, 0 },
            Rgba8{ 0, 0, 255, 0 }, Rgba8{ 51, 102, 153, 0 } };
        w.mCloudLayers[1].mSpeedQ = 0; // 127 away: 2.0
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.cloudTexture, "Sky\\A.dds");
        ASSERT_EQ(d.extraCloudLayers.size(), 1u);
        const sdk::CloudLayerDescription& l = d.extraCloudLayers[0];
        EXPECT_EQ(l.texture, "Sky\\B.dds");
        EXPECT_FLOAT_EQ(l.alpha.sunrise, 0.2f);
        EXPECT_FLOAT_EQ(l.alpha.day, 1.f);
        EXPECT_FLOAT_EQ(l.alpha.sunset, 0.5f);
        EXPECT_FLOAT_EQ(l.alpha.night, 0.8f);
        EXPECT_EQ(l.tint.sunrise, (sdk::WeatherColour{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(l.tint.sunset, (sdk::WeatherColour{ 0.f, 0.f, 1.f }));
        EXPECT_EQ(l.tint.night, (sdk::WeatherColour{ 51 / 255.f, 102 / 255.f, 153 / 255.f }));
        EXPECT_FLOAT_EQ(l.speed, 2.f);
    }

    TEST(SkyrimWeatherLayers, aLayerWithoutAlphaOrColourIsOpaqueAndWhite)
    {
        WeatherRecord w = weatherRecord(0x41, "Bare");
        addLayer(w, 0, "Sky\\A.dds");
        addLayer(w, 1, "Sky\\B.dds", std::nullopt);
        const sdk::WeatherDescription d = describeWeather(w);
        ASSERT_EQ(d.extraCloudLayers.size(), 1u);
        EXPECT_FLOAT_EQ(d.extraCloudLayers[0].alpha.night, 1.f);
        EXPECT_EQ(d.extraCloudLayers[0].tint.day, (sdk::WeatherColour{ 1.f, 1.f, 1.f }));
    }

    TEST(SkyrimWeatherLayers, blankDuplicateInvisibleMissingAndUncountedLayersAreLeftOut)
    {
        WeatherRecord w = weatherRecord(0x42, "Skips");
        addLayer(w, 0, "Sky\\A.dds");
        addLayer(w, 1, "SKY\\A.DDS"); // the picture again (case-insensitive)
        addLayer(w, 2, "Sky\\Alpha.dds"); // the blank placeholder
        addLayer(w, 3, "Sky\\Faint.dds", std::array<float, 4>{ 0.01f, 0.04f, 0.f, 0.02f }); // never visible
        addLayer(w, 4, "Sky\\Gone.dds"); // not shipped
        addLayer(w, 5, "Sky\\C.dds");
        addLayer(w, 6, "Sky\\C.dds"); // twice
        addLayer(w, 7, "Sky\\SkyrimCloudsHORIZON01.dds"); // a horizon strip: not for the overhead dome
        addLayer(w, 8, "Sky\\D.dds"); // past LNAM
        w.mLayerCount = 8;
        const auto exists = [](const std::string& path) { return path != "Sky\\Gone.dds"; };
        const sdk::WeatherDescription d = describeWeather(w, exists);
        ASSERT_EQ(d.extraCloudLayers.size(), 1u);
        EXPECT_EQ(d.extraCloudLayers[0].texture, "Sky\\C.dds");
        // A texture whose name merely ends in "alpha.dds" is not the placeholder.
        WeatherRecord other = weatherRecord(0x43, "Other");
        addLayer(other, 0, "Sky\\A.dds");
        addLayer(other, 1, "Sky\\MyAlpha.dds");
        EXPECT_EQ(describeWeather(other).extraCloudLayers.size(), 1u);
    }

    TEST(SkyrimWeatherLayers, atMostTheSdkMaximumAreKeptInOrder)
    {
        WeatherRecord w = weatherRecord(0x44, "Many");
        for (std::size_t i = 0; i < 12; ++i)
        {
            const std::string name = "Sky\\L" + std::to_string(i) + ".dds";
            addLayer(w, i, name.c_str());
        }
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_EQ(d.cloudTexture, "Sky\\L0.dds");
        ASSERT_EQ(d.extraCloudLayers.size(), sdk::kMaxExtraCloudLayers);
        EXPECT_EQ(d.extraCloudLayers.front().texture, "Sky\\L1.dds");
        EXPECT_EQ(d.extraCloudLayers.back().texture, "Sky\\L8.dds");
    }

    TEST(SkyrimWeatherLayers, nonFiniteAlphaCountsAsZero)
    {
        WeatherRecord w = weatherRecord(0x45, "Nan");
        addLayer(w, 0, "Sky\\A.dds");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        addLayer(w, 1, "Sky\\B.dds", std::array<float, 4>{ nan, 0.5f, nan, 7.f });
        const sdk::WeatherDescription d = describeWeather(w);
        ASSERT_EQ(d.extraCloudLayers.size(), 1u);
        EXPECT_FLOAT_EQ(d.extraCloudLayers[0].alpha.sunrise, 0.f);
        EXPECT_FLOAT_EQ(d.extraCloudLayers[0].alpha.day, 0.5f);
        EXPECT_FLOAT_EQ(d.extraCloudLayers[0].alpha.night, 1.f); // clamped
    }

    ImageSpaceRecord imageSpace(FormId id, float saturation, float brightness, float contrast, float amount = 0.f)
    {
        ImageSpaceRecord r;
        r.mCommon.mId = id;
        r.mCinematic = ImageSpaceRecord::Cinematic{ saturation, brightness, contrast };
        if (amount > 0.f)
            r.mTint = ImageSpaceRecord::Tint{ amount, 0.25f, 0.5f, 0.75f };
        return r;
    }

    TEST(SkyrimWeatherImageSpace, fourImageSpacesGiveTheGradeByTimeOfDay)
    {
        WeatherRecord w = weatherRecord(0x50, "Graded");
        w.mHasImageSpaces = true;
        w.mImageSpaces = { 0x1, 0x2, 0x3, 0x4 };
        std::map<FormId, ImageSpaceRecord> table;
        table[0x1] = imageSpace(0x1, 1.1f, 1.2f, 1.3f);
        table[0x2] = imageSpace(0x2, 1.5f, 1.05f, 1.75f, 0.5f);
        table[0x3] = imageSpace(0x3, 0.9f, 0.95f, 1.f);
        // 0x4 is not in the table
        const ImageSpaceLookup lookup = [&](FormId id) {
            return table.count(id) != 0 ? &table.at(id) : static_cast<const ImageSpaceRecord*>(nullptr);
        };
        const sdk::WeatherDescription d = describeWeather(w, {}, lookup);
        ASSERT_TRUE(d.hasImageSpace);
        EXPECT_FLOAT_EQ(d.imageSpace.sunrise.saturation, 1.1f);
        EXPECT_FLOAT_EQ(d.imageSpace.sunrise.brightness, 1.2f);
        EXPECT_FLOAT_EQ(d.imageSpace.sunrise.contrast, 1.3f);
        EXPECT_FLOAT_EQ(d.imageSpace.day.saturation, 1.5f);
        EXPECT_FLOAT_EQ(d.imageSpace.day.brightness, 1.05f);
        EXPECT_FLOAT_EQ(d.imageSpace.day.contrast, 1.75f);
        EXPECT_FLOAT_EQ(d.imageSpace.day.tintAmount, 0.5f);
        EXPECT_EQ(d.imageSpace.day.tint, (sdk::WeatherColour{ 0.25f, 0.5f, 0.75f }));
        EXPECT_FLOAT_EQ(d.imageSpace.sunset.brightness, 0.95f);
        // The missing one is neutral.
        EXPECT_EQ(d.imageSpace.night, sdk::ImageSpaceGrade{});
    }

    TEST(SkyrimWeatherImageSpace, noLookupNoIdsOrNoCinematicMeansNoImageSpace)
    {
        WeatherRecord w = weatherRecord(0x51, "None");
        w.mHasImageSpaces = true;
        w.mImageSpaces = { 0x1, 0x2, 0x3, 0x4 };
        EXPECT_FALSE(describeWeather(w).hasImageSpace); // no lookup given
        ImageSpaceRecord noCinematic;
        const ImageSpaceLookup lookup = [&](FormId) { return &noCinematic; };
        EXPECT_FALSE(describeWeather(w, {}, lookup).hasImageSpace);
        w.mHasImageSpaces = false;
        const ImageSpaceRecord cinematic = imageSpace(1, 1.f, 1.f, 1.f);
        const ImageSpaceLookup full = [&](FormId) { return &cinematic; };
        EXPECT_FALSE(describeWeather(w, {}, full).hasImageSpace);
    }

    TEST(SkyrimWeatherImageSpace, theTableLooksImageSpacesUpInItsIndex)
    {
        WeatherRecords r;
        WeatherRecord w = weatherRecord(0x60, "InTable");
        w.mHasImageSpaces = true;
        w.mImageSpaces = { 0x7, 0x7, 0x7, 0x7 };
        r.mWeathers.push_back(w);
        r.mImageSpaces.push_back(imageSpace(0x7, 1.f, 1.25f, 1.f));
        WeatherIndex index;
        index.add(r);
        const WeatherTable table(index);
        ASSERT_EQ(table.weathers().size(), 1u);
        ASSERT_TRUE(table.weathers()[0].hasImageSpace);
        EXPECT_FLOAT_EQ(table.weathers()[0].imageSpace.day.brightness, 1.25f);
    }

    TEST(SkyrimClimateSky, theWinningClimateGivesItsSunPicturesAndMoons)
    {
        WeatherRecords r;
        ClimateRecord c = climate(2, 33, 60, 96, 123);
        c.mSunTexture = "Sky\\Sun.dds";
        c.mSunGlareTexture = "Sky\\SunGlare.dds";
        c.mTiming->mMoonByte = 0x40 | 0x03; // Masser only
        r.mClimates = { c };
        WeatherIndex index;
        index.add(r);
        sdk::TimeOfDayDescription out;
        ASSERT_TRUE(describeTimeOfDay(index, out));
        EXPECT_EQ(out.sunTexture, "Sky\\Sun.dds");
        EXPECT_EQ(out.sunGlareTexture, "Sky\\SunGlare.dds");
        EXPECT_TRUE(out.masser);
        EXPECT_FALSE(out.secunda);
        // A picture that does not exist is not named.
        sdk::TimeOfDayDescription probed;
        ASSERT_TRUE(describeTimeOfDay(index, probed, [](const std::string& p) { return p == "Sky\\Sun.dds"; }));
        EXPECT_EQ(probed.sunTexture, "Sky\\Sun.dds");
        EXPECT_TRUE(probed.sunGlareTexture.empty());
    }

    TEST(SkyrimClimateSky, aClimateWithNoMoonBitsHasNoMoonsAndNoPicturesLeavesThemEmpty)
    {
        WeatherRecords r;
        ClimateRecord c = climate(2, 33, 60, 96, 123);
        c.mTiming->mMoonByte = 3;
        r.mClimates = { c };
        WeatherIndex index;
        index.add(r);
        sdk::TimeOfDayDescription out;
        ASSERT_TRUE(describeTimeOfDay(index, out));
        EXPECT_FALSE(out.masser);
        EXPECT_FALSE(out.secunda);
        EXPECT_TRUE(out.sunTexture.empty());
        EXPECT_TRUE(out.sunGlareTexture.empty());
    }
}
