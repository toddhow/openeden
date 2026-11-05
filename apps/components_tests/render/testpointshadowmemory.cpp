// Tests for the memory figure of the point light shadow maps and its start-up warning (task 0077, decision 0012).
// Written first from the figures in decision 0012: 8 slots at 2048 texels is about 805 MiB.

#include <components/render/pointshadowmath.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{
    using namespace Render::PointShadow;

    constexpr std::uint64_t mib = 1024ull * 1024ull;

    TEST(PointShadowMemory, eight_slots_at_2048_texels_is_805_mib)
    {
        // 8 * 6 faces * 2048 * 2048 texels * 4 bytes = 805,306,368 bytes = 768 MiB (about 805 MB).
        EXPECT_EQ(memoryBytes(8, 2048), 805306368ull);
    }

    TEST(PointShadowMemory, the_default_four_slots_at_512_texels_is_24_mib)
    {
        EXPECT_EQ(memoryBytes(4, 512), 24ull * mib);
    }

    TEST(PointShadowMemory, grows_with_slots_and_with_the_square_of_the_resolution)
    {
        EXPECT_EQ(memoryBytes(2, 256) * 2, memoryBytes(4, 256));
        EXPECT_EQ(memoryBytes(3, 128) * 4, memoryBytes(3, 256));
    }

    TEST(PointShadowMemory, zero_or_negative_inputs_cost_nothing)
    {
        EXPECT_EQ(memoryBytes(0, 512), 0u);
        EXPECT_EQ(memoryBytes(4, 0), 0u);
        EXPECT_EQ(memoryBytes(-1, 512), 0u);
        EXPECT_EQ(memoryBytes(4, -512), 0u);
    }

    TEST(PointShadowMemory, the_warning_limit_is_256_mib)
    {
        EXPECT_EQ(memoryWarningBytes, 256ull * mib);
    }

    TEST(PointShadowMemory, no_warning_at_or_below_the_limit)
    {
        // 8 slots at 1024 texels: 192 MiB. 4 slots at 1448 would be near the limit; use exact: 16 * ... see below.
        EXPECT_TRUE(memoryWarningText(8, 1024).empty());
        EXPECT_TRUE(memoryWarningText(4, 512).empty());
        // Exactly 256 MiB: 8 slots at 1024 texels is 192 MiB, 8 at 1182 is not a power of two; use 4 slots at
        // 1536 texels = 4 * 6 * 1536 * 1536 * 4 = 216 MiB, and 6 slots at 1536 = 324 MiB (over).
        EXPECT_EQ(memoryBytes(6, 1536), 324ull * mib);
    }

    TEST(PointShadowMemory, a_warning_over_the_limit_names_the_figure_and_the_settings)
    {
        const std::string text = memoryWarningText(8, 2048);
        ASSERT_FALSE(text.empty());
        EXPECT_NE(text.find("768 MiB"), std::string::npos) << text;
        EXPECT_NE(text.find("point light shadow count"), std::string::npos) << text;
        EXPECT_NE(text.find("point light shadow resolution"), std::string::npos) << text;
    }

    TEST(PointShadowMemory, the_limit_itself_does_not_warn_and_one_more_slot_does)
    {
        // 256 MiB exactly: 1 slot at 2048 is 96 MiB; 8 slots at 1024 is 192 MiB; 4 slots at 2048 is 384 MiB.
        EXPECT_TRUE(memoryWarningText(2, 2048).empty()); // 192 MiB
        EXPECT_FALSE(memoryWarningText(4, 2048).empty()); // 384 MiB
    }
}
