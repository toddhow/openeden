// Task 0088 stage b: the live terrain's chunk fade, as a pure state machine (Terrain::ChunkFader).
//
// Written before the implementation (red first against a stub that does nothing). The live terrain's chunks dither in
// when they appear, and out when nothing replaces them; a chunk that IS replaced (split or merged) stays solid until
// the replacement is solid, so the ground never has a hole.

#include <components/terrain/chunkfade.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
    using Terrain::Bounds;
    using Terrain::ChunkFader;

    Bounds square(float size, float x, float y)
    {
        return Bounds{ size, vsg::vec2(x, y) };
    }

    const auto never = [](const Bounds&) { return false; };
    const auto always = [](const Bounds&) { return true; };

    std::vector<Bounds> children(const Bounds& parent)
    {
        const float h = parent.size * 0.5f;
        return { square(h, parent.min.x, parent.min.y), square(h, parent.min.x + h, parent.min.y),
            square(h, parent.min.x, parent.min.y + h), square(h, parent.min.x + h, parent.min.y + h) };
    }

    // One frame of 0.1 s of a 0.4 s fade moves a fade by 0.25.
    constexpr float frame = 0.1f;
    constexpr float fadeTime = 0.4f;
}

TEST(TerrainChunkFade, a_new_chunk_fades_in_over_the_fade_time)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    fader.setDesired(std::vector{ a }, false, never, never);
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 0.f);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(a), 0.25f, 1e-5f);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(a), 0.5f, 1e-5f);
    fader.step(frame, fadeTime);
    fader.step(frame, fadeTime);
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 1.f);
    fader.step(frame, fadeTime);
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 1.f);
    EXPECT_FALSE(fader.active());
}

TEST(TerrainChunkFade, an_instant_view_starts_solid_and_forgets_the_old_chunks)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    const Bounds b = square(1, 5, 5);
    fader.setDesired(std::vector{ a }, true, never, never);
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 1.f);
    fader.setDesired(std::vector{ b }, true, never, never);
    EXPECT_TRUE(fader.leaving().empty());
    EXPECT_FALSE(fader.active());
}

TEST(TerrainChunkFade, a_chunk_where_something_else_just_drew_starts_solid)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    const Bounds b = square(1, 1, 0);
    fader.setDesired(std::vector{ a, b }, false, never, [&a](const Bounds& x) { return x == a; });
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 1.f);
    EXPECT_FLOAT_EQ(fader.fadeOf(b), 0.f);
}

TEST(TerrainChunkFade, a_chunk_that_leaves_with_nothing_over_it_fades_out_and_is_dropped)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    fader.setDesired(std::vector{ a }, true, never, never);
    fader.setDesired(std::vector<Bounds>{}, false, never, never);
    ASSERT_EQ(fader.leaving().size(), 1u);
    EXPECT_FLOAT_EQ(fader.fadeOf(a), 1.f);
    EXPECT_FALSE(fader.step(frame, fadeTime));
    EXPECT_NEAR(fader.fadeOf(a), 0.75f, 1e-5f);
    fader.step(frame, fadeTime);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(a), 0.25f, 1e-5f);
    EXPECT_TRUE(fader.step(frame, fadeTime));
    EXPECT_TRUE(fader.leaving().empty());
    EXPECT_EQ(fader.size(), 0u);
}

TEST(TerrainChunkFade, a_leaving_chunk_that_something_else_covers_is_dropped_at_once)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    fader.setDesired(std::vector{ a }, true, never, never);
    fader.setDesired(std::vector<Bounds>{}, false, always, never);
    EXPECT_TRUE(fader.leaving().empty());
    EXPECT_EQ(fader.size(), 0u);
}

TEST(TerrainChunkFade, a_split_chunk_stays_solid_until_its_children_are_solid)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, true, never, never);
    const auto kids = children(parent);
    fader.setDesired(kids, false, never, never);
    ASSERT_EQ(fader.leaving().size(), 1u);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FALSE(fader.step(frame, fadeTime));
        EXPECT_FLOAT_EQ(fader.fadeOf(parent), 1.f);
    }
    // The fourth step makes the children solid, and the parent goes in the same step.
    EXPECT_TRUE(fader.step(frame, fadeTime));
    for (const Bounds& kid : kids)
        EXPECT_FLOAT_EQ(fader.fadeOf(kid), 1.f);
    EXPECT_TRUE(fader.leaving().empty());
}

