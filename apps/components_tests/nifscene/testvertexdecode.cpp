#include <components/nifscene/vertexdecode.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "../nif/nifbuilder.hpp"

// Task 0082: the packed vertex formats of Skyrim SE decoded to plain arrays. The byte mapping and the field order are
// the schema's BSVertexData (research note 0044, items 3 and 4); the expected values are worked out by hand here.
namespace
{
    using namespace NifScene;

    constexpr std::uint16_t Vertex = 0x1;
    constexpr std::uint16_t UVs = 0x2;
    constexpr std::uint16_t UVs2 = 0x4;
    constexpr std::uint16_t Normals = 0x8;
    constexpr std::uint16_t Tangents = 0x10;
    constexpr std::uint16_t Colors = 0x20;
    constexpr std::uint16_t Skinned = 0x40;
    constexpr std::uint16_t Land = 0x80;
    constexpr std::uint16_t Eye = 0x100;
    constexpr std::uint16_t Instance = 0x200;
    constexpr std::uint16_t FullPrecision = 0x400;

    using Nif::Testing::toHalf;

    // A byte of packed vertex data as the reader stores it (a char, signed on some platforms).
    constexpr char B(unsigned value)
    {
        return static_cast<char>(static_cast<unsigned char>(value));
    }

    TEST(NifSceneHalf, converts_exact_values)
    {
        EXPECT_EQ(halfToFloat(0x0000), 0.f);
        EXPECT_EQ(halfToFloat(0x3C00), 1.f);
        EXPECT_EQ(halfToFloat(0xBC00), -1.f);
        EXPECT_EQ(halfToFloat(0x4000), 2.f);
        EXPECT_EQ(halfToFloat(0x3800), 0.5f);
        EXPECT_EQ(halfToFloat(0x7BFF), 65504.f);
        EXPECT_EQ(halfToFloat(0x0400), std::ldexp(1.f, -14)); // smallest normal
        EXPECT_EQ(halfToFloat(0x0001), std::ldexp(1.f, -24)); // smallest subnormal
        EXPECT_EQ(halfToFloat(0x03FF), 1023.f * std::ldexp(1.f, -24));
        EXPECT_EQ(halfToFloat(0x3555), 0.333251953125f);
    }

    TEST(NifSceneHalf, keeps_the_sign_of_zero_and_special_values)
    {
        EXPECT_TRUE(std::signbit(halfToFloat(0x8000)));
        EXPECT_EQ(halfToFloat(0x8000), 0.f);
        EXPECT_EQ(halfToFloat(0x7C00), std::numeric_limits<float>::infinity());
        EXPECT_EQ(halfToFloat(0xFC00), -std::numeric_limits<float>::infinity());
        EXPECT_TRUE(std::isnan(halfToFloat(0x7E00)));
        EXPECT_TRUE(std::isnan(halfToFloat(0x7C01)));
    }

    TEST(NifSceneHalf, round_trips_every_finite_half_through_the_test_encoder)
    {
        for (std::uint32_t bits = 0; bits <= 0xFFFF; ++bits)
        {
            const std::uint16_t half = static_cast<std::uint16_t>(bits);
            const float value = halfToFloat(half);
            if (!std::isfinite(value))
                continue;
            ASSERT_EQ(toHalf(value), half) << "half bits " << bits;
        }
    }

    TEST(NifSceneNormByte, maps_zero_to_minus_one_and_255_to_one)
    {
        EXPECT_FLOAT_EQ(normByte(0), -1.f);
        EXPECT_FLOAT_EQ(normByte(255), 1.f);
        EXPECT_NEAR(normByte(127), -1.f / 255.f, 1e-6f);
        EXPECT_NEAR(normByte(128), 1.f / 255.f, 1e-6f);
    }

