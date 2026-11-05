// Task 0088 stage e: the complementary dither of the terrain tiles (components/lod/terrainfade.*).
//
// Written before the implementation. A tile that is replaced (a coarser tile covers its finer tiles, or finer tiles
// replace a refined tile) used to stay opaque under the incoming draw until that was solid. Now the state machine also
// says "this tile shows the complement of the incoming draw's fade" (`mComplementOf`, the incoming fade, or -1 for a
// tile that shows its own fade), so that each pixel is drawn by exactly one of the two.
#include <gtest/gtest.h>

#include <components/lod/terrainfade.hpp>

#include <vector>

namespace
{
    using namespace Lod;

    constexpr float fadeSeconds = 0.4f;

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
        void step(float seconds) { stepTerrainFades(mStates, mInputs, mEvents, seconds, fadeSeconds); }
    };

    // Tile 0 is the coarse tile, tile 1 a finer one under it.
    Rig solidFinerTile()
    {
        Rig rig(2);
        rig.mInputs[0] = { false, TileChoice::Unset, -1, RefinedGate::Stay };
        rig.mInputs[1] = { true, TileChoice::Tile, 0, RefinedGate::Stay };
        for (int i = 0; i < 10; ++i)
            rig.step(0.1f);
        return rig;
    }
}

TEST(TerrainFadeComplement, a_tile_that_shows_its_own_fade_has_no_complement)
{
    Rig rig = solidFinerTile();
    EXPECT_LT(rig.mStates[1].mComplementOf, 0.f);
    rig.mInputs[1] = { true, TileChoice::Tile, 0, RefinedGate::Stay };
    rig.step(0.1f);
    EXPECT_LT(rig.mStates[1].mComplementOf, 0.f);
}

TEST(TerrainFadeComplement, a_solid_finer_tile_under_an_incoming_coarser_tile_shows_the_complement_of_its_fade)
{
    Rig rig = solidFinerTile();
    rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
    rig.mInputs[1] = { true, TileChoice::Covered, 0, RefinedGate::Stay };
    rig.step(0.1f);
    ASSERT_EQ(rig.mStates[0].mFade.mPhase, FadePhase::FadingIn);
    EXPECT_NEAR(rig.mStates[0].mFade.mFade, 0.25f, 1e-5f);
    EXPECT_NEAR(rig.mStates[1].mComplementOf, 0.25f, 1e-5f);
    rig.step(0.1f);
    EXPECT_NEAR(rig.mStates[1].mComplementOf, 0.5f, 1e-5f);
    // Its own fade stays 1: only what the draw shows changes.
    EXPECT_EQ(rig.mStates[1].mFade.mFade, 1.f);
    rig.step(0.1f);
    rig.step(0.1f);
    EXPECT_EQ(rig.mEvents[1], TerrainFadeEvent::Remove);
    EXPECT_LT(rig.mStates[1].mComplementOf, 0.f);
}

TEST(TerrainFadeComplement, a_finer_tile_that_is_still_fading_in_keeps_its_own_fade)
{
    Rig rig(2);
    rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
    rig.mInputs[1] = { true, TileChoice::Covered, 0, RefinedGate::Stay };
    rig.step(0.1f);
    EXPECT_EQ(rig.mStates[1].mFade.mPhase, FadePhase::FadingIn);
    EXPECT_LT(rig.mStates[1].mComplementOf, 0.f);
}

TEST(TerrainFadeComplement, a_refined_tile_shows_the_complement_of_its_replacement_when_the_caller_names_one)
{
    Rig rig(1);
    rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
    for (int i = 0; i < 10; ++i)
        rig.step(0.1f);
    rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::Stay, 0.5f };
    rig.step(0.1f);
    EXPECT_NEAR(rig.mStates[0].mComplementOf, 0.5f, 1e-5f);
    rig.mInputs[0].mReplacementFade = -1.f;
    rig.step(0.1f);
    EXPECT_LT(rig.mStates[0].mComplementOf, 0.f);
}

TEST(TerrainFadeComplement, a_tile_that_fades_out_over_the_live_terrain_keeps_its_own_fade)
{
    Rig rig(1);
    rig.mInputs[0] = { true, TileChoice::Tile, -1, RefinedGate::Stay };
    for (int i = 0; i < 10; ++i)
        rig.step(0.1f);
    rig.mInputs[0] = { true, TileChoice::Live, -1, RefinedGate::ToLive, 0.5f };
    rig.step(0.1f);
    EXPECT_LT(rig.mStates[0].mComplementOf, 0.f);
}
