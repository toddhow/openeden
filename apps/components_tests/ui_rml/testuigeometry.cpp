// Task 0104 (decision 0015, UI system U1): the pure part of the RmlUi render interface: what RmlUi hands over as the
// bytes the GPU reads, the scissor and the matrices. No device needed.
#include <components/ui_rml/uigeometry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
    using namespace UiRml;

    Rml::Vertex vertex(float x, float y, Rml::byte r, Rml::byte g, Rml::byte b, Rml::byte a, float u, float v)
    {
        Rml::Vertex out;
        out.position = { x, y };
        out.colour = Rml::ColourbPremultiplied(r, g, b, a);
        out.tex_coord = { u, v };
        return out;
    }

    // clip = m * (x, y, 0, 1), m column-major
    std::array<float, 4> apply(const float* m, float x, float y)
    {
        std::array<float, 4> out{};
        for (int row = 0; row < 4; ++row)
            out[row] = m[0 * 4 + row] * x + m[1 * 4 + row] * y + m[3 * 4 + row];
        return out;
    }

    TEST(UiRmlGeometry, a_vertex_is_twenty_bytes_in_the_order_the_pipeline_reads)
    {
        EXPECT_EQ(sizeof(GpuVertex), 20u);
        EXPECT_EQ(offsetof(GpuVertex, mX), 0u);
        EXPECT_EQ(offsetof(GpuVertex, mRed), 8u);
        EXPECT_EQ(offsetof(GpuVertex, mU), 12u);
        EXPECT_EQ(sizeof(PushConstants), 72u);
        EXPECT_EQ(offsetof(PushConstants, mTranslation), 64u);
    }

    TEST(UiRmlGeometry, vertices_keep_position_premultiplied_colour_and_texture_coordinate)
    {
        const std::array<Rml::Vertex, 2> in{ vertex(1.5f, -2.f, 10, 20, 30, 40, 0.25f, 0.75f),
            vertex(100.f, 200.f, 255, 255, 255, 255, 1.f, 0.f) };
        const std::vector<GpuVertex> out = packVertices(in);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mX, 1.5f);
        EXPECT_EQ(out[0].mY, -2.f);
        EXPECT_EQ(out[0].mRed, 10);
        EXPECT_EQ(out[0].mGreen, 20);
        EXPECT_EQ(out[0].mBlue, 30);
        EXPECT_EQ(out[0].mAlpha, 40);
        EXPECT_EQ(out[0].mU, 0.25f);
        EXPECT_EQ(out[0].mV, 0.75f);
        EXPECT_EQ(out[1].mX, 100.f);
        EXPECT_EQ(out[1].mAlpha, 255);
    }

    TEST(UiRmlGeometry, no_vertices_give_an_empty_list)
    {
        EXPECT_TRUE(packVertices({}).empty());
    }

    TEST(UiRmlGeometry, indices_are_copied_as_unsigned_32_bit_values)
    {
        const std::array<int, 6> in{ 0, 1, 2, 0, 2, 3 };
        std::vector<std::uint32_t> out{ 99 };
        EXPECT_TRUE(packIndices(in, 4, out));
        EXPECT_EQ(out, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
    }

    TEST(UiRmlGeometry, an_index_outside_the_vertex_buffer_refuses_the_whole_list)
    {
        std::vector<std::uint32_t> out{ 7 };
        const std::array<int, 3> tooHigh{ 0, 1, 4 };
        EXPECT_FALSE(packIndices(tooHigh, 4, out));
        EXPECT_TRUE(out.empty());
        const std::array<int, 3> negative{ 0, -1, 2 };
        out = { 7 };
        EXPECT_FALSE(packIndices(negative, 4, out));
        EXPECT_TRUE(out.empty());
        const std::array<int, 3> exactlyLast{ 0, 1, 3 };
        EXPECT_TRUE(packIndices(exactlyLast, 4, out));
    }

    TEST(UiRmlGeometry, a_list_that_is_not_whole_triangles_is_refused)
    {
        std::vector<std::uint32_t> out;
        const std::array<int, 4> four{ 0, 1, 2, 3 };
        EXPECT_FALSE(packIndices(four, 4, out));
        EXPECT_TRUE(out.empty());
    }

    TEST(UiRmlGeometry, a_scissor_inside_the_framebuffer_is_unchanged)
    {
        const ScissorRect r = clampScissor(Rml::Rectanglei::FromPositionSize({ 10, 20 }, { 100, 50 }), 800, 600);
        EXPECT_EQ(r.mX, 10);
        EXPECT_EQ(r.mY, 20);
        EXPECT_EQ(r.mWidth, 100u);
        EXPECT_EQ(r.mHeight, 50u);
    }

    TEST(UiRmlGeometry, a_scissor_with_a_negative_origin_moves_to_zero_and_shrinks)
    {
        const ScissorRect r = clampScissor(Rml::Rectanglei::FromPositionSize({ -30, -10 }, { 100, 50 }), 800, 600);
        EXPECT_EQ(r.mX, 0);
        EXPECT_EQ(r.mY, 0);
        EXPECT_EQ(r.mWidth, 70u);
        EXPECT_EQ(r.mHeight, 40u);
    }

    TEST(UiRmlGeometry, a_scissor_past_the_edge_is_cut)
    {
        const ScissorRect r = clampScissor(Rml::Rectanglei::FromPositionSize({ 750, 590 }, { 100, 50 }), 800, 600);
        EXPECT_EQ(r.mX, 750);
        EXPECT_EQ(r.mWidth, 50u);
        EXPECT_EQ(r.mHeight, 10u);
    }

    TEST(UiRmlGeometry, an_empty_inverted_or_outside_scissor_has_no_size)
    {
        EXPECT_EQ(clampScissor(Rml::Rectanglei::FromPositionSize({ 10, 10 }, { 0, 0 }), 800, 600).mWidth, 0u);
        const ScissorRect inverted = clampScissor(Rml::Rectanglei::FromCorners({ 100, 100 }, { 50, 50 }), 800, 600);
        EXPECT_EQ(inverted.mWidth, 0u);
        EXPECT_EQ(inverted.mHeight, 0u);
        const ScissorRect outside
            = clampScissor(Rml::Rectanglei::FromPositionSize({ 900, 10 }, { 100, 100 }), 800, 600);
        EXPECT_EQ(outside.mWidth, 0u);
        const ScissorRect above = clampScissor(Rml::Rectanglei::FromPositionSize({ 10, -200 }, { 100, 100 }), 800, 600);
        EXPECT_EQ(above.mHeight, 0u);
    }

    TEST(UiRmlGeometry, the_orthographic_matrix_maps_the_window_corners_to_vulkan_clip_space)
    {
        float m[16];
        orthographic(800, 600, m);
        auto topLeft = apply(m, 0, 0);
        EXPECT_FLOAT_EQ(topLeft[0], -1.f);
        EXPECT_FLOAT_EQ(topLeft[1], -1.f);
        EXPECT_FLOAT_EQ(topLeft[2], 0.f);
        EXPECT_FLOAT_EQ(topLeft[3], 1.f);
        auto bottomRight = apply(m, 800, 600);
        EXPECT_FLOAT_EQ(bottomRight[0], 1.f);
        EXPECT_FLOAT_EQ(bottomRight[1], 1.f);
        auto middle = apply(m, 400, 300);
        EXPECT_FLOAT_EQ(middle[0], 0.f);
        EXPECT_FLOAT_EQ(middle[1], 0.f);
    }

    TEST(UiRmlGeometry, a_zero_sized_window_does_not_divide_by_zero)
    {
        float m[16];
        orthographic(0, 0, m);
        for (const float v : m)
            EXPECT_TRUE(std::isfinite(v));
    }

    TEST(UiRmlGeometry, multiply_composes_the_second_matrix_first)
    {
        // a: translate by (5, 7); b: scale by 2. a * b applied to (1, 1): scale first, then translate: (7, 9).
        float a[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 7, 0, 1 };
        float b[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float out[16];
        multiply(a, b, out);
        const auto p = apply(out, 1, 1);
        EXPECT_FLOAT_EQ(p[0], 7.f);
        EXPECT_FLOAT_EQ(p[1], 9.f);
        float swapped[16];
        multiply(b, a, swapped);
        const auto q = apply(swapped, 1, 1);
        EXPECT_FLOAT_EQ(q[0], 12.f); // translate first (6, 8), then scale
        EXPECT_FLOAT_EQ(q[1], 16.f);
    }

    TEST(UiRmlGeometry, multiplying_by_the_identity_changes_nothing)
    {
        float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float m[16];
        orthographic(1280, 720, m);
        float out[16];
        multiply(m, identity, out);
        EXPECT_EQ(std::memcmp(m, out, sizeof(m)), 0);
        multiply(identity, m, out);
        EXPECT_EQ(std::memcmp(m, out, sizeof(m)), 0);
    }
}
