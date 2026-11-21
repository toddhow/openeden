// Task 0355 stage b: a neutral light (a game pack's light, made by MWWorld::makeNeutralEsmLight) can carry a brightness
// multiplier its 8 bit colour cannot hold: ESM::Light::NeutralGain in the flags and the multiplier in mData.mWeight.
// MWAnim::addLight (components/mwanimation/light.cpp) applies it to the light's colour; a record without the flag
// (every Morrowind light, whatever its weight) is read exactly as before.
#include <components/animation/autoplay.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/light.hpp>

#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    ESM::Light record(int flags, float weight)
    {
        ESM::Light light;
        light.blank();
        light.mData.mFlags = flags;
        light.mData.mWeight = weight;
        light.mData.mRadius = 256;
        light.mData.mColor = 0x00402010; // red 0x10, green 0x20, blue 0x40
        return light;
    }

    vsg::vec3 colourOf(const ESM::Light& record)
    {
        auto parent = vsg::Group::create();
        Anim::Controllers controllers;
        vsg::Node* node = MWAnim::addLight(parent.get(), parent.get(), record, controllers);
        auto* light = dynamic_cast<vsg::Light*>(node);
        EXPECT_NE(light, nullptr);
        return light != nullptr ? light->color : vsg::vec3(-9.f, -9.f, -9.f);
    }

    TEST(LightGain, aMorrowindLightsWeightNeverChangesItsColour)
    {
        const vsg::vec3 plain = colourOf(record(ESM::Light::Dynamic, 0.f));
        const vsg::vec3 heavy = colourOf(record(ESM::Light::Dynamic, 5.f));
        EXPECT_EQ(plain, heavy);
        EXPECT_GT(plain.z, 0.f);
    }

    TEST(LightGain, theGainFlagMultipliesTheColourByTheWeight)
    {
        const vsg::vec3 plain = colourOf(record(ESM::Light::Dynamic, 3.f));
        const vsg::vec3 gained = colourOf(record(ESM::Light::Dynamic | ESM::Light::NeutralGain, 3.f));
        EXPECT_FLOAT_EQ(gained.x, plain.x * 3.f);
        EXPECT_FLOAT_EQ(gained.y, plain.y * 3.f);
        EXPECT_FLOAT_EQ(gained.z, plain.z * 3.f);
    }

    TEST(LightGain, aNegativeLightKeepsItsSignAndGainsToo)
    {
        const vsg::vec3 plain = colourOf(record(ESM::Light::Dynamic | ESM::Light::Negative, 2.f));
        const vsg::vec3 gained
            = colourOf(record(ESM::Light::Dynamic | ESM::Light::Negative | ESM::Light::NeutralGain, 2.f));
        EXPECT_LT(plain.z, 0.f);
        EXPECT_FLOAT_EQ(gained.z, plain.z * 2.f);
    }

    TEST(LightGain, aWeightThatIsNotFiniteOrNotAboveZeroIsIgnored)
    {
        const vsg::vec3 plain = colourOf(record(ESM::Light::Dynamic, 1.f));
        for (const float bad :
            { 0.f, -2.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            EXPECT_EQ(colourOf(record(ESM::Light::Dynamic | ESM::Light::NeutralGain, bad)), plain) << bad;
    }

    TEST(LightGain, theFlagIsNotOneOfTheFileFormatsBits)
    {
        const int fileBits = ESM::Light::Dynamic | ESM::Light::Carry | ESM::Light::Negative | ESM::Light::Flicker
            | ESM::Light::Fire | ESM::Light::OffDefault | ESM::Light::FlickerSlow | ESM::Light::Pulse
            | ESM::Light::PulseSlow;
        EXPECT_EQ(ESM::Light::NeutralGain & fileBits, 0);
    }
}
