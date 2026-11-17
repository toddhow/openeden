#include <apps/openmw/mwworld/neutralkeepcell.hpp>

#include <gtest/gtest.h>

// Task 0274: a neutral cell that holds a living script-created actor is not unloaded by a grid change.

namespace
{
    using MWWorld::unloadAfterGridChange;

    TEST(NeutralKeepCell, aCellInsideTheGridIsNeverUnloaded)
    {
        EXPECT_FALSE(unloadAfterGridChange(false, false, false));
        EXPECT_FALSE(unloadAfterGridChange(false, true, false));
        EXPECT_FALSE(unloadAfterGridChange(false, true, true));
    }

    TEST(NeutralKeepCell, anOrdinaryCellOutsideTheGridIsUnloaded)
    {
        EXPECT_TRUE(unloadAfterGridChange(true, false, false));
        EXPECT_TRUE(unloadAfterGridChange(true, true, false));
    }

    TEST(NeutralKeepCell, aNeutralCellWithALiveCreatedActorStays)
    {
        EXPECT_FALSE(unloadAfterGridChange(true, true, true));
    }

    TEST(NeutralKeepCell, aMorrowindCellIsNotKeptWhatEverTheFlagSays)
    {
        EXPECT_TRUE(unloadAfterGridChange(true, false, true));
    }
}
