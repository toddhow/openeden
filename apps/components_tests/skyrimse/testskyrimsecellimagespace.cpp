// Task 0355 stage c: the image space a cell names (CELL XCIM -> IMGS) as the SDK's grade. Records are built by hand;
// every expected number is written out from the sample values. Written before the code (the first run was against
// stubs that give no grade).
#include <game_skyrimse/weather_source.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    ImageSpaceRecord imageSpace(FormId id, float saturation, float brightness, float contrast, float amount = -1.f)
    {
        ImageSpaceRecord r;
        r.mCommon.mId = id;
        r.mCinematic = ImageSpaceRecord::Cinematic{ saturation, brightness, contrast };
        if (amount >= 0.f)
            r.mTint = ImageSpaceRecord::Tint{ amount, 0.25f, 0.5f, 0.75f };
        return r;
    }

    CellWeatherLinks cell(FormId id, FormId imageSpaceId)
    {
        CellWeatherLinks c;
        c.mCommon.mId = id;
        c.mImageSpace = imageSpaceId;
        return c;
    }

    WeatherIndex indexOf(std::vector<CellWeatherLinks> cells, std::vector<ImageSpaceRecord> spaces)
    {
        WeatherRecords records;
        records.mCells = std::move(cells);
        records.mImageSpaces = std::move(spaces);
        WeatherIndex index;
        index.add(records);
        return index;
    }

    TEST(SkyrimCellImageSpace, aRecordGivesItsThreeNumbersAndTheTint)
    {
        const auto grade = describeImageSpaceGrade(imageSpace(0x1, 0.5f, 1.25f, 1.5f, 0.4f));
        ASSERT_TRUE(grade.has_value());
        EXPECT_FLOAT_EQ(grade->saturation, 0.5f);
        EXPECT_FLOAT_EQ(grade->brightness, 1.25f);
        EXPECT_FLOAT_EQ(grade->contrast, 1.5f);
        EXPECT_FLOAT_EQ(grade->tintAmount, 0.4f);
        EXPECT_EQ(grade->tint, (sdk::WeatherColour{ 0.25f, 0.5f, 0.75f }));
    }

    TEST(SkyrimCellImageSpace, aRecordWithoutTintHasNoTintAndOneWithoutCinematicValuesHasNoGrade)
    {
        const auto plain = describeImageSpaceGrade(imageSpace(0x1, 1.f, 1.f, 1.f));
        ASSERT_TRUE(plain.has_value());
        EXPECT_FLOAT_EQ(plain->tintAmount, 0.f);

        ImageSpaceRecord hdrOnly;
        hdrOnly.mHdr = std::array<float, 9>{};
        EXPECT_FALSE(describeImageSpaceGrade(hdrOnly).has_value());
    }

    TEST(SkyrimCellImageSpace, theTintAmountIsClampedToZeroToOne)
    {
        const auto grade = describeImageSpaceGrade(imageSpace(0x1, 1.f, 1.f, 1.f, 3.f));
        ASSERT_TRUE(grade.has_value());
        EXPECT_FLOAT_EQ(grade->tintAmount, 1.f);
    }

    TEST(SkyrimCellImageSpace, itIsTheConversionTheWeatherUses)
    {
        // One mechanism: a weather whose four image spaces are this record gives the same grade at every time.
        const ImageSpaceRecord record = imageSpace(0x7, 0.7f, 0.9f, 1.2f, 0.3f);
        WeatherRecord w;
        w.mCommon.mId = 0x20;
        w.mHasImageSpaces = true;
        w.mImageSpaces = { 0x7, 0x7, 0x7, 0x7 };
        const ImageSpaceLookup lookup = [&](FormId) { return &record; };
        const sdk::WeatherDescription d = describeWeather(w, {}, lookup);
        ASSERT_TRUE(d.hasImageSpace);
        const auto grade = describeImageSpaceGrade(record);
        ASSERT_TRUE(grade.has_value());
        EXPECT_EQ(d.imageSpace.day, *grade);
        EXPECT_EQ(d.imageSpace.night, *grade);
    }

    TEST(SkyrimCellImageSpace, aCellGivesTheGradeOfTheImageSpaceItNames)
    {
        const WeatherIndex index = indexOf({ cell(0x100, 0x7) }, { imageSpace(0x7, 0.6f, 1.1f, 1.3f, 0.2f) });
        const WeatherTable table(index);
        sdk::ImageSpaceGrade grade;
        ASSERT_TRUE(table.imageSpaceForCell(0x100, grade));
        EXPECT_FLOAT_EQ(grade.saturation, 0.6f);
        EXPECT_FLOAT_EQ(grade.brightness, 1.1f);
        EXPECT_FLOAT_EQ(grade.contrast, 1.3f);
        EXPECT_FLOAT_EQ(grade.tintAmount, 0.2f);
    }

    TEST(SkyrimCellImageSpace, noCellNoNameAnUnknownRecordOrNoCinematicValuesGiveNothingAndANeutralOut)
    {
        ImageSpaceRecord hdrOnly;
        hdrOnly.mCommon.mId = 0x9;
        hdrOnly.mHdr = std::array<float, 9>{};
        const WeatherIndex index = indexOf({ cell(0x100, kNullFormId), cell(0x101, 0x55), cell(0x102, 0x9) },
            { imageSpace(0x7, 0.6f, 1.1f, 1.3f), hdrOnly });
        const WeatherTable table(index);
        for (const FormId id : { FormId(0x100), FormId(0x101), FormId(0x102), FormId(0x999) })
        {
            sdk::ImageSpaceGrade grade;
            grade.saturation = 3.f; // must come back neutral
            EXPECT_FALSE(table.imageSpaceForCell(id, grade)) << std::hex << id;
            EXPECT_EQ(grade, sdk::ImageSpaceGrade{});
        }
    }

    TEST(SkyrimCellImageSpace, aLaterRecordOfTheSameImageSpaceWins)
    {
        WeatherRecords first;
        first.mCells = { cell(0x100, 0x7) };
        first.mImageSpaces = { imageSpace(0x7, 0.5f, 1.f, 1.f) };
        WeatherRecords second;
        second.mImageSpaces = { imageSpace(0x7, 0.25f, 1.f, 1.f) };
        WeatherIndex index;
        index.add(first);
        index.add(second);
        const WeatherTable table(index);
        sdk::ImageSpaceGrade grade;
        ASSERT_TRUE(table.imageSpaceForCell(0x100, grade));
        EXPECT_FLOAT_EQ(grade.saturation, 0.25f);
    }
}
