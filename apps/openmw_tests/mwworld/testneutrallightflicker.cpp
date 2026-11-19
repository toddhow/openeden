// Task 0355 stage h: a Skyrim light's own flicker numbers reach the engine's light record. makeNeutralEsmLight carries a
// usable period and intensity amplitude in ESM::Light (NeutralFlicker, packed mTime, the seed in mValue) only when
// asked for; without the request, or with no usable numbers, the record is as it was.
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>

#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/lightflicker.hpp>

#include "apps/openmw/mwworld/neutrallight.hpp"

namespace
{
    using namespace openeden::sdk::game;
    namespace NF = MWAnim::NeutralFlicker;

    LightComponent flickerLight(LightAnimation animation, double period, double intensity)
    {
        LightComponent l;
        l.radius = 256;
        l.animation = animation;
        l.flickerPeriod = period;
        l.flickerIntensity = intensity;
        l.flickerMovement = 4.0;
        return l;
    }

    TEST(NeutralLightFlicker, withTheRequestAUsablePairIsCarriedInTheRecord)
    {
        const ESM::Light r
            = MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::Flicker, 0.25, 0.4), false, true, 99u);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralFlicker, 0);
        EXPECT_NE(r.mData.mFlags & ESM::Light::Flicker, 0);
        const auto p = NF::unpack(r.mData.mTime, r.mData.mValue);
        ASSERT_TRUE(p.has_value());
        EXPECT_NEAR(p->mPeriod, 0.25f, 0.001f);
        EXPECT_NEAR(p->mAmplitude, 0.4f, 0.001f);
        EXPECT_EQ(p->mSeed, 99u);
    }

    TEST(NeutralLightFlicker, aPulseCarriesItToo)
    {
        const ESM::Light r
            = MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::Pulse, 0.5, 0.3), false, true, 5u);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralFlicker, 0);
        EXPECT_NE(r.mData.mFlags & ESM::Light::Pulse, 0);
    }

    TEST(NeutralLightFlicker, withoutTheRequestTheRecordIsAsBefore)
    {
        const LightComponent l = flickerLight(LightAnimation::Flicker, 0.25, 0.4);
        const ESM::Light off = MWWorld::makeNeutralEsmLight(l, false, false, 99u);
        EXPECT_EQ(off.mData.mFlags & ESM::Light::NeutralFlicker, 0);
        EXPECT_EQ(off.mData.mTime, 0);
        EXPECT_EQ(off.mData.mValue, 0);
        const ESM::Light plain = MWWorld::makeNeutralEsmLight(l);
        EXPECT_EQ(plain.mData.mFlags, off.mData.mFlags);
    }

    TEST(NeutralLightFlicker, aLightThatDoesNotAnimateOrHasNoUsableNumbersKeepsTheOldRule)
    {
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::None, 0.25, 0.4), false, true, 1u)
                          .mData.mFlags
                & ESM::Light::NeutralFlicker,
            0);
        // The 12 flickers of the masters with an amplitude of 0 and the 11 slow flickers keep Morrowind's rule.
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::Flicker, 0.25, 0.0), false, true, 1u)
                          .mData.mFlags
                & ESM::Light::NeutralFlicker,
            0);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::FlickerSlow, 1.0, 0.0), false, true, 1u)
                          .mData.mFlags
                & ESM::Light::NeutralFlicker,
            0);
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::Flicker, 0.0, 0.4), false, true, 1u)
                          .mData.mFlags
                & ESM::Light::NeutralFlicker,
            0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(MWWorld::makeNeutralEsmLight(flickerLight(LightAnimation::Flicker, nan, 0.4), false, true, 1u)
                          .mData.mFlags
                & ESM::Light::NeutralFlicker,
            0);
    }

    TEST(NeutralLightFlicker, theFlagDoesNotDisturbTheGainOrTheOtherBits)
    {
        LightComponent l = flickerLight(LightAnimation::Flicker, 0.25, 0.4);
        l.red = 255;
        l.green = 200;
        l.blue = 100;
        l.fade = 3.0;
        const ESM::Light r = MWWorld::makeNeutralEsmLight(l, true, true, 3u);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralGain, 0);
        EXPECT_NE(r.mData.mFlags & ESM::Light::NeutralFlicker, 0);
        EXPECT_NE(r.mData.mFlags & ESM::Light::Dynamic, 0);
        EXPECT_FLOAT_EQ(r.mData.mWeight, 3.f);
    }

    TEST(NeutralLightFlicker, theSeedFollowsThePlacementAndNothingElse)
    {
        EXPECT_EQ(MWWorld::neutralLightSeed(2, 1234), MWWorld::neutralLightSeed(2, 1234));
        EXPECT_NE(MWWorld::neutralLightSeed(2, 1234), MWWorld::neutralLightSeed(2, 1235));
        EXPECT_NE(MWWorld::neutralLightSeed(2, 1234), MWWorld::neutralLightSeed(3, 1234));
        std::set<std::uint32_t> seen;
        for (std::uint32_t i = 0; i < 2000; ++i)
            seen.insert(MWWorld::neutralLightSeed(1, i));
        EXPECT_GT(seen.size(), 1990u); // neighbouring references do not share a seed
    }
}
