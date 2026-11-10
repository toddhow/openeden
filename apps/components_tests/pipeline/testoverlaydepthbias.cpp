// Task 0075 (the lava flip): the TAA overlay pass draws blended things with the unjittered camera against the jittered
// depth, so their pipelines get a slope-scaled depth bias of at least the jitter's size. Pure decisions only.
#include <gtest/gtest.h>

#include <components/pipeline/gbufferblend.hpp>
#include <components/render/taamath.hpp>

namespace
{
    Pipeline::Options overlayKey()
    {
        Pipeline::Options key;
        key.blend = true;
        key.depthTest = true;
        key.depthWrite = false;
        return key;
    }

    TEST(RenderTaaOverlayBias, slope_is_zero_without_jitter)
    {
        EXPECT_EQ(Render::Taa::overlayDepthBiasSlope(0.f), 0.f);
        EXPECT_EQ(Render::Taa::overlayDepthBiasSlope(-1.f), 0.f);
    }

    TEST(RenderTaaOverlayBias, slope_covers_the_largest_shift_the_jitter_makes)
    {
        // The two axes together move a position by at most `scale` pixels along the steepest axis.
        for (float scale = 0.05f; scale <= 1.f; scale += 0.05f)
            EXPECT_GE(Render::Taa::overlayDepthBiasSlope(scale), scale) << scale;
    }

    TEST(RenderTaaOverlayBias, slope_follows_the_jitter_scale_and_stops_at_one)
    {
        EXPECT_FLOAT_EQ(Render::Taa::overlayDepthBiasSlope(1.f), 1.25f);
        EXPECT_FLOAT_EQ(Render::Taa::overlayDepthBiasSlope(0.5f), 0.625f);
        EXPECT_FLOAT_EQ(Render::Taa::overlayDepthBiasSlope(4.f), 1.25f);
    }

    TEST(PipelineOverlayDepthBias, a_blended_depth_tested_draw_without_depth_write_gets_it)
    {
        EXPECT_TRUE(Pipeline::usesOverlayDepthBias(overlayKey(), 1.25f));
    }

    TEST(PipelineOverlayDepthBias, a_zero_slope_leaves_every_pipeline_as_it_was)
    {
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(overlayKey(), 0.f));
    }

    TEST(PipelineOverlayDepthBias, opaque_draws_do_not)
    {
        Pipeline::Options opaque = overlayKey();
        opaque.blend = false;
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(opaque, 1.25f));
        opaque.depthWrite = true;
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(opaque, 1.25f));
    }

    TEST(PipelineOverlayDepthBias, a_blended_draw_that_writes_depth_gets_it_too)
    {
        // Found in the game: the Red Mountain lava is blended with depth writes (colour mode 0).
        Pipeline::Options key = overlayKey();
        key.depthWrite = true;
        EXPECT_TRUE(Pipeline::usesOverlayDepthBias(key, 1.25f));
    }

    TEST(PipelineOverlayDepthBias, draws_without_a_depth_test_do_not)
    {
        Pipeline::Options key = overlayKey();
        key.depthTest = false;
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(key, 1.25f));
    }

    TEST(PipelineOverlayDepthBias, other_shaders_and_pipelines_with_a_bias_of_their_own_do_not)
    {
        Pipeline::Options water = overlayKey();
        water.shader.path = "water";
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(water, 1.25f));
        Pipeline::Options decal = overlayKey();
        decal.depthBias = true;
        EXPECT_FALSE(Pipeline::usesOverlayDepthBias(decal, 1.25f));
    }
}
