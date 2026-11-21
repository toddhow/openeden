#include <game_skyrimse/solitude.hpp>
#include <game_skyrimse/triggers.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

// Task 0346 (Solitude: the execution of Roggvir, SolitudeOpening): the gate's box trigger (REFR 0x85373, centre z
// -8180, half height 200) hangs 36 units above the road the player stands on (feet at z -8416), so only his BODY
// reaches it. The player's body is a vertical segment from his feet to `solitude::kPlayerBodyHeight` above them; other
// subjects keep the point test.

namespace
{
    using namespace game_skyrimse;

    TriggerVolume gateBox()
    {
        TriggerVolume v;
        v.mType = kPrimitiveBox;
        v.mBounds[0] = 276.0;
        v.mBounds[1] = 362.0;
        v.mBounds[2] = 200.0;
        v.mPosition[0] = -65237.0;
        v.mPosition[1] = 104652.0;
        v.mPosition[2] = -8180.0;
        v.mAcceptBox = true;
        return v;
    }

    TriggerPlace outside()
    {
        TriggerPlace p;
        p.mKnown = true;
        p.mInterior = false;
        return p;
    }

    std::vector<TriggerEdge> step(TriggerTracker& t, double z)
    {
        std::vector<TriggerEdge> edges;
        t.update(outside(), -65237.0, 104652.0, z, edges);
        return edges;
    }

    TEST(SkyrimSolitudeBody, aBodyStandingBelowTheBoxReachesIntoIt)
    {
        EXPECT_FALSE(volumeContains(gateBox(), -65237.0, 104652.0, -8416.0)); // the feet are 36 units below the box
        EXPECT_TRUE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -8416.0, solitude::kPlayerBodyHeight));
    }

    TEST(SkyrimSolitudeBody, aBodyWhoseHeadStaysBelowTheBoxDoesNotReachIt)
    {
        // feet at -8600, head at -8472: still 92 units under the lower face (-8380)
        EXPECT_FALSE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -8600.0, solitude::kPlayerBodyHeight));
    }

    TEST(SkyrimSolitudeBody, aBodyStandingAboveTheBoxDoesNotReachIt)
    {
        // the body goes UP from the feet: feet at -7900 are above the upper face (-7980)
        EXPECT_FALSE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -7900.0, solitude::kPlayerBodyHeight));
    }

    TEST(SkyrimSolitudeBody, aBodyBesideTheBoxDoesNotReachIt)
    {
        EXPECT_FALSE(volumeContainsBody(gateBox(), -65237.0 + 400.0, 104652.0, -8180.0, solitude::kPlayerBodyHeight));
    }

    TEST(SkyrimSolitudeBody, aHeightOfZeroOrANonFiniteOneIsThePointTest)
    {
        EXPECT_FALSE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -8416.0, 0.0));
        EXPECT_FALSE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -8416.0, -5.0));
        EXPECT_FALSE(
            volumeContainsBody(gateBox(), -65237.0, 104652.0, -8416.0, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_TRUE(volumeContainsBody(gateBox(), -65237.0, 104652.0, -8200.0, 0.0));
    }

    TEST(SkyrimSolitudeBody, theTrackerEntersWithTheBodyOnlyWhenAskedTo)
    {
        TriggerTracker feetOnly;
        ASSERT_TRUE(feetOnly.add(0x85373, gateBox()));
        EXPECT_TRUE(step(feetOnly, -8416.0).empty());
        EXPECT_FALSE(feetOnly.inside(0x85373));

        TriggerTracker body;
        ASSERT_TRUE(body.add(0x85373, gateBox()));
        body.setPlayerBodyHeight(solitude::kPlayerBodyHeight);
        const std::vector<TriggerEdge> edges = step(body, -8416.0);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_TRUE(edges.front().mEntered);
        EXPECT_EQ(edges.front().mReference, 0x85373u);
        EXPECT_TRUE(step(body, -8416.0).empty()); // no second edge while he stays
        const std::vector<TriggerEdge> left = step(body, -8700.0);
        ASSERT_EQ(left.size(), 1u);
        EXPECT_FALSE(left.front().mEntered);
    }

    TEST(SkyrimSolitudeBody, anotherSubjectKeepsThePointTest)
    {
        TriggerTracker body;
        ASSERT_TRUE(body.add(0x85373, gateBox()));
        body.setPlayerBodyHeight(solitude::kPlayerBodyHeight);
        std::vector<TriggerEdge> edges;
        body.updateSubject(0x14000, outside(), -65237.0, 104652.0, -8416.0, edges);
        EXPECT_TRUE(edges.empty());
        EXPECT_FALSE(body.subjectInside(0x14000, 0x85373));
    }

    TEST(SkyrimSolitudeBody, aSphereIsReachedByTheBodyToo)
    {
        TriggerVolume sphere;
        sphere.mType = kPrimitiveSphere;
        sphere.mBounds[0] = 50.0;
        sphere.mPosition[0] = 0.0;
        sphere.mPosition[1] = 0.0;
        sphere.mPosition[2] = 100.0;
        EXPECT_FALSE(volumeContains(sphere, 0.0, 0.0, 0.0));
        EXPECT_TRUE(volumeContainsBody(sphere, 0.0, 0.0, 0.0, solitude::kPlayerBodyHeight));
    }

    TEST(SkyrimSolitudeBody, theSettingAndTheBodyAreNamed)
    {
        EXPECT_EQ(solitude::kKey, "solitude");
        EXPECT_DOUBLE_EQ(solitude::kPlayerBodyHeight, 128.0);
        EXPECT_EQ(TriggerTracker().playerBodyHeight(), 0.0);
    }
}
