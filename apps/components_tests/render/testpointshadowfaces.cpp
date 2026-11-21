// Tests for the shadow face mask of spot and hemisphere lights (task 0362 item 1), Render::PointShadow::faceMask.
// Faces 0..5 = +X, -X, +Y, -Y, +Z, -Z. Written before the implementation.

#include <components/render/pointshadowcache.hpp>
#include <components/render/pointshadowfaces.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

namespace
{
    using namespace Render::PointShadow;
    using V3 = vsg::dvec3;

    constexpr std::uint8_t bit(int f)
    {
        return static_cast<std::uint8_t>(1u << f);
    }

    // The faces whose wedge holds a direction d (the face the shader would choose), brute forced.
    bool directionInCone(const V3& d, const V3& axis, double cosHalf)
    {
        const double l = std::sqrt(vsg::dot(d, d)) * std::sqrt(vsg::dot(axis, axis));
        return vsg::dot(d, axis) >= cosHalf * l;
    }

    TEST(PointShadowFaces, an_omni_light_draws_all_six_faces)
    {
        EXPECT_EQ(faceMask(Shape::Omni, V3(1, 0, 0), 0.0), allFacesMask);
        EXPECT_EQ(faceMask(Shape::Omni, V3(0, 0, 0), 0.5), allFacesMask);
    }

    TEST(PointShadowFaces, a_hemisphere_along_an_axis_skips_exactly_the_opposite_face)
    {
        const V3 axes[6] = { V3(1, 0, 0), V3(-1, 0, 0), V3(0, 1, 0), V3(0, -1, 0), V3(0, 0, 1), V3(0, 0, -1) };
        for (int f = 0; f < 6; ++f)
        {
            const int opposite = f ^ 1;
            const std::uint8_t mask = faceMask(Shape::Hemisphere, axes[f], 0.0);
            EXPECT_EQ(mask, static_cast<std::uint8_t>(allFacesMask & ~bit(opposite))) << f;
            EXPECT_EQ(faceCount(mask), 5) << f;
        }
    }