    TEST(NifScenePackedStride, adds_the_fields_the_flags_select)
    {
        // Full precision positions are three floats and a fourth dword (bitangent X, or unused).
        EXPECT_EQ(packedVertexStride(Vertex | FullPrecision), 16u);
        // Half precision positions are three halves and a fourth half.
        EXPECT_EQ(packedVertexStride(Vertex), 8u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | FullPrecision), 20u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | FullPrecision), 24u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | Tangents | FullPrecision), 28u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | Tangents | Colors | FullPrecision), 32u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | Tangents | Colors | Skinned | FullPrecision), 44u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | Tangents | Colors | Skinned | Eye | FullPrecision), 48u);
        EXPECT_EQ(packedVertexStride(Vertex | UVs | Normals | Tangents | Colors | Skinned | Eye), 40u);
    }

    TEST(NifScenePackedStride, a_shape_without_positions_has_none)
    {
        EXPECT_EQ(packedVertexStride(UVs | FullPrecision), 4u);
        EXPECT_EQ(packedVertexStride(0), 0u);
    }

    TEST(NifScenePackedStride, refuses_combinations_the_format_cannot_hold)
    {
        EXPECT_EQ(packedVertexStride(Vertex | Tangents | FullPrecision), 0u); // tangents need normals
        EXPECT_EQ(packedVertexStride(Vertex | UVs2 | FullPrecision), 0u);
        EXPECT_EQ(packedVertexStride(Vertex | Land | FullPrecision), 0u);
        EXPECT_EQ(packedVertexStride(Vertex | Instance | FullPrecision), 0u);
    }

    Nif::BSVertexData fullVertex(float x, float y, float z, float bitangentX)
    {
        Nif::BSVertexData v{};
        v.mVertex = vsg::vec4(x, y, z, bitangentX);
        return v;
    }

    TEST(NifSceneDecode, full_precision_positions_and_the_bitangent_x_in_w)
    {
        std::vector<Nif::BSVertexData> in = { fullVertex(1.f, 2.f, 3.f, 0.25f), fullVertex(-4.f, 5.f, -6.f, -0.5f) };
        in[0].mNormal = { B(128), B(128), B(255), B(128) };
        in[0].mTangent = { B(255), B(128), B(128), B(128) };
        const DecodedVertices out = decodeVertices(Vertex | Normals | Tangents | FullPrecision, in);
        ASSERT_EQ(out.mPositions.size(), 2u);
        EXPECT_EQ(out.mPositions[0], (Vec3{ 1.f, 2.f, 3.f }));
        EXPECT_EQ(out.mPositions[1], (Vec3{ -4.f, 5.f, -6.f }));
        ASSERT_EQ(out.mTangents.size(), 2u);
        // The glTF tangent is the NIF bitangent: (bitangent X, bitangent Y byte, bitangent Z byte).
        EXPECT_FLOAT_EQ(out.mTangents[0][0], 0.25f);
        EXPECT_NEAR(out.mTangents[0][1], 1.f / 255.f, 1e-6f);
        EXPECT_NEAR(out.mTangents[0][2], 1.f / 255.f, 1e-6f);
    }

    TEST(NifSceneDecode, half_precision_positions_and_the_bitangent_x_in_the_fourth_half)
    {
        Nif::BSVertexData v{};
        v.mHalfVertex = { toHalf(1.5f), toHalf(-2.f), toHalf(0.25f), toHalf(0.5f) };
        v.mNormal = { B(128), B(255), B(128), B(128) };
        v.mTangent = { B(255), B(128), B(128), B(128) };
        const std::vector<Nif::BSVertexData> in = { v };
        const DecodedVertices out = decodeVertices(Vertex | Normals | Tangents, in);
        ASSERT_EQ(out.mPositions.size(), 1u);
        EXPECT_EQ(out.mPositions[0], (Vec3{ 1.5f, -2.f, 0.25f }));
        ASSERT_EQ(out.mTangents.size(), 1u);
        EXPECT_FLOAT_EQ(out.mTangents[0][0], 0.5f);
    }

    TEST(NifSceneDecode, the_fourth_slot_is_not_a_bitangent_without_the_tangents_attribute)
    {
        std::vector<Nif::BSVertexData> in = { fullVertex(1.f, 2.f, 3.f, 7.f) };
        const DecodedVertices out = decodeVertices(Vertex | Normals | FullPrecision, in);
        EXPECT_TRUE(out.mTangents.empty());
        ASSERT_EQ(out.mPositions.size(), 1u);
        EXPECT_EQ(out.mPositions[0], (Vec3{ 1.f, 2.f, 3.f }));
    }

    TEST(NifSceneDecode, normals_map_bytes_to_minus_one_to_one)
    {
        std::vector<Nif::BSVertexData> in = { fullVertex(0, 0, 0, 0) };
        in[0].mNormal = { B(0), B(255), B(128), B(128) };
        const DecodedVertices out = decodeVertices(Vertex | Normals | FullPrecision, in);
        ASSERT_EQ(out.mNormals.size(), 1u);
        EXPECT_FLOAT_EQ(out.mNormals[0][0], -1.f);
        EXPECT_FLOAT_EQ(out.mNormals[0][1], 1.f);
        EXPECT_NEAR(out.mNormals[0][2], 1.f / 255.f, 1e-6f);
    }

    TEST(NifSceneDecode, bytes_above_127_are_not_read_as_negative)
    {
        // A std::array<char, 4> is signed on some platforms; the decoder must read the byte value.
        std::vector<Nif::BSVertexData> in = { fullVertex(0, 0, 0, 0) };
        in[0].mNormal = { B(200), B(255), B(0), B(0) };
        const DecodedVertices out = decodeVertices(Vertex | Normals | FullPrecision, in);
        EXPECT_NEAR(out.mNormals[0][0], 200.f / 127.5f - 1.f, 1e-6f);
        EXPECT_FLOAT_EQ(out.mNormals[0][1], 1.f);
    }

    // normal (0, 0, 1) and NIF bitangent (0, 1, 0): the glTF tangent is (0, 1, 0) and cross(normal, tangent) is
    // (-1, 0, 0), so the glTF bitangent equals the NIF tangent exactly when the NIF tangent is (-1, 0, 0).
    Nif::BSVertexData tangentCase(char tangentX)
    {
        Nif::BSVertexData v = fullVertex(0, 0, 0, 0.f);
        v.mNormal = { B(128), B(128), B(255), B(255) }; // normal ~(0,0,1), bitangent Y = 1
        v.mTangent = { tangentX, B(128), B(128), B(128) }; // NIF tangent along +-X, bitangent Z ~0
        return v;
    }

    TEST(NifSceneDecode, tangent_sign_is_positive_when_the_nif_tangent_is_cross_normal_tangent)
    {
        const std::vector<Nif::BSVertexData> in = { tangentCase(B(0)) }; // NIF tangent (-1, 0, 0)
        const DecodedVertices out = decodeVertices(Vertex | Normals | Tangents | FullPrecision, in);
        ASSERT_EQ(out.mTangents.size(), 1u);
        EXPECT_NEAR(out.mTangents[0][1], 1.f, 1e-6f);
        EXPECT_FLOAT_EQ(out.mTangents[0][3], 1.f);
    }

    TEST(NifSceneDecode, tangent_sign_is_negative_when_the_nif_tangent_is_opposite)
    {
        const std::vector<Nif::BSVertexData> in = { tangentCase(B(255)) }; // (+1, 0, 0)
        const DecodedVertices out = decodeVertices(Vertex | Normals | Tangents | FullPrecision, in);
        ASSERT_EQ(out.mTangents.size(), 1u);
        EXPECT_FLOAT_EQ(out.mTangents[0][3], -1.f);
    }

    TEST(NifSceneDecode, uvs_colours_weights_and_bones)
    {
        Nif::BSVertexData v = fullVertex(0, 0, 0, 0);
        v.mUV = { toHalf(0.25f), toHalf(0.75f) };
        v.mVertColor = { B(255), B(0), B(51), B(128) };
        v.mBoneWeights = { toHalf(0.5f), toHalf(0.25f), toHalf(0.25f), toHalf(0.f) };
        v.mBoneIndices = { B(3), B(1), B(2), B(0) };
        const std::vector<Nif::BSVertexData> in = { v };
        const DecodedVertices out = decodeVertices(Vertex | UVs | Colors | Skinned | FullPrecision, in);
        ASSERT_EQ(out.mUV0.size(), 1u);
        EXPECT_EQ(out.mUV0[0], (Vec2{ 0.25f, 0.75f }));
        ASSERT_EQ(out.mColors.size(), 1u);
        EXPECT_FLOAT_EQ(out.mColors[0][0], 1.f);
        EXPECT_FLOAT_EQ(out.mColors[0][1], 0.f);
        EXPECT_FLOAT_EQ(out.mColors[0][2], 0.2f);
        EXPECT_FLOAT_EQ(out.mColors[0][3], 128.f / 255.f);
        ASSERT_EQ(out.mWeights.size(), 1u);
        EXPECT_EQ(out.mWeights[0], (std::array<float, 4>{ 0.5f, 0.25f, 0.25f, 0.f }));
        ASSERT_EQ(out.mBoneIndices.size(), 1u);
        EXPECT_EQ(out.mBoneIndices[0], (std::array<std::uint8_t, 4>{ 3, 1, 2, 0 }));
    }

    TEST(NifSceneDecode, an_attribute_without_its_flag_leaves_its_array_empty)
    {
        const std::vector<Nif::BSVertexData> in = { fullVertex(1, 2, 3, 0) };
        const DecodedVertices out = decodeVertices(Vertex | FullPrecision, in);
        EXPECT_EQ(out.mPositions.size(), 1u);
        EXPECT_TRUE(out.mNormals.empty());
        EXPECT_TRUE(out.mTangents.empty());
        EXPECT_TRUE(out.mUV0.empty());
        EXPECT_TRUE(out.mColors.empty());
        EXPECT_TRUE(out.mWeights.empty());
        EXPECT_TRUE(out.mBoneIndices.empty());
    }

    TEST(NifSceneDecode, no_vertices_gives_empty_arrays)
    {
        const DecodedVertices out = decodeVertices(Vertex | Normals | Skinned | FullPrecision, {});
        EXPECT_TRUE(out.mPositions.empty());
        EXPECT_TRUE(out.mNormals.empty());
        EXPECT_TRUE(out.mWeights.empty());
    }

    TEST(NifSceneDecode, vertex_data_size_zero_means_no_data)
    {
        // The skinned Skyrim SE shapes store no vertices in the shape: an empty span decodes to nothing and does not
        // read anything.
        const DecodedVertices out
            = decodeVertices(Vertex | UVs | Skinned | FullPrecision, std::span<const Nif::BSVertexData>());
        EXPECT_TRUE(out.mPositions.empty());
    }
}