TEST(TerrainChunkFade, children_that_merge_into_a_parent_stay_until_the_parent_is_solid)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    const auto kids = children(parent);
    fader.setDesired(kids, true, never, never);
    fader.setDesired(std::vector{ parent }, false, never, never);
    EXPECT_EQ(fader.leaving().size(), 4u);
    fader.step(frame, fadeTime);
    for (const Bounds& kid : kids)
        EXPECT_FLOAT_EQ(fader.fadeOf(kid), 1.f);
    fader.step(frame, fadeTime);
    fader.step(frame, fadeTime);
    EXPECT_TRUE(fader.step(frame, fadeTime));
    EXPECT_TRUE(fader.leaving().empty());
    EXPECT_FLOAT_EQ(fader.fadeOf(parent), 1.f);
}

TEST(TerrainChunkFade, a_leaving_chunk_that_is_wanted_again_goes_on_from_its_value)
{
    ChunkFader fader;
    const Bounds a = square(1, 0, 0);
    fader.setDesired(std::vector{ a }, true, never, never);
    fader.setDesired(std::vector<Bounds>{}, false, never, never);
    fader.step(frame, fadeTime);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(a), 0.5f, 1e-5f);
    fader.setDesired(std::vector{ a }, false, never, never);
    EXPECT_TRUE(fader.leaving().empty());
    EXPECT_NEAR(fader.fadeOf(a), 0.5f, 1e-5f);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(a), 0.75f, 1e-5f);
}

TEST(TerrainChunkFade, a_chunk_that_is_still_fading_in_and_is_replaced_rises_to_solid_first)
{
    ChunkFader fader;
    const Bounds parent = square(2, 0, 0);
    fader.setDesired(std::vector{ parent }, false, never, never);
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(parent), 0.25f, 1e-5f);
    fader.setDesired(children(parent), false, never, never);
    // The old chunk could not be left half drawn under half drawn children: it goes to full while they come in.
    fader.step(frame, fadeTime);
    EXPECT_NEAR(fader.fadeOf(parent), 0.5f, 1e-5f);
    for (int i = 0; i < 10; ++i)
        fader.step(frame, fadeTime);
    EXPECT_TRUE(fader.leaving().empty());
    for (const Bounds& kid : children(parent))
        EXPECT_FLOAT_EQ(fader.fadeOf(kid), 1.f);
}

TEST(TerrainChunkFade, an_unknown_chunk_reads_as_solid)
{
    ChunkFader fader;
    EXPECT_FLOAT_EQ(fader.fadeOf(square(1, 3, 3)), 1.f);
}

TEST(TerrainChunkFade, overlap_ignores_touching_edges)
{
    EXPECT_TRUE(Terrain::boundsOverlap(square(2, 0, 0), square(1, 1, 1)));
    EXPECT_FALSE(Terrain::boundsOverlap(square(1, 0, 0), square(1, 1, 0)));
    EXPECT_FALSE(Terrain::boundsOverlap(square(1, 0, 0), square(1, 0, 1)));
    EXPECT_TRUE(Terrain::boundsOverlap(square(1, 0, 0), square(1, 0, 0)));
}

namespace
{
    // A random partition of an 8 by 8 region into squares of size 1, 2, 4 or 8, some of them left out.
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

    // 1 where some chunk at fade 1 covers the unit cell.
    std::vector<int> solidCells(const ChunkFader& fader, const std::vector<Bounds>& wanted)
    {
        std::vector<Bounds> all = wanted;
        for (const Bounds& leaving : fader.leaving())
            all.push_back(leaving);
        std::vector<int> cells(64, 0);
        for (const Bounds& chunk : all)
        {
            if (fader.fadeOf(chunk) < 1.f)
                continue;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    if (Terrain::boundsOverlap(chunk, square(1, static_cast<float>(x), static_cast<float>(y))))
                        cells[y * 8 + x] = 1;
        }
        return cells;
    }

