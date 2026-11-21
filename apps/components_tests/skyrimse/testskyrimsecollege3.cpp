// Task 0327 (College of Winterhold, third slice): Mirabelle's tour package leads to a marker in the exterior while the
// player stands in the Hall of Attainment. The exterior cells were loaded earlier (the player began outside), so the
// source can still LOCATE the marker; it is nevertheless in another cell than the player's, so the actor has to wait
// and be carried through the doors instead of being sent to a place in a cell that is not there.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/placed_refs.hpp>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x0001C1A0;
    constexpr WorldId kRef = 0x1C1B9;
    constexpr FormId kMarker = 0x2030B;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out.clear();
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Travel; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            if (reference != kMarker)
                return false;
            x = 116838;
            y = 112260;
            z = -7744; // remembered from a cell that was loaded before the player went indoors
            return true;
        }
        bool targetIsElsewhere(FormId reference) const override { return mElsewhere && reference == kMarker; }
        bool mElsewhere = true;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle h, const ActorGoal& g) override
        {
            mGoals.push_back({ h.reference, g.x, g.y, g.z });
            return true;
        }
        bool cancelGoal(ActorHandle) override { return true; }
        bool placeActor(ActorHandle, double, double, double, float) override
        {
            ++mPlaced;
            return true;
        }
        ActorClock clock() const override { return { 12.f, 0, true }; }
        struct Goal
        {
            WorldId mRef;
            double mX, mY, mZ;
        };
        std::vector<Goal> mGoals;
        int mPlaced = 0;
    };

    struct Rig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;

        Rig()
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 0 });
            brain = std::make_unique<PackageBrain>(src);
            brain->setCarryTravellers(true);
            ActorInfo i;
            i.handle = { kRef, 1 };
            i.base = kBase;
            i.editorId = "Mirabelle";
            i.x = -66;
            i.y = 290;
            i.z = 0;
            brain->onActorAdded(i);
            brain->forcePackage(kRef, &pkg);
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    TEST(SkyrimCollege3, aSceneTravelToAnotherCellWaitsEvenWhenTheMarkerWasLocatedBefore)
    {
        Rig r;
        r.step(6);
        EXPECT_TRUE(r.w.mGoals.empty());
        EXPECT_EQ(r.w.mPlaced, 0);
        EXPECT_EQ(r.brain->travellerRefs().count(kRef), 1u);
    }

    TEST(SkyrimCollege3, onceTheTargetIsInThePlayersCellTheActorWalksToTheLocatedMarker)
    {
        Rig r;
        r.step(4);
        r.src.mElsewhere = false;
        r.step(6);
        ASSERT_FALSE(r.w.mGoals.empty());
        EXPECT_NEAR(r.w.mGoals.front().mX, 116838.0, 1e-6);
        EXPECT_TRUE(r.brain->travellerRefs().empty());
    }

    // ---- the scene's "the player is within 300 of the marker"
    // ---------------------------------------------------------
    constexpr FormId kNpc = 0x01000200;
    constexpr FormId kMarkerRef = 0x01000501; // placed in the exterior (cell 0x101) at (30000, 0, 0)
    constexpr FormId kInteriorRef = 0x01000502; // placed in the Hall (cell 0x100)

    struct PlayerRig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        PlayerRig()
        {
            ActorRecords ar;
            ActorNpc a;
            a.mCommon.mId = kNpc;
            ar.mNpcs.push_back(a);
            mActors.add(ar);
            PlacedRefRecords r;
            PlacedEntry marker;
            marker.mId = kMarkerRef;
            marker.mBase = kNpc;
            marker.mCell = 0x01000101;
            marker.mX = 30000.f;
            PlacedEntry inside;
            inside.mId = kInteriorRef;
            inside.mBase = kNpc;
            inside.mCell = 0x01000100;
            inside.mX = 10.f;
            r.mRefs.push_back(marker);
            r.mRefs.push_back(inside);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
        }
    };

    TEST(SkyrimCollege3, thePlayerOutdoorsIsMeasuredToAPlacedMarkerFromWhereHeStands)
    {
        PlayerRig r;
        r.mWorld.setPlayerPosition(29900.0, 0.0, 0.0);
        r.mWorld.setPlayerPlace(true, false, kNullFormId, 0x01000900);
        EXPECT_FLOAT_EQ(r.mWorld.distanceTo(SkyrimConditionWorld::kPlayer, kMarkerRef), 100.f);
    }

    TEST(SkyrimCollege3, thePlayerInTheHallIsInfinitelyFarFromAnExteriorMarker)
    {
        PlayerRig r;
        r.mWorld.setPlayerPosition(29900.0, 0.0, 0.0); // interior coordinates are not the world's
        r.mWorld.setPlayerPlace(true, true, 0x01000100, kNullFormId);
        EXPECT_GT(r.mWorld.distanceTo(SkyrimConditionWorld::kPlayer, kMarkerRef), 1e30f);
    }

    TEST(SkyrimCollege3, thePlayerInTheHallIsMeasuredToAReferenceOfThatHall)
    {
        PlayerRig r;
        r.mWorld.setPlayerPosition(0.0, 0.0, 0.0);
        r.mWorld.setPlayerPlace(true, true, 0x01000100, kNullFormId);
        EXPECT_FLOAT_EQ(r.mWorld.distanceTo(SkyrimConditionWorld::kPlayer, kInteriorRef), 10.f);
    }

    TEST(SkyrimCollege3, withoutThePlayersPositionTheOldAnswerStays)
    {
        PlayerRig r;
        EXPECT_GT(r.mWorld.distanceTo(SkyrimConditionWorld::kPlayer, kMarkerRef), 1e30f);
    }
}
