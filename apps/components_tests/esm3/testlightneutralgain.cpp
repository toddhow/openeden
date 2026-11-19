// Task 0355 stage b: ESM::Light::NeutralGain is an OpenEden flag, never a bit of the file format. A light loaded from a
// plugin or a save that carries it must not keep it, or its mWeight would brighten the light (MWAnim::addLight).
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadligh.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>

namespace
{
    ESM::Light roundTrip(int flags, float weight)
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_light");
        light.mData.mFlags = flags;
        light.mData.mWeight = weight;
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
        bool deleted = false;
        result.load(reader, deleted);
        return result;
    }

    TEST(LightNeutralGain, aLoadedLightNeverKeepsTheNeutralGainBit)
    {
        const ESM::Light loaded = roundTrip(ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralGain, 3.f);
        EXPECT_EQ(loaded.mData.mFlags & ESM::Light::NeutralGain, 0);
    }

    TEST(LightNeutralGain, theFileFormatsOwnBitsAndTheWeightSurvive)
    {
        const ESM::Light loaded = roundTrip(ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralGain, 3.f);
        EXPECT_EQ(loaded.mData.mFlags, ESM::Light::Dynamic | ESM::Light::Flicker);
        EXPECT_EQ(loaded.mData.mWeight, 3.f);
        EXPECT_EQ(roundTrip(ESM::Light::Carry, 1.f).mData.mFlags, ESM::Light::Carry);
    }
}
