#include <gtest/gtest.h>

#include <cmath>

#include <vsg/lighting/PointLight.h>

#include <components/view/collectlights.hpp>

namespace
{
    // Task 0025 (F2): the point lights of the linear view are converted when they are packed for the GPU.
    vsg::ref_ptr<vsg::PointLight> makeLight(const vsg::vec3& color, float intensity)
    {
        auto light = vsg::PointLight::create();
        light->color = color;
        light->intensity = intensity;
        light->position = vsg::dvec3(0.0, 0.0, 0.0);
        return light;
    }

    TEST(ViewCollectLightsTest, pack_keeps_the_colour_by_default)
    {
        View::CollectLights collect(4);
        auto light = makeLight(vsg::vec3(0.5f, 0.25f, 1.f), 300.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        const vsg::vec4 packed = collect.data()->at(0);
        EXPECT_FLOAT_EQ(packed.x, 0.5f);
        EXPECT_FLOAT_EQ(packed.y, 0.25f);
        EXPECT_FLOAT_EQ(packed.z, 1.f);
        EXPECT_FLOAT_EQ(packed.w, 300.f);
    }

    TEST(ViewCollectLightsTest, pack_converts_the_colour_to_linear_when_asked_and_not_the_intensity)
    {
        View::CollectLights collect(4);
        collect.setLinear(true);
        auto light = makeLight(vsg::vec3(0.5f, 0.25f, 1.f), 300.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        const vsg::vec4 packed = collect.data()->at(0);
        EXPECT_NEAR(packed.x, std::pow(0.5f, 2.2f), 1e-6f);
        EXPECT_NEAR(packed.y, std::pow(0.25f, 2.2f), 1e-6f);
        EXPECT_FLOAT_EQ(packed.z, 1.f);
        EXPECT_FLOAT_EQ(packed.w, 300.f);
    }

    TEST(ViewCollectLightsTest, pack_keeps_the_sign_of_a_darkness_light_in_the_linear_view)
    {
        View::CollectLights collect(4);
        collect.setLinear(true);
        auto light = makeLight(vsg::vec3(-0.5f, -0.5f, -0.5f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), light.get());
        collect.pack();
        const vsg::vec4 packed = collect.data()->at(0);
        EXPECT_FALSE(std::isnan(packed.x));
        EXPECT_NEAR(packed.x, -std::pow(0.5f, 2.2f), 1e-6f);
    }
}
