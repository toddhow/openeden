// Tests for Render::DepthMath (task 0066, stage a): the reversed depth buffer at the far distances of the whole-map
// view. Written from the spec alone, without reading the implementation.
//
// The facts under test: vsg::perspective stores depth 1 at the near plane and 0 at the far plane (reversed), float
// depth then resolves under one world unit out to 600,000 units, the conventional 0..1 mapping would not, and the
// shaders' reconstruction of the view distance from a stored depth with the float inverse projection stays usable.

#include <components/render/depthmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec4.h>

namespace
{
    using namespace Render::DepthMath;

    constexpr double nearClip = 1.0; // [Camera] near clip, files/settings-default.cfg
    constexpr double farClip = 600000.0; // about the diagonal of Morrowind's land (task 0066)

    constexpr double sampleDistances[]
        = { 1.0, 2.0, 10.0, 100.0, 1000.0, 7168.0, 40000.0, 100000.0, 300000.0, 600000.0 };

    TEST(RenderDepthMathTest, depth_is_one_at_the_near_plane_and_zero_at_the_far_plane)
    {
        EXPECT_NEAR(ndcDepth(nearClip, farClip, nearClip), 1.0, 1e-12);
        EXPECT_NEAR(ndcDepth(nearClip, farClip, farClip), 0.0, 1e-12);
    }

    TEST(RenderDepthMathTest, depth_falls_as_the_distance_grows)
    {
        double previous = 2.0;
        for (const double distance : sampleDistances)
        {
            const double depth = ndcDepth(nearClip, farClip, distance);
            EXPECT_LT(depth, previous) << "at distance " << distance;
            EXPECT_GE(depth, 0.0);
            EXPECT_LE(depth, 1.0);
            previous = depth;
        }
    }

    TEST(RenderDepthMathTest, matches_the_depth_vsg_perspective_produces)
    {
        const vsg::dmat4 projection = vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, nearClip, farClip);
        for (const double distance : sampleDistances)
        {
            const vsg::dvec4 clip = projection * vsg::dvec4(0.0, 0.0, -distance, 1.0);
            EXPECT_NEAR(clip.z / clip.w, ndcDepth(nearClip, farClip, distance), 1e-12) << "at distance " << distance;
        }
    }

    TEST(RenderDepthMathTest, distance_round_trips_through_the_depth_in_double_precision)
    {
        for (const double distance : sampleDistances)
        {
            const double depth = ndcDepth(nearClip, farClip, distance);
            EXPECT_NEAR(distanceFromNdcDepth(nearClip, farClip, depth), distance, distance * 1e-9)
                << "at distance " << distance;
        }
    }

    TEST(RenderDepthMathTest, float_depth_resolves_under_one_unit_out_to_the_far_plane)
    {
        for (const double distance : { 100000.0, 300000.0, 590000.0 })
        {
            const double resolution = depthResolution(nearClip, farClip, distance);
            EXPECT_GT(resolution, 0.0) << "at distance " << distance;
            EXPECT_LT(resolution, 1.0) << "at distance " << distance;
        }
    }

    TEST(RenderDepthMathTest, float_depth_is_finest_near_the_camera)
    {
        EXPECT_LT(depthResolution(nearClip, farClip, 10.0), 1e-3);
        EXPECT_LT(depthResolution(nearClip, farClip, 10.0), depthResolution(nearClip, farClip, 100000.0));
    }

    TEST(RenderDepthMathTest, the_conventional_mapping_would_not_resolve_far_surfaces)
    {
        // Why the buffer is reversed: with 0 at the near plane float depth steps are hundreds of units at 100,000.
        EXPECT_GT(forwardDepthResolution(nearClip, farClip, 100000.0), 100.0);
        EXPECT_GT(
            forwardDepthResolution(nearClip, farClip, 100000.0), 100.0 * depthResolution(nearClip, farClip, 100000.0));
    }

    TEST(RenderDepthMathTest, a_larger_near_clip_loosens_nothing_that_matters_at_the_far_plane)
    {
        // The default near clip is 1; mods that raise it must not make the far field worse.
        EXPECT_LT(depthResolution(10.0, farClip, 300000.0), 1.0);
    }

    // The shaders (files/shaders/lib/view/depthbuffer.glsl) reconstruct the view-space z from a stored float depth with
    // the float inverse of the projection (sceneData.invProjection = vsg::inverse(mat4)).
    TEST(RenderDepthMathTest, float_inverse_projection_reconstructs_the_view_distance)
    {
        const vsg::mat4 projection(vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, nearClip, farClip));
        const vsg::mat4 inverse = vsg::inverse(projection);
        for (const double distance : { 10.0, 1000.0, 7168.0, 40000.0, 100000.0, 300000.0 })
        {
            const float stored = static_cast<float>(ndcDepth(nearClip, farClip, distance));
            const float viewZ = viewZFromDepth(inverse, stored);
            // The stored float depth itself carries the resolution the earlier tests bound; allow ten times it, plus a
            // relative 1e-3 for the float inverse.
            const double tolerance = 10.0 * depthResolution(nearClip, farClip, distance) + distance * 1e-3;
            EXPECT_NEAR(-static_cast<double>(viewZ), distance, tolerance) << "at distance " << distance;
        }
    }

    TEST(RenderDepthMathTest, reconstruction_at_depth_zero_is_the_far_plane_or_beyond)
    {
        const vsg::mat4 projection(vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, nearClip, farClip));
        const vsg::mat4 inverse = vsg::inverse(projection);
        // A sky pixel keeps the cleared depth 0. Its view distance must not be nearer than the far plane's
        // neighbourhood (the shaders treat it as "very far"), and must not be NaN.
        const float viewZ = viewZFromDepth(inverse, 0.0f);
        EXPECT_TRUE(std::isfinite(viewZ) || std::isinf(viewZ));
        EXPECT_FALSE(std::isnan(viewZ));
        EXPECT_GE(std::fabs(static_cast<double>(viewZ)), farClip * 0.99);
    }

    TEST(RenderDepthMathTest, bad_input_is_handled)
    {
        EXPECT_FALSE(std::isnan(depthResolution(nearClip, farClip, nearClip)));
        EXPECT_EQ(depthResolution(0.0, farClip, 100.0), 0.0); // a zero near clip has no valid projection
        EXPECT_EQ(depthResolution(nearClip, nearClip, 100.0), 0.0); // far must be beyond near
        EXPECT_EQ(depthResolution(nearClip, farClip, 0.0), 0.0); // behind the near plane
    }
}

namespace
{
    // Prints the numbers task 0066's stage log quotes (near 1, far 600,000): the smallest distance step the float
    // depth buffer tells apart, reversed and conventional, and the float inverse projection's reconstruction error.
    TEST(RenderDepthMathTest, prints_the_precision_table)
    {
        const vsg::mat4 projection(vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, nearClip, farClip));
        const vsg::mat4 inverse = vsg::inverse(projection);
        for (const double distance : { 100.0, 7168.0, 40000.0, 100000.0, 300000.0, 590000.0 })
        {
            const float stored = static_cast<float>(ndcDepth(nearClip, farClip, distance));
            std::printf("distance %8.0f  reversed step %10.5f  conventional step %10.3f  float inverse error %9.4f\n",
                distance, depthResolution(nearClip, farClip, distance),
                forwardDepthResolution(nearClip, farClip, distance),
                std::fabs(-static_cast<double>(viewZFromDepth(inverse, stored)) - distance));
        }
        SUCCEED();
    }
}
