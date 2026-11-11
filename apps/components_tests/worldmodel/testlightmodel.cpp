// Task 0113, stage N3a-5: world_model.hpp 1.1 adds the light component and the cell lighting, at the END of their
// structs. These tests pin the additions (version, defaults, field order) and that a 1.0 consumer's view of the types is
// unchanged (a WorldObject and a WorldCell built without the new fields compare equal to a default one).
#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace openeden::sdk::game;

    TEST(WorldModelLight, theVersionIsOnePointOneAndTheMajorIsStillOne)
    {
        EXPECT_EQ(kWorldModelVersion.major, 1);
        EXPECT_EQ(kWorldModelVersion.minor, 1);
    }

    TEST(WorldModelLight, enumerationValuesAreFixed)
    {
        EXPECT_EQ(static_cast<int>(LightAnimation::None), 0);
        EXPECT_EQ(static_cast<int>(LightAnimation::Flicker), 1);
        EXPECT_EQ(static_cast<int>(LightAnimation::FlickerSlow), 2);
        EXPECT_EQ(static_cast<int>(LightAnimation::Pulse), 3);
        EXPECT_EQ(static_cast<int>(LightAnimation::PulseSlow), 4);
    }

    TEST(WorldModelLight, defaultsAreWhatAConsumerAssumes)
    {
        const LightComponent l;
        EXPECT_EQ(l.radius, 0.0);
        EXPECT_EQ(l.red, 255);
        EXPECT_EQ(l.green, 255);
        EXPECT_EQ(l.blue, 255);
        EXPECT_EQ(l.fade, 1.0);
        EXPECT_EQ(l.animation, LightAnimation::None);
        EXPECT_FALSE(l.negative);
        EXPECT_FALSE(l.offByDefault);
        EXPECT_EQ(l.flags, 0u);
        const CellLighting c;
        EXPECT_EQ(c.ambient, (std::array<std::uint8_t, 3>{ 0, 0, 0 }));
        EXPECT_EQ(c.fogNear, 0.0);
        EXPECT_EQ(c.fogFar, 0.0);
        const WorldObject o;
        EXPECT_FALSE(o.light.has_value());
        const WorldCell cell;
        EXPECT_FALSE(cell.lighting.has_value());
    }

    TEST(WorldModelLight, fieldOrderIsPinnedByDesignatedInitializers)
    {
        const LightComponent light{ .radius = 300.0,
            .red = 1,
            .green = 2,
            .blue = 3,
            .fade = 1.5,
            .animation = LightAnimation::Pulse,
            .negative = true,
            .offByDefault = true,
            .flags = 0x85u };
        const CellLighting lighting{ .ambient = { 1, 2, 3 },
            .directional = { 4, 5, 6 },
            .fogColor = { 7, 8, 9 },
            .fogNear = 10.0,
            .fogFar = 20.0 };
        WorldObject object;
        object.light = light;
        WorldCell cell;
        cell.lighting = lighting;
        cell.objects.push_back(object);
        EXPECT_EQ(cell.objects[0].light->flags, 0x85u);
        EXPECT_EQ(cell.lighting->fogFar, 20.0);
    }

    TEST(WorldModelLight, aLightAndALightingTakePartInEquality)
    {
        WorldObject a, b;
        EXPECT_EQ(a, b);
        a.light = LightComponent{};
        EXPECT_NE(a, b);
        b.light = LightComponent{};
        EXPECT_EQ(a, b);
        b.light->radius = 1.0;
        EXPECT_NE(a, b);
        WorldCell c, d;
        c.lighting = CellLighting{};
        EXPECT_NE(c, d);
        d.lighting = CellLighting{};
        EXPECT_EQ(c, d);
    }
}
