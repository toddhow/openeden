// Task 0117, stage E0: Skyrim SE exterior decoding (LAND, LTEX, TXST, WRLD, WATR, exterior cell water) on generated
// records and plugins. No game data.
#include <game_skyrimse/land.hpp>
#include <game_skyrimse/worldspace.hpp>

#include <gtest/gtest.h>

#include <zlib.h>

#include <cfloat>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupWorld = 1, kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

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
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context(bool localized = false) { return DecodeContext{ mResolver, mLimits, mNotes, localized }; }
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

    Bytes vhgt(float offset, const std::vector<std::int8_t>& gradient)
    {
        Bytes data;
        data.f32(offset);
        for (std::int8_t g : gradient)
            data.u8(static_cast<std::uint8_t>(g));
        data.u8(0).u8(0).u8(0);
        return sub4("VHGT", data);
    }

    std::vector<std::int8_t> flat()
    {
        return std::vector<std::int8_t>(LandConstants::kVertCount, 0);
    }

    Bytes btxt(std::uint32_t tex, std::uint8_t quadrant)
    {
        return sub4("BTXT", Bytes().u32(tex).u8(quadrant).u8(0).u16(0));
    }
    Bytes atxt(std::uint32_t tex, std::uint8_t quadrant, std::uint16_t layer)
    {
        return sub4("ATXT", Bytes().u32(tex).u8(quadrant).u8(0).u16(layer));
    }
    Bytes vtxt(const std::vector<std::pair<std::uint16_t, float>>& entries)
    {
        Bytes data;
        for (const auto& e : entries)
            data.u16(e.first).u16(0).f32(e.second);
        return sub4("VTXT", data);
    }

    // ---- heights ---------------------------------------------------------------------------------------------------

    TEST(SkyrimLandHeights, aFlatGradientGivesTheOffsetTimesEight)
    {
        const std::vector<float> h = decodeHeights(10.0f, [] {
            static const std::vector<std::int8_t> g = flat();
            return std::span<const std::int8_t>(g);
        }());
        ASSERT_EQ(h.size(), LandConstants::kVertCount);
        for (float v : h)
            EXPECT_FLOAT_EQ(v, 80.0f);
    }

    TEST(SkyrimLandHeights, theFirstColumnRunsDownTheRowsAndEachRowRunsAcross)
    {
        std::vector<std::int8_t> g = flat();
        g[0] = 1; // (row 0, col 0): offset + 1
        g[1] = 2; // (row 0, col 1): + 2 more
        g[33] = 3; // (row 1, col 0): first column continues from the row start of row 0
        g[34] = -1; // (row 1, col 1)
        const std::vector<float> h = decodeHeights(0.0f, g);
        ASSERT_EQ(h.size(), LandConstants::kVertCount);
        EXPECT_FLOAT_EQ(h[0], 8.0f);
        EXPECT_FLOAT_EQ(h[1], 24.0f);
        EXPECT_FLOAT_EQ(h[2], 24.0f);
        EXPECT_FLOAT_EQ(h[33], 32.0f); // 1 + 3 = 4 steps
        EXPECT_FLOAT_EQ(h[34], 24.0f); // 4 - 1 = 3 steps
    }

    TEST(SkyrimLandHeights, aWrongGradientSizeGivesNothing)
    {
        const std::vector<std::int8_t> g(10, 0);
        EXPECT_TRUE(decodeHeights(0.0f, g).empty());
    }

    // ---- LAND ------------------------------------------------------------------------------------------------------

    TEST(SkyrimLandDecode, heightsNormalsColoursAndFlagsAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgt(2.0f, flat()));
        subs.append(sub4("VNML", Bytes().text(std::string(3267, '\x7f'))));
        subs.append(sub4("VCLR", Bytes().text(std::string(3267, '\x20'))));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_TRUE(land.mHasDataFlags);
        EXPECT_EQ(land.mDataFlags, 31u);
        ASSERT_TRUE(land.mHasHeights);
        EXPECT_FLOAT_EQ(land.mHeightOffset, 2.0f);
        ASSERT_EQ(land.mHeights.size(), LandConstants::kVertCount);
        EXPECT_FLOAT_EQ(land.mHeights.front(), 16.0f);
        EXPECT_FLOAT_EQ(land.mHeights.back(), 16.0f);
        ASSERT_TRUE(land.mHasNormals);
        ASSERT_EQ(land.mNormals.size(), 3267u);
        EXPECT_EQ(land.mNormals[0], 127);
        ASSERT_TRUE(land.mHasColours);
        ASSERT_EQ(land.mColours.size(), 3267u);
        EXPECT_EQ(land.mColours[5], 0x20);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimLandDecode, aRecordWithNoDataHasNoHeights)
    {
        Fixture f;
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(sub4("DATA", Bytes().u32(0))), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_FALSE(land.mHasHeights);
        EXPECT_TRUE(land.mHeights.empty());
        EXPECT_EQ(land.layerCount(), 0u);
    }

    TEST(SkyrimLandDecode, wrongSizedFieldsAreCountedAndSkipped)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("VHGT", Bytes().f32(1.0f).text("short")));
        subs.append(sub4("VNML", Bytes().text("short")));
        subs.append(sub4("VCLR", Bytes().text(std::string(3268, 'x'))));
        subs.append(sub4("DATA", Bytes().u8(1)));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_FALSE(land.mHasHeights);
        EXPECT_FALSE(land.mHasNormals);
        EXPECT_FALSE(land.mHasColours);
        EXPECT_FALSE(land.mHasDataFlags);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 4u);
        EXPECT_EQ(f.mNotes.mOddBy["LAND.VHGT"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["LAND.VCLR"], 1u);
    }

    TEST(SkyrimLandDecode, aNonFiniteHeightOffsetIsRefusedWithALocatedMessage)
    {
        Fixture f;
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(
            view(vhgt(std::numeric_limits<float>::quiet_NaN(), flat())), raw("LAND"), f.context(), land, why));
        EXPECT_NE(why.find("VHGT"), std::string::npos) << why;
    }

    TEST(SkyrimLandDecode, textureLayersFollowTheirQuadrant)
    {
        Fixture f;
        Bytes subs;
        subs.append(btxt(0x00000A01, 0));
        subs.append(btxt(0x00000A02, 3));
        subs.append(atxt(0x00000B01, 0, 0));
        subs.append(vtxt({ { 3, 0.5f }, { 40, 1.0f } }));
        subs.append(atxt(0x01000B02, 0, 1)); // the plugin itself
        subs.append(vtxt({ { 288, 0.25f } }));
        subs.append(atxt(0x00000B03, 2, 0));
        subs.append(vtxt({ { 0, 0.75f } }));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_EQ(land.mQuadrants[0].mBase, 0x00000A01u);
        EXPECT_EQ(land.mQuadrants[1].mBase, kNullFormId);
        EXPECT_EQ(land.mQuadrants[3].mBase, 0x00000A02u);
        ASSERT_EQ(land.mQuadrants[0].mLayers.size(), 2u);
        EXPECT_EQ(land.mQuadrants[0].mLayers[0].mTexture, 0x00000B01u);
        EXPECT_EQ(land.mQuadrants[0].mLayers[0].mLayerIndex, 0u);
        ASSERT_EQ(land.mQuadrants[0].mLayers[0].mOpacity.size(), 2u);
        EXPECT_EQ(land.mQuadrants[0].mLayers[0].mOpacity[1].mPosition, 40u);
        EXPECT_FLOAT_EQ(land.mQuadrants[0].mLayers[0].mOpacity[1].mOpacity, 1.0f);
        EXPECT_EQ(land.mQuadrants[0].mLayers[1].mTexture, 0x01000B02u);
        EXPECT_EQ(land.mQuadrants[0].mLayers[1].mOpacity[0].mPosition, 288u);
        ASSERT_EQ(land.mQuadrants[2].mLayers.size(), 1u);
        EXPECT_EQ(land.layerCount(), 3u);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimLandDecode, anAtxtWithNoVtxtIsAnEmptyLayer)
    {
        Fixture f;
        Bytes subs;
        subs.append(atxt(0x00000B01, 1, 0));
        subs.append(atxt(0x00000B02, 1, 1));
        subs.append(vtxt({ { 5, 1.0f } }));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        ASSERT_EQ(land.mQuadrants[1].mLayers.size(), 2u);
        EXPECT_TRUE(land.mQuadrants[1].mLayers[0].mOpacity.empty());
        EXPECT_EQ(land.mQuadrants[1].mLayers[1].mOpacity.size(), 1u);
    }

    TEST(SkyrimLandDecode, aVtxtWithNoAtxtIsCountedAndDropped)
    {
        Fixture f;
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(vtxt({ { 1, 1.0f } })), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_EQ(land.layerCount(), 0u);
        EXPECT_EQ(f.mNotes.mOddBy["LAND.VTXT"], 1u);
    }

    TEST(SkyrimLandDecode, badQuadrantsAndOddSizesAreCounted)
    {
        Fixture f;
        Bytes subs;
        subs.append(btxt(0x00000A01, 4));
        subs.append(sub4("BTXT", Bytes().u32(1)));
        subs.append(atxt(0x00000B01, 9, 0));
        subs.append(vtxt({ { 1, 1.0f } })); // belongs to the refused ATXT
        subs.append(sub4("ATXT", Bytes().u32(1)));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_EQ(land.layerCount(), 0u);
        EXPECT_EQ(land.mQuadrants[0].mBase, kNullFormId);
        EXPECT_GE(f.mNotes.mOddSubrecords, 4u);
    }

    TEST(SkyrimLandDecode, badOpacityEntriesAreDroppedAndCounted)
    {
        Fixture f;
        Bytes subs;
        subs.append(atxt(0x00000B01, 0, 0));
        subs.append(vtxt(
            { { 289, 0.5f }, { 4, std::numeric_limits<float>::infinity() }, { 5, -0.5f }, { 6, 1.5f }, { 7, 0.5f } }));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        ASSERT_EQ(land.mQuadrants[0].mLayers.size(), 1u);
        ASSERT_EQ(land.mQuadrants[0].mLayers[0].mOpacity.size(), 1u);
        EXPECT_EQ(land.mQuadrants[0].mLayers[0].mOpacity[0].mPosition, 7u);
        EXPECT_EQ(f.mNotes.mOddBy["LAND.VTXT"], 4u);
    }

    TEST(SkyrimLandDecode, aVtxtOfAFractionalEntryIsOdd)
    {
        Fixture f;
        Bytes subs;
        subs.append(atxt(0x00000B01, 0, 0));
        subs.append(sub4("VTXT", Bytes().text("123456789")));
        LandRecord land;
        std::string why;
        ASSERT_TRUE(decodeLand(view(subs), raw("LAND"), f.context(), land, why)) << why;
        EXPECT_EQ(f.mNotes.mOddBy["LAND.VTXT"], 1u);
        EXPECT_TRUE(land.mQuadrants[0].mLayers[0].mOpacity.empty());
    }

    TEST(SkyrimLandHostile, aVtxtWithMoreEntriesThanTheGridHasIsRefusedBeforeItIsRead)
    {
        Fixture f;
        Bytes subs;
        subs.append(atxt(0x00000B01, 0, 0));
        // 20,000 entries (160,000 bytes) cannot fit a u16 size: the fixture uses the largest legal one.
        subs.append(sub4("VTXT", Bytes().text(std::string(8 * 8000, '\0'))));
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(view(subs), raw("LAND"), f.context(), land, why));
        EXPECT_NE(why.find("VTXT"), std::string::npos) << why;
        EXPECT_NE(why.find("8000"), std::string::npos) << why;
    }

    TEST(SkyrimLandHostile, tooManyLayersInOneQuadrantAreRefused)
    {
        Fixture f;
        f.mLimits.mMaxLayersPerQuadrant = 3;
        Bytes subs;
        for (std::uint16_t i = 0; i < 4; ++i)
        {
            subs.append(atxt(0x00000B01, 0, i));
            subs.append(vtxt({ { 1, 1.0f } }));
        }
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(view(subs), raw("LAND"), f.context(), land, why));
        EXPECT_NE(why.find("layers"), std::string::npos) << why;
    }

    TEST(SkyrimLandHostile, tooManyLayersInOneLandAreRefused)
    {
        Fixture f;
        f.mLimits.mMaxLayersPerLand = 5;
        Bytes subs;
        for (std::uint8_t q = 0; q < 4; ++q)
            for (std::uint16_t i = 0; i < 2; ++i)
            {
                subs.append(atxt(0x00000B01, q, i));
                subs.append(vtxt({ { 1, 1.0f } }));
            }
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(view(subs), raw("LAND"), f.context(), land, why));
    }

    TEST(SkyrimLandHostile, aSubrecordClaimingMoreBytesThanRemainIsALocatedError)
    {
        Fixture f;
        Bytes subs;
        subs.fourCC("VHGT").u16(1096).text("short");
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(view(subs), raw("LAND"), f.context(), land, why));
        EXPECT_NE(why.find("VHGT"), std::string::npos) << why;
    }

    TEST(SkyrimLandHostile, aCutShortSubrecordHeaderIsAnError)
    {
        Fixture f;
        LandRecord land;
        std::string why;
        EXPECT_FALSE(decodeLand(view(Bytes().text("VHG")), raw("LAND"), f.context(), land, why));
        EXPECT_FALSE(why.empty());
    }

    TEST(SkyrimLandHostile, everyTruncationOfARealisticRecordEndsCleanly)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgt(1.0f, flat()));
        subs.append(btxt(0x00000A01, 0));
        subs.append(atxt(0x00000B01, 0, 0));
        subs.append(vtxt({ { 3, 0.5f } }));
        const std::string full = subs.str();
        for (std::size_t n = 0; n <= full.size(); n += 7)
        {
            const std::string cut = full.substr(0, n);
            LandRecord land;
            std::string why;
            const std::span<const std::byte> bytes{ reinterpret_cast<const std::byte*>(cut.data()), cut.size() };
            (void)decodeLand(bytes, raw("LAND"), f.context(), land, why);
        }
        SUCCEED();
    }

    // ---- LTEX and TXST ---------------------------------------------------------------------------------------------

    TEST(SkyrimGroundTexture, fieldsAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("LandGrass")));
        subs.append(sub4("TNAM", Bytes().u32(0x00000C01)));
        subs.append(sub4("MNAM", Bytes().u32(0x00000C02)));
        subs.append(sub4("HNAM", Bytes().u8(30).u8(25)));
        subs.append(sub4("SNAM", Bytes().u8(2)));
        subs.append(sub4("GNAM", Bytes().u32(0x00000C03)));
        subs.append(sub4("GNAM", Bytes().u32(0x01000C04)));
        GroundTextureRecord ltex;
        std::string why;
        ASSERT_TRUE(decodeGroundTexture(view(subs), raw("LTEX"), f.context(), ltex, why)) << why;
        EXPECT_EQ(ltex.mCommon.mEditorId, "LandGrass");
        EXPECT_EQ(ltex.mTextureSet, 0x00000C01u);
        EXPECT_EQ(ltex.mMaterial, 0x00000C02u);
        EXPECT_EQ(ltex.mFriction, 30);
        EXPECT_EQ(ltex.mRestitution, 25);
        EXPECT_EQ(ltex.mSpecularExponent, 2);
        ASSERT_EQ(ltex.mGrass.size(), 2u);
        EXPECT_EQ(ltex.mGrass[1], 0x01000C04u);
    }

    TEST(SkyrimGroundTexture, tooManyGrassEntriesAreRefused)
    {
        Fixture f;
        f.mLimits.mMaxGrassPerGroundTexture = 2;
        Bytes subs;
        for (int i = 0; i < 3; ++i)
            subs.append(sub4("GNAM", Bytes().u32(0x00000C03 + i)));
        GroundTextureRecord ltex;
        std::string why;
        EXPECT_FALSE(decodeGroundTexture(view(subs), raw("LTEX"), f.context(), ltex, why));
        EXPECT_NE(why.find("GNAM"), std::string::npos) << why;
    }

    TEST(SkyrimGroundTexture, wrongSizesAreCounted)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("TNAM", Bytes().u8(1)));
        subs.append(sub4("HNAM", Bytes().u8(1)));
        GroundTextureRecord ltex;
        std::string why;
        ASSERT_TRUE(decodeGroundTexture(view(subs), raw("LTEX"), f.context(), ltex, why)) << why;
        EXPECT_EQ(ltex.mTextureSet, kNullFormId);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 2u);
    }

    TEST(SkyrimTextureSet, pathsAndFlagsAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("SetA")));
        subs.append(sub4("TX00", Bytes().zstring("textures\\landscape\\a.dds")));
        subs.append(sub4("TX01", Bytes().zstring("textures\\landscape\\a_n.dds")));
        subs.append(sub4("TX07", Bytes().zstring("textures\\landscape\\a_s.dds")));
        subs.append(sub4("DNAM", Bytes().u16(3)));
        TextureSetRecord set;
        std::string why;
        ASSERT_TRUE(decodeTextureSet(view(subs), raw("TXST"), f.context(), set, why)) << why;
        EXPECT_EQ(set.mPaths[0], "textures\\landscape\\a.dds");
        EXPECT_EQ(set.mPaths[1], "textures\\landscape\\a_n.dds");
        EXPECT_EQ(set.mPaths[2], "");
        EXPECT_EQ(set.mPaths[7], "textures\\landscape\\a_s.dds");
        EXPECT_EQ(set.mFlags, 3);
    }

    TEST(SkyrimTextureSet, anOverlongPathIsRefused)
    {
        Fixture f;
        f.mLimits.mMaxStringBytes = 16;
        TextureSetRecord set;
        std::string why;
        EXPECT_FALSE(decodeTextureSet(
            view(sub4("TX00", Bytes().zstring(std::string(40, 'a')))), raw("TXST"), f.context(), set, why));
        EXPECT_NE(why.find("TX00"), std::string::npos) << why;
    }

    // ---- WRLD, WATR, XCLW ------------------------------------------------------------------------------------------

    TEST(SkyrimWorldSpace, everyFieldIsDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("TestWorld")));
        subs.append(sub4("WNAM", Bytes().u32(0x00000D01)));
        subs.append(sub4("PNAM", Bytes().u16(0x7F)));
        subs.append(sub4("CNAM", Bytes().u32(0x00000D02)));
        subs.append(sub4("NAM2", Bytes().u32(0x00000D03)));
        subs.append(sub4("NAM3", Bytes().u32(0x00000D04)));
        subs.append(sub4("NAM4", Bytes().f32(-14000.0f)));
        subs.append(sub4("DNAM", Bytes().f32(-27000.0f).f32(-14000.0f)));
        subs.append(sub4("MNAM", Bytes().u32(1000).u32(2000).u16(0xFFC8).u16(51).u16(61).u16(0xFFD5)));
        subs.append(sub4("DATA", Bytes().u8(0x21)));
        subs.append(sub4("NAM0", Bytes().f32(-233472.0f).f32(-176128.0f)));
        subs.append(sub4("NAM9", Bytes().f32(253952.0f).f32(208896.0f)));
        subs.append(sub4("ONAM", Bytes().f32(1.0f).f32(2.0f).f32(3.0f).f32(4.0f)));
        subs.append(sub4("WCTR", Bytes().u16(3).u16(0xFFFE)));
        WorldSpaceRecord w;
        std::string why;
        ASSERT_TRUE(decodeWorldSpace(view(subs), raw("WRLD"), f.context(), w, why)) << why;
        EXPECT_EQ(w.mCommon.mEditorId, "TestWorld");
        EXPECT_EQ(w.mParent, 0x00000D01u);
        EXPECT_EQ(w.mParentUsage, 0x7F);
        EXPECT_EQ(w.mClimate, 0x00000D02u);
        EXPECT_EQ(w.mWater, 0x00000D03u);
        EXPECT_EQ(w.mLodWater, 0x00000D04u);
        EXPECT_FLOAT_EQ(*w.mLodWaterHeight, -14000.0f);
        EXPECT_FLOAT_EQ(*w.mDefaultLandHeight, -27000.0f);
        EXPECT_FLOAT_EQ(*w.mDefaultWaterHeight, -14000.0f);
        ASSERT_TRUE(w.mMap.has_value());
        EXPECT_EQ(w.mMap->mUsableWidth, 1000);
        EXPECT_EQ(w.mMap->mNorthWestX, -56);
        EXPECT_EQ(w.mMap->mSouthEastY, -43);
        EXPECT_EQ(w.mFlags, 0x21);
        EXPECT_FLOAT_EQ(w.mMinBounds->x, -233472.0f);
        EXPECT_FLOAT_EQ(w.mMaxBounds->y, 208896.0f);
        EXPECT_FLOAT_EQ((*w.mMapOffset)[3], 4.0f);
        EXPECT_EQ((*w.mCenterCell)[1], -2);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 0u);
    }

    TEST(SkyrimWorldSpace, absentFieldsStayAbsent)
    {
        Fixture f;
        WorldSpaceRecord w;
        std::string why;
        ASSERT_TRUE(decodeWorldSpace(view(sub4("EDID", Bytes().zstring("X"))), raw("WRLD"), f.context(), w, why));
        EXPECT_FALSE(w.mParentUsage.has_value());
        EXPECT_FALSE(w.mDefaultWaterHeight.has_value());
        EXPECT_FALSE(w.mMap.has_value());
        EXPECT_EQ(w.mParent, kNullFormId);
    }

    TEST(SkyrimWorldSpace, wrongSizesAreCountedNotGuessed)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("PNAM", Bytes().u8(1)));
        subs.append(sub4("DNAM", Bytes().f32(1.0f)));
        subs.append(sub4("NAM0", Bytes().f32(1.0f)));
        subs.append(sub4("MNAM", Bytes().u32(1)));
        WorldSpaceRecord w;
        std::string why;
        ASSERT_TRUE(decodeWorldSpace(view(subs), raw("WRLD"), f.context(), w, why));
        EXPECT_FALSE(w.mParentUsage.has_value());
        EXPECT_FALSE(w.mDefaultLandHeight.has_value());
        EXPECT_EQ(f.mNotes.mOddSubrecords, 4u);
    }

    TEST(SkyrimWorldSpace, aLocalisedNameIsAnId)
    {
        Fixture f;
        WorldSpaceRecord w;
        std::string why;
        ASSERT_TRUE(decodeWorldSpace(view(sub4("FULL", Bytes().u32(77))), raw("WRLD"), f.context(true), w, why));
        EXPECT_TRUE(w.mName.mLocalized);
        EXPECT_EQ(w.mName.mId, 77u);
    }

    TEST(SkyrimWater, basicFieldsAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Sea")));
        subs.append(sub4("NNAM", Bytes().zstring("textures\\water\\n.dds")));
        subs.append(sub4("ANAM", Bytes().u8(75)));
        subs.append(sub4("FNAM", Bytes().u8(1)));
        subs.append(sub4("SNAM", Bytes().u32(0x00000E01)));
        subs.append(sub4("DNAM", Bytes().text(std::string(232, 'x'))));
        WaterRecord w;
        std::string why;
        ASSERT_TRUE(decodeWater(view(subs), raw("WATR"), f.context(), w, why)) << why;
        EXPECT_EQ(w.mNoiseMap, "textures\\water\\n.dds");
        EXPECT_EQ(w.mOpacity, 75);
        EXPECT_EQ(w.mFlags, 1);
        EXPECT_EQ(w.mSound, 0x00000E01u);
        EXPECT_EQ(w.mDnamSize, 232u);
    }

    TEST(SkyrimCellWater, theSentinelsAreClassified)
    {
        EXPECT_EQ(classifyCellWater(std::nullopt).mKind, CellWaterKind::Absent);
        EXPECT_EQ(classifyCellWater(FLT_MAX).mKind, CellWaterKind::UseWorldDefault);
        EXPECT_EQ(classifyCellWater(-2147483648.0f).mKind, CellWaterKind::NoWater);
        EXPECT_EQ(classifyCellWater(4294967296.0f).mKind, CellWaterKind::NoWater);
        EXPECT_EQ(classifyCellWater(std::numeric_limits<float>::quiet_NaN()).mKind, CellWaterKind::Invalid);
        EXPECT_EQ(classifyCellWater(std::numeric_limits<float>::infinity()).mKind, CellWaterKind::Invalid);
        const CellWater h = classifyCellWater(600.0f);
        EXPECT_EQ(h.mKind, CellWaterKind::Height);
        EXPECT_FLOAT_EQ(h.mHeight, 600.0f);
        EXPECT_EQ(classifyCellWater(-14000.0f).mKind, CellWaterKind::Height);
        EXPECT_EQ(classifyCellWater(10624.0f).mKind, CellWaterKind::Height);
        EXPECT_EQ(classifyCellWater(0.0f).mKind, CellWaterKind::Height);
    }

    // ---- the walk --------------------------------------------------------------------------------------------------

    constexpr std::uint32_t kWorldId = 0x01000900, kCellId = 0x01000A00, kLandId = 0x01000B00;

    Bytes cellRecord(std::uint32_t id, std::int32_t x, std::int32_t y, const Bytes& extra = Bytes())
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u16(0x0002)));
        subs.append(sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0)));
        subs.append(extra);
        return record4("CELL", 0, id, subs);
    }

    Bytes landRecord(std::uint32_t id, float offset)
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgt(offset, flat()));
        return record4("LAND", 0, id, subs);
    }

    // CELL top group is not used for exteriors: WRLD top group > world record, then a world children group (type 1)
    // holding exterior block (4) > sub-block (5) > the CELL record and its children (6) > temporary (9) > LAND.
    Bytes exteriorPlugin(const Bytes& landInTemporary, const Bytes& cellExtra = Bytes())
    {
        Bytes temporary = group4(kCellId, kGrupTemporary, landInTemporary);
        Bytes cellChildren = group4(kCellId, kGrupCellChildren, temporary);
        Bytes sub = cellRecord(kCellId, 3, -2, cellExtra);
        sub.append(cellChildren);
        Bytes world = record4("WRLD", 0, kWorldId, sub4("EDID", Bytes().zstring("W")));
        world.append(group4(kWorldId, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, sub))));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("WRLD"), 0, world));
        return bytes;
    }

    TEST(SkyrimExteriorWalk, aLandRecordGetsItsWorldCellAndGrid)
    {
        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(parseExteriorBytes(
            exteriorPlugin(landRecord(kLandId, 3.0f)).str(), "test.esp", order, ExteriorLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mWorlds.size(), 1u);
        EXPECT_EQ(data.mWorlds[0].mCommon.mEditorId, "W");
        ASSERT_EQ(data.mLands.size(), 1u);
        const LandRecord& land = data.mLands[0];
        EXPECT_EQ(land.mWorld, data.mWorlds[0].mCommon.mId);
        EXPECT_EQ(land.mCell, 0x01000A00u);
        ASSERT_TRUE(land.mGrid.has_value());
        EXPECT_EQ(land.mGrid->x, 3);
        EXPECT_EQ(land.mGrid->y, -2);
        ASSERT_TRUE(land.mHasHeights);
        EXPECT_FLOAT_EQ(land.mHeights[0], 24.0f);
        ASSERT_EQ(data.mCells.size(), 1u);
        EXPECT_TRUE(data.mCells[0].mHasLand);
        EXPECT_EQ(data.mCells[0].mWorld, data.mWorlds[0].mCommon.mId);
        EXPECT_EQ(data.mCounts[Content::fourCC("LAND")].mParsed, 1u);
    }

    TEST(SkyrimExteriorWalk, cellWaterIsClassified)
    {
        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(parseExteriorBytes(exteriorPlugin(Bytes(), sub4("XCLW", Bytes().f32(600.0f))).str(), "test.esp",
            order, ExteriorLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mCells.size(), 1u);
        EXPECT_EQ(data.mCells[0].mWater.mKind, CellWaterKind::Height);
        EXPECT_FLOAT_EQ(data.mCells[0].mWater.mHeight, 600.0f);
        EXPECT_EQ(data.mCells[0].mFlags, 0x0002);
        EXPECT_FALSE(data.mCells[0].mHasLand);
        EXPECT_TRUE(data.mLands.empty());
    }

    TEST(SkyrimExteriorWalk, aCompressedLandRecordIsInflated)
    {
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(31)));
        subs.append(vhgt(1.0f, flat()));
        const std::string plain = subs.str();
        uLongf bound = compressBound(static_cast<uLong>(plain.size()));
        std::string packed(bound, '\0');
        ASSERT_EQ(compress2(reinterpret_cast<Bytef*>(packed.data()), &bound,
                      reinterpret_cast<const Bytef*>(plain.data()), static_cast<uLong>(plain.size()), 6),
            Z_OK);
        packed.resize(bound);
        Bytes body;
        body.u32(static_cast<std::uint32_t>(plain.size())).text(packed);
        const Bytes land = record4("LAND", 0x00040000, kLandId, body);

        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(parseExteriorBytes(exteriorPlugin(land).str(), "test.esp", order, ExteriorLimits(), data, error))
            << error.describe();
        ASSERT_EQ(data.mLands.size(), 1u);
        EXPECT_TRUE(data.mLands[0].mHasHeights);
    }

    TEST(SkyrimExteriorWalk, aBadLandIsAnErrorAndTheWalkGoesOn)
    {
        Bytes bad;
        bad.fourCC("VHGT").u16(1096).text("short");
        const Bytes brokenLand = record4("LAND", 0, 0x01000B01, bad);
        Bytes temporary = brokenLand;
        temporary.append(landRecord(kLandId, 1.0f));
        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(
            parseExteriorBytes(exteriorPlugin(temporary).str(), "test.esp", order, ExteriorLimits(), data, error));
        EXPECT_EQ(data.mLands.size(), 1u);
        ASSERT_EQ(data.mErrors.size(), 1u);
        EXPECT_EQ(data.mErrors[0].mRecordType, Content::fourCC("LAND"));
        EXPECT_EQ(data.mCounts[Content::fourCC("LAND")].mFailed, 1u);
    }

    TEST(SkyrimExteriorWalk, groundTexturesTextureSetsAndWatersAreCollected)
    {
        Bytes top;
        top.append(
            group4(Content::fourCC("LTEX"), 0, record4("LTEX", 0, 0x01000C00, sub4("TNAM", Bytes().u32(0x01000C10)))));
        top.append(
            group4(Content::fourCC("TXST"), 0, record4("TXST", 0, 0x01000C10, sub4("TX00", Bytes().zstring("a.dds")))));
        top.append(group4(Content::fourCC("WATR"), 0, record4("WATR", 0, 0x01000C20, sub4("ANAM", Bytes().u8(9)))));
        Bytes bytes = header();
        bytes.append(top);
        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(parseExteriorBytes(bytes.str(), "test.esp", order, ExteriorLimits(), data, error));
        ASSERT_EQ(data.mGroundTextures.size(), 1u);
        EXPECT_EQ(data.mGroundTextures[0].mTextureSet, 0x01000C10u);
        ASSERT_EQ(data.mTextureSets.size(), 1u);
        EXPECT_EQ(data.mTextureSets[0].mPaths[0], "a.dds");
        ASSERT_EQ(data.mWaters.size(), 1u);
        EXPECT_EQ(data.mWaters[0].mOpacity, 9);
    }

    TEST(SkyrimExteriorWalk, theRecordCapIsEnforced)
    {
        Bytes top;
        Bytes recs;
        for (std::uint32_t i = 0; i < 3; ++i)
            recs.append(record4("TXST", 0, 0x01000C10 + i, sub4("TX00", Bytes().zstring("a.dds"))));
        top.append(group4(Content::fourCC("TXST"), 0, recs));
        Bytes bytes = header();
        bytes.append(top);
        ExteriorLimits limits;
        limits.mMaxRecordsPerType = 2;
        const LoadOrder order = oneMaster();
        ExteriorData data;
        Content::Error error;
        ASSERT_TRUE(parseExteriorBytes(bytes.str(), "test.esp", order, limits, data, error));
        EXPECT_EQ(data.mTextureSets.size(), 2u);
        EXPECT_EQ(data.mErrors.size(), 1u);
    }
}
