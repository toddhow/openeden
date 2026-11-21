#include <game_skyrimse/cart_rides.hpp>
#include <game_skyrimse/triggers.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <map>

// Task 0274 (the Helgen cart ride): box trigger volumes that fire for an actor other than the player, and carts that
// trail a horse with riders sitting on them. Pure rules; the numbers are worked out by hand in the comments.

namespace
{
    using namespace game_skyrimse;

    constexpr double kPi = 3.14159265358979323846;

    TriggerVolume box(
        double x, double y, double z, double bx, double by, double bz, double rotation, bool accept = true)
    {
        TriggerVolume v;
        v.mType = kPrimitiveBox;
        v.mBounds[0] = bx;
        v.mBounds[1] = by;
        v.mBounds[2] = bz;
        v.mPosition[0] = x;
        v.mPosition[1] = y;
        v.mPosition[2] = z;
        v.mRotationZ = rotation;
        v.mAcceptBox = accept;
        return v;
    }

    TriggerPlace exterior()
    {
        TriggerPlace p;
        p.mKnown = true;
        return p;
    }

    // ---- boxes ---------------------------------------------------------------------------------------------------

    TEST(SkyrimTriggerBox, aBoxIsSupportedOnlyWhenTheVolumeAcceptsBoxes)
    {
        EXPECT_FALSE(triggerSupported(box(0, 0, 0, 100, 100, 100, 0, false)));
        EXPECT_TRUE(triggerSupported(box(0, 0, 0, 100, 100, 100, 0, true)));
        EXPECT_FALSE(triggerSupported(box(0, 0, 0, 100, 0, 100, 0, true)));
        EXPECT_FALSE(triggerSupported(box(0, 0, 0, 100, 100, -1, 0, true)));
        EXPECT_FALSE(triggerSupported(box(0, 0, 0, 100, std::numeric_limits<double>::infinity(), 100, 0, true)));
        EXPECT_FALSE(triggerSupported(box(0, 0, 0, 100, 100, 100, std::nan(""), true)));
    }

    TEST(SkyrimTriggerBox, aBoxTakesItsBoundsAsHalfExtents)
    {
        const TriggerVolume v = box(10, 20, 30, 100, 50, 25, 0);
        EXPECT_TRUE(volumeContains(v, 10, 20, 30));
        EXPECT_TRUE(volumeContains(v, 109, 20, 30));
        EXPECT_FALSE(volumeContains(v, 111, 20, 30));
        EXPECT_TRUE(volumeContains(v, 10, 69, 30));
        EXPECT_FALSE(volumeContains(v, 10, 71, 30));
        EXPECT_TRUE(volumeContains(v, 10, 20, 54));
        EXPECT_FALSE(volumeContains(v, 10, 20, 56));
        EXPECT_FALSE(volumeContains(v, 10, 20, 4));
    }

    // Rotation a = pi/2 about Z, clockwise as the data stores it: the box's local +Y points east (sin a, cos a) =
    // (1, 0) and its local +X points south (cos a, -sin a) = (0, -1). The long bound (100) is along local X, so the box
    // is long from north to south.
    TEST(SkyrimTriggerBox, aBoxIsTurnedClockwiseByItsRotation)
    {
        const TriggerVolume v = box(0, 0, 0, 100, 10, 10, kPi / 2.0);
        EXPECT_TRUE(volumeContains(v, 0, -90, 0));
        EXPECT_TRUE(volumeContains(v, 0, 90, 0));
        EXPECT_FALSE(volumeContains(v, 90, 0, 0));
        EXPECT_TRUE(volumeContains(v, 5, 0, 0));
        EXPECT_FALSE(volumeContains(v, 15, 0, 0));
    }

    // MQ101SetStage20 (the data): a slab across the road at (21456.557, -79366.04, 8841.403), rotation 4.7773757,
    // bounds 1351.77 x 192.11 x 348.29. Local Y is 192 wide: along the road, east or west of the centre.
    TEST(SkyrimTriggerBox, theStageTwentySlabOfTheRealData)
    {
        const TriggerVolume v = box(21456.557, -79366.04, 8841.403, 1351.7656, 192.10938, 348.2871, 4.7773757);
        EXPECT_TRUE(volumeContains(v, 21456.557, -79366.04, 8841.403));
        EXPECT_TRUE(volumeContains(v, 21456.557 + 150.0, -79366.04, 8841.403 + 100.0));
        EXPECT_FALSE(volumeContains(v, 21456.557 + 300.0, -79366.04, 8841.403));
        EXPECT_FALSE(volumeContains(v, 21456.557, -79366.04, 8841.403 + 400.0));
        // 1,300 units north or south along its long axis is still inside
        EXPECT_TRUE(volumeContains(v, 21456.557 + 80.0, -79366.04 + 1300.0, 8841.403));
        EXPECT_FALSE(volumeContains(v, 21456.557 + 80.0, -79366.04 + 1400.0, 8841.403));
    }

