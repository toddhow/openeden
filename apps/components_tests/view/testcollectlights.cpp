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

namespace
{
    TEST(ViewCollectLightsTest, pack_converts_every_light_and_counts_them)
    {
        View::CollectLights collect(4);
        collect.setLinear(true);
        auto first = makeLight(vsg::vec3(0.5f, 0.5f, 0.5f), 100.f);
        auto second = makeLight(vsg::vec3(0.25f, 0.25f, 0.25f), 200.f);
        collect.pointLights.emplace_back(vsg::dmat4(), first.get());
        collect.pointLights.emplace_back(vsg::dmat4(), second.get());
        collect.pack();
        // Two vec4 per light: colour and intensity, then position; the count is in the first light's position.w.
        EXPECT_NEAR(collect.data()->at(0).x, std::pow(0.5f, 2.2f), 1e-6f);
        EXPECT_NEAR(collect.data()->at(2).x, std::pow(0.25f, 2.2f), 1e-6f);
        EXPECT_FLOAT_EQ(collect.data()->at(2).w, 200.f);
        EXPECT_FLOAT_EQ(collect.data()->at(1).w, 2.f);
    }
}

namespace
{
    // Task 0077: the shadow entries are written in the loop that writes the lights, so an entry follows its light when
    // the list changes from one traversal to the next.
    TEST(ViewCollectLightsTest, a_shadow_entry_follows_its_light_when_another_light_is_inserted_before_it)
    {
        const std::size_t entryOffset = 2;
        auto shadowData = vsg::vec4Array::create(entryOffset + 4 + 8);
        for (auto& v : *shadowData)
            v = vsg::vec4(-7.f, -7.f, -7.f, -7.f);
        View::CollectLights collect(4);
        collect.setShadowTarget(shadowData, entryOffset);
        auto a = makeLight(vsg::vec3(1.f, 1.f, 1.f), 100.f);
        auto b = makeLight(vsg::vec3(1.f, 1.f, 1.f), 120.f);
        auto inserted = makeLight(vsg::vec3(1.f, 1.f, 1.f), 140.f);
        collect.setShadowEntries({ { reinterpret_cast<std::uintptr_t>(a.get()), 2, 0.5f } });

        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pointLights.emplace_back(vsg::dmat4(), b.get());
        collect.pack();
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 0).x, 2.f);
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 0).y, 0.5f);
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 1).x, -1.f);

        // The next traversal sees another light first: a is now the second, and must keep its slot there.
        collect.pointLights.clear();
        collect.pointLights.emplace_back(vsg::dmat4(), inserted.get());
        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pointLights.emplace_back(vsg::dmat4(), b.get());
        collect.pack();
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 0).x, -1.f);
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 1).x, 2.f);
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 1).y, 0.5f);
        EXPECT_FLOAT_EQ(shadowData->at(entryOffset + 2).x, -1.f);
    }

    TEST(ViewCollectLightsTest, the_snapshots_hold_what_a_later_update_needs_so_that_no_node_is_read_again)
    {
        auto shadowData = vsg::vec4Array::create(2 + 4 + 8);
        View::CollectLights collect(4);
        collect.setShadowTarget(shadowData, 2);
        auto a = makeLight(vsg::vec3(0.5f, 0.25f, 1.f), 300.f);
        vsg::dmat4 translate;
        translate[3][0] = 10.0;
        translate[3][1] = 20.0;
        translate[3][2] = 30.0;
        collect.pointLights.emplace_back(translate, a.get());
        collect.pack();
        ASSERT_EQ(collect.snapshots().size(), 1u);
        const View::LightSnapshot& snapshot = collect.snapshots()[0];
        EXPECT_EQ(snapshot.id, reinterpret_cast<std::uintptr_t>(a.get()));
        EXPECT_DOUBLE_EQ(snapshot.eyePosition.x, 10.0);
        EXPECT_DOUBLE_EQ(snapshot.eyePosition.y, 20.0);
        EXPECT_DOUBLE_EQ(snapshot.eyePosition.z, 30.0);
        EXPECT_FLOAT_EQ(snapshot.intensity, 300.f);
        EXPECT_FLOAT_EQ(snapshot.color.y, 0.25f);
    }

    TEST(ViewCollectLightsTest, without_a_shadow_target_nothing_is_kept)
    {
        View::CollectLights collect(4);
        auto a = makeLight(vsg::vec3(1.f, 1.f, 1.f), 100.f);
        collect.pointLights.emplace_back(vsg::dmat4(), a.get());
        collect.pack();
        EXPECT_TRUE(collect.snapshots().empty());
    }
}