    bool cellWanted(const std::vector<Bounds>& wanted, int x, int y)
    {
        return std::any_of(wanted.begin(), wanted.end(), [x, y](const Bounds& b) {
            return Terrain::boundsOverlap(b, square(1, static_cast<float>(x), static_cast<float>(y)));
        });
    }
}

// The invariant that matters: a cell that was drawn solid and that some wanted chunk covers is still drawn solid after
// every step and every change of the wanted set, until the wanted chunk is itself solid. No hole.
TEST(TerrainChunkFade, no_cell_that_is_wanted_ever_loses_a_solid_cover_before_its_replacement_is_solid)
{
    for (unsigned seed = 1; seed <= 300; ++seed)
    {
        std::mt19937 rng(seed);
        ChunkFader fader;
        std::vector<Bounds> wanted;
        partition(rng, square(8, 0, 0), wanted);
        fader.setDesired(wanted, true, never, never);
        for (int round = 0; round < 12; ++round)
        {
            const std::vector<int> before = solidCells(fader, wanted);
            std::vector<Bounds> next;
            partition(rng, square(8, 0, 0), next);
            fader.setDesired(next, false, never, never);
            wanted = next;
            // Right after the change nothing may have gone.
            const std::vector<int> after = solidCells(fader, wanted);
            for (int i = 0; i < 64; ++i)
            {
                if (before[i] && cellWanted(wanted, i % 8, i / 8))
                {
                    ASSERT_TRUE(after[i])
                        << "seed " << seed << " round " << round << " cell " << i << " after the change";
                }
            }
            for (int s = 0; s < 6; ++s)
            {
                const std::vector<int> prev = solidCells(fader, wanted);
                fader.step(frame, fadeTime);
                const std::vector<int> now = solidCells(fader, wanted);
                for (int i = 0; i < 64; ++i)
                {
                    if (prev[i] && cellWanted(wanted, i % 8, i / 8))
                    {
                        ASSERT_TRUE(now[i]) << "seed " << seed << " round " << round << " step " << s << " cell " << i;
                    }
                }
            }
        }
        // Left alone long enough, every wanted chunk is solid and nothing leaves.
        for (int s = 0; s < 20; ++s)
            fader.step(frame, fadeTime);
        EXPECT_TRUE(fader.leaving().empty()) << seed;
        for (const Bounds& chunk : wanted)
            EXPECT_FLOAT_EQ(fader.fadeOf(chunk), 1.f) << seed;
    }
}

TEST(TerrainChunkFade, the_same_calls_give_the_same_result)
{
    auto run = [](unsigned seed) {
        std::mt19937 rng(seed);
        ChunkFader fader;
        std::vector<float> trace;
        for (int round = 0; round < 8; ++round)
        {
            std::vector<Bounds> next;
            partition(rng, square(8, 0, 0), next);
            fader.setDesired(next, round == 0, never, never);
            for (int s = 0; s < 3; ++s)
            {
                fader.step(frame, fadeTime);
                for (const Bounds& b : next)
                    trace.push_back(fader.fadeOf(b));
                for (const Bounds& b : fader.leaving())
                    trace.push_back(fader.fadeOf(b));
            }
        }
        return trace;
    };
    EXPECT_EQ(run(7), run(7));
}

TEST(TerrainChunkFade, a_new_view_shares_area_with_the_old_one_only_when_a_chunk_overlaps)
{
    ChunkFader fader;
    EXPECT_FALSE(fader.sharesAreaWith(std::vector{ square(1, 0, 0) }));
    fader.setDesired(std::vector{ square(2, 0, 0), square(1, 6, 6) }, true, never, never);
    EXPECT_TRUE(fader.sharesAreaWith(std::vector{ square(1, 1, 1) }));
    EXPECT_FALSE(fader.sharesAreaWith(std::vector{ square(1, 2, 0), square(1, 4, 4) }));
    // A leaving chunk counts as well: it is still drawn.
    fader.setDesired(std::vector<Bounds>{}, false, never, never);
    EXPECT_TRUE(fader.sharesAreaWith(std::vector{ square(1, 6, 6) }));
}
