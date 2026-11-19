// Task 0355 stage g: ESM::Light::NeutralSpot, NeutralShadowCasts and NeutralShadowNever are OpenEden flags, never bits of
// the file format. A light loaded from a plugin or a save must not keep them, nor the cone angle beside the record.
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadligh.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>

namespace
{
    ESM::Light roundTrip(int flags, float spotFov)
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_light");
        light.mData.mFlags = flags;
        light.mNeutralSpotFov = spotFov;
        light.mData.mRadius = 100;
        ESM::ESMWriter writer;
        auto stream = std::make_unique<std::stringstream>();
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(*stream);
        writer.startRecord(ESM::REC_LIGH);
        light.save(writer);
        writer.endRecord(ESM::REC_LIGH);
        ESM::ESMReader reader;
        reader.open(std::move(stream), "stream");
        EXPECT_TRUE(reader.hasMoreRecs());
        reader.getRecName();
        reader.getRecHeader();
        ESM::Light result;
        result.mNeutralSpotFov = 99.f; // load() must reset it
        bool deleted = false;
        result.load(reader, deleted);
        return result;
    }

    TEST(LightNeutralTypes, aLoadedLightNeverKeepsTheNeutralTypeBitsOrTheCone)
    {
        const int all = ESM::Light::NeutralSpot | ESM::Light::NeutralShadowCasts | ESM::Light::NeutralShadowNever;
        const ESM::Light loaded = roundTrip(ESM::Light::Dynamic | ESM::Light::Flicker | all, 60.f);
        EXPECT_EQ(loaded.mData.mFlags & all, 0);
        EXPECT_EQ(loaded.mNeutralSpotFov, 0.f);
    }

    TEST(LightNeutralTypes, theFileFormatsOwnBitsSurvive)
    {
        const ESM::Light loaded
            = roundTrip(ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralSpot, 60.f);
        EXPECT_EQ(loaded.mData.mFlags, ESM::Light::Dynamic | ESM::Light::Flicker);
    }
}
