// Task 0087 stage c: the terrain tile fade as a pure state machine (components/lod/terrainfade.*).
//
// Written before the implementation, from the spec: a tile that appears fades in over what drew its square; a tile
// that is replaced stays solid until the replacement is solid and then goes at once; a tile given to the live terrain
// fades out over it; a fade can turn around; nothing that was drawn opaque is ever left uncovered where the live
// terrain does not draw. The last is checked on random sequences over a small tile tree.
#include <gtest/gtest.h>

#include <components/lod/terrainfade.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float fadeSeconds = 0.4f;
    constexpr float frame = 1.f / 60.f;

    struct Rig
    {
        std::vector<TerrainFadeState> mStates;
        std::vector<TerrainFadeInput> mInputs;
        std::vector<TerrainFadeEvent> mEvents;

        explicit Rig(std::size_t count)
            : mStates(count)
            , mInputs(count)
            , mEvents(count, TerrainFadeEvent::None)
        {
        }
        void step(float seconds = frame) { stepTerrainFades(mStates, mInputs, mEvents, seconds, fadeSeconds); }
        void run(int frames)
        {
            for (int i = 0; i < frames; ++i)
                step();
        }
    };

    TEST(TerrainFade, a_new_tile_fades_in_over_the_fade_time_and_covers_only_when_solid)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.step(0.1f);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::FadingIn);
        EXPECT_NEAR(rig.mStates[0].mFade.mFade, 0.25f, 1e-5);
        EXPECT_FALSE(rig.mStates[0].mCovers);
        rig.step(0.1f);
        rig.step(0.1f);
        EXPECT_FALSE(rig.mStates[0].mCovers);
        rig.step(0.1f);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Solid);
        EXPECT_EQ(rig.mStates[0].mFade.mFade, 1.f);
        EXPECT_TRUE(rig.mStates[0].mCovers);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::None);
    }

    TEST(TerrainFade, a_tile_given_to_the_live_terrain_fades_out_and_stops_covering_at_once)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        ASSERT_TRUE(rig.mStates[0].mCovers);
        rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::ToLive };
        rig.step(0.1f);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::StartedLeaving);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::FadingOut);
        EXPECT_NEAR(rig.mStates[0].mFade.mFade, 0.75f, 1e-5);
        EXPECT_FALSE(rig.mStates[0].mCovers) << "the live terrain draws beneath from the first frame of the fade";
        rig.step(0.1f);
        rig.step(0.1f);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::None);
        rig.step(0.1f);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::Finished);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Hidden);
        EXPECT_FALSE(rig.mStates[0].mCovers);
    }

    TEST(TerrainFade, a_refined_tile_covered_by_solid_tiles_goes_at_once)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::Covered };
        rig.step();
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::Remove);
        EXPECT_FALSE(rig.mStates[0].mCovers);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Hidden);
    }

    TEST(TerrainFade, a_refined_tile_stays_solid_while_something_depends_on_it)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::Stay };
        rig.run(600);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Solid);
        EXPECT_TRUE(rig.mStates[0].mCovers);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::None);
    }

    TEST(TerrainFade, a_tile_under_a_drawn_ancestor_stays_until_the_ancestor_is_solid_then_goes_in_that_frame)
    {
        // 0: the parent, chosen; 1: a child, covered.
        Rig rig(2);
        rig.mInputs[1] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        ASSERT_TRUE(rig.mStates[1].mCovers);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.mInputs[1] = { true, TileChoice::Covered, 0, RefinedGate::Stay };
        int frames = 0;
        while (!rig.mStates[0].mCovers && frames < 100)
        {
            rig.step();
            ++frames;
            if (!rig.mStates[0].mCovers)
            {
                EXPECT_NE(rig.mEvents[1], TerrainFadeEvent::Remove);
                EXPECT_TRUE(rig.mStates[1].mCovers) << "the child still draws the square while the parent fades in";
            }
        }
        ASSERT_TRUE(rig.mStates[0].mCovers);
        EXPECT_EQ(rig.mEvents[1], TerrainFadeEvent::Remove) << "gone in the frame the parent became solid";
        EXPECT_FALSE(rig.mStates[1].mCovers);
    }

    TEST(TerrainFade, the_ancestor_is_the_nearest_one_that_is_chosen_as_a_tile)
    {
        // 0: chosen tile; 1: covered, in between (not drawn); 2: covered, built, under 1.
        Rig rig(3);
        rig.mInputs[2] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.mInputs[1] = { false, TileChoice::Covered, 0, RefinedGate::Stay };
        rig.mInputs[2] = { true, TileChoice::Covered, 1, RefinedGate::Stay };
        rig.run(60);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Solid);
        EXPECT_EQ(rig.mStates[2].mFade.mPhase, FadePhase::Hidden) << "removed once tile 0 was solid";
    }

    TEST(TerrainFade, a_fade_out_turns_around_from_the_value_it_has)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::ToLive };
        rig.step(0.1f);
        rig.step(0.1f);
        const float mid = rig.mStates[0].mFade.mFade;
        EXPECT_NEAR(mid, 0.5f, 1e-5);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.step(0.1f);
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::FadingIn);
        EXPECT_NEAR(rig.mStates[0].mFade.mFade, mid + 0.25f, 1e-5) << "rises from where it was, no jump";
        EXPECT_FALSE(rig.mStates[0].mCovers);
    }

    TEST(TerrainFade, a_fade_in_turns_around_from_the_value_it_has)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.step(0.1f);
        rig.step(0.1f);
        EXPECT_NEAR(rig.mStates[0].mFade.mFade, 0.5f, 1e-5);
        rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::ToLive };
        rig.step(0.1f);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::StartedLeaving);
        EXPECT_NEAR(rig.mStates[0].mFade.mFade, 0.25f, 1e-5);
    }

    TEST(TerrainFade, a_tile_that_is_not_built_is_reset)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0].mBuilt = false;
        rig.step();
        EXPECT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::Hidden);
        EXPECT_FALSE(rig.mStates[0].mCovers);
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::None);
    }

    TEST(TerrainFade, a_tile_never_chosen_again_goes_to_the_live_terrain)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.run(40);
        rig.mInputs[0].mChoice = TileChoice::Unset;
        rig.step();
        EXPECT_EQ(rig.mEvents[0], TerrainFadeEvent::StartedLeaving);
    }

    TEST(TerrainFade, no_time_passing_changes_nothing)
    {
        Rig rig(1);
        rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
        rig.step(0.1f);
        const float before = rig.mStates[0].mFade.mFade;
        rig.step(0.f);
        EXPECT_EQ(rig.mStates[0].mFade.mFade, before);
    }

    // ---- random sequences over a small tree --------------------------------------------------------------------

    // Tile 0 is the root (level 2); tiles 1 to 4 are its four children (level 1). A cell is one of the sixteen leaf
    // cells; child k covers cells 4k to 4k + 3.
    struct Sim
    {
        static constexpr std::size_t tiles = 5;
        Rig mRig{ tiles };
        std::vector<TileChoice> mChoice = std::vector<TileChoice>(tiles, TileChoice::Unset);
        std::vector<bool> mBuilt = std::vector<bool>(tiles, false);
        std::vector<bool> mFailed = std::vector<bool>(tiles, false);
        std::vector<int> mBuildIn = std::vector<int>(tiles, 0);
        bool mLiveRange = false;
        std::mt19937 mRandom;
        std::vector<bool> mCovered = std::vector<bool>(16, false);
        int mGaps = 0;
        int mJumps = 0;
        int mEventErrors = 0;

        Sim(unsigned seed, bool liveRange)
            : mLiveRange(liveRange)
            , mRandom(seed)
        {
            mRig.mInputs[0].mParent = -1;
            for (std::size_t i = 1; i < tiles; ++i)
                mRig.mInputs[i].mParent = 0;
        }

        // The selection's choices after selectTileTree: a root that is a Tile covers its children.
        void chooseSelection()
        {
            std::uniform_int_distribution<int> pick(0, 99);
            if (pick(mRandom) < 50)
            {
                mChoice[0] = TileChoice::Tile;
                for (std::size_t i = 1; i < tiles; ++i)
                    mChoice[i] = TileChoice::Covered;
            }
            else
            {
                mChoice[0] = TileChoice::Live;
                for (std::size_t i = 1; i < tiles; ++i)
                    mChoice[i] = pick(mRandom) < 70 ? TileChoice::Tile : TileChoice::Live;
            }
        }

        bool solid(std::size_t i) const { return mBuilt[i] && terrainTileIsSolid(mRig.mStates[i]); }

        RefinedGate gateOf(std::size_t i) const
        {
            if (mChoice[i] != TileChoice::Live)
                return RefinedGate::Stay;
            if (mLiveRange)
                return RefinedGate::ToLive;
            if (i != 0)
                return RefinedGate::Stay; // a leaf: nothing finer, nothing else draws it
            // The root: finer tiles must cover its whole square, every one chosen as a tile and solid.
            for (std::size_t c = 1; c < tiles; ++c)
            {
                if (mChoice[c] != TileChoice::Tile || mFailed[c] || !solid(c))
                    return RefinedGate::Stay;
            }
            return RefinedGate::Covered;
        }

        void step()
        {
            // Builds: a chosen tile is built after a delay unless it fails.
            for (std::size_t i = 0; i < tiles; ++i)
            {
                if (mChoice[i] == TileChoice::Tile && !mBuilt[i] && !mFailed[i])
                {
                    if (mBuildIn[i] <= 0)
                        mBuilt[i] = true;
                    else
                        --mBuildIn[i];
                }
            }
            for (std::size_t i = 0; i < tiles; ++i)
            {
                mRig.mInputs[i].mBuilt = mBuilt[i];
                mRig.mInputs[i].mChoice = mChoice[i];
                mRig.mInputs[i].mGate = gateOf(i);
            }
            const auto before = mRig.mStates;
            mRig.step();
            for (std::size_t i = 0; i < tiles; ++i)
            {
                const TerrainFadeState& state = mRig.mStates[i];
                const TerrainFadeEvent event = mRig.mEvents[i];
                if (event == TerrainFadeEvent::Remove || event == TerrainFadeEvent::Finished)
                {
                    if (!mBuilt[i])
                        ++mEventErrors;
                    mBuilt[i] = false;
                    mBuildIn[i] = 0;
                }
                if (!mBuilt[i] && (state.mCovers || state.mFade.mPhase != FadePhase::Hidden))
                    ++mEventErrors;
                if (state.mFade.mFade < 0.f || state.mFade.mFade > 1.f)
                    ++mEventErrors;
                if (state.mFade.mPhase == FadePhase::Solid && state.mFade.mFade != 1.f)
                    ++mEventErrors;
                // No jump: a tile's share moves by at most one frame's step unless it was reset.
                if (mBuilt[i] && event == TerrainFadeEvent::None && before[i].mFade.mPhase != FadePhase::Hidden
                    && std::abs(state.mFade.mFade - before[i].mFade.mFade) > frame / fadeSeconds + 1e-5f)
                    ++mJumps;
            }
        }

        // Cells covered by a tile that is covering now (the live terrain leaves those out).
        std::vector<bool> coveredCells() const
        {
            std::vector<bool> cells(16, false);
            for (std::size_t i = 0; i < tiles; ++i)
            {
                if (!mBuilt[i] || !mRig.mStates[i].mCovers)
                    continue;
                const int first = i == 0 ? 0 : static_cast<int>(i - 1) * 4;
                const int count = i == 0 ? 16 : 4;
                for (int c = first; c < first + count; ++c)
                    cells[static_cast<std::size_t>(c)] = true;
            }
            return cells;
        }

        void run(int steps, bool selectionChanges)
        {
            std::uniform_int_distribution<int> pick(0, 99);
            for (int s = 0; s < steps; ++s)
            {
                if (selectionChanges && pick(mRandom) < 4)
                {
                    chooseSelection();
                    for (std::size_t i = 0; i < tiles; ++i)
                    {
                        if (mChoice[i] == TileChoice::Tile && !mBuilt[i])
                            mBuildIn[i] = pick(mRandom) % 40;
                        // A tile that fails to build keeps failing, one in ten of the children.
                        if (i > 0 && mChoice[i] == TileChoice::Tile && !mBuilt[i] && pick(mRandom) < 10)
                            mFailed[i] = true;
                    }
                }
                step();
                // Outside the live range, a cell that something opaque covered stays covered.
                if (!mLiveRange)
                {
                    const std::vector<bool> now = coveredCells();
                    for (std::size_t c = 0; c < 16; ++c)
                    {
                        if (mCovered[c] && !now[c])
                            ++mGaps;
                    }
                    mCovered = now;
                }
            }
        }
    };

    TEST(TerrainFadeSim, nothing_opaque_is_left_uncovered_where_the_live_terrain_does_not_draw)
    {
        for (unsigned seed = 1; seed <= 300; ++seed)
        {
            SCOPED_TRACE(seed);
            Sim sim(seed, false);
            sim.chooseSelection();
            sim.run(1500, true);
            EXPECT_EQ(sim.mGaps, 0);
            EXPECT_EQ(sim.mJumps, 0);
            EXPECT_EQ(sim.mEventErrors, 0);
        }
    }

    TEST(TerrainFadeSim, the_machine_keeps_its_invariants_where_the_live_terrain_draws)
    {
        for (unsigned seed = 1; seed <= 300; ++seed)
        {
            SCOPED_TRACE(seed);
            Sim sim(seed, true);
            sim.chooseSelection();
            sim.run(1500, true);
            EXPECT_EQ(sim.mJumps, 0);
            EXPECT_EQ(sim.mEventErrors, 0);
        }
    }

    TEST(TerrainFadeSim, a_selection_that_stops_changing_settles)
    {
        for (unsigned seed = 1; seed <= 100; ++seed)
        {
            SCOPED_TRACE(seed);
            for (const bool liveRange : { false, true })
            {
                Sim sim(seed, liveRange);
                sim.chooseSelection();
                sim.run(300, true);
                sim.run(400, false);
                // Nothing is mid-fade: every built tile is solid, or its fade is over.
                for (std::size_t i = 0; i < Sim::tiles; ++i)
                {
                    if (!sim.mBuilt[i])
                        continue;
                    EXPECT_TRUE(sim.solid(i))
                        << "tile " << i << " phase " << static_cast<int>(sim.mRig.mStates[i].mFade.mPhase);
                }
            }
        }
    }

    TEST(TerrainFadeSim, two_runs_of_the_same_sequence_are_identical)
    {
        for (const bool liveRange : { false, true })
        {
            Sim a(7, liveRange);
            Sim b(7, liveRange);
            a.chooseSelection();
            b.chooseSelection();
            for (int i = 0; i < 200; ++i)
            {
                a.run(10, true);
                b.run(10, true);
                for (std::size_t t = 0; t < Sim::tiles; ++t)
                {
                    ASSERT_EQ(a.mRig.mStates[t].mFade.mFade, b.mRig.mStates[t].mFade.mFade);
                    ASSERT_EQ(a.mRig.mStates[t].mFade.mPhase, b.mRig.mStates[t].mFade.mPhase);
                    ASSERT_EQ(a.mRig.mStates[t].mCovers, b.mRig.mStates[t].mCovers);
                }
            }
        }
    }

    TEST(TerrainFadeSim, a_replacement_that_fails_to_build_keeps_the_old_tile_at_full)
    {
        // Root solid, then refined to four children of which one never builds: the root stays at full, outside the
        // live range, and covers its whole square for good.
        Sim sim(1, false);
        sim.mChoice[0] = TileChoice::Tile;
        for (std::size_t i = 1; i < Sim::tiles; ++i)
            sim.mChoice[i] = TileChoice::Covered;
        sim.run(80, false);
        ASSERT_TRUE(sim.solid(0));
        sim.mChoice[0] = TileChoice::Live;
        for (std::size_t i = 1; i < Sim::tiles; ++i)
            sim.mChoice[i] = TileChoice::Tile;
        sim.mFailed[3] = true;
        sim.run(600, false);
        EXPECT_TRUE(sim.solid(0));
        EXPECT_EQ(sim.mRig.mStates[0].mFade.mFade, 1.f);
        EXPECT_EQ(sim.mGaps, 0);
    }

    TEST(TerrainFadeSim, a_reversal_in_the_middle_of_the_swap_does_not_leave_a_hole)
    {
        // Root solid; refined; the children fade in; before they are solid the selection goes back to the root.
        for (int reverseAt = 1; reverseAt < 40; ++reverseAt)
        {
            SCOPED_TRACE(reverseAt);
            Sim sim(2, false);
            sim.mChoice[0] = TileChoice::Tile;
            for (std::size_t i = 1; i < Sim::tiles; ++i)
                sim.mChoice[i] = TileChoice::Covered;
            sim.run(80, false);
            sim.mChoice[0] = TileChoice::Live;
            for (std::size_t i = 1; i < Sim::tiles; ++i)
                sim.mChoice[i] = TileChoice::Tile;
            sim.run(reverseAt, false);
            sim.mChoice[0] = TileChoice::Tile;
            for (std::size_t i = 1; i < Sim::tiles; ++i)
                sim.mChoice[i] = TileChoice::Covered;
            sim.run(200, false);
            EXPECT_EQ(sim.mGaps, 0);
            EXPECT_EQ(sim.mJumps, 0);
            EXPECT_EQ(sim.mEventErrors, 0);
            EXPECT_TRUE(sim.solid(0));
        }
    }
}
