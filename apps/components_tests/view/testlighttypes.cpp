// Task 0355 stage g: spot lights and shadow rules through the light buffer. A View::TypedPointLight carries a spot cone
// and the game's shadow rule; CollectLights::pack() writes each light's spot vec4 after the lights (the layout is in
// components/view/lightdata.hpp) and the shadow rule into the snapshots; a plain vsg::PointLight is a light with no
// cone and no rule, exactly as before.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <vsg/core/Array.h>
#include <vsg/maths/mat4.h>

#include <components/render/pointshadowselect.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/lightdata.hpp>
#include <components/view/typedlight.hpp>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    vsg::ref_ptr<View::TypedPointLight> spot(double fovDegrees, vsg::vec3 axis = { 1.f, 0.f, 0.f })
    {
        auto light = vsg::ref_ptr<View::TypedPointLight>(new View::TypedPointLight);
        light->color = vsg::vec3(1.f, 0.5f, 0.25f);
        light->intensity = 300.f;
        light->spotCosHalf = View::spotConeCos(fovDegrees);
        light->spotAxis = axis;
        return light;
    }

    vsg::ref_ptr<vsg::PointLight> plain()
    {
        auto light = vsg::PointLight::create();
        light->color = vsg::vec3(1.f, 1.f, 1.f);
        light->intensity = 100.f;
        return light;
    }

    TEST(ViewLightTypes, theConeCosIsTheCosOfHalfTheFovAndZeroForNonsense)
    {
        EXPECT_NEAR(View::spotConeCos(90.0), std::cos(pi / 4.0), 1e-6);
        EXPECT_NEAR(View::spotConeCos(30.0), std::cos(pi / 12.0), 1e-6);
        EXPECT_EQ(View::spotConeCos(0.0), 0.f);
        EXPECT_EQ(View::spotConeCos(171.0), 0.f);
        EXPECT_EQ(View::spotConeCos(std::numeric_limits<double>::quiet_NaN()), 0.f);
    }

    TEST(ViewLightTypes, theBufferLayoutHoldsTheSpotRegionFromTwoLightsUp)
    {
        EXPECT_EQ(View::lightVec4Count(1), 2u);
        EXPECT_EQ(View::lightVec4Count(2), 6u); // 2 lights + one struct of two spot vec4
        EXPECT_EQ(View::lightVec4Count(4), 12u);
        EXPECT_EQ(View::lightVec4Count(5), 16u); // 5 + 3 structs
        EXPECT_EQ(View::lightStructCount(8), 12u);
    }

    TEST(ViewLightTypes, maxLightsOfReadsTheMarkerAndFallsBackToHalfTheSize)
    {
        View::CollectLights collect(5);
        EXPECT_EQ(collect.maxLights(), 5u);
        EXPECT_EQ(View::maxLightsOf(*collect.data()), 5u);
        // A buffer of any other origin: two vec4 a light, no marker.
        auto other = vsg::vec4Array::create(16);
        for (auto& v : *other)
            v = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        EXPECT_EQ(View::maxLightsOf(*other), 8u);
        EXPECT_EQ(View::maxLightsOf(*vsg::vec4Array::create(2)), 1u);
        // A marker that does not fit the size is not believed.
        other->at(3).w = 3.f;
        EXPECT_EQ(View::maxLightsOf(*other), 8u);
    }

    TEST(ViewLightTypes, aSpotLightsAxisIsPackedInEyeSpaceWithItsConeCos)
    {
        View::CollectLights collect(4);
        auto a = plain();
        auto b = spot(90.0);
        // 90 degrees about Z: local +X becomes eye +Y (column major: the first column is the image of +X).
        vsg::dmat4 rotate(0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pointLights.emplace_back(rotate, b.get());
        collect.pack();
        const vsg::vec4 s0 = collect.data()->at(8 + 0);
        const vsg::vec4 s1 = collect.data()->at(8 + 1);
        EXPECT_EQ(s0, vsg::vec4(0.f, 0.f, 0.f, 0.f)); // a plain light is no spot
        EXPECT_NEAR(s1.x, 0.f, 1e-6f);
        EXPECT_NEAR(s1.y, 1.f, 1e-6f);
        EXPECT_NEAR(s1.z, 0.f, 1e-6f);
        EXPECT_NEAR(s1.w, std::cos(pi / 4.0), 1e-6f);
    }

    TEST(ViewLightTypes, theLightsAndTheCountAreWhereTheyWereAndTheMarkerSurvivesThePack)
    {
        View::CollectLights collect(4);
        auto a = plain();
        auto b = spot(60.0);
        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pointLights.emplace_back(vsg::dmat4(), b.get());
        collect.pack();
        EXPECT_FLOAT_EQ(collect.data()->at(0).w, 100.f);
        EXPECT_FLOAT_EQ(collect.data()->at(2).w, 300.f);
        EXPECT_FLOAT_EQ(collect.data()->at(1).w, 2.f); // the count
        EXPECT_FLOAT_EQ(collect.data()->at(3).w, 4.f); // the marker of the spot region
        EXPECT_EQ(collect.data()->size(), View::lightVec4Count(4));
    }

    TEST(ViewLightTypes, aLaterPackLeavesNoStaleSpotBehind)
    {
        View::CollectLights collect(4);
        auto s = spot(60.0);
        collect.pointLights.emplace_back(vsg::dmat4(), s.get());
        collect.pack();
        EXPECT_GT(collect.data()->at(8).w, 0.f);
        auto p = plain();
        collect.pointLights.clear();
        collect.pointLights.emplace_back(vsg::dmat4(), p.get());
        collect.pack();
        EXPECT_EQ(collect.data()->at(8), vsg::vec4(0.f, 0.f, 0.f, 0.f));
    }

    TEST(ViewLightTypes, aSingleLightBufferHasNoSpotRegionAndStillPacks)
    {
        View::CollectLights collect(1);
        auto s = spot(60.0);
        collect.pointLights.emplace_back(vsg::dmat4(), s.get());
        collect.pack();
        EXPECT_EQ(collect.data()->size(), 2u);
        EXPECT_FLOAT_EQ(collect.data()->at(0).w, 300.f);
    }

    TEST(ViewLightTypes, theSnapshotsCarryTheShadowRule)
    {
        auto shadowData = vsg::vec4Array::create(2 + 4 + 8);
        View::CollectLights collect(4);
        collect.setShadowTarget(shadowData, 2);
        auto a = plain();
        auto b = spot(60.0);
        b->shadowRule = Render::PointShadow::Rule::Never;
        auto c = spot(60.0);
        c->shadowRule = Render::PointShadow::Rule::Casts;
        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pointLights.emplace_back(vsg::dmat4(), b.get());
        collect.pointLights.emplace_back(vsg::dmat4(), c.get());
        collect.pack();
        ASSERT_EQ(collect.snapshots().size(), 3u);
        EXPECT_EQ(collect.snapshots()[0].shadowRule, Render::PointShadow::Rule::Unspecified);
        EXPECT_EQ(collect.snapshots()[1].shadowRule, Render::PointShadow::Rule::Never);
        EXPECT_EQ(collect.snapshots()[2].shadowRule, Render::PointShadow::Rule::Casts);
    }

    TEST(ViewLightTypes, aLightMayCastUnlessItsRecordSaysNeverOrItIsDarkOrColourless)
    {
        using Render::PointShadow::lightMayCast;
        using Render::PointShadow::Rule;
        EXPECT_TRUE(lightMayCast(1.f, 0.f, 0.f, Rule::Unspecified));
        EXPECT_TRUE(lightMayCast(1.f, 0.f, 0.f, Rule::Casts));
        EXPECT_FALSE(lightMayCast(1.f, 1.f, 1.f, Rule::Never));
        EXPECT_FALSE(lightMayCast(-1.f, -1.f, -1.f, Rule::Casts));
        EXPECT_FALSE(lightMayCast(0.f, 0.f, 0.f, Rule::Unspecified));
        EXPECT_FALSE(lightMayCast(1.f, -0.1f, 0.f, Rule::Unspecified));
    }
}
