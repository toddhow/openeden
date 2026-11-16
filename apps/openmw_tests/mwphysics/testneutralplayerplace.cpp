// Task 0211: the filter that hands the player's place to the pack only when it changes
// (apps/openmw/mwphysics/neutralplayerplace.hpp, pure). Written from the header before the code.
#include "apps/openmw/mwphysics/neutralplayerplace.hpp"

#include <gtest/gtest.h>

namespace
{
    using MWPhysics::PlayerPlaceFilter;
    using openeden::sdk::game::PlayerPlace;

    PlayerPlace exterior(openeden::sdk::game::WorldId world, int x, int y)
    {
        PlayerPlace p;
        p.world = world;
        p.gridX = x;
        p.gridY = y;
        return p;
    }

    PlayerPlace interior(openeden::sdk::game::WorldId cell)
    {
        PlayerPlace p;
        p.interior = true;
        p.cell = cell;
        return p;
    }

    TEST(NeutralPlayerPlace, anInteriorWithNoCellAndAnExteriorWithNoWorldAreNotValid)
    {
        PlayerPlaceFilter filter;
        EXPECT_FALSE(filter.valid(PlayerPlace()));
        EXPECT_FALSE(filter.valid(interior(openeden::sdk::game::kNoWorldId)));
        EXPECT_FALSE(filter.valid(exterior(openeden::sdk::game::kNoWorldId, 1, 2)));
        EXPECT_TRUE(filter.valid(interior(5)));
        EXPECT_TRUE(filter.valid(exterior(3, 0, 0)));
    }

    TEST(NeutralPlayerPlace, theFirstValidPlaceIsAcceptedAndTheSameOneAgainIsNot)
    {
        PlayerPlaceFilter filter;
        EXPECT_TRUE(filter.accept(exterior(3, 4, 5)));
        EXPECT_FALSE(filter.accept(exterior(3, 4, 5)));
        EXPECT_FALSE(filter.accept(exterior(3, 4, 5)));
    }

    TEST(NeutralPlayerPlace, aDifferentGridSquareAWorldOrAnInteriorIsAcceptedAndGoingBackIsToo)
    {
        PlayerPlaceFilter filter;
        EXPECT_TRUE(filter.accept(exterior(3, 4, 5)));
        EXPECT_TRUE(filter.accept(exterior(3, 5, 5)));
        EXPECT_TRUE(filter.accept(exterior(3, 5, 6)));
        EXPECT_TRUE(filter.accept(exterior(4, 5, 6)));
        EXPECT_TRUE(filter.accept(interior(9)));
        EXPECT_FALSE(filter.accept(interior(9)));
        EXPECT_TRUE(filter.accept(interior(10)));
        EXPECT_TRUE(filter.accept(exterior(4, 5, 6)));
    }

    TEST(NeutralPlayerPlace, anInvalidPlaceIsNeverAcceptedAndDoesNotForgetTheLastOne)
    {
        PlayerPlaceFilter filter;
        EXPECT_TRUE(filter.accept(exterior(3, 4, 5)));
        EXPECT_FALSE(filter.accept(PlayerPlace()));
        EXPECT_FALSE(filter.accept(exterior(3, 4, 5)));
    }

    TEST(NeutralPlayerPlace, resetMakesTheNextValidPlaceAcceptedAgain)
    {
        PlayerPlaceFilter filter;
        EXPECT_TRUE(filter.accept(interior(9)));
        filter.reset();
        EXPECT_TRUE(filter.accept(interior(9)));
    }

    TEST(NeutralPlayerPlace, theInteriorsGridFieldsAreIgnoredAndSoIsAnExteriorsCell)
    {
        PlayerPlaceFilter filter;
        PlayerPlace a = interior(9);
        a.gridX = 4;
        PlayerPlace b = interior(9);
        b.gridX = 8;
        EXPECT_TRUE(filter.accept(a));
        EXPECT_FALSE(filter.accept(b));
        PlayerPlace c = exterior(3, 1, 1);
        c.cell = 77;
        PlayerPlace d = exterior(3, 1, 1);
        EXPECT_TRUE(filter.accept(c));
        EXPECT_FALSE(filter.accept(d));
    }
}
