// Task 0088 stage e: the complementary dither of the live terrain's chunks.
//
// Written before the implementation. A chunk that is replaced by finer or coarser chunks used to stay opaque under them
// until they were solid (two layers, depth tested, then a pop). Now it keeps exactly the pixels the incoming chunks
// drop: the fragment stage reads a "fade code", the chunk's own fade (0 to 1) or, below zero, -1 minus the INCOMING
// fade (the chunk then keeps the pixels that fade drops). The hand-over is exact per pixel: every pixel is drawn by
// exactly one of the two, no hole and no doubled pixel.

#include <components/pipeline/terrain.hpp>
#include <components/terrain/batchdata.hpp>
#include <components/terrain/builder.hpp>
#include <components/terrain/chunkfade.hpp>

#include <vsg/core/Value.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using Terrain::Bounds;
    using Terrain::ChunkFader;
    using Terrain::ChunkShare;

    Bounds square(float size, float x, float y)
    {
        return Bounds{ size, vsg::vec2(x, y) };
    }

    const auto never = [](const Bounds&) { return false; };

    std::vector<Bounds> children(const Bounds& parent)
    {
        const float h = parent.size * 0.5f;
        return { square(h, parent.min.x, parent.min.y), square(h, parent.min.x + h, parent.min.y),
            square(h, parent.min.x, parent.min.y + h), square(h, parent.min.x + h, parent.min.y + h) };
    }

    constexpr float frame = 0.1f;
    constexpr float fadeTime = 0.4f;

    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // What the vertex stage hands the fragment stage for a draw: the share through the instance index and back.
    float codeOf(const ChunkShare& share)
    {
        const ChunkShare back = Terrain::decodeChunkShare(Terrain::encodeChunkShare(0, share));
        return Terrain::fadeCode(back);
    }
}

TEST(TerrainChunkComplement, a_solid_share_keeps_the_instance_value_and_an_inverted_one_sets_bit_23)
{
    const std::uint32_t base = 2u | (1u << 8) | (3u << 16) | (5u << 24);
    EXPECT_EQ(Terrain::encodeChunkShare(base, ChunkShare{ 1.f, false }), base);
    const std::uint32_t inverted = Terrain::encodeChunkShare(base, ChunkShare{ 0.5f, true });
    EXPECT_NE(inverted & 0x00800000u, 0u);
    EXPECT_EQ(inverted & 0x0f0fu, base & 0x0f0fu);
    EXPECT_EQ(inverted >> 24, base >> 24);
    EXPECT_EQ((inverted >> 16) & 0xfu, 3u);
    // An inverted share of 0 keeps every pixel: it is not the solid value.
    EXPECT_NE(Terrain::encodeChunkShare(base, ChunkShare{ 0.f, true }), base);
}

TEST(TerrainChunkComplement, the_share_round_trips_with_its_flag)
{
    const std::uint32_t base = 3u | (2u << 8) | (7u << 16) | (15u << 24);
    for (int step = 0; step <= 255; step += 5)
    {
        const float fade = static_cast<float>(step) / 255.f;
        for (const bool inverted : { false, true })
        {
            const ChunkShare back = Terrain::decodeChunkShare(Terrain::encodeChunkShare(base, ChunkShare{ fade, inverted }));
            EXPECT_NEAR(back.mFade, fade, 1e-6f) << step;
            EXPECT_EQ(back.mInverted, inverted) << step;
        }
    }
}

TEST(TerrainChunkComplement, an_instance_without_room_is_left_alone)
{
    const std::uint32_t full = 0x00800000u;
    EXPECT_EQ(Terrain::encodeChunkShare(full, ChunkShare{ 0.3f, true }), full);
    EXPECT_EQ(Terrain::encodeChunkShare(0x10u, ChunkShare{ 0.3f, true }), 0x10u);
}

TEST(TerrainChunkComplement, the_fade_code_is_the_fade_or_minus_one_minus_the_incoming_fade)
{
    EXPECT_FLOAT_EQ(Terrain::fadeCode(ChunkShare{ 0.25f, false }), 0.25f);
    EXPECT_FLOAT_EQ(Terrain::fadeCode(ChunkShare{ 0.25f, true }), -1.25f);
    EXPECT_FLOAT_EQ(Terrain::fadeCode(ChunkShare{ 1.f, false }), 1.f);
}

TEST(TerrainChunkComplement, the_pixel_rule_keeps_what_the_incoming_chunk_drops)
{
    for (int step = 0; step <= 64; ++step)
    {
        const float fade = static_cast<float>(step) / 64.f;
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
            {
                const bool incoming = Terrain::fadeCodeKeepsPixel(x, y, fade);
                const bool leaving = Terrain::fadeCodeKeepsPixel(x, y, -1.f - fade);
                ASSERT_NE(incoming, leaving) << "fade " << fade << " pixel " << x << "," << y;
            }
    }
}

