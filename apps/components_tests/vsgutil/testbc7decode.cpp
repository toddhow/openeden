// Task 0089: the BC7 block decoder. Generated data only. Expected values come from the BPTC specification's formulas
// (hand-built blocks) and from the D3D11 reference decoder (the pseudo-random block hashes below).

#include <components/vsgutil/bc7decode.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>

namespace
{
    // Writes fields into a 128 bit block, least significant bit first, as BC7 is laid out.
    struct BitWriter
    {
        std::array<uint8_t, 16> bytes{};
        int position = 0;
        void put(uint32_t value, int bits)
        {
            for (int i = 0; i < bits; ++i, ++position)
            {
                if ((value >> i) & 1)
                {
                    bytes[position / 8] = static_cast<uint8_t>(bytes[position / 8] | (1 << (position % 8)));
                }
            }
        }
    };

    struct Texel
    {
        uint8_t r, g, b, a;
        bool operator==(const Texel& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    };

    std::array<Texel, 16> decode(const BitWriter& writer)
    {
        std::array<Texel, 16> out{};
        vsgUtil::Bc7::decodeBlock(writer.bytes.data(), reinterpret_cast<uint8_t*>(out.data()));
        return out;
    }

    // 7 bit endpoint to 8 bits, as the specification does it.
    uint8_t expand7(uint32_t v)
    {
        return static_cast<uint8_t>((v << 1) | (v >> 6));
    }

    // The index weights of the specification.
    constexpr int weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    constexpr int weights2[4] = { 0, 21, 43, 64 };
    uint8_t lerp(int a, int b, int weight)
    {
        return static_cast<uint8_t>((a * (64 - weight) + b * weight + 32) >> 6);
    }

    // FNV-1a over the decoded blocks of a pseudo random image, in block order.
    uint64_t hashOfRandomBlocks(int mode, int blockCount)
    {
        uint32_t state = 1000 + static_cast<uint32_t>(mode);
        auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return static_cast<uint8_t>(state >> 24);
        };
        uint64_t hash = 0xcbf29ce484222325ull;
        for (int i = 0; i < blockCount; ++i)
        {
            uint8_t block[16];
            for (auto& b : block)
            {
                b = next();
            }
            block[0] = static_cast<uint8_t>((block[0] & ~((1 << (mode + 1)) - 1)) | (1 << mode));
            uint8_t rgba[64];
            vsgUtil::Bc7::decodeBlock(block, rgba);
            for (uint8_t b : rgba)
            {
                hash = (hash ^ b) * 0x100000001b3ull;
            }
        }
        return hash;
    }

    // Mode 6 block with the given endpoints (7 bit r, g, b, a for each end), p bits and indices.
    BitWriter mode6(const uint32_t (&e0)[4], const uint32_t (&e1)[4], uint32_t p0, uint32_t p1, const int (&index)[16])
    {
        BitWriter w;
        w.put(1 << 6, 7);
        for (int c = 0; c < 4; ++c)
        {
            w.put(e0[c], 7);
            w.put(e1[c], 7);
        }
        w.put(p0, 1);
        w.put(p1, 1);
        for (int i = 0; i < 16; ++i)
        {
            w.put(static_cast<uint32_t>(index[i]), i == 0 ? 3 : 4);
        }
        return w;
    }
}

TEST(Bc7Decode, mode_6_endpoints_get_their_p_bit_and_index_weights)
{
    const uint32_t e0[4] = { 0x10, 0x20, 0x30, 0x40 };
    const uint32_t e1[4] = { 0x70, 0x60, 0x50, 0x7f };
    const int index[16] = { 0, 7, 8, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14 };
    const auto texels = decode(mode6(e0, e1, 1, 0, index));
    // An 8 bit endpoint is the 7 bit value with the p bit appended.
    int a0[4];
    int a1[4];
    for (int c = 0; c < 4; ++c)
    {
        a0[c] = static_cast<int>((e0[c] << 1) | 1);
        a1[c] = static_cast<int>(e1[c] << 1);
    }
    for (int i = 0; i < 16; ++i)
    {
        const int w = weights4[index[i]];
        EXPECT_EQ(texels[i].r, lerp(a0[0], a1[0], w)) << i;
        EXPECT_EQ(texels[i].g, lerp(a0[1], a1[1], w)) << i;
        EXPECT_EQ(texels[i].b, lerp(a0[2], a1[2], w)) << i;
        EXPECT_EQ(texels[i].a, lerp(a0[3], a1[3], w)) << i;
    }
}

TEST(Bc7Decode, mode_5_expands_seven_bit_colour_and_rotates_alpha)
{
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        BitWriter w;
        w.put(1 << 5, 6);
        w.put(static_cast<uint32_t>(rotation), 2);
        const uint32_t r0 = 0x11, r1 = 0x22, g0 = 0x33, g1 = 0x44, b0 = 0x55, b1 = 0x66;
        for (uint32_t v : { r0, r1, g0, g1, b0, b1 })
        {
            w.put(v, 7);
        }
        w.put(0x77, 8);
        w.put(0x88, 8);
        w.put(0, 31); // colour indices: all end 0
        w.put(0, 31); // alpha indices: all end 0
        const auto texels = decode(w);
        uint8_t channel[4] = { expand7(r0), expand7(g0), expand7(b0), 0x77 };
        if (rotation > 0)
        {
            std::swap(channel[3], channel[rotation - 1]);
        }
        for (const Texel& t : texels)
        {
            EXPECT_EQ(t, (Texel{ channel[0], channel[1], channel[2], channel[3] })) << rotation;
        }
    }
}

