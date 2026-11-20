// Task 0361b: the vertical pair of a Skyrim SE ambient cube. The records store the colour a surface facing DOWN takes
// before the one a surface facing UP takes (the sky colour is last); Render::AmbientCube::fromStoredOrder puts them in
// the order `sample` and the shaders read. Before it, every up-facing surface (the ground) took the dark ground colour
// and shadowed ground drew black.
#include <components/render/ambientcube.hpp>

#include <gtest/gtest.h>

namespace
{
    using Render::AmbientCube::Cube;

    // SkyrimClear's day record as stored (xEdit order of the fields, linear 0..1 of the 8 bit values).
    Cube skyrimClearDay()
    {
        const auto c = [](float r, float g, float b) { return vsg::vec3(r / 255.f, g / 255.f, b / 255.f); };
        return { c(108, 146, 164), c(94, 134, 142), c(101, 145, 154), c(100, 135, 155), c(77, 129, 136),
            c(191, 214, 221) };
    }

    TEST(AmbientCubeStoredOrder, onlyTheVerticalPairIsSwapped)
    {
        const Cube stored = skyrimClearDay();
        const Cube ordered = Render::AmbientCube::fromStoredOrder(stored);
        for (std::size_t i = 0; i < 4; ++i)
            EXPECT_EQ(ordered[i], stored[i]) << i;
        EXPECT_EQ(ordered[4], stored[5]);
        EXPECT_EQ(ordered[5], stored[4]);
    }

    TEST(AmbientCubeStoredOrder, aSurfaceFacingUpTakesTheStoredSkyColour)
    {
        const Cube stored = skyrimClearDay();
        const vsg::vec3 up = Render::AmbientCube::sample(Render::AmbientCube::fromStoredOrder(stored), vsg::vec3(0, 0, 1));
        EXPECT_EQ(up, stored[5]);
        const vsg::vec3 down
            = Render::AmbientCube::sample(Render::AmbientCube::fromStoredOrder(stored), vsg::vec3(0, 0, -1));
        EXPECT_EQ(down, stored[4]);
    }

    TEST(AmbientCubeStoredOrder, theSkyIsTheBrightestFaceAndTheGroundTheDarkestOfADay)
    {
        const Cube ordered = Render::AmbientCube::fromStoredOrder(skyrimClearDay());
        const float up = Render::AmbientCube::luminance(ordered[4]);
        const float down = Render::AmbientCube::luminance(ordered[5]);
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float side = Render::AmbientCube::luminance(ordered[i]);
            EXPECT_GT(up, side) << i;
            EXPECT_LT(down, side) << i;
        }
    }

    TEST(AmbientCubeStoredOrder, aShadowedGroundIsLitByTheSkyAndNotByTheGroundBounce)
    {
        const Cube stored = skyrimClearDay();
        const vsg::vec3 flat(0.f, 0.f, 0.f);
        const vsg::vec3 lit = Render::AmbientCube::ambientFor(
            Render::AmbientCube::fromStoredOrder(stored), vsg::vec3(0, 0, 1), flat, flat);
        EXPECT_GT(lit.x, 2.0f * stored[4].x); // the red channel was a third of the sky's
    }

    TEST(AmbientCubeStoredOrder, aTiltedSlopeBlendsTheSkyWithTheSidesBySquaredNormal)
    {
        const Cube ordered = Render::AmbientCube::fromStoredOrder(skyrimClearDay());
        const vsg::vec3 s = Render::AmbientCube::sample(ordered, vsg::vec3(1, 0, 1)); // 45 degrees: half and half
        const vsg::vec3 expected = ordered[0] * 0.5f + ordered[4] * 0.5f;
        EXPECT_NEAR(s.x, expected.x, 1e-5f);
        EXPECT_NEAR(s.y, expected.y, 1e-5f);
        EXPECT_NEAR(s.z, expected.z, 1e-5f);
    }
}