TEST(TerrainChunkComplement, the_dither_table_is_the_shaders)
{
    for (const char* file : { "lib/view/dither.glsl", "lib/view/dither.slang" })
    {
        const std::string text = readSource(file);
        std::vector<int> numbers;
        const std::regex number("[0-9]+");
        const std::size_t start = text.find('{');
        ASSERT_NE(start, std::string::npos);
        const std::size_t end = text.find("float limit");
        const std::string table = text.substr(start, end - start);
        // The table comments say "0..63" and "8x8": take the numbers of the rows only.
        std::string rows;
        std::istringstream lines(table);
        for (std::string line; std::getline(lines, line);)
        {
            const std::size_t comment = line.find("/*");
            if (comment != std::string::npos)
                line = line.substr(0, comment);
            rows += line + "\n";
        }
        for (std::sregex_iterator it(rows.begin(), rows.end(), number), last; it != last; ++it)
            numbers.push_back(std::stoi(it->str()));
        // glsl: "int dither[8][8]" contributes 8 and 8 before the 64 table numbers.
        if (numbers.size() == 66)
            numbers.erase(numbers.begin(), numbers.begin() + 2);
        ASSERT_EQ(numbers.size(), 64u) << file;
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                EXPECT_EQ(numbers[x * 8 + y], Terrain::ditherTable(x, y)) << file << " " << x << "," << y;
    }
}

TEST(TerrainChunkComplement, a_replaced_solid_chunk_shows_the_complement_of_its_replacement)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, true, never, never);
    fader.setDesired(children(parent), false, never, never);
    // Right after the change the children are at 0, so the parent keeps every pixel (the complement of nothing).
    ChunkShare share = fader.shareOf(parent);
    EXPECT_TRUE(share.mInverted);
    EXPECT_FLOAT_EQ(share.mFade, 0.f);
    fader.step(frame, fadeTime);
    share = fader.shareOf(parent);
    EXPECT_TRUE(share.mInverted);
    EXPECT_NEAR(share.mFade, 0.25f, 1e-5f);
    for (const Bounds& kid : children(parent))
    {
        EXPECT_FALSE(fader.shareOf(kid).mInverted);
        EXPECT_NEAR(fader.shareOf(kid).mFade, 0.25f, 1e-5f);
    }
}

TEST(TerrainChunkComplement, the_weakest_replacement_decides)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, true, never, never);
    const auto kids = children(parent);
    fader.setDesired(std::vector{ kids[0] }, false, never, never);
    fader.step(frame, fadeTime);
    fader.step(frame, fadeTime);
    // Three more kids appear later at 0: the parent follows the weakest.
    fader.setDesired(kids, false, never, never);
    EXPECT_FLOAT_EQ(fader.shareOf(parent).mFade, 0.f);
}

TEST(TerrainChunkComplement, a_chunk_only_partly_replaced_stays_a_normal_solid_chunk)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, true, never, never);
    // One child only: the rest of the parent's square has nothing to take over, so no pixel may be dropped there.
    fader.setDesired(std::vector{ children(parent)[0] }, false, never, never);
    EXPECT_FALSE(fader.shareOf(parent).mInverted);
    EXPECT_FLOAT_EQ(fader.shareOf(parent).mFade, 1.f);
}

TEST(TerrainChunkComplement, a_chunk_still_fading_in_that_is_replaced_rises_normally_first)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, false, never, never);
    fader.step(frame, fadeTime);
    fader.setDesired(children(parent), false, never, never);
    EXPECT_FALSE(fader.shareOf(parent).mInverted);
}

TEST(TerrainChunkComplement, nothing_replaces_a_chunk_that_fades_out_so_it_stays_normal)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    fader.setDesired(std::vector{ a }, true, never, never);
    fader.setDesired(std::vector<Bounds>{}, false, never, never);
    EXPECT_FALSE(fader.shareOf(a).mInverted);
}

namespace
{
    void partition(std::mt19937& rng, const Bounds& at, std::vector<Bounds>& out)
    {
        std::uniform_int_distribution<int> coin(0, 99);
        if (at.size > 1.f && coin(rng) < 55)
        {
            for (const Bounds& child : children(at))
                partition(rng, child, out);
        }
        else if (coin(rng) < 85)
            out.push_back(at);
    }

    // How many chunks keep the pixel (px, py) of the unit cell (cx, cy).
    int keepers(const ChunkFader& fader, const std::vector<Bounds>& wanted, int cx, int cy, int px, int py)
    {
        std::vector<Bounds> all = wanted;
        for (const Bounds& leaving : fader.leaving())
            all.push_back(leaving);
        int count = 0;
        for (const Bounds& chunk : all)
        {
            if (!Terrain::boundsOverlap(chunk, square(1, static_cast<float>(cx), static_cast<float>(cy))))
                continue;
            if (Terrain::fadeCodeKeepsPixel(px, py, codeOf(fader.shareOf(chunk))))
                ++count;
        }
        return count;
    }

