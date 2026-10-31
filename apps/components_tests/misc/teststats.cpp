#include <components/detournavigator/stats.hpp>
#include <components/misc/stats.hpp>

#include <gtest/gtest.h>

namespace
{
    TEST(MiscStats, keepsTheLastValuePerFrameAndName)
    {
        Misc::Stats stats;
        EXPECT_FALSE(stats.getAttribute(1, "a").has_value());
        stats.setAttribute(1, "a", 2.0);
        stats.setAttribute(1, "a", 3.0);
        stats.setAttribute(2, "a", 4.0);
        EXPECT_EQ(stats.getAttribute(1, "a"), 3.0);
        EXPECT_EQ(stats.getAttribute(2, "a"), 4.0);
        EXPECT_FALSE(stats.getAttribute(1, "b").has_value());
    }

    TEST(MiscStats, detourNavigatorReportsTheNamesItAlwaysHad)
    {
        DetourNavigator::Stats navigator{};
        navigator.mUpdater.mJobs = 5;
        navigator.mRecast.mTiles = 7;
        Misc::Stats stats;
        DetourNavigator::reportStats(navigator, 3, stats);
        EXPECT_EQ(stats.getAttribute(3, "NavMesh Jobs"), 5.0);
        EXPECT_EQ(stats.getAttribute(3, "NavMesh Recast Tiles"), 7.0);
        EXPECT_FALSE(stats.getAttribute(3, "NavMesh DbJobs Read").has_value());
    }
}