TEST(Bc7Decode, mode_5_colour_and_alpha_indices_are_separate)
{
    BitWriter w;
    w.put(1 << 5, 6);
    w.put(0, 2);
    for (uint32_t v : { 0u, 127u, 0u, 127u, 0u, 127u })
    {
        w.put(v, 7);
    }
    w.put(0, 8);
    w.put(255, 8);
    for (int i = 0; i < 16; ++i)
    {
        w.put(i == 1 ? 3 : 0, i == 0 ? 1 : 2); // colour: texel 1 is the second end
    }
    for (int i = 0; i < 16; ++i)
    {
        w.put(i == 2 ? 3 : 0, i == 0 ? 1 : 2); // alpha: texel 2 is the second end
    }
    const auto texels = decode(w);
    EXPECT_EQ(texels[0], (Texel{ 0, 0, 0, 0 }));
    EXPECT_EQ(texels[1], (Texel{ 255, 255, 255, 0 }));
    EXPECT_EQ(texels[2], (Texel{ 0, 0, 0, 255 }));
}

TEST(Bc7Decode, mode_4_index_selection_swaps_which_index_set_is_colour)
{
    for (int select = 0; select < 2; ++select)
    {
        BitWriter w;
        w.put(1 << 4, 5);
        w.put(0, 2); // rotation
        w.put(static_cast<uint32_t>(select), 1);
        for (uint32_t v : { 0u, 31u, 0u, 31u, 0u, 31u })
        {
            w.put(v, 5);
        }
        w.put(0, 6);
        w.put(63, 6);
        // The first set has 2 bit indices, the second 3 bit indices. Texel 1: first set 3, second set 0.
        for (int i = 0; i < 16; ++i)
        {
            w.put(i == 1 ? 3 : 0, i == 0 ? 1 : 2);
        }
        for (int i = 0; i < 16; ++i)
        {
            w.put(0, i == 0 ? 2 : 3);
        }
        const auto texels = decode(w);
        // select 0: the 2 bit set is the colour, so texel 1 is white and its alpha (3 bit set) is 0.
        // select 1: the 3 bit set is the colour (texel 1 black) and the 2 bit set is alpha (texel 1 opaque).
        if (select == 0)
        {
            EXPECT_EQ(texels[1], (Texel{ 255, 255, 255, 0 }));
        }
        else
        {
            EXPECT_EQ(texels[1], (Texel{ 0, 0, 0, 255 }));
        }
    }
}

TEST(Bc7Decode, mode_1_second_subset_uses_the_shared_p_bit_and_its_own_endpoints)
{
    // Partition 0 of two subsets: columns 0 and 1 are subset 0, columns 2 and 3 subset 1.
    BitWriter w;
    w.put(1 << 1, 2);
    w.put(0, 6);
    // r, g, b of the four endpoints (6 bits each): subset 0 black to black, subset 1 white to white.
    for (int c = 0; c < 3; ++c)
    {
        for (uint32_t v : { 0u, 0u, 63u, 63u })
        {
            w.put(v, 6);
        }
    }
    w.put(0, 1); // shared p bit of subset 0
    w.put(1, 1); // shared p bit of subset 1
    w.put(0, 46);
    const auto texels = decode(w);
    for (int i = 0; i < 16; ++i)
    {
        if (i % 4 < 2)
        {
            EXPECT_EQ(texels[i], (Texel{ 0, 0, 0, 255 })) << i;
        }
        else
        {
            EXPECT_EQ(texels[i], (Texel{ 255, 255, 255, 255 })) << i; // 63 and p bit 1: 0x7f, expanded to 255
        }
    }
}

TEST(Bc7Decode, mode_3_two_bit_indices_use_the_two_bit_weights)
{
    // Partition 0 again; mode 3 has 7 bit endpoints, one p bit per endpoint and 2 bit indices.
    BitWriter w;
    w.put(1 << 3, 4);
    w.put(0, 6);
    for (int c = 0; c < 3; ++c)
    {
        for (uint32_t v : { 0u, 127u, 0u, 127u })
        {
            w.put(v, 7);
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        w.put(0, 1); // p bits
    }
    // Indices (2 bits; the anchors of partition 0 are texel 0 and texel 15 and have 1 bit): texel 1 gets index 1.
    for (int i = 0; i < 16; ++i)
    {
        const bool anchor = i == 0 || i == 15;
        w.put(i == 1 ? 1 : 0, anchor ? 1 : 2);
    }
    const auto texels = decode(w);
    EXPECT_EQ(texels[1].r, lerp(0, 254, weights2[1]));
    EXPECT_EQ(texels[0].r, 0);
}

TEST(Bc7Decode, a_block_with_no_mode_bit_is_all_zero_including_alpha)
{
    BitWriter w; // all zero bytes: no mode bit set
    const auto texels = decode(w);
    for (const Texel& t : texels)
    {
        EXPECT_EQ(t, (Texel{ 0, 0, 0, 0 }));
    }
}

// The hashes were taken from the D3D11 reference decoder (WARP and a hardware device agree) over 4096 random blocks
// of each mode: this covers every partition, anchor, rotation, index selection and p bit layout.
TEST(Bc7Decode, random_blocks_of_every_mode_match_the_reference_decoder)
{
    const uint64_t expected[8]
        = { 0xc376f42e41544f98ull, 0x41aa21c752135ceeull, 0xfa735ff99c3001cbull, 0xafeeb0d651e563fcull,
              0xe07e470378e811c7ull, 0x24bf8558e98f4f4bull, 0xcb409b313925099bull, 0x216bc1bda50ca942ull };
    for (int mode = 0; mode < 8; ++mode)
    {
        EXPECT_EQ(hashOfRandomBlocks(mode, 64 * 64), expected[mode]) << "mode " << mode;
    }
}
