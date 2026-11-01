#include <gtest/gtest.h>

#include <components/render/probegpu.hpp>

namespace Render::Gi
{
    namespace
    {
        Volume smallVolume()
        {
            Volume v;
            v.origin = vsg::vec3(-10.f, 20.f, 30.f);
            v.spacing = 64.f;
            v.size = { 2, 1, 1 };
            for (int p = 0; p < 2; ++p)
            {
                Sh9 sh;
                for (int k = 0; k < shCoefficients; ++k)
                    sh.c[k] = vsg::vec3(static_cast<float>(p * 100 + k), static_cast<float>(p * 100 + k) + 0.25f, -1.f);
                v.probes.push_back(sh);
            }
            return v;
        }
    }

    TEST(RenderProbeGpuTest, a_packed_volume_has_the_header_then_nine_vec4_per_probe)
    {
        const auto packed = packVolume(smallVolume(), vsg::vec3(0.1f, 0.2f, 0.3f), 1.5f);
        ASSERT_EQ(packed.size(), packedSize(2));
        EXPECT_EQ(packed.size(), headerVec4s + 2 * vec4sPerProbe);
    }

    TEST(RenderProbeGpuTest, the_header_holds_origin_spacing_size_enabled_bias_strength_and_baked_ambient)
    {
        const auto packed = packVolume(smallVolume(), vsg::vec3(0.1f, 0.2f, 0.3f), 1.5f);
        EXPECT_EQ(packed[0], vsg::vec4(-10.f, 20.f, 30.f, 64.f));
        EXPECT_EQ(packed[1], vsg::vec4(2.f, 1.f, 1.f, 1.f));
        EXPECT_EQ(packed[2], vsg::vec4(normalBiasFraction, 1.5f, 0.f, 0.f));
        EXPECT_EQ(packed[3], vsg::vec4(0.1f, 0.2f, 0.3f, 0.f));
    }

    TEST(RenderProbeGpuTest, probe_i_coefficient_k_is_at_four_plus_nine_i_plus_k)
    {
        const auto packed = packVolume(smallVolume(), vsg::vec3(0.f, 0.f, 0.f), 1.f);
        EXPECT_EQ(packed[headerVec4s + 0 * vec4sPerProbe + 3], vsg::vec4(3.f, 3.25f, -1.f, 0.f));
        EXPECT_EQ(packed[headerVec4s + 1 * vec4sPerProbe + 8], vsg::vec4(108.f, 108.25f, -1.f, 0.f));
    }

    TEST(RenderProbeGpuTest, the_buffer_capacity_holds_the_largest_volume)
    {
        EXPECT_EQ(packedSize(maxVolumeProbes), 4u + 9u * 8192u);
    }
}
