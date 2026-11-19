// Task 0355 stage g: MWAnim::addLight makes a View::TypedPointLight for a record with the neutral spot or shadow bits,
// and the plain vsg::PointLight it always made for every other record (all of Morrowind's).
#include <components/animation/autoplay.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/light.hpp>
#include <components/view/typedlight.hpp>

#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <typeinfo>

namespace
{
    ESM::Light record(int flags, float fov = 0.f)
    {
        ESM::Light light;
        light.blank();
        light.mData.mFlags = flags;
        light.mData.mRadius = 256;
        light.mData.mColor = 0x00402010;
        light.mNeutralSpotFov = fov;
        return light;
    }

    vsg::Node* make(const ESM::Light& r)
    {
        static vsg::ref_ptr<vsg::Group> parent;
        parent = vsg::Group::create();
        static Anim::Controllers controllers;
        controllers.clear();
        return MWAnim::addLight(parent.get(), parent.get(), r, controllers);
    }

    TEST(LightTypes, aRecordWithNoNeutralBitsIsTheOldPlainPointLight)
    {
        vsg::Node* node = make(record(ESM::Light::Dynamic | ESM::Light::Flicker));
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(typeid(*node), typeid(vsg::PointLight));
    }

    TEST(LightTypes, aSpotRecordGivesTheConeAndTheAxisAlongLocalX)
    {
        vsg::Node* node = make(record(ESM::Light::Dynamic | ESM::Light::NeutralSpot, 60.f));
        auto* typed = dynamic_cast<View::TypedPointLight*>(node);
        ASSERT_NE(typed, nullptr);
        EXPECT_NEAR(typed->spotCosHalf, std::cos(30.0 * 3.14159265358979 / 180.0), 1e-6);
        EXPECT_EQ(typed->spotAxis, vsg::vec3(1.f, 0.f, 0.f));
        EXPECT_EQ(typed->shadowRule, Render::PointShadow::Rule::Unspecified);
    }

    TEST(LightTypes, theShadowBitsGiveTheRuleAndNoCone)
    {
        auto* casts
            = dynamic_cast<View::TypedPointLight*>(make(record(ESM::Light::Dynamic | ESM::Light::NeutralShadowCasts)));
        ASSERT_NE(casts, nullptr);
        EXPECT_EQ(casts->shadowRule, Render::PointShadow::Rule::Casts);
        EXPECT_EQ(casts->spotCosHalf, 0.f);
        auto* never
            = dynamic_cast<View::TypedPointLight*>(make(record(ESM::Light::Dynamic | ESM::Light::NeutralShadowNever)));
        ASSERT_NE(never, nullptr);
        EXPECT_EQ(never->shadowRule, Render::PointShadow::Rule::Never);
    }

    TEST(LightTypes, aSpotBitWithAnAbsurdFovIsNoCone)
    {
        auto* typed = dynamic_cast<View::TypedPointLight*>(make(record(ESM::Light::NeutralSpot, 0.f)));
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->spotCosHalf, 0.f);
    }

    TEST(LightTypes, aTypedLightKeepsItsRadiusAndNegativeColour)
    {
        vsg::Node* node = make(record(ESM::Light::Dynamic | ESM::Light::NeutralSpot | ESM::Light::Negative, 60.f));
        auto* light = dynamic_cast<vsg::PointLight*>(node);
        ASSERT_NE(light, nullptr);
        EXPECT_FLOAT_EQ(light->intensity, 256.f);
        EXPECT_LT(light->color.x, 0.f);
    }
}
