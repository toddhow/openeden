// Task 0355 stage j: View::CollectLights::pack fades the lights by their distance from the eye when a game pack's
// interior asks for it (setDistanceFade). Written before the code. Default (no fade set): every light packed as before.
#include <gtest/gtest.h>

#include <cmath>

#include <vsg/lighting/PointLight.h>

#include <components/view/collectlights.hpp>

namespace
{
    // A light whose eye-space position is (0, 0, -distance): the identity view matrix and the light at that place.
    vsg::ref_ptr<vsg::PointLight> lightAt(double distance, const vsg::vec3& color, float intensity)
    {
        auto light = vsg::PointLight::create();
        light->color = color;
        light->intensity = intensity;
        light->position = vsg::dvec3(0.0, 0.0, -distance);
        return light;
    }

    TEST(ViewCollectLightsFade, withNoFadeSetEveryLightIsPackedAsBefore)
    {
        View::CollectLights collect(4);
        auto light = lightAt(1e6, vsg::vec3(0.5f, 0.25f, 1.f), 300.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        const vsg::vec4 packed = collect.data()->at(0);
        EXPECT_FLOAT_EQ(packed.x, 0.5f);
        EXPECT_FLOAT_EQ(packed.y, 0.25f);
        EXPECT_FLOAT_EQ(packed.z, 1.f);
        EXPECT_FLOAT_EQ(packed.w, 300.f);
    }

    TEST(ViewCollectLightsFade, theColourScalesWithTheFactorAndTheRadiusAndPositionDoNot)
    {
        View::CollectLights collect(4);
        collect.setDistanceFade(4000.f, 5000.f);
        auto inside = lightAt(1000.0, vsg::vec3(0.5f, 0.5f, 0.5f), 300.f);
        auto half = lightAt(4500.0, vsg::vec3(0.5f, 0.25f, 1.f), 200.f);
        auto gone = lightAt(5000.0, vsg::vec3(1.f, 1.f, 1.f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), inside.get());
        collect.pointLights.emplace_back(vsg::dmat4(), half.get());
        collect.pointLights.emplace_back(vsg::dmat4(), gone.get());
        collect.pack();
        const auto& d = *collect.data();
        EXPECT_FLOAT_EQ(d.at(0).x, 0.5f); // inside the begin: untouched
        EXPECT_FLOAT_EQ(d.at(0).w, 300.f);
        EXPECT_FLOAT_EQ(d.at(2).x, 0.25f); // half way: half the colour
        EXPECT_FLOAT_EQ(d.at(2).y, 0.125f);
        EXPECT_FLOAT_EQ(d.at(2).z, 0.5f);
        EXPECT_FLOAT_EQ(d.at(2).w, 200.f); // the radius stays
        EXPECT_NEAR(d.at(3).z, -4500.f, 1e-2f); // the eye position stays
        EXPECT_FLOAT_EQ(d.at(4).x, 0.f); // at the end: none
        EXPECT_FLOAT_EQ(d.at(4).w, 100.f);
        EXPECT_FLOAT_EQ(d.at(1).w, 3.f); // the count of lights stays 3: nothing is dropped
    }

    TEST(ViewCollectLightsFade, aDarknessLightFadesTowardZeroAndKeepsItsSign)
    {
        View::CollectLights collect(4);
        collect.setDistanceFade(0.f, 1000.f);
        auto dark = lightAt(500.0, vsg::vec3(-0.5f, -0.5f, -0.5f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), dark.get());
        collect.pack();
        EXPECT_FLOAT_EQ(collect.data()->at(0).x, -0.25f);
    }

    TEST(ViewCollectLightsFade, theLinearViewFadesTheLinearColour)
    {
        View::CollectLights collect(4);
        collect.setLinear(true);
        collect.setDistanceFade(0.f, 1000.f);
        auto light = lightAt(500.0, vsg::vec3(0.5f, 0.5f, 0.5f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        // Half the LINEAR intensity: the factor multiplies after the conversion, not before.
        EXPECT_NEAR(collect.data()->at(0).x, 0.5f * std::pow(0.5f, 2.2f), 1e-6f);
    }

    TEST(ViewCollectLightsFade, aFadeCanBeSwitchedOffAgain)
    {
        View::CollectLights collect(4);
        collect.setDistanceFade(0.f, 1000.f);
        collect.setDistanceFade(0.f, 0.f);
        auto light = lightAt(5000.0, vsg::vec3(0.5f, 0.5f, 0.5f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        EXPECT_FLOAT_EQ(collect.data()->at(0).x, 0.5f);
    }
}
