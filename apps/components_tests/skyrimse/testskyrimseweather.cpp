// Task 0127 (TBD-E): Skyrim SE weather, climate and region weather lists on generated records and plugins. No game
// data. Every byte layout used here is the one measured on the installed masters (see weather.cpp); expected values are
// written out by hand, not derived with the code under test.
#include <game_skyrimse/weather.hpp>

#include <gtest/gtest.h>

#include <zlib.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupWorld = 1, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupExtBlock = 4,
                            kGrupExtSub = 5;

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

    std::unique_ptr<Content::Plugin> openHeader(const Bytes& h)
    {
        Content::Error error;
        return Content::Plugin::open(
            std::make_unique<std::istringstream>(h.str(), std::ios::binary), "test.esp", error);
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = openHeader(header());
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

    Bytes rgbaBytes(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 0)
    {
        return Bytes().u8(r).u8(g).u8(b).u8(a);
    }

    // DATA with each byte distinct so a shifted read shows.
    Bytes weatherData()
    {
        Bytes d;
        for (std::uint8_t i = 0; i < 19; ++i)
            d.u8(static_cast<std::uint8_t>(10 + i));
        return d;
    }

    bool decodeW(Fixture& f, const Bytes& subs, WeatherRecord& out, std::string& why)
    {
        return decodeWeather(view(subs), raw("WTHR"), f.context(), f.mLimits, out, why);
    }

    // ---- WTHR: DATA, FNAM, simple links ---------------------------------------------------------------------------

    TEST(SkyrimWeatherDecode, dataBytesAreReadAtTheirOffsets)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("TestWeather"));
        subs.append(sub4("DATA", weatherData()));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        EXPECT_EQ(w.mCommon.mEditorId, "TestWeather");
        EXPECT_EQ(w.mCommon.mId, 0x01000800u);
        ASSERT_TRUE(w.mData.has_value());
        EXPECT_EQ(w.mData->mWindSpeed, 10);
        EXPECT_EQ(w.mData->mTransitionDelta, 13);
        EXPECT_EQ(w.mData->mSunGlare, 14);
        EXPECT_EQ(w.mData->mSunDamage, 15);
        EXPECT_EQ(w.mData->mPrecipitationBegin, 16);
        EXPECT_EQ(w.mData->mPrecipitationEnd, 17);
        EXPECT_EQ(w.mData->mThunderBegin, 18);
        EXPECT_EQ(w.mData->mThunderEnd, 19);
        EXPECT_EQ(w.mData->mThunderFrequency, 20);
        EXPECT_EQ(w.mData->mFlags, 21);
        EXPECT_EQ(w.mData->mLightningR, 22);
        EXPECT_EQ(w.mData->mLightningG, 23);
        EXPECT_EQ(w.mData->mLightningB, 24);
        EXPECT_EQ(w.mData->mWindDirection, 27);
        EXPECT_EQ(w.mData->mWindDirectionRange, 28);
        // the bytes the installed data always leaves at 0 are kept raw
        EXPECT_EQ(w.mData->mRaw[1], 11);
        EXPECT_EQ(w.mData->mRaw[2], 12);
        EXPECT_EQ(w.mData->mRaw[15], 25);
        EXPECT_EQ(w.mData->mRaw[16], 26);
    }

    TEST(SkyrimWeatherDecode, aDataOfAnotherSizeIsOddAndDecodesToNothing)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs = sub4("DATA", Bytes().u8(1).u8(2).u8(3));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        EXPECT_FALSE(w.mData.has_value());
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.DATA"], 1u);
    }

    TEST(SkyrimWeatherDecode, fogIsEightFloats)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes fog;
        for (float v : { 1200.0f, 80000.0f, 1300.0f, 40000.0f, 0.4f, 0.5f, 0.85f, 0.9f })
            fog.f32(v);
        ASSERT_TRUE(decodeW(f, sub4("FNAM", fog), w, why)) << why;
        ASSERT_TRUE(w.mFog.has_value());
        EXPECT_FLOAT_EQ(w.mFog->mDayNear, 1200.0f);
        EXPECT_FLOAT_EQ(w.mFog->mDayFar, 80000.0f);
        EXPECT_FLOAT_EQ(w.mFog->mNightNear, 1300.0f);
        EXPECT_FLOAT_EQ(w.mFog->mNightFar, 40000.0f);
        EXPECT_FLOAT_EQ(w.mFog->mDayPower, 0.4f);
        EXPECT_FLOAT_EQ(w.mFog->mNightPower, 0.5f);
        EXPECT_FLOAT_EQ(w.mFog->mDayMax, 0.85f);
        EXPECT_FLOAT_EQ(w.mFog->mNightMax, 0.9f);
    }

    TEST(SkyrimWeatherDecode, aFogWithANanIsOddAndDecodesToNothing)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes fog;
        for (int i = 0; i < 8; ++i)
            fog.f32(i == 3 ? std::numeric_limits<float>::quiet_NaN() : 1.0f);
        ASSERT_TRUE(decodeW(f, sub4("FNAM", fog), w, why)) << why;
        EXPECT_FALSE(w.mFog.has_value());
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.FNAM"], 1u);
    }

    TEST(SkyrimWeatherDecode, linksAreResolvedAndTheRawBlocksAreNotTreatedAsIds)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        subs.append(sub4("MNAM", Bytes().u32(0x01000900)));
        subs.append(sub4("NNAM", Bytes().u32(0)));
        subs.append(sub4("IMSP", Bytes().u32(0x00000001).u32(0x00000002).u32(0x01000003).u32(0x00000004)));
        subs.append(sub4("HNAM", Bytes().u32(0).u32(0x00000011).u32(0).u32(0x00000012)));
        subs.append(sub4("NAM2", Bytes().u32(0x00FFFFFF).u32(0x00FFFFFF).u32(0x00FFFFFF).u32(0x00FFFFFF)));
        subs.append(sub4("NAM3", Bytes().u32(1).u32(2).u32(3).u32(4)));
        subs.append(sub4("ONAM", Bytes().u32(0xAABBCCDD)));
        subs.append(sub4("MODL", Bytes().zstring("Sky\\Aurora.nif")));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        EXPECT_EQ(w.mPrecipitation, 0x01000900u);
        EXPECT_EQ(w.mVisualEffect, kNullFormId);
        EXPECT_TRUE(w.mHasImageSpaces);
        EXPECT_EQ(w.mImageSpaces[0], 0x00000001u);
        EXPECT_EQ(w.mImageSpaces[2], 0x01000003u);
        EXPECT_TRUE(w.mHasVolumetric);
        EXPECT_EQ(w.mVolumetric[1], 0x00000011u);
        EXPECT_EQ(w.mVolumetric[3], 0x00000012u);
        ASSERT_TRUE(w.mNam2.has_value());
        EXPECT_EQ((*w.mNam2)[0], 0xFF);
        EXPECT_EQ((*w.mNam2)[3], 0x00);
        ASSERT_TRUE(w.mNam3.has_value());
        EXPECT_EQ((*w.mNam3)[4], 2);
        ASSERT_TRUE(w.mOnam.has_value());
        EXPECT_EQ((*w.mOnam)[0], 0xDD);
        EXPECT_EQ(w.mAuroraModel, "Sky\\Aurora.nif");
        // none of the raw blocks went through the form id resolver
        EXPECT_EQ(f.mNotes.mUnresolvedFormIds, 0u);
    }

    TEST(SkyrimWeatherDecode, aLinkToAPluginOutsideTheLoadOrderIsUnresolvedAndCounted)
    {
        // the plugin names a master the load order does not hold; an id into it cannot resolve
        const Bytes h = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 }, { "Gone.esm", 0 } }, 24, 44);
        const auto plugin = openHeader(h);
        const LoadOrder order = oneMaster();
        const FormIdResolver resolver(plugin->header(), "test.esp", order);
        const ExteriorLimits exteriorLimits;
        DecodeNotes notes;
        const DecodeContext context{ resolver, exteriorLimits, notes, false };
        WeatherRecord w;
        std::string why;
        const Bytes subs = sub4("MNAM", Bytes().u32(0x01000900));
        // the record's own id is in Skyrim.esm (index 0), so only the link is counted
        ASSERT_TRUE(decodeWeather(view(subs), raw("WTHR", 0x00000800), context, WeatherLimits(), w, why)) << why;
        EXPECT_EQ(w.mPrecipitation, kUnresolvedFormId);
        EXPECT_EQ(notes.mUnresolvedFormIds, 1u);
    }

    TEST(SkyrimWeatherDecode, layerCountAndNam1AreKeptAsStored)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs = sub4("LNAM", Bytes().u32(29));
        subs.append(sub4("NAM1", Bytes().u32(0xEFE77F00)));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        ASSERT_TRUE(w.mLayerCount.has_value());
        EXPECT_EQ(*w.mLayerCount, 29u);
        ASSERT_TRUE(w.mNam1.has_value());
        EXPECT_EQ(*w.mNam1, 0xEFE77F00u);
    }

    // ---- WTHR: NAM0 colour table -----------------------------------------------------------------------------------

    Bytes colourTable(std::uint32_t types)
    {
        Bytes b;
        for (std::uint32_t type = 0; type < types; ++type)
            for (std::uint32_t tod = 0; tod < 4; ++tod)
                b.append(rgbaBytes(static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(tod),
                    static_cast<std::uint8_t>(type * 4 + tod), 0xAA));
        return b;
    }

    TEST(SkyrimWeatherColours, theFullTableHasSeventeenTypesOfFourTimesOfDay)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        ASSERT_TRUE(decodeW(f, sub4("NAM0", colourTable(17)), w, why)) << why;
        EXPECT_EQ(w.mColourTypes, 17u);
        // type-major, then time of day: the colour at (type 6, time 3) is the 28th entry
        EXPECT_EQ(w.mColours[6][3], (Rgba8{ 6, 3, 27, 0xAA }));
        EXPECT_EQ(w.mColours[16][0], (Rgba8{ 16, 0, 64, 0xAA }));
        EXPECT_EQ(w.mColours[0][1], (Rgba8{ 0, 1, 1, 0xAA }));
    }

    TEST(SkyrimWeatherColours, theShorterTablesOfTheInstalledDataAreAccepted)
    {
        for (std::uint32_t types : { 14u, 13u })
        {
            Fixture f;
            WeatherRecord w;
            std::string why;
            ASSERT_TRUE(decodeW(f, sub4("NAM0", colourTable(types)), w, why)) << why;
            EXPECT_EQ(w.mColourTypes, types);
            EXPECT_EQ(w.mColours[types - 1][3],
                (Rgba8{
                    static_cast<std::uint8_t>(types - 1), 3, static_cast<std::uint8_t>((types - 1) * 4 + 3), 0xAA }));
            EXPECT_EQ(w.mColours[types][0], (Rgba8{})) << "a type the table does not hold stays zero";
            EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
        }
    }

    TEST(SkyrimWeatherColours, aSizeThatIsNotWholeTypesOrIsTooLongIsOdd)
    {
        for (std::size_t size : { std::size_t{ 100 }, std::size_t{ 17 * 16 + 16 }, std::size_t{ 0 } })
        {
            Fixture f;
            WeatherRecord w;
            std::string why;
            Bytes data;
            for (std::size_t i = 0; i < size; ++i)
                data.u8(1);
            ASSERT_TRUE(decodeW(f, sub4("NAM0", data), w, why)) << why;
            EXPECT_EQ(w.mColourTypes, 0u) << size;
            EXPECT_EQ(f.mNotes.mOddBy["WTHR.NAM0"], 1u) << size;
        }
    }

    // ---- WTHR: cloud layers ----------------------------------------------------------------------------------------

    TEST(SkyrimWeatherClouds, theLayerIsNamedByTheFirstCharacterOfItsTextureSubrecord)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        subs.append(sub4("00TX", Bytes().zstring("Sky\\a.dds")));
        subs.append(sub4("90TX", Bytes().zstring("Sky\\nine.dds")));
        subs.append(sub4(":0TX", Bytes().zstring("Sky\\ten.dds")));
        subs.append(sub4(";0TX", Bytes().zstring("Sky\\eleven.dds")));
        subs.append(sub4("L0TX", Bytes().zstring("Sky\\last.dds")));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        ASSERT_EQ(w.mCloudSlots, 29u);
        ASSERT_EQ(w.mCloudLayers.size(), 29u);
        EXPECT_TRUE(w.mCloudLayers[0].mHasTexture);
        EXPECT_EQ(w.mCloudLayers[0].mTexture, "Sky\\a.dds");
        EXPECT_EQ(w.mCloudLayers[9].mTexture, "Sky\\nine.dds");
        EXPECT_EQ(w.mCloudLayers[10].mTexture, "Sky\\ten.dds");
        EXPECT_EQ(w.mCloudLayers[11].mTexture, "Sky\\eleven.dds");
        EXPECT_EQ(w.mCloudLayers[28].mTexture, "Sky\\last.dds");
        EXPECT_FALSE(w.mCloudLayers[1].mHasTexture);
        EXPECT_TRUE(w.mCloudLayers[1].mTexture.empty());
    }

    TEST(SkyrimWeatherClouds, aLayerIndexPastThirtyOneIsOdd)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        // 'P' is layer 32
        ASSERT_TRUE(decodeW(f, sub4("P0TX", Bytes().zstring("Sky\\x.dds")), w, why)) << why;
        EXPECT_EQ(w.mCloudSlots, 0u);
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.P0TX"], 1u);
        // 'O' is layer 31, the last slot
        Fixture g;
        WeatherRecord v;
        ASSERT_TRUE(decodeW(g, sub4("O0TX", Bytes().zstring("Sky\\y.dds")), v, why)) << why;
        EXPECT_EQ(v.mCloudSlots, 32u);
        EXPECT_EQ(v.mCloudLayers[31].mTexture, "Sky\\y.dds");
    }

    TEST(SkyrimWeatherClouds, coloursAlphasAndSpeedsAreOnePerLayer)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes pnam, jnam, qnam, rnam;
        for (std::uint32_t layer = 0; layer < 32; ++layer)
        {
            for (std::uint32_t tod = 0; tod < 4; ++tod)
            {
                pnam.append(rgbaBytes(static_cast<std::uint8_t>(layer), static_cast<std::uint8_t>(tod), 7, 0));
                jnam.f32(static_cast<float>(layer) + static_cast<float>(tod) / 10.0f);
            }
            qnam.u8(static_cast<std::uint8_t>(100 + layer));
            rnam.u8(static_cast<std::uint8_t>(200 + layer));
        }
        Bytes subs = sub4("PNAM", pnam);
        subs.append(sub4("JNAM", jnam));
        subs.append(sub4("QNAM", qnam));
        subs.append(sub4("RNAM", rnam));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        ASSERT_EQ(w.mCloudSlots, 32u);
        ASSERT_TRUE(w.mCloudLayers[5].mColour.has_value());
        EXPECT_EQ((*w.mCloudLayers[5].mColour)[2], (Rgba8{ 5, 2, 7, 0 }));
        ASSERT_TRUE(w.mCloudLayers[31].mAlpha.has_value());
        EXPECT_FLOAT_EQ((*w.mCloudLayers[31].mAlpha)[3], 31.3f);
        ASSERT_TRUE(w.mCloudLayers[7].mSpeedQ.has_value());
        EXPECT_EQ(*w.mCloudLayers[7].mSpeedQ, 107);
        EXPECT_EQ(*w.mCloudLayers[7].mSpeedR, 207);
    }

    TEST(SkyrimWeatherClouds, theShortOldArraysOfOneRecordGiveFewerSlots)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes pnam;
        for (int i = 0; i < 64; ++i) // 64 bytes: 4 layers of 4 colours
            pnam.u8(static_cast<std::uint8_t>(i));
        ASSERT_TRUE(decodeW(f, sub4("PNAM", pnam), w, why)) << why;
        EXPECT_EQ(w.mCloudSlots, 4u);
        ASSERT_EQ(w.mCloudLayers.size(), 4u);
        ASSERT_TRUE(w.mCloudLayers[3].mColour.has_value());
        EXPECT_EQ((*w.mCloudLayers[3].mColour)[0], (Rgba8{ 48, 49, 50, 51 }));
        EXPECT_EQ((*w.mCloudLayers[3].mColour)[3], (Rgba8{ 60, 61, 62, 63 }));
        EXPECT_FALSE(w.mCloudLayers[3].mAlpha.has_value());
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimWeatherClouds, anArrayThatIsNotWholeLayersIsOdd)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes odd;
        for (int i = 0; i < 20; ++i)
            odd.u8(1);
        Bytes subs = sub4("PNAM", odd);
        Bytes toolong;
        for (int i = 0; i < 40; ++i)
            toolong.u8(1);
        subs.append(sub4("QNAM", toolong)); // over 32 layers
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        EXPECT_EQ(w.mCloudSlots, 0u);
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.PNAM"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.QNAM"], 1u);
    }

    // ---- WTHR: DALC, sounds, sky statics --------------------------------------------------------------------------

    Bytes dalc32(std::uint8_t seed)
    {
        Bytes b;
        for (std::uint8_t i = 0; i < 6; ++i)
            b.append(rgbaBytes(static_cast<std::uint8_t>(seed + i), seed, i, 0));
        b.append(rgbaBytes(1, 2, 3, 4)); // specular
        b.f32(1.5f);
        return b;
    }

    TEST(SkyrimWeatherDalc, fourOfThemAreTheTimesOfDayInFileOrder)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        for (std::uint8_t tod = 0; tod < 4; ++tod)
            subs.append(sub4("DALC", dalc32(static_cast<std::uint8_t>(tod * 10))));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        ASSERT_EQ(w.mDirectionalAmbient.size(), 4u);
        EXPECT_EQ(w.mDirectionalAmbient[2].mDirections[3], (Rgba8{ 23, 20, 3, 0 }));
        EXPECT_TRUE(w.mDirectionalAmbient[0].mHasSpecular);
        EXPECT_EQ(w.mDirectionalAmbient[0].mSpecular, (Rgba8{ 1, 2, 3, 4 }));
        EXPECT_FLOAT_EQ(w.mDirectionalAmbient[0].mFresnelPower, 1.5f);
    }

    TEST(SkyrimWeatherDalc, theShortFormHasOnlyTheSixColours)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes b;
        for (std::uint8_t i = 0; i < 6; ++i)
            b.append(rgbaBytes(i, 0, 0, 0));
        ASSERT_TRUE(decodeW(f, sub4("DALC", b), w, why)) << why;
        ASSERT_EQ(w.mDirectionalAmbient.size(), 1u);
        EXPECT_FALSE(w.mDirectionalAmbient[0].mHasSpecular);
        EXPECT_EQ(w.mDirectionalAmbient[0].mDirections[5], (Rgba8{ 5, 0, 0, 0 }));
    }

    TEST(SkyrimWeatherDalc, aFifthOrAWrongSizedOneIsOdd)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        for (int i = 0; i < 5; ++i)
            subs.append(sub4("DALC", dalc32(1)));
        subs.append(sub4("DALC", Bytes().u32(1)));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        EXPECT_EQ(w.mDirectionalAmbient.size(), 4u);
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.DALC"], 2u);
    }

    TEST(SkyrimWeatherLists, soundsAndSkyStaticsRepeat)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        subs.append(sub4("SNAM", Bytes().u32(0x00001000).u32(1)));
        subs.append(sub4("SNAM", Bytes().u32(0x01001001).u32(3)));
        subs.append(sub4("SNAM", Bytes().u32(1)));
        subs.append(sub4("TNAM", Bytes().u32(0x00002000)));
        subs.append(sub4("TNAM", Bytes().u32(0x00002001)));
        ASSERT_TRUE(decodeW(f, subs, w, why)) << why;
        ASSERT_EQ(w.mSounds.size(), 2u);
        EXPECT_EQ(w.mSounds[0].mSound, 0x00001000u);
        EXPECT_EQ(w.mSounds[0].mType, 1u);
        EXPECT_EQ(w.mSounds[1].mSound, 0x01001001u);
        EXPECT_EQ(w.mSounds[1].mType, 3u);
        ASSERT_EQ(w.mSkyStatics.size(), 2u);
        EXPECT_EQ(w.mSkyStatics[1], 0x00002001u);
        EXPECT_EQ(f.mNotes.mOddBy["WTHR.SNAM"], 1u);
    }

    TEST(SkyrimWeatherLists, moreEntriesThanTheCapFailTheRecordWithALocatedMessage)
    {
        Fixture f;
        f.mLimits.mMaxEntriesPerList = 3;
        WeatherRecord w;
        std::string why;
        Bytes subs;
        for (int i = 0; i < 4; ++i)
            subs.append(sub4("TNAM", Bytes().u32(0x1000u + static_cast<std::uint32_t>(i))));
        EXPECT_FALSE(decodeW(f, subs, w, why));
        EXPECT_NE(why.find("TNAM"), std::string::npos) << why;
        EXPECT_NE(why.find("3"), std::string::npos) << why;
    }

    TEST(SkyrimWeatherDecode, aTextureOverTheStringLimitFailsTheRecord)
    {
        Fixture f;
        f.mLimits.mMaxStringBytes = 8;
        f.mExteriorLimits.mMaxStringBytes = 8;
        WeatherRecord w;
        std::string why;
        EXPECT_FALSE(decodeW(f, sub4("00TX", Bytes().zstring("Sky\\a_very_long_name.dds")), w, why));
        EXPECT_NE(why.find("00TX"), std::string::npos) << why;
    }

    TEST(SkyrimWeatherDecode, aSubrecordThatClaimsMoreThanRemainsFailsTheRecordWithALocatedMessage)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes bad;
        bad.fourCC("DATA").u16(500).text("short");
        EXPECT_FALSE(decodeW(f, bad, w, why));
        EXPECT_NE(why.find("DATA"), std::string::npos) << why;
        EXPECT_NE(why.find("500"), std::string::npos) << why;
    }

    TEST(SkyrimWeatherDecode, anXxxxSubrecordCarriesTheSizeOfTheNextOne)
    {
        Fixture f;
        WeatherRecord w;
        std::string why;
        Bytes big;
        for (int i = 0; i < 70000; ++i)
            big.u8(1);
        Bytes subs = sub4("XXXX", Bytes().u32(70000));
        subs.fourCC("MODL").u16(0).append(big);
        subs.append(sub4("LNAM", Bytes().u32(7)));
        // the 70000 byte string is over the default limit of 64 KiB
        EXPECT_FALSE(decodeW(f, subs, w, why));
        EXPECT_NE(why.find("MODL"), std::string::npos) << why;
    }

    // ---- CLMT ------------------------------------------------------------------------------------------------------

    TEST(SkyrimClimateDecode, weatherListTexturesModelAndTiming)
    {
        Fixture f;
        ClimateRecord c;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("TestClimate"));
        subs.append(sub4("WLST",
            Bytes()
                .u32(0x00000100)
                .u32(60)
                .u32(0)
                .u32(0x01000200)
                .u32(static_cast<std::uint32_t>(-5))
                .u32(0x00000300)));
        subs.append(sub4("FNAM", Bytes().zstring("Sky\\Sun.dds")));
        subs.append(sub4("GNAM", Bytes().zstring("Sky\\SunGlare.dds")));
        subs.append(sub4("MODL", Bytes().zstring("Sky\\Stars.nif")));
        subs.append(sub4("TNAM", Bytes().u8(33).u8(60).u8(96).u8(123).u8(50).u8(195)));
        ASSERT_TRUE(decodeClimate(view(subs), raw("CLMT"), f.context(), f.mLimits, c, why)) << why;
        EXPECT_EQ(c.mCommon.mEditorId, "TestClimate");
        ASSERT_EQ(c.mWeathers.size(), 2u);
        EXPECT_EQ(c.mWeathers[0].mWeather, 0x00000100u);
        EXPECT_EQ(c.mWeathers[0].mChance, 60);
        EXPECT_EQ(c.mWeathers[0].mGlobal, kNullFormId);
        EXPECT_EQ(c.mWeathers[1].mWeather, 0x01000200u);
        EXPECT_EQ(c.mWeathers[1].mChance, -5);
        EXPECT_EQ(c.mWeathers[1].mGlobal, 0x00000300u);
        EXPECT_EQ(c.mSunTexture, "Sky\\Sun.dds");
        EXPECT_EQ(c.mSunGlareTexture, "Sky\\SunGlare.dds");
        EXPECT_EQ(c.mSkyModel, "Sky\\Stars.nif");
        ASSERT_TRUE(c.mTiming.has_value());
        EXPECT_EQ(c.mTiming->sunriseBeginMinutes(), 330u);
        EXPECT_EQ(c.mTiming->sunriseEndMinutes(), 600u);
        EXPECT_EQ(c.mTiming->sunsetBeginMinutes(), 960u);
        EXPECT_EQ(c.mTiming->sunsetEndMinutes(), 1230u);
        EXPECT_EQ(c.mTiming->mVolatility, 50);
        EXPECT_TRUE(c.mTiming->hasMasser());
        EXPECT_TRUE(c.mTiming->hasSecunda());
        EXPECT_EQ(c.mTiming->phaseLength(), 3u);
    }

    TEST(SkyrimClimateDecode, aClimateWithoutMoonsAndWithoutTexturesIsFine)
    {
        Fixture f;
        ClimateRecord c;
        std::string why;
        Bytes subs = sub4("TNAM", Bytes().u8(23).u8(63).u8(94).u8(130).u8(0).u8(3));
        ASSERT_TRUE(decodeClimate(view(subs), raw("CLMT"), f.context(), f.mLimits, c, why)) << why;
        EXPECT_FALSE(c.mTiming->hasMasser());
        EXPECT_FALSE(c.mTiming->hasSecunda());
        EXPECT_EQ(c.mTiming->phaseLength(), 3u);
        EXPECT_TRUE(c.mSunTexture.empty());
        EXPECT_TRUE(c.mWeathers.empty());
    }

    TEST(SkyrimClimateDecode, aListThatIsNotWholeEntriesAndAWrongTimingAreOdd)
    {
        Fixture f;
        ClimateRecord c;
        std::string why;
        Bytes subs = sub4("WLST", Bytes().u32(1).u32(2).u32(3).u8(9));
        subs.append(sub4("TNAM", Bytes().u8(1).u8(2)));
        ASSERT_TRUE(decodeClimate(view(subs), raw("CLMT"), f.context(), f.mLimits, c, why)) << why;
        EXPECT_TRUE(c.mWeathers.empty());
        EXPECT_FALSE(c.mTiming.has_value());
        EXPECT_EQ(f.mNotes.mOddBy["CLMT.WLST"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["CLMT.TNAM"], 1u);
    }

    TEST(SkyrimClimateDecode, aListOverTheCapFailsTheRecord)
    {
        Fixture f;
        f.mLimits.mMaxEntriesPerList = 2;
        ClimateRecord c;
        std::string why;
        Bytes list;
        for (int i = 0; i < 3; ++i)
            list.u32(1).u32(10).u32(0);
        EXPECT_FALSE(decodeClimate(view(sub4("WLST", list)), raw("CLMT"), f.context(), f.mLimits, c, why));
        EXPECT_NE(why.find("WLST"), std::string::npos) << why;
    }

    // ---- REGN ------------------------------------------------------------------------------------------------------

    Bytes rdat(std::uint32_t type, std::uint8_t flags, std::uint8_t priority)
    {
        return sub4("RDAT", Bytes().u32(type).u8(flags).u8(priority).u16(0));
    }

    TEST(SkyrimRegionDecode, aWeatherSectionCarriesItsPriorityFlagsAndEntries)
    {
        Fixture f;
        RegionWeatherRecord r;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("TestRegion"));
        subs.append(sub4("WNAM", Bytes().u32(0x01000777)));
        subs.append(rdat(3, 1, 90));
        subs.append(sub4("RDWT", Bytes().u32(0x00000100).u32(70).u32(0).u32(0x00000101).u32(15).u32(0)));
        ASSERT_TRUE(decodeRegionWeather(view(subs), raw("REGN"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "TestRegion");
        EXPECT_EQ(r.mWorld, 0x01000777u);
        EXPECT_TRUE(r.mHasWeather);
        EXPECT_EQ(r.mFlags, 1);
        EXPECT_EQ(r.mPriority, 90);
        ASSERT_EQ(r.mWeathers.size(), 2u);
        EXPECT_EQ(r.mWeathers[0].mWeather, 0x00000100u);
        EXPECT_EQ(r.mWeathers[0].mChance, 70);
        EXPECT_EQ(r.mWeathers[1].mChance, 15);
    }

    TEST(SkyrimRegionDecode, otherSectionsAreIgnoredAndOnlyAWeatherSectionOwnsAnRdwt)
    {
        Fixture f;
        RegionWeatherRecord r;
        std::string why;
        Bytes subs = rdat(7, 0, 50); // a sound section
        subs.append(sub4("RDSA", Bytes().u32(1).u32(2).u32(3)));
        subs.append(sub4("RDWT", Bytes().u32(0x00000100).u32(100).u32(0))); // not after a weather section
        ASSERT_TRUE(decodeRegionWeather(view(subs), raw("REGN"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_FALSE(r.mHasWeather);
        EXPECT_TRUE(r.mWeathers.empty());
        EXPECT_EQ(f.mNotes.mOddBy["REGN.RDWT"], 1u);
    }

    TEST(SkyrimRegionDecode, anRdwtThatIsNotWholeEntriesOrAnRdatOfAnotherSizeIsOdd)
    {
        Fixture f;
        RegionWeatherRecord r;
        std::string why;
        Bytes subs = sub4("RDAT", Bytes().u32(3)); // 4 bytes, not 8
        subs.append(rdat(3, 0, 60));
        subs.append(sub4("RDWT", Bytes().u32(1).u32(2)));
        ASSERT_TRUE(decodeRegionWeather(view(subs), raw("REGN"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_TRUE(r.mHasWeather);
        EXPECT_TRUE(r.mWeathers.empty());
        EXPECT_EQ(f.mNotes.mOddBy["REGN.RDAT"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["REGN.RDWT"], 1u);
    }

    TEST(SkyrimRegionDecode, anEmptyRdwtIsAWeatherSectionWithNoEntries)
    {
        // not seen in the installed data (every RDWT has 1 to 7 entries), but legal framing
        Fixture f;
        RegionWeatherRecord r;
        std::string why;
        Bytes subs = rdat(3, 0, 50);
        subs.append(sub4("RDWT", Bytes()));
        ASSERT_TRUE(decodeRegionWeather(view(subs), raw("REGN"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_TRUE(r.mHasWeather);
        EXPECT_TRUE(r.mWeathers.empty());
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimRegionDecode, aListOverTheCapFailsTheRecord)
    {
        Fixture f;
        f.mLimits.mMaxEntriesPerList = 1;
        RegionWeatherRecord r;
        std::string why;
        Bytes subs = rdat(3, 0, 50);
        subs.append(sub4("RDWT", Bytes().u32(1).u32(2).u32(0).u32(1).u32(2).u32(0)));
        EXPECT_FALSE(decodeRegionWeather(view(subs), raw("REGN"), f.context(), f.mLimits, r, why));
        EXPECT_NE(why.find("RDWT"), std::string::npos) << why;
    }

    // ---- the walk --------------------------------------------------------------------------------------------------

    constexpr std::uint32_t kWorldId = 0x01000900, kExtCellId = 0x01000A00, kIntCellId = 0x01000C00;

    Bytes extCell()
    {
        Bytes subs = sub4("DATA", Bytes().u16(2));
        subs.append(sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(-4)).u32(7).u32(0)));
        subs.append(sub4("XCLR", Bytes().u32(0x01000E00).u32(0x00000E01)));
        subs.append(sub4("XCWT", Bytes().u32(0x00000F00)));
        return record4("CELL", 0, kExtCellId, subs);
    }

    Bytes intCell()
    {
        Bytes subs = sub4("DATA", Bytes().u16(1));
        subs.append(sub4("XCCM", Bytes().u32(0x01000E00)));
        subs.append(sub4("XCIM", Bytes().u32(0x01000D00)));
        return record4("CELL", 0, kIntCellId, subs);
    }

    Bytes wthrRecord(std::uint32_t id, const char* edid, bool compressed = false)
    {
        Bytes subs = sub4("EDID", Bytes().zstring(edid));
        subs.append(sub4("DATA", weatherData()));
        if (!compressed)
            return record4("WTHR", 0, id, subs);
        const std::string plain = subs.str();
        uLongf bound = compressBound(static_cast<uLong>(plain.size()));
        std::string packed(bound, '\0');
        EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(packed.data()), &bound,
                      reinterpret_cast<const Bytef*>(plain.data()), static_cast<uLong>(plain.size()), 6),
            Z_OK);
        packed.resize(bound);
        Bytes body;
        body.u32(static_cast<std::uint32_t>(plain.size())).text(packed);
        return record4("WTHR", 0x00040000, id, body);
    }

    Bytes walkPlugin(const Bytes& wthrs)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("WTHR"), 0, wthrs));
        Bytes clmt = record4("CLMT", 0, 0x01000B00,
            Bytes()
                .append(sub4("EDID", Bytes().zstring("C")))
                .append(sub4("WLST", Bytes().u32(0x01000800).u32(100).u32(0))));
        bytes.append(group4(Content::fourCC("CLMT"), 0, clmt));
        Bytes regn = record4("REGN", 0, 0x01000E00,
            Bytes()
                .append(sub4("EDID", Bytes().zstring("R")))
                .append(rdat(3, 0, 70))
                .append(sub4("RDWT", Bytes().u32(0x01000800).u32(100).u32(0))));
        bytes.append(group4(Content::fourCC("REGN"), 0, regn));
        Bytes world = record4("WRLD", 0, kWorldId,
            Bytes().append(sub4("EDID", Bytes().zstring("W"))).append(sub4("CNAM", Bytes().u32(0x01000B00))));
        world.append(group4(kWorldId, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, extCell()))));
        bytes.append(group4(Content::fourCC("WRLD"), 0, world));
        bytes.append(
            group4(Content::fourCC("CELL"), 0, group4(0, kGrupInteriorBlock, group4(0, kGrupInteriorSub, intCell()))));
        return bytes;
    }

    TEST(SkyrimWeatherWalk, collectsWeathersClimatesRegionsWorldsAndCellLinks)
    {
        const LoadOrder order = oneMaster();
        WeatherRecords data;
        Content::Error error;
        ASSERT_TRUE(parseWeatherRecordsBytes(
            walkPlugin(wthrRecord(0x01000800, "Clear")).str(), "test.esp", order, WeatherLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mWeathers.size(), 1u);
        EXPECT_EQ(data.mWeathers[0].mCommon.mEditorId, "Clear");
        ASSERT_EQ(data.mClimates.size(), 1u);
        EXPECT_EQ(data.mClimates[0].mWeathers.size(), 1u);
        ASSERT_EQ(data.mRegions.size(), 1u);
        EXPECT_EQ(data.mRegions[0].mPriority, 70);
        ASSERT_EQ(data.mWorlds.size(), 1u);
        EXPECT_EQ(data.mWorlds[0].mClimate, 0x01000B00u);
        ASSERT_EQ(data.mCells.size(), 2u);
        const CellWeatherLinks& ext = data.mCells[0];
        EXPECT_EQ(ext.mWorld, data.mWorlds[0].mCommon.mId);
        ASSERT_TRUE(ext.mGrid.has_value());
        EXPECT_EQ(ext.mGrid->x, -4);
        EXPECT_EQ(ext.mGrid->y, 7);
        ASSERT_EQ(ext.mRegions.size(), 2u);
        EXPECT_EQ(ext.mRegions[0], 0x01000E00u);
        EXPECT_EQ(ext.mRegions[1], 0x00000E01u);
        EXPECT_EQ(ext.mWater, 0x00000F00u);
        const CellWeatherLinks& in = data.mCells[1];
        EXPECT_EQ(in.mWorld, kNullFormId);
        EXPECT_FALSE(in.mGrid.has_value());
        EXPECT_EQ(in.mWeatherRegion, 0x01000E00u);
        EXPECT_EQ(in.mImageSpace, 0x01000D00u);
        EXPECT_EQ(data.mCounts[Content::fourCC("WTHR")].mParsed, 1u);
        EXPECT_EQ(data.mCounts[Content::fourCC("CELL")].mParsed, 2u);
    }

    TEST(SkyrimWeatherWalk, aCompressedWeatherIsInflated)
    {
        const LoadOrder order = oneMaster();
        WeatherRecords data;
        Content::Error error;
        ASSERT_TRUE(parseWeatherRecordsBytes(
            walkPlugin(wthrRecord(0x01000800, "Packed", true)).str(), "test.esp", order, WeatherLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mWeathers.size(), 1u);
        EXPECT_EQ(data.mWeathers[0].mCommon.mEditorId, "Packed");
        EXPECT_TRUE(data.mWeathers[0].mData.has_value());
    }

    TEST(SkyrimWeatherWalk, aBadWeatherIsAnErrorAndTheWalkGoesOn)
    {
        Bytes bad;
        bad.fourCC("DATA").u16(500).text("short");
        Bytes wthrs = record4("WTHR", 0, 0x01000801, bad);
        wthrs.append(wthrRecord(0x01000800, "Good"));
        const LoadOrder order = oneMaster();
        WeatherRecords data;
        Content::Error error;
        ASSERT_TRUE(parseWeatherRecordsBytes(walkPlugin(wthrs).str(), "test.esp", order, WeatherLimits(), data, error));
        EXPECT_EQ(data.mWeathers.size(), 1u);
        ASSERT_EQ(data.mErrors.size(), 1u);
        EXPECT_EQ(data.mErrors[0].mRecordType, Content::fourCC("WTHR"));
        EXPECT_EQ(data.mCounts[Content::fourCC("WTHR")].mFailed, 1u);
        EXPECT_EQ(data.mCounts[Content::fourCC("WTHR")].mSeen, 2u);
        EXPECT_EQ(data.mClimates.size(), 1u) << "the records after the bad one are still read";
    }

    TEST(SkyrimWeatherWalk, aPluginThatIsNotSkyrimSeIsRefused)
    {
        const LoadOrder order = oneMaster();
        WeatherRecords data;
        Content::Error error;
        // a Fallout New Vegas style header (form version 15, HEDR 1.34)
        Bytes bytes = tes4Header(1.34f, 0, "A", "B", 0, {}, 24, 15);
        EXPECT_FALSE(parseWeatherRecordsBytes(bytes.str(), "test.esp", order, WeatherLimits(), data, error));
    }

    // ---- the index -------------------------------------------------------------------------------------------------

    WorldSpaceRecord worldRec(
        FormId id, FormId climate, FormId parent = kNullFormId, std::optional<std::uint16_t> usage = std::nullopt)
    {
        WorldSpaceRecord w;
        w.mCommon.mId = id;
        w.mClimate = climate;
        w.mParent = parent;
        w.mParentUsage = usage;
        return w;
    }

    TEST(SkyrimWeatherIndex, aLaterPluginReplacesAnEarlierRecordAndADeletedOneRemovesIt)
    {
        WeatherRecords a, b;
        WeatherRecord w;
        w.mCommon.mId = 0x100;
        w.mCommon.mEditorId = "First";
        a.mWeathers.push_back(w);
        w.mCommon.mEditorId = "Second";
        b.mWeathers.push_back(w);
        WeatherRecord gone;
        gone.mCommon.mId = 0x200;
        a.mWeathers.push_back(gone);
        gone.mCommon.mFlags = RecordFlags::Deleted;
        b.mWeathers.push_back(gone);
        WeatherIndex index;
        index.add(a);
        EXPECT_NE(index.weather(0x200), nullptr);
        index.add(b);
        ASSERT_NE(index.weather(0x100), nullptr);
        EXPECT_EQ(index.weather(0x100)->mCommon.mEditorId, "Second");
        EXPECT_EQ(index.weather(0x200), nullptr);
        EXPECT_EQ(index.weatherCount(), 1u);
        EXPECT_EQ(index.weather(0x300), nullptr);
    }

    TEST(SkyrimWeatherIndex, aWorldTakesItsOwnClimate)
    {
        WeatherRecords r;
        r.mWorlds.push_back(worldRec(0x10, 0x812));
        WeatherIndex index;
        index.add(r);
        EXPECT_EQ(index.climateOfWorld(0x10), std::optional<FormId>(0x812));
    }

    TEST(SkyrimWeatherIndex, aWorldInheritsFromItsParentOnlyWhenTheClimateBitIsSet)
    {
        WeatherRecords r;
        r.mWorlds.push_back(worldRec(0x10, 0x812));
        r.mWorlds.push_back(worldRec(0x20, kNullFormId, 0x10, std::uint16_t{ 127 })); // WhiterunWorld's PNAM
        r.mWorlds.push_back(worldRec(0x30, kNullFormId, 0x10, std::uint16_t{ 0x04 })); // Sovngarde-like: map only
        r.mWorlds.push_back(worldRec(0x40, kNullFormId, 0x10)); // no PNAM
        r.mWorlds.push_back(worldRec(0x50, kNullFormId, 0x20, std::uint16_t{ 0x10 })); // grandchild
        WeatherIndex index;
        index.add(r);
        EXPECT_EQ(index.climateOfWorld(0x20), std::optional<FormId>(0x812));
        EXPECT_EQ(index.climateOfWorld(0x30), std::nullopt);
        EXPECT_EQ(index.climateOfWorld(0x40), std::nullopt);
        EXPECT_EQ(index.climateOfWorld(0x50), std::optional<FormId>(0x812));
        EXPECT_EQ(index.climateOfWorld(0x99), std::nullopt) << "an unknown world";
    }

    TEST(SkyrimWeatherIndex, aLoopOfParentsEndsWithoutAClimate)
    {
        WeatherRecords r;
        r.mWorlds.push_back(worldRec(0x10, kNullFormId, 0x20, std::uint16_t{ 0x10 }));
        r.mWorlds.push_back(worldRec(0x20, kNullFormId, 0x10, std::uint16_t{ 0x10 }));
        r.mWorlds.push_back(worldRec(0x30, kNullFormId, 0x30, std::uint16_t{ 0x10 }));
        WeatherIndex index;
        index.add(r);
        EXPECT_EQ(index.climateOfWorld(0x10), std::nullopt);
        EXPECT_EQ(index.climateOfWorld(0x30), std::nullopt);
    }

    TEST(SkyrimWeatherIndex, aChainLongerThanSixteenLevelsEndsWithoutAClimate)
    {
        WeatherRecords r;
        r.mWorlds.push_back(worldRec(0x100, 0x812));
        for (FormId i = 1; i <= 20; ++i)
            r.mWorlds.push_back(worldRec(0x100 + i, kNullFormId, 0x100 + i - 1, std::uint16_t{ 0x10 }));
        WeatherIndex index;
        index.add(r);
        EXPECT_EQ(index.climateOfWorld(0x100 + 10), std::optional<FormId>(0x812));
        EXPECT_EQ(index.climateOfWorld(0x100 + 20), std::nullopt);
    }

    RegionWeatherRecord regionRec(
        FormId id, std::uint8_t priority, std::vector<WeatherChance> weathers, bool has = true)
    {
        RegionWeatherRecord r;
        r.mCommon.mId = id;
        r.mHasWeather = has;
        r.mPriority = priority;
        r.mWeathers = std::move(weathers);
        return r;
    }

    CellWeatherLinks cellRec(FormId world, std::vector<FormId> regions, FormId weatherRegion = kNullFormId)
    {
        CellWeatherLinks c;
        c.mCommon.mId = 0x7000;
        c.mWorld = world;
        c.mRegions = std::move(regions);
        c.mWeatherRegion = weatherRegion;
        return c;
    }

    ClimateRecord climateRec(FormId id, FormId weather)
    {
        ClimateRecord c;
        c.mCommon.mId = id;
        c.mWeathers.push_back({ weather, 100, kNullFormId });
        return c;
    }

    TEST(SkyrimWeatherIndex, theRegionWithTheHighestPriorityGivesTheList)
    {
        WeatherRecords r;
        r.mRegions.push_back(regionRec(0xA, 50, { { 0x1, 100, 0 } }));
        r.mRegions.push_back(regionRec(0xB, 90, { { 0x2, 100, 0 } }));
        r.mRegions.push_back(regionRec(0xC, 70, { { 0x3, 100, 0 } }));
        WeatherIndex index;
        index.add(r);
        const auto list = index.effectiveWeatherList(cellRec(0x10, { 0xA, 0xB, 0xC }));
        EXPECT_EQ(list.mSource, WeatherIndex::EffectiveList::Source::Region);
        EXPECT_EQ(list.mFrom, 0xBu);
        ASSERT_NE(list.mWeathers, nullptr);
        EXPECT_EQ((*list.mWeathers)[0].mWeather, 0x2u);
    }

    TEST(SkyrimWeatherIndex, aTieInPriorityGoesToTheEarlierRegionOfTheCell)
    {
        WeatherRecords r;
        r.mRegions.push_back(regionRec(0xA, 80, { { 0x1, 100, 0 } }));
        r.mRegions.push_back(regionRec(0xB, 80, { { 0x2, 100, 0 } }));
        WeatherIndex index;
        index.add(r);
        EXPECT_EQ(index.effectiveWeatherList(cellRec(0x10, { 0xB, 0xA })).mFrom, 0xBu);
        EXPECT_EQ(index.effectiveWeatherList(cellRec(0x10, { 0xA, 0xB })).mFrom, 0xAu);
    }

    TEST(SkyrimWeatherIndex, regionsWithoutAWeatherSectionOrWithAnEmptyListAreSkipped)
    {
        WeatherRecords r;
        r.mRegions.push_back(regionRec(0xA, 100, {}, false)); // sound-only region
        r.mRegions.push_back(regionRec(0xB, 99, {})); // weather section, no entries
        r.mRegions.push_back(regionRec(0xC, 10, { { 0x3, 100, 0 } }));
        WeatherIndex index;
        index.add(r);
        const auto list = index.effectiveWeatherList(cellRec(0x10, { 0xA, 0xB, 0xC, 0xDEAD }));
        EXPECT_EQ(list.mFrom, 0xCu) << "an unknown region id is skipped too";
    }

    TEST(SkyrimWeatherIndex, withNoRegionListTheWorldsClimateApplies)
    {
        WeatherRecords r;
        r.mClimates.push_back(climateRec(0x812, 0x41));
        r.mWorlds.push_back(worldRec(0x10, 0x812));
        WeatherIndex index;
        index.add(r);
        const auto list = index.effectiveWeatherList(cellRec(0x10, {}));
        EXPECT_EQ(list.mSource, WeatherIndex::EffectiveList::Source::Climate);
        EXPECT_EQ(list.mFrom, 0x812u);
        EXPECT_EQ((*list.mWeathers)[0].mWeather, 0x41u);
    }

    TEST(SkyrimWeatherIndex, anInteriorCellTakesItsXccmRegionAndIgnoresItsOwnRegionList)
    {
        WeatherRecords r;
        r.mRegions.push_back(regionRec(0xA, 50, { { 0x1, 100, 0 } }));
        r.mRegions.push_back(regionRec(0xB, 99, { { 0x2, 100, 0 } }));
        r.mRegions.push_back(regionRec(0xC, 80, {}, false)); // no weather section
        WeatherIndex index;
        index.add(r);
        // the XCCM region wins over a higher-priority XCLR one
        auto list = index.effectiveWeatherList(cellRec(kNullFormId, { 0xB }, 0xA));
        EXPECT_EQ(list.mSource, WeatherIndex::EffectiveList::Source::Region);
        EXPECT_EQ(list.mFrom, 0xAu);
        // an XCCM region without a weather list falls back to the XCLR scan, then to nothing for an interior
        list = index.effectiveWeatherList(cellRec(kNullFormId, { 0xB }, 0xC));
        EXPECT_EQ(list.mFrom, 0xBu);
        list = index.effectiveWeatherList(cellRec(kNullFormId, {}, 0xC));
        EXPECT_EQ(list.mSource, WeatherIndex::EffectiveList::Source::None);
    }

    TEST(SkyrimWeatherIndex, nothingApplyingGivesAnEmptyAnswer)
    {
        WeatherRecords r;
        r.mWorlds.push_back(worldRec(0x10, 0x812)); // the climate record is missing
        WeatherIndex index;
        index.add(r);
        const auto list = index.effectiveWeatherList(cellRec(0x10, {}));
        EXPECT_EQ(list.mSource, WeatherIndex::EffectiveList::Source::None);
        EXPECT_EQ(list.mWeathers, nullptr);
        EXPECT_EQ(
            index.effectiveWeatherList(cellRec(kNullFormId, {})).mSource, WeatherIndex::EffectiveList::Source::None);
    }

    TEST(SkyrimWeatherPick, theRollSelectsByCumulativeShareOfTheSum)
    {
        const std::vector<WeatherChance> list{ { 0xA, 50, 0 }, { 0xB, 30, 0 }, { 0xC, 20, 0 } };
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.0), std::optional<FormId>(0xA));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.499), std::optional<FormId>(0xA));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.5), std::optional<FormId>(0xB));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.799), std::optional<FormId>(0xB));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.8), std::optional<FormId>(0xC));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.9999999), std::optional<FormId>(0xC));
    }

    TEST(SkyrimWeatherPick, aSumOtherThanOneHundredIsScaled)
    {
        // an installed list adds to 85: 51 + 34
        const std::vector<WeatherChance> list{ { 0xA, 51, 0 }, { 0xB, 34, 0 } };
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.59), std::optional<FormId>(0xA)); // 0.59 * 85 = 50.15 < 51
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.61), std::optional<FormId>(0xB)); // 0.61 * 85 = 51.85
    }

    TEST(SkyrimWeatherPick, negativeAndZeroChancesNeverWinAndAnOutOfRangeRollIsClamped)
    {
        const std::vector<WeatherChance> list{ { 0xA, -40, 0 }, { 0xB, 0, 0 }, { 0xC, 10, 0 } };
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.0), std::optional<FormId>(0xC));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 5.0), std::optional<FormId>(0xC));
        EXPECT_EQ(WeatherIndex::pickWeather(list, -1.0), std::optional<FormId>(0xC));
        EXPECT_EQ(
            WeatherIndex::pickWeather(list, std::numeric_limits<double>::quiet_NaN()), std::optional<FormId>(0xC));
    }

    TEST(SkyrimWeatherPick, anEmptyListOrAllZeroGivesNothing)
    {
        EXPECT_EQ(WeatherIndex::pickWeather({}, 0.5), std::nullopt);
        EXPECT_EQ(WeatherIndex::pickWeather({ { 0xA, 0, 0 }, { 0xB, -3, 0 } }, 0.5), std::nullopt);
    }

    TEST(SkyrimWeatherPick, hugeChancesDoNotOverflow)
    {
        const std::vector<WeatherChance> list{ { 0xA, std::numeric_limits<std::int32_t>::max(), 0 },
            { 0xB, std::numeric_limits<std::int32_t>::max(), 0 } };
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.25), std::optional<FormId>(0xA));
        EXPECT_EQ(WeatherIndex::pickWeather(list, 0.75), std::optional<FormId>(0xB));
    }

    // ---- IMGS (task 0127, stage W-S3) -------------------------------------------------------------------------------

    TEST(SkyrimImageSpaceDecode, cinematicTintAndHdrAreRead)
    {
        Fixture f;
        ImageSpaceRecord r;
        std::string why;
        Bytes subs = sub4("EDID", Bytes().zstring("ISTest"));
        subs.append(sub4("CNAM", Bytes().f32(1.5f).f32(1.1f).f32(1.25f)));
        subs.append(sub4("TNAM", Bytes().f32(0.5f).f32(0.25f).f32(0.5f).f32(0.75f)));
        Bytes hdr;
        for (int i = 0; i < 9; ++i)
            hdr.f32(static_cast<float>(i) + 0.5f);
        subs.append(sub4("HNAM", hdr));
        ASSERT_TRUE(decodeImageSpace(view(subs), raw("IMGS"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "ISTest");
        ASSERT_TRUE(r.mCinematic.has_value());
        EXPECT_FLOAT_EQ(r.mCinematic->mSaturation, 1.5f);
        EXPECT_FLOAT_EQ(r.mCinematic->mBrightness, 1.1f);
        EXPECT_FLOAT_EQ(r.mCinematic->mContrast, 1.25f);
        ASSERT_TRUE(r.mTint.has_value());
        EXPECT_FLOAT_EQ(r.mTint->mAmount, 0.5f);
        EXPECT_FLOAT_EQ(r.mTint->mR, 0.25f);
        EXPECT_FLOAT_EQ(r.mTint->mG, 0.5f);
        EXPECT_FLOAT_EQ(r.mTint->mB, 0.75f);
        ASSERT_TRUE(r.mHdr.has_value());
        EXPECT_FLOAT_EQ((*r.mHdr)[8], 8.5f);
    }

    TEST(SkyrimImageSpaceDecode, wrongSizesAndNonFiniteFloatsAreOddAndLeaveTheFieldOut)
    {
        Fixture f;
        ImageSpaceRecord r;
        std::string why;
        Bytes subs = sub4("CNAM", Bytes().f32(1.f).f32(1.f)); // 8 bytes
        subs.append(sub4("TNAM", Bytes().f32(0.f).f32(std::numeric_limits<float>::quiet_NaN()).f32(0.f).f32(0.f)));
        subs.append(sub4("HNAM", Bytes().f32(1.f)));
        ASSERT_TRUE(decodeImageSpace(view(subs), raw("IMGS"), f.context(), f.mLimits, r, why)) << why;
        EXPECT_FALSE(r.mCinematic.has_value());
        EXPECT_FALSE(r.mTint.has_value());
        EXPECT_FALSE(r.mHdr.has_value());
        EXPECT_EQ(f.mNotes.mOddBy["IMGS.CNAM"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["IMGS.TNAM"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["IMGS.HNAM"], 1u);
    }
}
