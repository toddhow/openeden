// Task 0088 stage b: how a live terrain chunk's fade rides in the draw's firstInstance, and that both shader languages
// read it.
//
// Written before the implementation. A chunk's draw packs lod (byte 0), shift (byte 1) and the grid position (bytes 2
// and 3) into firstInstance; lod, shift and grid never pass 15, so the high nibble of byte 0 and of byte 1 are free and
// hold the share of the dither that is DROPPED (255 minus the fade in 255ths), so that a solid chunk (and every other
// user of the terrain pipeline) keeps the old value exactly. The vertex stage decodes it into a flat varying, and the
// fragment stage multiplies the batch's fade by it.

#include <components/terrain/chunkfade.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

TEST(TerrainChunkFadeInstance, a_solid_chunk_keeps_the_instance_value)
{
    const std::uint32_t base = 2u | (1u << 8) | (3u << 16) | (5u << 24);
    EXPECT_EQ(Terrain::encodeChunkFade(base, 1.f), base);
    EXPECT_EQ(Terrain::encodeChunkFade(base, 2.f), base);
}

TEST(TerrainChunkFadeInstance, a_hidden_chunk_drops_everything_and_keeps_the_other_fields)
{
    const std::uint32_t base = 2u | (1u << 8) | (3u << 16) | (5u << 24);
    const std::uint32_t hidden = Terrain::encodeChunkFade(base, 0.f);
    EXPECT_EQ(hidden & 0x0f, 2u);
    EXPECT_EQ((hidden >> 8) & 0x0f, 1u);
    EXPECT_EQ(hidden >> 16, base >> 16);
    EXPECT_EQ(hidden, base | 0xf0u | 0xf000u);
    EXPECT_FLOAT_EQ(Terrain::decodeChunkFade(hidden), 0.f);
}

TEST(TerrainChunkFadeInstance, the_fade_round_trips_to_a_255th)
{
    const std::uint32_t base = 4u | (3u << 8) | (15u << 16) | (15u << 24);
    for (int step = 0; step <= 255; ++step)
    {
        const float fade = static_cast<float>(step) / 255.f;
        const std::uint32_t instance = Terrain::encodeChunkFade(base, fade);
        EXPECT_NEAR(Terrain::decodeChunkFade(instance), fade, 1e-6f) << step;
        EXPECT_EQ(instance & 0x0f0f, base & 0x0f0f) << step;
        EXPECT_EQ(instance >> 16, base >> 16) << step;
    }
}

TEST(TerrainChunkFadeInstance, a_fade_outside_zero_to_one_is_clamped)
{
    const std::uint32_t base = 1u;
    EXPECT_FLOAT_EQ(Terrain::decodeChunkFade(Terrain::encodeChunkFade(base, -3.f)), 0.f);
    EXPECT_EQ(Terrain::encodeChunkFade(base, 7.f), base);
}

TEST(TerrainChunkFadeInstance, an_instance_that_has_no_room_is_left_alone)
{
    // A lod above 15 would overlap the fade bits: the chunk then draws solid.
    const std::uint32_t base = 0x10u;
    EXPECT_EQ(Terrain::encodeChunkFade(base, 0.25f), base);
    EXPECT_FLOAT_EQ(Terrain::decodeChunkFade(0u), 1.f);
}

TEST(TerrainChunkFadeInstance, the_vertex_stages_decode_it_and_mask_lod_and_shift)
{
    const std::string glsl = readSource("terrain/main.vert");
    EXPECT_TRUE(contains(glsl, "int lod = gl_InstanceIndex & 0xf;"));
    EXPECT_TRUE(contains(glsl, "int shiftSize = (gl_InstanceIndex >> 8) & 0xf;"));
    EXPECT_TRUE(contains(glsl, "vert_out.chunkFade"));
    EXPECT_TRUE(contains(glsl, "255.0"));
    const std::string slang = readSource("terrain/terrain.vert.slang");
    EXPECT_TRUE(contains(slang, "int lod = instanceIndex & 0xf;"));
    EXPECT_TRUE(contains(slang, "int shiftSize = (instanceIndex >> 8) & 0xf;"));
    EXPECT_TRUE(contains(slang, "output.io.chunkFade"));
    EXPECT_TRUE(contains(slang, "255.0"));
}

TEST(TerrainChunkFadeInstance, the_varying_is_flat_and_last_in_both_languages)
{
    const std::string glsl = readSource("terrain/inout.glsl");
    const std::size_t glslFlat = glsl.find("flat float chunkFade;");
    ASSERT_NE(glslFlat, std::string::npos);
    EXPECT_GT(glslFlat, glsl.find("vec3 viewBinormal;"));
    const std::string slang = readSource("terrain/inout.slang");
    const std::size_t slangFlat = slang.find("nointerpolation float chunkFade : V_CHUNKFADE;");
    ASSERT_NE(slangFlat, std::string::npos);
    EXPECT_GT(slangFlat, slang.find("float3 viewBinormal"));
}

TEST(TerrainChunkFadeInstance, the_fragment_stages_multiply_the_batch_fade_by_the_chunks)
{
    const std::string glsl = readSource("terrain/main.frag");
    EXPECT_TRUE(contains(glsl, "float code = frag_in.chunkFade < 1.0 ? frag_in.chunkFade : batch.fade;"));
    EXPECT_TRUE(
        contains(glsl, "else if (code < 1.0 && dither8(int(gl_FragCoord.x) & 7, int(gl_FragCoord.y) & 7, code) == 0.0)"));
    const std::string slang = readSource("terrain/terrain.frag.slang");
    EXPECT_TRUE(contains(slang, "float code = input.io.chunkFade < 1.0 ? input.io.chunkFade : data.batch.fade;"));
    EXPECT_TRUE(contains(slang, "dither8(int(input.fragCoord.x) & 7, int(input.fragCoord.y) & 7, -1.0 - code) != 0.0"));
}
