// Task 0355 stage h: ESM::Light::NeutralFlicker is an OpenEden flag, never a bit of the file format. A light loaded from
// a plugin or a save must not keep it, or its mTime and mValue would be read as a period and a seed.
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadligh.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>

namespace
{
    ESM::Light roundTrip(int flags)
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_light_flicker");
        light.mData.mFlags = flags;
        light.mData.mTime = 123;
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

    TEST(LightNeutralFlicker, aLoadedLightNeverKeepsTheNeutralFlickerBit)
    {
        const ESM::Light loaded = roundTrip(ESM::Light::Dynamic | ESM::Light::Flicker | ESM::Light::NeutralFlicker);
        EXPECT_EQ(loaded.mData.mFlags & ESM::Light::NeutralFlicker, 0);
        EXPECT_EQ(loaded.mData.mFlags, ESM::Light::Dynamic | ESM::Light::Flicker);
        EXPECT_EQ(loaded.mData.mTime, 123);
    }

    TEST(LightNeutralFlicker, theFlagIsNotOneOfTheFileFormatsBitsNorTheGainBit)
    {
        const int fileBits = ESM::Light::Dynamic | ESM::Light::Carry | ESM::Light::Negative | ESM::Light::Flicker
            | ESM::Light::Fire | ESM::Light::OffDefault | ESM::Light::FlickerSlow | ESM::Light::Pulse
            | ESM::Light::PulseSlow | ESM::Light::NeutralGain;
        EXPECT_EQ(ESM::Light::NeutralFlicker & fileBits, 0);
    }
}
