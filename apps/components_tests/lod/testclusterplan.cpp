// Task 0059 (stage S4d): the hand-over rules between object clusters and the live objects. Written from the
// specification in components/lod/clusterfade.hpp and components/lod/clusterplan.hpp only. No GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/clusterfade.hpp>
#include <components/lod/clusterplan.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float frameSeconds = 1.0f / 60.0f;

    TileFade tileAt(FadePhase phase, float fade)
    {
        TileFade tile;
        tile.mPhase = phase;
        tile.mFade = fade;
        return tile;
    }

    CoverTile coverTile(FadePhase phase, float fade, bool wanted, std::vector<Cell> cells)
    {
        CoverTile tile;
        tile.mFade = tileAt(phase, fade);
        tile.mWanted = wanted;
        tile.mCells = std::move(cells);
        return tile;
    }

    // ---- the fade transitions ----------------------------------------------------------------------------------

    TEST(LodClusterPlan, fading_out_without_live_objects_turns_to_reclaiming_and_never_exceeds_one)
    {
        TileFade tile = tileAt(FadePhase::FadingOut, 0.5f);
        stepTileFade(tile, false, false, 0.1f, 1.0f);
        EXPECT_EQ(tile.mPhase, FadePhase::Reclaiming);
        EXPECT_GE(tile.mFade, 0.5f);
        EXPECT_LE(tile.mFade, 1.0f);
        const float before = tile.mFade;
        stepTileFade(tile, false, false, 0.1f, 1.0f);
        EXPECT_EQ(tile.mPhase, FadePhase::Reclaiming);
        EXPECT_GT(tile.mFade, before);
        EXPECT_LE(tile.mFade, 1.0f);
    }

    TEST(LodClusterPlan, reclaiming_with_live_objects_back_turns_to_fading_out)
    {
        TileFade tile = tileAt(FadePhase::Reclaiming, 0.6f);
        stepTileFade(tile, false, true, 0.1f, 1.0f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingOut);
        EXPECT_LE(tile.mFade, 0.6f + 1e-5f);
        EXPECT_GE(tile.mFade, 0.0f);
    }

    TEST(LodClusterPlan, reclaiming_wanted_again_turns_to_fading_in)
    {
        TileFade tile = tileAt(FadePhase::Reclaiming, 0.6f);
        stepTileFade(tile, true, false, 0.1f, 1.0f);
        EXPECT_EQ(tile.mPhase, FadePhase::FadingIn);
        EXPECT_GE(tile.mFade, 0.6f - 1e-5f);
        EXPECT_LE(tile.mFade, 1.0f);
    }

    TEST(LodClusterPlan, reclaiming_reaching_one_waits_for_live_with_the_fade_exactly_one)
    {
        TileFade tile = tileAt(FadePhase::Reclaiming, 0.95f);
        stepTileFade(tile, false, false, 0.1f, 1.0f);
        EXPECT_EQ(tile.mPhase, FadePhase::WaitingForLive);
        EXPECT_EQ(tile.mFade, 1.0f);
        EXPECT_FALSE(tileCoversStatics(tile));
        EXPECT_TRUE(tileIsDrawn(tile));
    }

    TEST(LodClusterPlan, reclaiming_does_not_cover_statics_but_is_drawn)
    {
        const TileFade tile = tileAt(FadePhase::Reclaiming, 1.0f);
        EXPECT_FALSE(tileCoversStatics(tile));
        EXPECT_TRUE(tileIsDrawn(tile));
    }

    TEST(LodClusterPlan, a_live_chunk_lost_during_fading_out_keeps_the_fade_in_range_and_covers_only_when_solid)
    {
        TileFade tile = tileAt(FadePhase::Solid, 1.0f);
        bool everReclaimed = false;
        for (int frame = 0; frame < 300; ++frame)
        {
            const bool liveReady = frame < 10 ? false : (frame < 15 ? true : (frame < 35 ? false : true));
            stepTileFade(tile, false, liveReady, frameSeconds, 1.0f);
            EXPECT_GE(tile.mFade, 0.0f) << "frame " << frame;
            EXPECT_LE(tile.mFade, 1.0f) << "frame " << frame;
            if (tile.mPhase == FadePhase::Reclaiming)
                everReclaimed = true;
            if (tileCoversStatics(tile))
                EXPECT_EQ(tile.mFade, 1.0f) << "frame " << frame;
            EXPECT_EQ(tileCoversStatics(tile), tile.mPhase == FadePhase::Solid) << "frame " << frame;
        }
        EXPECT_TRUE(everReclaimed);
        EXPECT_EQ(tile.mPhase, FadePhase::Hidden);
        EXPECT_FALSE(tileIsDrawn(tile));
    }

    // ---- stepCoverage ----------------------------------------------------------------------------------------

    std::vector<std::size_t> solidIndices(const std::vector<CoverTile>& tiles)
    {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].mFade.mPhase == FadePhase::Solid)
                result.push_back(i);
        return result;
    }

    std::set<Cell> expectedSolidCells(const std::vector<CoverTile>& tiles)
    {
        std::set<Cell> result;
        for (const CoverTile& tile : tiles)
            if (tile.mFade.mPhase == FadePhase::Solid)
                result.insert(tile.mCells.begin(), tile.mCells.end());
        return result;
    }

    TEST(LodClusterPlan, a_single_tile_life_reports_a_change_exactly_when_the_solid_set_changes)
    {
        std::vector<CoverTile> tiles{ coverTile(FadePhase::Hidden, 0, true, { { 0, 0 }, { 1, 0 } }) };
        const float dt = 0.1f;
        float unwantedFor = -1;
        int changes = 0;
        for (int frame = 0; frame < 80; ++frame)
        {
            if (frame == 30)
            {
                tiles[0].mWanted = false;
                unwantedFor = 0;
            }
            const std::vector<std::size_t> before = solidIndices(tiles);
            const bool ready = unwantedFor >= 1.0f;
            const bool changed = stepCoverage(tiles, dt, 0.5f, [ready](const Cell&) { return ready; });
            if (unwantedFor >= 0)
                unwantedFor += dt;
            EXPECT_EQ(changed, before != solidIndices(tiles)) << "frame " << frame;
            EXPECT_EQ(solidCells(tiles), expectedSolidCells(tiles)) << "frame " << frame;
            if (changed)
                ++changes;
        }
        EXPECT_EQ(changes, 2); // became solid, then left solid
        EXPECT_EQ(tiles[0].mFade.mPhase, FadePhase::Hidden);
    }

    // Coarse tile A over four cells, finer tile B over two of them.
    struct Overlap
    {
        std::vector<CoverTile> mTiles;
        std::vector<Cell> mCalls; // the cells asked after B became Solid
        bool mReadyForOwnCells = false;
        bool mBSolid = false;
    };

    void stepOverlap(Overlap& overlap)
    {
        const std::set<Cell> shared{ { 0, 0 }, { 1, 0 } };
        const bool bSolidBefore = overlap.mBSolid;
        stepCoverage(overlap.mTiles, 0.1f, 0.5f, [&overlap, &shared, bSolidBefore](const Cell& cell) {
            if (bSolidBefore)
                overlap.mCalls.push_back(cell);
            if (shared.count(cell) != 0)
                return false; // the shared cells never become ready: only B's cover can account for them
            return overlap.mReadyForOwnCells;
        });
        overlap.mBSolid = overlap.mTiles[1].mFade.mPhase == FadePhase::Solid;
    }

    // A is Solid and has just become unwanted; B is Hidden and has just become wanted.
    Overlap startOverlap(bool readyForOwnCells)
    {
        Overlap overlap;
        overlap.mReadyForOwnCells = readyForOwnCells;
        overlap.mTiles.push_back(coverTile(FadePhase::Solid, 1.0f, false, { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }));
        overlap.mTiles.push_back(coverTile(FadePhase::Hidden, 0.0f, true, { { 0, 0 }, { 1, 0 } }));
        return overlap;
    }

    void runUntilBSolid(Overlap& overlap)
    {
        for (int frame = 0; frame < 100 && !overlap.mBSolid; ++frame)
        {
            stepOverlap(overlap);
            // A does not fade out before B is solid: the live objects are not ready for the shared cells.
            if (!overlap.mBSolid)
                EXPECT_EQ(overlap.mTiles[0].mFade.mPhase, FadePhase::WaitingForLive);
        }
        ASSERT_TRUE(overlap.mBSolid);
    }

    TEST(LodClusterPlan, the_finer_tile_takes_over_but_the_coarse_tile_waits_for_the_cells_only_it_lists)
    {
        Overlap overlap = startOverlap(false);
        runUntilBSolid(overlap);
        overlap.mCalls.clear();
        for (int frame = 0; frame < 20; ++frame)
        {
            stepOverlap(overlap);
            EXPECT_EQ(overlap.mTiles[0].mFade.mPhase, FadePhase::WaitingForLive) << "frame " << frame;
            EXPECT_EQ(overlap.mTiles[1].mFade.mPhase, FadePhase::Solid);
            EXPECT_TRUE(tileIsDrawn(overlap.mTiles[0].mFade));
        }
        // The shared cells were never asked about, and the cells only A lists were.
        for (const Cell& cell : overlap.mCalls)
            EXPECT_TRUE(cell == Cell(0, 1) || cell == Cell(1, 1)) << cell.first << "," << cell.second;
        EXPECT_FALSE(overlap.mCalls.empty());
    }

    TEST(LodClusterPlan, the_coarse_tile_fades_out_once_its_own_cells_are_ready_while_the_finer_tile_covers_the_rest)
    {
        Overlap overlap = startOverlap(true);
        runUntilBSolid(overlap);
        overlap.mCalls.clear();
        bool sawFadingOut = false;
        for (int frame = 0; frame < 60; ++frame)
        {
            stepOverlap(overlap);
            if (overlap.mTiles[0].mFade.mPhase == FadePhase::FadingOut)
                sawFadingOut = true;
            EXPECT_EQ(overlap.mTiles[1].mFade.mPhase, FadePhase::Solid);
        }
        EXPECT_TRUE(sawFadingOut);
        EXPECT_EQ(overlap.mTiles[0].mFade.mPhase, FadePhase::Hidden);
        for (const Cell& cell : overlap.mCalls)
            EXPECT_TRUE(cell == Cell(0, 1) || cell == Cell(1, 1)) << cell.first << "," << cell.second;
        // Only B's cells are covered at the end.
        EXPECT_EQ(solidCells(overlap.mTiles), (std::set<Cell>{ { 0, 0 }, { 1, 0 } }));
    }

    TEST(LodClusterPlan, solid_cells_are_the_union_of_the_cells_of_solid_tiles_only)
    {
        std::vector<CoverTile> tiles;
        tiles.push_back(coverTile(FadePhase::Solid, 1.0f, true, { { 0, 0 }, { 1, 0 } }));
        tiles.push_back(coverTile(FadePhase::WaitingForLive, 1.0f, false, { { 5, 5 } }));
        tiles.push_back(coverTile(FadePhase::FadingIn, 0.5f, true, { { 6, 6 } }));
        tiles.push_back(coverTile(FadePhase::Reclaiming, 0.5f, false, { { 7, 7 } }));
        tiles.push_back(coverTile(FadePhase::Solid, 1.0f, true, { { 1, 0 }, { 2, 0 } }));
        tiles.push_back(coverTile(FadePhase::Hidden, 0.0f, false, { { 8, 8 } }));
        EXPECT_EQ(solidCells(tiles), (std::set<Cell>{ { 0, 0 }, { 1, 0 }, { 2, 0 } }));
    }

    TEST(LodClusterPlan, a_solid_tile_with_no_cells_covers_nothing)
    {
        std::vector<CoverTile> tiles;
        tiles.push_back(coverTile(FadePhase::Solid, 1.0f, true, {}));
        EXPECT_TRUE(solidCells(tiles).empty());
        stepCoverage(tiles, 0.1f, 0.5f, [](const Cell&) { return true; });
        EXPECT_TRUE(solidCells(tiles).empty());
    }

    struct TileState
    {
        FadePhase mPhase;
        float mFade;
    };

    std::vector<TileState> runThreeTiles(const std::vector<std::size_t>& order, int frames)
    {
        std::vector<CoverTile> base;
        base.push_back(coverTile(FadePhase::Solid, 1.0f, false, { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }));
        base.push_back(coverTile(FadePhase::Hidden, 0.0f, true, { { 0, 0 }, { 1, 0 } }));
        base.push_back(coverTile(FadePhase::Solid, 1.0f, true, { { 4, 4 } }));
        std::vector<CoverTile> tiles;
        for (std::size_t index : order)
            tiles.push_back(base[index]);
        for (int frame = 0; frame < frames; ++frame)
        {
            const bool ready = frame >= 3;
            stepCoverage(tiles, 0.1f, 0.5f, [ready](const Cell& cell) { return cell.second == 1 && ready; });
        }
        std::vector<TileState> result(order.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            result[order[i]] = TileState{ tiles[i].mFade.mPhase, tiles[i].mFade.mFade };
        return result;
    }

    TEST(LodClusterPlan, the_result_does_not_depend_on_the_order_of_the_tiles)
    {
        std::vector<std::size_t> order(3);
        std::iota(order.begin(), order.end(), std::size_t(0));
        const std::vector<TileState> expected = runThreeTiles(order, 25);
        EXPECT_EQ(expected[0].mPhase, FadePhase::Hidden); // the scenario does run through to the end
        do
        {
            const std::vector<TileState> actual = runThreeTiles(order, 25);
            for (std::size_t i = 0; i < 3; ++i)
            {
                EXPECT_EQ(actual[i].mPhase, expected[i].mPhase) << "tile " << i;
                EXPECT_EQ(actual[i].mFade, expected[i].mFade) << "tile " << i;
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }

    TEST(LodClusterPlan, live_ready_is_not_asked_when_no_tile_is_waiting_fading_out_or_reclaiming)
    {
        std::vector<CoverTile> tiles;
        tiles.push_back(coverTile(FadePhase::Hidden, 0.0f, false, { { 0, 0 } }));
        tiles.push_back(coverTile(FadePhase::Hidden, 0.0f, true, { { 1, 0 } }));
        tiles.push_back(coverTile(FadePhase::FadingIn, 0.5f, true, { { 2, 0 } }));
        tiles.push_back(coverTile(FadePhase::Solid, 1.0f, true, { { 3, 0 } }));
        int calls = 0;
        for (int frame = 0; frame < 3; ++frame)
        {
            stepCoverage(tiles, 0.01f, 10.0f, [&calls](const Cell&) {
                ++calls;
                return true;
            });
        }
        EXPECT_EQ(calls, 0);
    }

    TEST(LodClusterPlan, stepping_is_deterministic)
    {
        const auto run = [] {
            Overlap overlap = startOverlap(true);
            std::vector<TileState> states;
            for (int frame = 0; frame < 60; ++frame)
            {
                stepOverlap(overlap);
                for (const CoverTile& tile : overlap.mTiles)
                    states.push_back(TileState{ tile.mFade.mPhase, tile.mFade.mFade });
            }
            return states;
        };
        const std::vector<TileState> first = run();
        const std::vector<TileState> second = run();
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            EXPECT_EQ(first[i].mPhase, second[i].mPhase);
            EXPECT_EQ(first[i].mFade, second[i].mFade);
        }
    }

    // ---- liveStaticsReady ----------------------------------------------------------------------------------------

    TEST(LodClusterPlan, live_statics_ready_follows_the_documented_truth_table)
    {
        const std::optional<bool> shownStates[]
            = { std::nullopt, std::optional<bool>(false), std::optional<bool>(true) };
        for (bool clustered : { false, true })
        {
            for (bool rebuilding : { false, true })
            {
                for (const std::optional<bool>& shown : shownStates)
                {
                    for (bool chunkExpected : { false, true })
                    {
                        bool expected = false;
                        if (clustered || rebuilding)
                            expected = false;
                        else if (shown.has_value())
                            expected = !*shown;
                        else
                            expected = !chunkExpected;
                        EXPECT_EQ(liveStaticsReady(clustered, rebuilding, shown, chunkExpected), expected)
                            << clustered << rebuilding << (shown ? (*shown ? "T" : "F") : "-") << chunkExpected;
                    }
                }
            }
        }
    }

    // ---- clusterDrawsReference -------------------------------------------------------------------------------------

    TEST(LodClusterPlan, cluster_draws_reference_finds_present_references_including_index_zero_and_file_all_ones)
    {
        const std::vector<ClusterRef> refs{ { 0, 0 }, { 0, 3 }, { 5, 1 }, { 5, 2 }, { 9, 0xFFFFFFFFu } };
        ASSERT_TRUE(std::is_sorted(refs.begin(), refs.end()));
        for (const ClusterRef& ref : refs)
            EXPECT_TRUE(clusterDrawsReference(refs, ref));
        EXPECT_TRUE(clusterDrawsReference(refs, ClusterRef{ 0, 0 }));
        EXPECT_TRUE(clusterDrawsReference(refs, ClusterRef{ 9, 0xFFFFFFFFu }));
    }

    TEST(LodClusterPlan, cluster_draws_reference_refuses_absent_references)
    {
        const std::vector<ClusterRef> refs{ { 1, 0 }, { 5, 1 }, { 5, 2 } };
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 0, 0 }));
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 6, 1 }));
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 0xFFFFFFFFu, 0xFFFFFFFFu }));
    }

    TEST(LodClusterPlan, cluster_draws_reference_refuses_everything_in_an_empty_list)
    {
        const std::vector<ClusterRef> refs;
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 0, 0 }));
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 7, 3 }));
    }

    TEST(LodClusterPlan, cluster_draws_reference_needs_both_fields_to_match)
    {
        const std::vector<ClusterRef> refs{ { 5, 1 }, { 8, 2 } };
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 5, 2 })); // index matches, content file does not
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 8, 1 }));
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 4, 1 })); // content file matches, index does not
        EXPECT_FALSE(clusterDrawsReference(refs, ClusterRef{ 4, 2 }));
    }
}