    // ---- subjects ------------------------------------------------------------------------------------------------

    TEST(SkyrimTriggerSubject, anActorEntersAndLeavesAVolumeWithItsOwnEdges)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, box(0, 0, 0, 100, 100, 100, 0)));
        std::vector<TriggerEdge> edges;
        t.updateSubject(0xFF800001, exterior(), 500, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        t.updateSubject(0xFF800001, exterior(), 50, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_EQ(edges[0].mReference, 0x500u);
        EXPECT_EQ(edges[0].mSubject, 0xFF800001u);
        EXPECT_TRUE(edges[0].mEntered);
        EXPECT_TRUE(t.subjectInside(0xFF800001, 0x500));
        edges.clear();
        t.updateSubject(0xFF800001, exterior(), 60, 0, 0, edges); // still inside: no edge
        EXPECT_TRUE(edges.empty());
        t.updateSubject(0xFF800001, exterior(), 500, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_FALSE(edges[0].mEntered);
        EXPECT_FALSE(t.subjectInside(0xFF800001, 0x500));
    }

    TEST(SkyrimTriggerSubject, subjectsAndThePlayerDoNotShareWhatIsInside)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, box(0, 0, 0, 100, 100, 100, 0)));
        std::vector<TriggerEdge> edges;
        t.updateSubject(0xA, exterior(), 0, 0, 0, edges);
        t.updateSubject(0xB, exterior(), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 2u);
        EXPECT_EQ(edges[0].mSubject, 0xAu);
        EXPECT_EQ(edges[1].mSubject, 0xBu);
        edges.clear();
        t.update(exterior(), 0, 0, 0, edges); // the player goes in as well
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_EQ(edges[0].mSubject, kNullFormId);
        EXPECT_TRUE(t.inside(0x500));
        edges.clear();
        t.updateSubject(0xA, exterior(), 900, 0, 0, edges); // A leaves; B and the player stay
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_FALSE(edges[0].mEntered);
        EXPECT_TRUE(t.subjectInside(0xB, 0x500));
        EXPECT_TRUE(t.inside(0x500));
    }

    TEST(SkyrimTriggerSubject, anUnknownPlaceChangesNothingAndForgetEmptiesASubject)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, box(0, 0, 0, 100, 100, 100, 0)));
        std::vector<TriggerEdge> edges;
        t.updateSubject(0xA, TriggerPlace(), 0, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        t.updateSubject(0xA, exterior(), 0, 0, 0, edges);
        ASSERT_EQ(edges.size(), 1u);
        t.forgetSubject(0xA);
        EXPECT_FALSE(t.subjectInside(0xA, 0x500));
        edges.clear();
        t.updateSubject(0xA, exterior(), 0, 0, 0, edges); // goes in again after being forgotten
        EXPECT_EQ(edges.size(), 1u);
    }

    TEST(SkyrimTriggerSubject, aSubjectOfTheNullReferenceOrBeyondTheLimitIsIgnored)
    {
        TriggerTracker t;
        ASSERT_TRUE(t.add(0x500, box(0, 0, 0, 100, 100, 100, 0)));
        std::vector<TriggerEdge> edges;
        t.updateSubject(kNullFormId, exterior(), 0, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
        for (FormId s = 1; s <= TriggerTracker::kMaxSubjects; ++s)
            t.updateSubject(s, exterior(), 0, 0, 0, edges);
        EXPECT_EQ(edges.size(), TriggerTracker::kMaxSubjects);
        edges.clear();
        t.updateSubject(TriggerTracker::kMaxSubjects + 1, exterior(), 0, 0, 0, edges);
        EXPECT_TRUE(edges.empty());
    }

    // ---- carts ---------------------------------------------------------------------------------------------------

    struct World
    {
        std::map<FormId, std::array<double, 3>> mAt;
        CartRides::Lookup lookup() const
        {
            return [this](FormId r, double& x, double& y, double& z) {
                const auto it = mAt.find(r);
                if (it == mAt.end())
                    return false;
                x = it->second[0];
                y = it->second[1];
                z = it->second[2];
                return true;
            };
        }
    };

    constexpr FormId kCart = 0xC1, kHorse = 0xB1, kRider = 0xA1, kPlayer = 0x14;

    // The cart stands at the origin facing north (heading 0); the horse is 300 units ahead of it, at (0, 300).
    CartRides tethered(World& w)
    {
        CartRides rides;
        EXPECT_TRUE(rides.tether(kCart, kHorse, CartPose{ 0, 0, 10, 0.f }));
        w.mAt[kHorse] = { 0, 300, 12 };
        return rides;
    }

    const CartRides::CartMove* cartOf(const CartRides::Result& r, FormId cart)
    {
        for (const auto& m : r.mCarts)
            if (m.mCart == cart)
                return &m;
        return nullptr;
    }

    TEST(SkyrimCartRides, aTetherIsRememberedAndRefusedWhenFullOrNotFinite)
    {
        CartRides rides;
        EXPECT_TRUE(rides.tether(kCart, kHorse, CartPose()));
        EXPECT_TRUE(rides.isTethered(kCart));
        EXPECT_EQ(rides.rigCount(), 1u);
        EXPECT_EQ(rides.horses(), std::vector<FormId>{ kHorse });
        EXPECT_FALSE(rides.tether(0xC2, 0xB2, CartPose{ std::nan(""), 0, 0, 0.f }));
        for (FormId c = 0x100; c < 0x100 + CartRides::kMaxRigs; ++c)
            (void)rides.tether(c, c + 0x1000, CartPose());
        EXPECT_EQ(rides.rigCount(), CartRides::kMaxRigs);
        EXPECT_FALSE(rides.tether(0x900, 0x901, CartPose()));
        EXPECT_TRUE(rides.tether(kCart, 0xB9, CartPose())); // the same cart again changes horse, no new rig
        EXPECT_EQ(rides.rigCount(), CartRides::kMaxRigs);
    }

    TEST(SkyrimCartRides, theHorseOfARigIsKnownAsAHorseAndNothingElseIs)
    {
        CartRides rides;
        ASSERT_TRUE(rides.tether(kCart, kHorse, CartPose()));
        EXPECT_TRUE(rides.isHorse(kHorse));
        EXPECT_FALSE(rides.isHorse(kCart));
        EXPECT_FALSE(rides.isHorse(kRider));
        rides.clear();
        EXPECT_FALSE(rides.isHorse(kHorse));
    }

    TEST(SkyrimCartRides, aStandingHorseLeavesTheCartWhereItWas)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.x, 0.0, 1e-9);
        EXPECT_NEAR(m->mPose.y, 0.0, 1e-9);
        EXPECT_NEAR(m->mPose.z, 10.0, 1e-9);
        EXPECT_NEAR(m->mPose.heading, 0.f, 1e-6);
    }

    TEST(SkyrimCartRides, aHorseThatIsNotKnownMovesNothing)
    {
        World w;
        CartRides rides;
        ASSERT_TRUE(rides.tether(kCart, kHorse, CartPose()));
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        EXPECT_TRUE(r.mCarts.empty());
        EXPECT_TRUE(r.mRiders.empty());
    }

    // The horse walks 100 units north; the cart stays 300 behind it: from (0, 0) to (0, 100), same heading.
    TEST(SkyrimCartRides, theCartTrailsTheHorseAtTheTetherDistance)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r); // captures the line
        for (int i = 1; i <= 100; ++i)
        {
            w.mAt[kHorse] = { 0, 300.0 + i, 12 };
            r = CartRides::Result();
            rides.step(1.0 / 60.0, w.lookup(), r);
        }
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.x, 0.0, 1e-6);
        EXPECT_NEAR(m->mPose.y, 100.0, 1e-6);
        EXPECT_NEAR(m->mPose.heading, 0.f, 1e-5);
        EXPECT_NEAR(m->mPose.z, 10.0, 1e-9); // the horse's z (12) minus the 2 it had above the cart
    }

    TEST(SkyrimCartRides, theCartKeepsItsHeightBelowTheHorseOnAHill)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        w.mAt[kHorse] = { 0, 320, 52 };
        r = CartRides::Result();
        rides.step(0.016, w.lookup(), r);
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.z, 50.0, 1e-9);
    }

    // One step of 0.1 s turns the line at most kTrailerTurnRate * 0.1 = 0.15 rad, however sharp the horse turns.
    TEST(SkyrimCartRides, theLineTurnsAtMostAtTheTrailerRate)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(0.1, w.lookup(), r);
        w.mAt[kHorse] = { 40, 300, 12 }; // 40 units east in 0.1 s: 400 units/s, due east
        r = CartRides::Result();
        rides.step(0.1, w.lookup(), r);
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.heading, kTrailerTurnRate * 0.1, 1e-6);
        // the line points 0.15 rad clockwise of north from the cart to the horse: the cart is behind it on that line
        EXPECT_NEAR(m->mPose.x, 40.0 - 300.0 * std::sin(kTrailerTurnRate * 0.1), 1e-6);
        EXPECT_NEAR(m->mPose.y, 300.0 - 300.0 * std::cos(kTrailerTurnRate * 0.1), 1e-6);
    }

    // The horse then keeps walking east for long enough: the cart ends up directly west of it, facing east (pi/2).
    TEST(SkyrimCartRides, aHorseThatKeepsGoingEastPullsTheCartRoundToFaceEast)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(1.0 / 60.0, w.lookup(), r);
        double x = 0.0;
        for (int i = 0; i < 600; ++i)
        {
            x += 2.0;
            w.mAt[kHorse] = { x, 300, 12 };
            r = CartRides::Result();
            rides.step(1.0 / 60.0, w.lookup(), r);
        }
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.heading, kPi / 2.0, 1e-4);
        EXPECT_NEAR(m->mPose.x, x - 300.0, 1e-3);
        EXPECT_NEAR(m->mPose.y, 300.0, 1e-3);
    }

    TEST(SkyrimCartRides, aSlowDriftIsNotAMove)
    {
        World w;
        CartRides rides = tethered(w);
        CartRides::Result r;
        rides.step(1.0 / 60.0, w.lookup(), r);
        // 0.05 unit a frame = 3 units a second, below kTrailerMinSpeed: the line does not turn
        double x = 0.0;
        for (int i = 0; i < 300; ++i)
        {
            x += 0.05;
            w.mAt[kHorse] = { x, 300, 12 };
            r = CartRides::Result();
            rides.step(1.0 / 60.0, w.lookup(), r);
        }
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(m->mPose.heading, 0.f, 1e-6);
    }

    // ---- riders --------------------------------------------------------------------------------------------------

    TEST(SkyrimCartRides, aRiderNeedsATetheredCartAndIsCounted)
    {
        CartRides rides;
        EXPECT_FALSE(rides.addRider(kRider, kCart));
        ASSERT_TRUE(rides.tether(kCart, kHorse, CartPose()));
        EXPECT_TRUE(rides.addRider(kRider, kCart));
        EXPECT_TRUE(rides.isRider(kRider));
        EXPECT_EQ(rides.riderCount(), 1u);
        EXPECT_EQ(rides.ridersOf(kCart), std::vector<FormId>{ kRider });
        EXPECT_EQ(rides.removeRider(kRider), kCart);
        EXPECT_FALSE(rides.isRider(kRider));
        EXPECT_EQ(rides.removeRider(kRider), kNullFormId);
    }

    TEST(SkyrimCartRides, aRiderKeepsItsPlaceOnTheCartAsItMoves)
    {
        World w;
        CartRides rides = tethered(w);
        ASSERT_TRUE(rides.addRider(kRider, kCart));
        w.mAt[kRider] = { 50, 20, 15 }; // 50 east, 20 north, 5 above the cart's z of 10
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        ASSERT_EQ(r.mRiders.size(), 1u);
        EXPECT_TRUE(r.mRiders[0].mFirst);
        EXPECT_EQ(r.mRiders[0].mCart, kCart);
        EXPECT_NEAR(r.mRiders[0].mPose.x, 50.0, 1e-9);
        EXPECT_NEAR(r.mRiders[0].mPose.y, 20.0, 1e-9);
        // the horse goes 100 north: the cart and the rider with it
        for (int i = 1; i <= 100; ++i)
        {
            w.mAt[kHorse] = { 0, 300.0 + i, 12 };
            r = CartRides::Result();
            rides.step(1.0 / 60.0, w.lookup(), r);
        }
        ASSERT_EQ(r.mRiders.size(), 1u);
        EXPECT_FALSE(r.mRiders[0].mFirst);
        EXPECT_NEAR(r.mRiders[0].mPose.x, 50.0, 1e-6);
        EXPECT_NEAR(r.mRiders[0].mPose.y, 120.0, 1e-6);
        EXPECT_NEAR(r.mRiders[0].mPose.z, 15.0, 1e-9);
        EXPECT_NEAR(r.mRiders[0].mPose.heading, 0.f, 1e-5);
    }

    // The cart turns clockwise by pi/2 (the horse walks east for long): the rider's offset (50 east, 20 north) in the
    // cart's frame becomes (lx cos + ly sin, -lx sin + ly cos) = (20, -50): 20 east and 50 south of the cart.
    TEST(SkyrimCartRides, aRiderTurnsWithTheCart)
    {
        World w;
        CartRides rides = tethered(w);
        ASSERT_TRUE(rides.addRider(kRider, kCart));
        w.mAt[kRider] = { 50, 20, 15 };
        CartRides::Result r;
        rides.step(1.0 / 60.0, w.lookup(), r);
        double x = 0.0;
        for (int i = 0; i < 600; ++i)
        {
            x += 2.0;
            w.mAt[kHorse] = { x, 300, 12 };
            r = CartRides::Result();
            rides.step(1.0 / 60.0, w.lookup(), r);
        }
        ASSERT_EQ(r.mRiders.size(), 1u);
        const CartRides::CartMove* m = cartOf(r, kCart);
        ASSERT_NE(m, nullptr);
        EXPECT_NEAR(r.mRiders[0].mPose.x, m->mPose.x + 20.0, 1e-3);
        EXPECT_NEAR(r.mRiders[0].mPose.y, m->mPose.y - 50.0, 1e-3);
        EXPECT_NEAR(r.mRiders[0].mPose.heading, kPi / 2.0, 1e-4);
    }

    TEST(SkyrimCartRides, thePlayerRidesLikeAnyoneElse)
    {
        World w;
        CartRides rides = tethered(w);
        ASSERT_TRUE(rides.addRider(kPlayer, kCart));
        w.mAt[kPlayer] = { -30, 40, 10 };
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        ASSERT_EQ(r.mRiders.size(), 1u);
        EXPECT_EQ(r.mRiders[0].mRider, kPlayer);
    }

    TEST(SkyrimCartRides, aRemovedRiderIsNotMovedAnyMore)
    {
        World w;
        CartRides rides = tethered(w);
        ASSERT_TRUE(rides.addRider(kRider, kCart));
        w.mAt[kRider] = { 0, 0, 10 };
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        ASSERT_EQ(r.mRiders.size(), 1u);
        (void)rides.removeRider(kRider);
        r = CartRides::Result();
        rides.step(0.016, w.lookup(), r);
        EXPECT_TRUE(r.mRiders.empty());
        EXPECT_FALSE(r.mCarts.empty());
    }

    TEST(SkyrimCartRides, aRiderOfAnotherCartMovesOver)
    {
        CartRides rides;
        ASSERT_TRUE(rides.tether(0xC1, 0xB1, CartPose()));
        ASSERT_TRUE(rides.tether(0xC2, 0xB2, CartPose()));
        ASSERT_TRUE(rides.addRider(kRider, 0xC1));
        ASSERT_TRUE(rides.addRider(kRider, 0xC2));
        EXPECT_EQ(rides.riderCount(), 1u);
        EXPECT_TRUE(rides.ridersOf(0xC1).empty());
        EXPECT_EQ(rides.ridersOf(0xC2), std::vector<FormId>{ kRider });
    }

    TEST(SkyrimCartRides, aRiderThatIsNotKnownYetIsPlacedWhenItIs)
    {
        World w;
        CartRides rides = tethered(w);
        ASSERT_TRUE(rides.addRider(kRider, kCart));
        CartRides::Result r;
        rides.step(0.016, w.lookup(), r);
        EXPECT_TRUE(r.mRiders.empty());
        w.mAt[kRider] = { 10, 10, 10 };
        r = CartRides::Result();
        rides.step(0.016, w.lookup(), r);
        ASSERT_EQ(r.mRiders.size(), 1u);
        EXPECT_TRUE(r.mRiders[0].mFirst);
    }

    TEST(SkyrimCartRides, theSameInputsGiveTheSameOutputs)
    {
        auto run = [] {
            World w;
            CartRides rides = tethered(w);
            (void)rides.addRider(kRider, kCart);
            w.mAt[kRider] = { 5, 5, 10 };
            CartRides::Result r;
            double x = 0.0;
            for (int i = 0; i < 200; ++i)
            {
                x += 1.5;
                w.mAt[kHorse] = { x, 300 + 0.5 * i, 12 };
                r = CartRides::Result();
                rides.step(1.0 / 60.0, w.lookup(), r);
            }
            return std::array<double, 4>{ r.mCarts[0].mPose.x, r.mCarts[0].mPose.y, r.mRiders[0].mPose.x,
                r.mRiders[0].mPose.y };
        };
        EXPECT_EQ(run(), run());
    }

    TEST(SkyrimCartRides, clearForgetsEverything)
    {
        World w;
        CartRides rides = tethered(w);
        (void)rides.addRider(kRider, kCart);
        rides.clear();
        EXPECT_EQ(rides.rigCount(), 0u);
        EXPECT_EQ(rides.riderCount(), 0u);
    }
}
