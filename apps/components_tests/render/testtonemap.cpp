#include <gtest/gtest.h>

#include <components/render/tonemap.hpp>

namespace
{
    using namespace Render;

    TEST(RenderTonemapTest, pushConstants_default_is_neutral)
    {
        const vsg::vec4 push = Tonemap::pushConstants(Tonemap::Parameters{});
        EXPECT_FLOAT_EQ(push.x, 1.f); // exposure 0 EV
        EXPECT_FLOAT_EQ(push.y, 0.f); // vanilla
        EXPECT_FLOAT_EQ(push.z, 0.f); // no encode
        EXPECT_FLOAT_EQ(push.w, 0.f); // no bloom
    }

    TEST(RenderTonemapTest, pushConstants_carry_every_parameter)
    {
        Tonemap::Parameters parameters;
        parameters.curve = Hdr::Tonemap::Aces;
        parameters.exposureStops = 2.f;
        parameters.encode = true;
        parameters.bloomStrength = 0.25f;
        const vsg::vec4 push = Tonemap::pushConstants(parameters);
        EXPECT_FLOAT_EQ(push.x, 4.f);
        EXPECT_FLOAT_EQ(push.y, 2.f);
        EXPECT_FLOAT_EQ(push.z, 1.f);
        EXPECT_FLOAT_EQ(push.w, 0.25f);
    }

    TEST(RenderTonemapTest, pushConstants_curve_indices_are_distinct_and_ordered)
    {
        Tonemap::Parameters parameters;
        parameters.curve = Hdr::Tonemap::Vanilla;
        const float vanilla = Tonemap::pushConstants(parameters).y;
        parameters.curve = Hdr::Tonemap::Reinhard;
        const float reinhard = Tonemap::pushConstants(parameters).y;
        parameters.curve = Hdr::Tonemap::Aces;
        const float aces = Tonemap::pushConstants(parameters).y;
        // The shader tests y > 0.5 and y > 1.5.
        EXPECT_LT(vanilla, 0.5f);
        EXPECT_GT(reinhard, 0.5f);
        EXPECT_LT(reinhard, 1.5f);
        EXPECT_GT(aces, 1.5f);
    }
}