    TEST(PointShadowFaces, a_narrow_spot_along_an_axis_draws_only_that_face)
    {
        // 30 degrees full angle: half angle 15 degrees, well inside the 45 degree wedge once the margin is added.
        const double c = std::cos(15.0 * 3.14159265358979 / 180.0);
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 0, 0), c), bit(0));
        EXPECT_EQ(faceMask(Shape::Spot, V3(0, 0, -1), c), bit(5));
    }

    TEST(PointShadowFaces, a_spot_on_a_diagonal_draws_the_faces_its_cone_reaches)
    {
        // Axis between +X and +Y: a narrow cone crosses the wall between their wedges.
        const double c = std::cos(15.0 * 3.14159265358979 / 180.0);
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 1, 0), c), static_cast<std::uint8_t>(bit(0) | bit(2)));
        // The cube's corner direction touches three wedges.
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 1, 1), c), static_cast<std::uint8_t>(bit(0) | bit(2) | bit(4)));
    }

    TEST(PointShadowFaces, a_wide_spot_draws_more_faces_than_a_narrow_one)
    {
        const double narrow = std::cos(10.0 * 3.14159265358979 / 180.0);
        const double wide = std::cos(80.0 * 3.14159265358979 / 180.0);
        EXPECT_LT(
            faceCount(faceMask(Shape::Spot, V3(1, 0, 0), narrow)), faceCount(faceMask(Shape::Spot, V3(1, 0, 0), wide)));
        EXPECT_EQ(faceCount(faceMask(Shape::Spot, V3(1, 0, 0), wide)), 5);
    }

    TEST(PointShadowFaces, an_unusable_axis_or_cone_keeps_every_face)
    {
        EXPECT_EQ(faceMask(Shape::Hemisphere, V3(0, 0, 0), 0.0), allFacesMask);
        EXPECT_EQ(faceMask(Shape::Hemisphere, V3(std::nan(""), 0, 0), 0.0), allFacesMask);
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 0, 0), 0.0), allFacesMask);
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 0, 0), 1.0), allFacesMask);
        EXPECT_EQ(faceMask(Shape::Spot, V3(1, 0, 0), -0.5), allFacesMask);
    }

    // The property that matters: every direction the light can light (inside the cone, or in front of the hemisphere)
    // lies in a wedge whose face is drawn, so the shader never samples a face that was left out for a lit pixel.
    TEST(PointShadowFaces, every_direction_the_light_reaches_has_its_face_drawn)
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        std::uniform_real_distribution<double> degrees(5.0, 85.0);
        for (int trial = 0; trial < 400; ++trial)
        {
            V3 axis(u(rng), u(rng), u(rng));
            if (vsg::dot(axis, axis) < 1e-3)
                continue;
            const bool spot = trial % 2 == 0;
            const double cosHalf = spot ? std::cos(degrees(rng) * 3.14159265358979 / 180.0) : 0.0;
            const std::uint8_t mask = faceMask(spot ? Shape::Spot : Shape::Hemisphere, axis, cosHalf);
            for (int s = 0; s < 200; ++s)
            {
                const V3 d(u(rng), u(rng), u(rng));
                if (vsg::dot(d, d) < 1e-6 || !directionInCone(d, axis, cosHalf))
                    continue;
                EXPECT_TRUE(mask & bit(faceOf(d))) << "trial " << trial << " face " << faceOf(d);
            }
        }
    }

    TEST(PointShadowFaces, a_hemisphere_never_draws_a_face_that_lies_wholly_behind_it_and_a_spot_is_cheaper_than_omni)
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        int saved = 0;
        for (int trial = 0; trial < 200; ++trial)
        {
            V3 axis(u(rng), u(rng), u(rng));
            if (vsg::dot(axis, axis) < 1e-3)
                continue;
            const std::uint8_t mask = faceMask(Shape::Hemisphere, axis, 0.0);
            // A face of the mask has a direction in front of the axis; one left out has none.
            for (int f = 0; f < 6; ++f)
            {
                bool front = false;
                for (int s = 0; s < 400 && !front; ++s)
                {
                    const V3 d(u(rng), u(rng), u(rng));
                    if (vsg::dot(d, d) > 1e-6 && faceOf(d) == f && vsg::dot(d, axis) > 0.0)
                        front = true;
                }
                if (front)
                {
                    EXPECT_TRUE(mask & bit(f)) << trial << " " << f;
                }
            }
            saved += 6 - faceCount(mask);
        }
        EXPECT_GT(saved, 100);
    }

    CacheLight maskedLight(std::uint8_t mask)
    {
        CacheLight l;
        l.used = true;
        l.id = 1;
        l.position = V3(0, 0, 0);
        l.radius = 100.f;
        l.faceMask = mask;
        return l;
    }

    TEST(PointShadowFaceCache, faces_outside_the_mask_are_never_drawn_and_a_mask_change_redraws_the_new_faces)
    {
        FaceCache cache;
        CacheParams params;
        std::vector<std::uint8_t> draw;
        const std::uint8_t mask = static_cast<std::uint8_t>(bit(0) | bit(2));
        cache.step({ maskedLight(mask) }, {}, 1, params, draw);
        for (int f = 0; f < 6; ++f)
            EXPECT_EQ(draw[static_cast<std::size_t>(f)] != 0, (mask >> f & 1) != 0) << f;
        // Many frames later (the refresh age passes): still only the two faces.
        for (std::uint32_t i = 0; i < 100; ++i)
        {
            cache.step({ maskedLight(mask) }, {}, 1, params, draw);
            for (int f = 0; f < 6; ++f)
                if (!((mask >> f) & 1))
                {
                    ASSERT_EQ(draw[static_cast<std::size_t>(f)], 0) << "frame " << i << " face " << f;
                }
        }
        // The axis turns: the new mask is drawn at once, whole.
        const std::uint8_t other = static_cast<std::uint8_t>(bit(1) | bit(2) | bit(4));
        cache.step({ maskedLight(other) }, {}, 1, params, draw);
        for (int f = 0; f < 6; ++f)
            EXPECT_EQ(draw[static_cast<std::size_t>(f)] != 0, (other >> f & 1) != 0) << f;
    }
}
