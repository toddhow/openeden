// Task 0355, stage k: a game pack's directional ambient in the weather manager's inputs: the conversion from the SDK's
// description and the blend of two weathers' cubes in a transition. Expectations are written out by hand.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

#include "apps/openmw/mwworld/neutralweather.hpp"

namespace
{
    namespace sdk = openeden::sdk::game;
    using Cube = Render::AmbientCube::Cube;

    sdk::WeatherDescription withCube()
    {
        sdk::WeatherDescription d;
        d.id = 0x30;
        d.editorId = "CubeWeather";
        d.hasAmbientCube = true;
        for (std::size_t face = 0; face < 6; ++face)
        {
            const float f = static_cast<float>(face);
            d.ambientCube.sunrise[face] = { 0.01f * f, 0.02f, 0.03f };
            d.ambientCube.day[face] = { 0.1f + 0.01f * f, 0.2f, 0.3f };
            d.ambientCube.sunset[face] = { 0.5f, 0.5f + 0.01f * f, 0.5f };
            d.ambientCube.night[face] = { 0.7f, 0.8f, 0.9f - 0.01f * f };
        }
        return d;
    }

    TEST(NeutralWeatherAmbientCube, theDescriptionBecomesSixFacesOfFourTimesOfDay)
    {
        const MWWorld::WeatherEntry entry = MWWorld::weatherEntryFromDescription(withCube(), 8);
        ASSERT_TRUE(entry.params.ambientCube.has_value());
        const MWWorld::AmbientCubeParams& cube = *entry.params.ambientCube;
        EXPECT_FLOAT_EQ(cube[3][0].x, 0.03f); // face -Y, sunrise
        EXPECT_FLOAT_EQ(cube[0][1].x, 0.1f); // face +X, day
        EXPECT_FLOAT_EQ(cube[5][1].x, 0.15f);
        EXPECT_FLOAT_EQ(cube[2][2].y, 0.52f); // face +Y, sunset
        EXPECT_FLOAT_EQ(cube[4][3].z, 0.86f); // face +Z, night
        EXPECT_FLOAT_EQ(cube[1][3].x, 0.7f);
    }

    TEST(NeutralWeatherAmbientCube, aDescriptionWithoutACubeGivesNone)
    {
        sdk::WeatherDescription d;
        d.id = 0x31;
        EXPECT_FALSE(MWWorld::weatherEntryFromDescription(d, 8).params.ambientCube.has_value());
    }

    Cube cubeOf(float v)
    {
        Cube c;
        for (std::size_t i = 0; i < 6; ++i)
            c[i] = vsg::vec3(v + 0.1f * static_cast<float>(i), v, 1.f - v);
        return c;
    }

    TEST(NeutralWeatherAmbientCube, theEndsOfATransitionAreTheTwoWeathers)
    {
        const Cube a = cubeOf(0.2f), b = cubeOf(0.6f);
        const vsg::vec3 flat(0.4f, 0.4f, 0.4f);
        Cube out;
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flat, b, flat, 0.f, out));
        EXPECT_FLOAT_EQ(out[3].x, a[3].x);
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flat, b, flat, 1.f, out));
        EXPECT_FLOAT_EQ(out[3].x, b[3].x);
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flat, b, flat, 0.5f, out));
        EXPECT_FLOAT_EQ(out[3].x, 0.5f * (a[3].x + b[3].x));
        EXPECT_FLOAT_EQ(out[5].z, 0.5f * (a[5].z + b[5].z));
    }

    TEST(NeutralWeatherAmbientCube, aWeatherWithNoCubeTakesItsFlatAmbientOnAllSixFaces)
    {
        const Cube a = cubeOf(0.2f);
        const vsg::vec3 flatA(0.3f, 0.3f, 0.3f), flatB(0.9f, 0.5f, 0.1f);
        Cube out;
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flatA, std::nullopt, flatB, 1.f, out));
        for (std::size_t i = 0; i < 6; ++i)
        {
            EXPECT_FLOAT_EQ(out[i].x, 0.9f);
            EXPECT_FLOAT_EQ(out[i].y, 0.5f);
            EXPECT_FLOAT_EQ(out[i].z, 0.1f);
        }
        ASSERT_TRUE(MWWorld::blendAmbientCubes(std::nullopt, flatA, a, flatB, 0.5f, out));
        EXPECT_FLOAT_EQ(out[2].x, 0.5f * (0.3f + a[2].x));
    }

    TEST(NeutralWeatherAmbientCube, neitherWeatherHavingACubeGivesNoCube)
    {
        Cube out = cubeOf(0.9f);
        EXPECT_FALSE(MWWorld::blendAmbientCubes(
            std::nullopt, vsg::vec3(0.f, 0.f, 0.f), std::nullopt, vsg::vec3(1.f, 1.f, 1.f), 0.5f, out));
    }

    TEST(NeutralWeatherAmbientCube, aBadFactorIsClamped)
    {
        const Cube a = cubeOf(0.2f), b = cubeOf(0.6f);
        const vsg::vec3 flat(0.4f, 0.4f, 0.4f);
        Cube out;
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flat, b, flat, 7.f, out));
        EXPECT_FLOAT_EQ(out[0].x, b[0].x);
        ASSERT_TRUE(MWWorld::blendAmbientCubes(a, flat, b, flat, std::numeric_limits<float>::quiet_NaN(), out));
        EXPECT_FLOAT_EQ(out[0].x, a[0].x);
    }
}