    bool cellWanted(const std::vector<Bounds>& wanted, int x, int y)
    {
        return std::any_of(wanted.begin(), wanted.end(), [x, y](const Bounds& b) {
            return Terrain::boundsOverlap(b, square(1, static_cast<float>(x), static_cast<float>(y)));
        });
    }
}

// No hole at pixel level: a pixel of a wanted cell that was drawn before a change or a step is drawn after it.
TEST(TerrainChunkComplement, no_pixel_of_a_wanted_cell_is_ever_lost)
{
    for (unsigned seed = 1; seed <= 40; ++seed)
    {
        std::mt19937 rng(seed);
        ChunkFader fader;
        std::vector<Bounds> wanted;
        partition(rng, square(8, 0, 0), wanted);
        fader.setDesired(wanted, true, never, never);
        auto snapshot = [&]() {
            std::vector<int> drawn(8 * 8 * 64, 0);
            for (int cy = 0; cy < 8; ++cy)
                for (int cx = 0; cx < 8; ++cx)
                    for (int p = 0; p < 64; ++p)
                        drawn[(cy * 8 + cx) * 64 + p] = keepers(fader, wanted, cx, cy, p % 8, p / 8) > 0 ? 1 : 0;
            return drawn;
        };
        auto check = [&](const std::vector<int>& before, const char* what, int round, int s) {
            const std::vector<int> after = snapshot();
            for (int cy = 0; cy < 8; ++cy)
                for (int cx = 0; cx < 8; ++cx)
                {
                    if (!cellWanted(wanted, cx, cy))
                        continue;
                    for (int p = 0; p < 64; ++p)
                        ASSERT_FALSE(before[(cy * 8 + cx) * 64 + p] && !after[(cy * 8 + cx) * 64 + p])
                            << what << " seed " << seed << " round " << round << " step " << s << " cell " << cx << ","
                            << cy << " pixel " << p;
                }
        };
        for (int round = 0; round < 8; ++round)
        {
            const std::vector<int> before = snapshot();
            std::vector<Bounds> next;
            partition(rng, square(8, 0, 0), next);
            fader.setDesired(next, false, never, never);
            wanted = next;
            check(before, "change", round, -1);
            for (int s = 0; s < 6; ++s)
            {
                const std::vector<int> prev = snapshot();
                fader.step(frame, fadeTime);
                check(prev, "step", round, s);
            }
        }
    }
}

// No doubled pixel where a leaving chunk is fully replaced by chunks that all fade alike: exactly one chunk draws each
// pixel of such a cell.
TEST(TerrainChunkComplement, a_split_hands_over_with_every_pixel_drawn_by_exactly_one_chunk)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, true, never, never);
    const auto kids = children(parent);
    fader.setDesired(kids, false, never, never);
    for (int s = 0; s < 3; ++s)
    {
        fader.step(frame, fadeTime);
        for (int cy = 0; cy < 2; ++cy)
            for (int cx = 0; cx < 2; ++cx)
                for (int p = 0; p < 64; ++p)
                    ASSERT_EQ(keepers(fader, kids, cx, cy, p % 8, p / 8), 1)
                        << "step " << s << " cell " << cx << "," << cy << " pixel " << p;
    }
}

// A tile's fade code goes through the batch uniform: its own fade, or -1 minus the fade of the draw that replaces it. Both
// are rounded to 1/1024 so that the two draws of a pair read the same number back (exact in a float).
TEST(TerrainChunkComplement, the_batch_uniform_holds_the_fade_code_rounded_to_a_1024th)
{
    Terrain::Builder::BatchData data;
    EXPECT_FALSE(Terrain::Builder::setBatchShare(data, 0.5f, -1.f));
    auto value = vsg::Value<Pipeline::Data::Batch>::create(Terrain::makeBatchUniform(vsg::vec2(), 33.f, 8192.f));
    data.batchUniform = value;
    EXPECT_TRUE(Terrain::Builder::setBatchShare(data, 0.3f, -1.f));
    EXPECT_FLOAT_EQ(value->value().fade, std::round(0.3f * 1024.f) / 1024.f);
    EXPECT_TRUE(Terrain::Builder::setBatchShare(data, 1.f, 0.3f));
    EXPECT_FLOAT_EQ(value->value().fade, -1.f - std::round(0.3f * 1024.f) / 1024.f);
    // The incoming draw (its own fade 0.3) and the complement of 0.3 read back the same number.
    Terrain::Builder::setBatchShare(data, 0.3f, -1.f);
    const float incoming = value->value().fade;
    Terrain::Builder::setBatchShare(data, 1.f, 0.3f);
    EXPECT_EQ(-1.f - value->value().fade, incoming);
    Terrain::Builder::setBatchShare(data, 7.f, -1.f);
    EXPECT_EQ(value->value().fade, 1.f);
    Terrain::Builder::setBatchShare(data, 0.f, 9.f);
    EXPECT_EQ(value->value().fade, -2.f);
}
