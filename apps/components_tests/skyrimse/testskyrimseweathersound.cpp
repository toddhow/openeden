// Task 0127, stage W-S4: the sounds of a Skyrim SE weather. SNDR and SOUN records built by hand with the layouts measured on
// the installed masters (tools/skyrimse_weather_check, aggregates in the task log: SNDR ANAM file names, BNAM 6 bytes with
// the static attenuation at bytes 4 to 5, LNAM byte 1 bit 3 marks a loop; WTHR SNAM = SNDR id + type 0 default, 1
// precipitation, 2 wind, 3 thunder). Expected numbers are written out by hand.
#include <game_skyrimse/weather_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(header().str(), std::ios::binary), "test.esp", error);
        }();
        ExteriorLimits mExteriorLimits;
        WeatherLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mExteriorLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id = 0x01000800)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    // BNAM as in the data: frequency shift, variance, priority, dB variance, then the attenuation u16 (1/100 dB).
    Bytes bnam(std::uint16_t attenuation)
    {
        return Bytes().u8(0).u8(0).u8(0x80).u8(0).u16(attenuation);
    }

    Bytes lnam(std::uint8_t byte1)
    {
        return Bytes().u8(4).u8(byte1).u8(0).u8(0);
    }

    // ---- SNDR / SOUN decode ------------------------------------------------------------------------------------------

    TEST(SkyrimSoundDecode, filesAttenuationAndLoopAreRead)
    {
        Fixture f;
        SoundDescriptorRecord r;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("AMBWeatherRainLightLP"));
        subs.append(sub4("CNAM", Bytes().u32(0x1EEF540A)));
        subs.append(sub4("ANAM", Bytes().zstring("Data\\Sound\\FX\\AMB\\Weather\\Rain\\a.wav")));
        subs.append(sub4("ANAM", Bytes().zstring("fx\\amb\\b.wav")));
        subs.append(sub4("BNAM", bnam(1410)));
        subs.append(sub4("LNAM", lnam(8)));
        ASSERT_TRUE(decodeSoundDescriptor(view(subs), raw("SNDR"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "AMBWeatherRainLightLP");
        EXPECT_EQ(r.mCommon.mId, 0x01000800u);
        ASSERT_EQ(r.mFiles.size(), 2u);
        EXPECT_EQ(r.mFiles[0], "Data\\Sound\\FX\\AMB\\Weather\\Rain\\a.wav");
        EXPECT_EQ(r.mFiles[1], "fx\\amb\\b.wav");
        ASSERT_TRUE(r.mAttenuation.has_value());
        EXPECT_EQ(*r.mAttenuation, 1410u);
        EXPECT_TRUE(r.mLoop);
        EXPECT_FALSE(r.mIsMarker);
    }

    TEST(SkyrimSoundDecode, onlyTheLoopBitOfLnamByte1MarksALoop)
    {
        Fixture f;
        std::string why;
        for (const auto& [value, expected] : { std::pair<int, bool>{ 0, false }, { 8, true }, { 16, false },
                 { 32, false }, { 24, true }, { 9, true } })
        {
            SoundDescriptorRecord r;
            Bytes subs = sub4("LNAM", lnam(static_cast<std::uint8_t>(value)));
            ASSERT_TRUE(decodeSoundDescriptor(view(subs), raw("SNDR"), f.context(), f.mLimits, r, why)) << why;
            EXPECT_EQ(r.mLoop, expected) << value;
        }
    }

    TEST(SkyrimSoundDecode, wrongSizesAreOddAndLeaveTheFieldOut)
    {
        Fixture f;
        SoundDescriptorRecord r;
        std::string why;
        Bytes subs = sub4("BNAM", Bytes().u8(1).u8(2).u8(3).u8(4));
        subs.append(sub4("LNAM", Bytes().u8(1).u8(8)));
        ASSERT_TRUE(decodeSoundDescriptor(view(subs), raw("SNDR"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_FALSE(r.mAttenuation.has_value());
        EXPECT_FALSE(r.mLoop);
        EXPECT_TRUE(r.mFiles.empty());
        EXPECT_EQ(f.mNotes.mOddBy["SNDR.BNAM"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["SNDR.LNAM"], 1u);
    }

    TEST(SkyrimSoundDecode, aSoundMarkerHasOneFileFromFnamAndItsOwnLoopRule)
    {
        Fixture f;
        std::string why;
        SoundDescriptorRecord r;
        Bytes subs = sub4("EDID", Bytes().zstring("AMBWindLightLP"));
        subs.append(sub4("FNAM", Bytes().zstring("fx\\amb\\amb_windlight_lp.wav")));
        ASSERT_TRUE(decodeSoundDescriptor(view(subs), raw("SOUN"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_TRUE(r.mIsMarker);
        ASSERT_EQ(r.mFiles.size(), 1u);
        EXPECT_EQ(r.mFiles[0], "fx\\amb\\amb_windlight_lp.wav");
        EXPECT_FALSE(r.mAttenuation.has_value());
        EXPECT_TRUE(r.mLoop); // a SOUN has no LNAM: the editor id ending in LP says so (a GUESS)

        SoundDescriptorRecord once;
        Bytes other = sub4("EDID", Bytes().zstring("UIMenuOK"));
        other.append(sub4("FNAM", Bytes().zstring("ui\\ok.wav")));
        ASSERT_TRUE(decodeSoundDescriptor(view(other), raw("SOUN"), f.context(), f.mLimits, once, why)) << why;
        EXPECT_FALSE(once.mLoop);
    }

    TEST(SkyrimSoundDecode, theNumberOfFilesIsCapped)
    {
        Fixture f;
        f.mLimits.mMaxEntriesPerList = 3;
        SoundDescriptorRecord r;
        std::string why;
        Bytes subs;
        for (int i = 0; i < 5; ++i)
            subs.append(sub4("ANAM", Bytes().zstring("fx\\a" + std::to_string(i) + ".wav")));
        EXPECT_FALSE(decodeSoundDescriptor(view(subs), raw("SNDR"), f.context(), f.mLimits, r, why));
        EXPECT_FALSE(why.empty());
    }

    // ---- paths and gain ------------------------------------------------------------------------------------------------

    TEST(SkyrimSoundPath, everyShapeSeenInTheDataBecomesAVfsPath)
    {
        EXPECT_EQ(normalizeSoundPath("Data\\Sound\\FX\\AMB\\Weather\\Rain\\A.wav"), "sound/fx/amb/weather/rain/a.wav");
        EXPECT_EQ(normalizeSoundPath("fx\\amb\\weather\\rain\\amb_weather_rain_light_lp.wav"),
            "sound/fx/amb/weather/rain/amb_weather_rain_light_lp.wav");
        EXPECT_EQ(normalizeSoundPath("\\Data\\Sound\\FX\\XXX_Placeholder_Beep.wav"), "sound/fx/xxx_placeholder_beep.wav");
        EXPECT_EQ(normalizeSoundPath("sound\\fx\\x.wav"), "sound/fx/x.wav");
        EXPECT_EQ(normalizeSoundPath("data\\Sound\\FX\\DLC02\\AMB\\x.wav"), "sound/fx/dlc02/amb/x.wav");
        EXPECT_EQ(normalizeSoundPath("fx/amb/x.wav"), "sound/fx/amb/x.wav");
    }

    TEST(SkyrimSoundPath, aDriveLetterADotDotOrNothingIsRefused)
    {
        EXPECT_EQ(normalizeSoundPath("C:\\Projects\\x\\Data\\Sound\\FX\\a.wav"), "");
        EXPECT_EQ(normalizeSoundPath("fx\\..\\..\\x.wav"), "");
        EXPECT_EQ(normalizeSoundPath(""), "");
        EXPECT_EQ(normalizeSoundPath("   "), "");
        EXPECT_EQ(normalizeSoundPath("fx\\a\\"), ""); // no file name
        EXPECT_EQ(normalizeSoundPath(std::string("fx\\a\0b.wav", 10)), "");
    }

    TEST(SkyrimSoundGain, theAttenuationInHundredthsOfADecibelIsAGain)
    {
        EXPECT_FLOAT_EQ(soundGain(std::nullopt), 1.f);
        EXPECT_FLOAT_EQ(soundGain(0), 1.f);
        EXPECT_NEAR(soundGain(1410), 0.1972f, 0.0005f); // 10 ^ (-14.10 / 20)
        EXPECT_NEAR(soundGain(352), 0.6668f, 0.0005f); // 10 ^ (-3.52 / 20)
        EXPECT_NEAR(soundGain(600), 0.5012f, 0.0005f); // 10 ^ (-6 / 20)
        EXPECT_FLOAT_EQ(soundGain(65535), 0.1f); // never quieter than the floor
    }

    // ---- the weather's four sounds ---------------------------------------------------------------------------------------

    SoundDescriptorRecord descriptor(FormId id, const char* editorId, std::vector<std::string> files, std::uint16_t att,
        bool loop)
    {
        SoundDescriptorRecord r;
        r.mCommon.mId = id;
        r.mCommon.mEditorId = editorId;
        r.mFiles = std::move(files);
        r.mAttenuation = att;
        r.mLoop = loop;
        return r;
    }

    WeatherRecord weatherWith(std::vector<WeatherSound> sounds)
    {
        WeatherRecord w;
        w.mCommon.mId = 0x10;
        w.mCommon.mEditorId = "SkyrimStormRain";
        w.mColourTypes = 17;
        w.mSounds = std::move(sounds);
        return w;
    }

    struct Library
    {
        std::map<FormId, SoundDescriptorRecord> mRecords;
        SoundLookup lookup()
        {
            return [this](FormId id) -> const SoundDescriptorRecord*
            {
                const auto it = mRecords.find(id);
                return it == mRecords.end() ? nullptr : &it->second;
            };
        }
    };

    TEST(SkyrimWeatherSounds, theFourSlotsComeFromTheirSnamTypes)
    {
        Library lib;
        lib.mRecords[0xA1] = descriptor(0xA1, "SnowLoop", { "Data\\Sound\\FX\\snow.wav" }, 1498, true);
        lib.mRecords[0xA2] = descriptor(0xA2, "RainLoop", { "fx\\rain.wav" }, 1410, true);
        lib.mRecords[0xA3] = descriptor(0xA3, "Gust", { "Data\\Sound\\FX\\gust1.wav", "Data\\Sound\\FX\\gust2.wav" }, 0, false);
        lib.mRecords[0xA4] = descriptor(0xA4, "Thunder", { "Data\\Sound\\FX\\t1.wav", "Data\\Sound\\FX\\t2.wav" }, 352, false);
        const WeatherRecord w = weatherWith({ { 0xA1, 0 }, { 0xA2, 1 }, { 0xA3, 2 }, { 0xA4, 3 } });
        const sdk::WeatherDescription d = describeWeather(w, {}, {}, lib.lookup());
        EXPECT_EQ(d.ambientSound.name, "SnowLoop");
        ASSERT_EQ(d.ambientSound.files.size(), 1u);
        EXPECT_EQ(d.ambientSound.files[0], "sound/fx/snow.wav");
        EXPECT_TRUE(d.ambientSound.loop);
        EXPECT_NEAR(d.ambientSound.volume, 0.1782f, 0.0005f); // 10 ^ (-14.98 / 20)
        EXPECT_EQ(d.precipitationSound.name, "RainLoop");
        EXPECT_EQ(d.precipitationSound.files[0], "sound/fx/rain.wav");
        EXPECT_TRUE(d.precipitationSound.loop);
        EXPECT_EQ(d.windSound.name, "Gust");
        ASSERT_EQ(d.windSound.files.size(), 2u);
        EXPECT_FALSE(d.windSound.loop);
        EXPECT_FLOAT_EQ(d.windSound.volume, 1.f);
        EXPECT_EQ(d.thunderSound.name, "Thunder");
        ASSERT_EQ(d.thunderSound.files.size(), 2u);
        EXPECT_EQ(d.thunderSound.files[1], "sound/fx/t2.wav");
        EXPECT_NEAR(d.thunderSound.volume, 0.6668f, 0.0005f);
    }

    TEST(SkyrimWeatherSounds, withNoLookupThereAreNoSounds)
    {
        const WeatherRecord w = weatherWith({ { 0xA1, 0 }, { 0xA4, 3 } });
        const sdk::WeatherDescription d = describeWeather(w);
        EXPECT_TRUE(d.ambientSound.files.empty());
        EXPECT_TRUE(d.thunderSound.files.empty());
    }

    TEST(SkyrimWeatherSounds, anUnresolvedOrUnknownIdAndAnUnknownTypeAreSkipped)
    {
        Library lib;
        lib.mRecords[0xA1] = descriptor(0xA1, "Known", { "fx\\k.wav" }, 0, true);
        const WeatherRecord w = weatherWith({ { kUnresolvedFormId, 1 }, { 0xFFF, 2 }, { 0xA1, 9 }, { 0xA1, 1 } });
        const sdk::WeatherDescription d = describeWeather(w, {}, {}, lib.lookup());
        EXPECT_TRUE(d.windSound.files.empty());
        EXPECT_EQ(d.precipitationSound.files.size(), 1u);
        EXPECT_TRUE(d.ambientSound.files.empty());
        EXPECT_TRUE(d.thunderSound.files.empty());
    }

    TEST(SkyrimWeatherSounds, severalEntriesOfOneTypeShareTheFilesAndTheFirstOnesVolume)
    {
        Library lib;
        lib.mRecords[0xB1] = descriptor(0xB1, "Distant", { "fx\\d1.wav", "fx\\d2.wav" }, 352, false);
        lib.mRecords[0xB2] = descriptor(0xB2, "Extra", { "fx\\e1.wav", "fx\\d1.wav" }, 0, false);
        const WeatherRecord w = weatherWith({ { 0xB1, 3 }, { 0xB2, 3 } });
        const sdk::WeatherDescription d = describeWeather(w, {}, {}, lib.lookup());
        EXPECT_EQ(d.thunderSound.name, "Distant");
        ASSERT_EQ(d.thunderSound.files.size(), 3u); // d1 d2 e1: no duplicate
        EXPECT_EQ(d.thunderSound.files[2], "sound/fx/e1.wav");
        EXPECT_NEAR(d.thunderSound.volume, 0.6668f, 0.0005f);
    }

    TEST(SkyrimWeatherSounds, aFileTheProbeDoesNotFindIsLeftOutAndASlotWithNoneLeftIsEmpty)
    {
        Library lib;
        lib.mRecords[0xC1] = descriptor(0xC1, "Mixed", { "fx\\here.wav", "fx\\gone.wav" }, 0, true);
        lib.mRecords[0xC2] = descriptor(0xC2, "AllGone", { "fx\\gone2.wav" }, 0, true);
        const WeatherRecord w = weatherWith({ { 0xC1, 1 }, { 0xC2, 0 } });
        const auto exists = [](const std::string& path) { return path == "sound/fx/here.wav"; };
        const sdk::WeatherDescription d = describeWeather(w, {}, {}, lib.lookup(), exists);
        ASSERT_EQ(d.precipitationSound.files.size(), 1u);
        EXPECT_EQ(d.precipitationSound.files[0], "sound/fx/here.wav");
        EXPECT_TRUE(d.ambientSound.files.empty());
    }

    TEST(SkyrimWeatherSounds, aRefusedPathIsLeftOut)
    {
        Library lib;
        lib.mRecords[0xD1] = descriptor(0xD1, "Bad", { "C:\\x\\a.wav", "fx\\..\\a.wav", "fx\\ok.wav" }, 0, false);
        const WeatherRecord w = weatherWith({ { 0xD1, 3 } });
        const sdk::WeatherDescription d = describeWeather(w, {}, {}, lib.lookup());
        ASSERT_EQ(d.thunderSound.files.size(), 1u);
        EXPECT_EQ(d.thunderSound.files[0], "sound/fx/ok.wav");
    }

    TEST(SkyrimWeatherSounds, atMostEightFilesAreKept)
    {
        Library lib;
        std::vector<std::string> many;
        for (int i = 0; i < 12; ++i)
            many.push_back("fx\\t" + std::to_string(i) + ".wav");
        lib.mRecords[0xE1] = descriptor(0xE1, "Many", many, 0, false);
        const WeatherRecord w = weatherWith({ { 0xE1, 3 } });
        EXPECT_EQ(describeWeather(w, {}, {}, lib.lookup()).thunderSound.files.size(), sdk::kMaxWeatherSoundFiles);
    }

    // ---- the walk and the index ------------------------------------------------------------------------------------------

    TEST(SkyrimWeatherSoundWalk, sndrAndSounRecordsReachTheIndex)
    {
        Bytes sndr = record4("SNDR", 0, 0x01000A00,
            Bytes()
                .append(sub4("EDID", Bytes().zstring("Rain")))
                .append(sub4("ANAM", Bytes().zstring("fx\\rain.wav")))
                .append(sub4("BNAM", bnam(1410)))
                .append(sub4("LNAM", lnam(8))));
        Bytes soun = record4("SOUN", 0, 0x01000A01,
            Bytes().append(sub4("EDID", Bytes().zstring("WindLP"))).append(sub4("FNAM", Bytes().zstring("fx\\wind.wav"))));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("SNDR"), 0, sndr));
        bytes.append(group4(Content::fourCC("SOUN"), 0, soun));
        const LoadOrder order = oneMaster();
        WeatherRecords data;
        Content::Error error;
        ASSERT_TRUE(parseWeatherRecordsBytes(bytes.str(), "test.esp", order, WeatherLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mSounds.size(), 2u);
        WeatherIndex index;
        index.add(data);
        const SoundDescriptorRecord* rain = index.sound(0x01000A00);
        ASSERT_NE(rain, nullptr);
        EXPECT_EQ(rain->mCommon.mEditorId, "Rain");
        EXPECT_TRUE(rain->mLoop);
        const SoundDescriptorRecord* wind = index.sound(0x01000A01);
        ASSERT_NE(wind, nullptr);
        EXPECT_TRUE(wind->mIsMarker);
        EXPECT_EQ(index.sound(0x01000A02), nullptr);
    }

    TEST(SkyrimWeatherSoundWalk, aTableWithASoundProbeDescribesTheWeathersSounds)
    {
        WeatherRecords data;
        WeatherRecord w = weatherWith({ { 0xA1, 3 } });
        w.mCommon.mId = 0x20;
        data.mWeathers.push_back(w);
        data.mSounds.push_back(descriptor(0xA1, "Thunder", { "fx\\t1.wav" }, 352, false));
        WeatherIndex index;
        index.add(data);
        const WeatherTable table(index, {}, [](const std::string&) { return true; });
        ASSERT_EQ(table.weathers().size(), 1u);
        EXPECT_EQ(table.weathers()[0].thunderSound.files.size(), 1u);
        const WeatherTable none(index, {}, [](const std::string&) { return false; });
        EXPECT_TRUE(none.weathers()[0].thunderSound.files.empty());
    }
}
