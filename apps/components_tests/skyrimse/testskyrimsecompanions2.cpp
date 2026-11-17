// Task 0277 (companions, second slice): the package brain's cross-cell travel. A Travel package whose target stands in a
// cell that is not the player's cell (Vilkas' "get to outside" package: the training marker is in Whiterun, the actor in
// Jorrvaskr's basement) waits and is carried with the player through doors until the target can be reached; a scene's
// forced package survives the door. Pure tests over a fake source and writer.
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <set>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x00013BA7;
    constexpr WorldId kRef = 0x10;
    constexpr FormId kMarker = 0x500;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            const auto it = mLists.find(base);
            if (it == mLists.end())
                return false;
            out = it->second;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord& p) const override
        {
            const auto it = mBehavior.find(p.mCommon.mId);
            return it == mBehavior.end() ? PackageBehavior::Stand : it->second;
        }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPlaces.find(reference);
            if (it == mPlaces.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        bool targetIsElsewhere(FormId reference) const override { return mElsewhere.count(reference) != 0; }
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, std::array<double, 3>> mPlaces;
        std::set<FormId> mElsewhere;
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
        bool placeActor(ActorHandle h, double x, double y, double z, float) override
        {
            mPlaced.push_back({ h.reference, x, y, z });
            return true;
        }
        bool actorPosition(ActorHandle h, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(h.reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        ActorClock clock() const override { return { 12.f, 0, true }; }
        struct Goal
        {
            WorldId mRef;
            double mX, mY, mZ;
        };
        std::vector<Goal> mGoals;
        std::vector<Goal> mPlaced;
        std::map<WorldId, std::array<double, 3>> mPositions;
    };

    ActorInfo infoOf(WorldId ref, std::uint32_t session)
    {
        ActorInfo i;
        i.handle = { ref, session };
        i.base = kBase;
        i.editorId = "Test";
        i.x = 100;
        i.y = 200;
        i.z = 66;
        return i;
    }

    struct Rig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;

        explicit Rig(bool carry, bool scene = true)
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 0 });
            src.mLists[kBase] = {};
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mElsewhere.insert(kMarker);
            brain = std::make_unique<PackageBrain>(src);
            brain->setCarryTravellers(carry);
            w.mPositions[kRef] = { 100, 200, 66 };
            brain->onActorAdded(infoOf(kRef, 1));
            if (scene)
                brain->forcePackage(kRef, &pkg); // a scene's package; without it the actor's own list is empty
            else
                src.mLists[kBase] = { &pkg };
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    TEST(SkyrimTravellers, aTravelToAnotherCellWaitsAndAsksToBeCarried)
    {
        Rig r(true);
        r.step(6);
        EXPECT_TRUE(r.w.mGoals.empty());
        EXPECT_NE(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
        EXPECT_EQ(r.brain->travellerRefs().count(kRef), 1u);
    }

    TEST(SkyrimTravellers, aTravelOfTheActorsOwnScheduleIsNotCarriedOnlyASceneLeadsThePlayer)
    {
        Rig r(true, false);
        r.step(6);
        EXPECT_TRUE(r.brain->travellerRefs().empty());
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimTravellers, withTheOptionOffTheOldBehaviourStaysAndNobodyIsCarried)
    {
        Rig r(false);
        r.step(6);
        EXPECT_TRUE(r.w.mGoals.empty());
        EXPECT_TRUE(r.brain->travellerRefs().empty());
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimTravellers, onceTheTargetIsInTheSameCellTheActorWalksToIt)
    {
        Rig r(true);
        r.step(4);
        ASSERT_TRUE(r.w.mGoals.empty());
        r.src.mElsewhere.clear();
        r.src.mPlaces[kMarker] = { 5000, 6000, 10 };
        r.step(6);
        ASSERT_FALSE(r.w.mGoals.empty());
        EXPECT_NEAR(r.w.mGoals.front().mX, 5000.0, 1e-6);
        EXPECT_NEAR(r.w.mGoals.front().mY, 6000.0, 1e-6);
        EXPECT_TRUE(r.brain->travellerRefs().empty());
    }

    TEST(SkyrimTravellers, aTargetThatIsNowHereButCannotBePlacedEndsTheWaitWithTheOldAnswer)
    {
        Rig r(true);
        r.step(4);
        r.src.mElsewhere.clear(); // the player's cell is the target's, nothing locates it
        r.step(6);
        EXPECT_TRUE(r.w.mGoals.empty());
        EXPECT_TRUE(r.brain->travellerRefs().empty());
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimTravellers, aSceneWalkThatCannotBeWalkedEndsWithTheActorPlacedAtItsAnchor)
    {
        Rig r(true);
        r.step(2);
        r.src.mElsewhere.clear();
        r.src.mPlaces[kMarker] = { 5000, 6000, 10 };
        r.step(2);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::Blocked);
        EXPECT_TRUE(r.w.mPlaced.empty()); // the request is written at the next update, where a writer is open
        r.step(1);
        ASSERT_EQ(r.w.mPlaced.size(), 1u);
        EXPECT_NEAR(r.w.mPlaced[0].mX, 5000.0, 1e-6);
        EXPECT_NEAR(r.w.mPlaced[0].mY, 6000.0, 1e-6);
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
        EXPECT_EQ(r.brain->stats().mTravelSnaps, 1u);
    }

    TEST(SkyrimTravellers, theSameFailureWithTheOptionOffDoesNotPlaceAnyone)
    {
        Rig r(false);
        r.src.mPlaces[kMarker] = { 5000, 6000, 10 };
        r.src.mElsewhere.clear();
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Failed);
    }

    TEST(SkyrimTravellers, aForcedPackageSurvivesTheActorBeingAddedAgainAfterADoor)
    {
        Rig r(true);
        PackageRecord other;
        other.mCommon.mId = 2;
        other.mHasPkdt = true;
        r.src.mBehavior[2] = PackageBehavior::Stand;
        ASSERT_TRUE(r.brain->forcePackage(kRef, &other));
        r.step(2);
        EXPECT_EQ(r.brain->view(kRef)->mActive, &other);
        r.brain->onActorRemoved({ kRef, 1 });
        r.brain->onActorAdded(infoOf(kRef, 2));
        r.step(2);
        EXPECT_EQ(r.brain->view(kRef)->mActive, &other);
    }

    TEST(SkyrimTravellers, aClearedForcedPackageIsNotRestored)
    {
        Rig r(true);
        PackageRecord other;
        other.mCommon.mId = 2;
        other.mHasPkdt = true;
        r.src.mBehavior[2] = PackageBehavior::Stand;
        ASSERT_TRUE(r.brain->forcePackage(kRef, &other));
        ASSERT_TRUE(r.brain->forcePackage(kRef, nullptr));
        r.brain->onActorRemoved({ kRef, 1 });
        r.brain->onActorAdded(infoOf(kRef, 2));
        r.step(2);
        EXPECT_NE(r.brain->view(kRef)->mActive, &other);
    }

    TEST(SkyrimTravellers, aForcedPackageForAnActorNobodyHasAddedYetIsKeptForWhenItAppears)
    {
        Rig r(true);
        PackageRecord other;
        other.mCommon.mId = 2;
        other.mHasPkdt = true;
        r.src.mBehavior[2] = PackageBehavior::Stand;
        EXPECT_FALSE(r.brain->forcePackage(0x99, &other)); // not there: still false, as before
        r.brain->onActorAdded(infoOf(0x99, 1));
        r.step(2);
        EXPECT_EQ(r.brain->view(0x99)->mActive, &other);
    }

    TEST(SkyrimTravellers, theActorTableCarriesTravellersAndFollowersAlikeButOnlyFollowersFight)
    {
        Rig r(true);
        SkyrimActorTable table;
        table.enablePackages(r.src, 1);
        table.setCarryTravellers(true);
        table.onActorAdded(infoOf(kRef, 1));
        ASSERT_TRUE(table.forcePackage(kRef, &r.pkg));
        WorldId ids[4] = {};
        for (int i = 0; i < 4; ++i)
            table.update(0.5f, r.w);
        EXPECT_EQ(table.followers(ids, 4), 1u);
        EXPECT_EQ(ids[0], kRef);
        EXPECT_FALSE(table.isFollower(kRef));
        EXPECT_EQ(table.sideOf(kRef), FollowerSide::None);
    }
}

// ---- the conversation's GetDistance with live places ----------------------------------------------------------------
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/placed_refs.hpp>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kNpc = 0x01000200;
    constexpr FormId kSpeakerRef = 0x01000500; // placed in the basement (cell 0x100), initial place (1000, 0, 0)
    constexpr FormId kMarkerRef = 0x01000501; // placed in the exterior (cell 0x101)

    struct DistanceRig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        DistanceRig()
        {
            ActorRecords ar;
            ActorNpc a;
            a.mCommon.mId = kNpc;
            ar.mNpcs.push_back(a);
            mActors.add(ar);
            PlacedRefRecords r;
            PlacedEntry speaker;
            speaker.mId = kSpeakerRef;
            speaker.mBase = kNpc;
            speaker.mCell = 0x01000100;
            speaker.mX = 1000.f;
            speaker.mFlags = PlacedFlags::IsActor;
            PlacedEntry marker;
            marker.mId = kMarkerRef;
            marker.mBase = kNpc;
            marker.mCell = 0x01000101;
            marker.mX = 30000.f;
            r.mRefs.push_back(speaker);
            r.mRefs.push_back(marker);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
            DialogueSpeaker s;
            s.mBase = kNpc;
            s.mReference = kSpeakerRef;
            s.mCell = 0x01000100;
            mWorld.setSpeaker(s);
        }
    };

    TEST(SkyrimLiveDistance, referencesOfTwoCellsAreInfinitelyFarWithoutLivePlaces)
    {
        DistanceRig r;
        EXPECT_GT(r.mWorld.distanceTo(SkyrimConditionWorld::kPlacedTag | kSpeakerRef, kMarkerRef), 1e30f);
    }

    TEST(SkyrimLiveDistance, anActorThatWalkedToTheLoadedTargetIsMeasuredFromWhereItStandsNow)
    {
        DistanceRig r;
        r.mWorld.setLivePlaces(
            [](std::uint32_t reference, double& x, double& y, double& z) {
                if (reference != kSpeakerRef)
                    return false;
                x = 3.0;
                y = 4.0;
                z = 0.0;
                return true;
            },
            [](std::uint32_t reference, double& x, double& y, double& z) {
                if (reference != kMarkerRef)
                    return false;
                x = 0.0;
                y = 0.0;
                z = 0.0;
                return true;
            });
        EXPECT_FLOAT_EQ(r.mWorld.distanceTo(SkyrimConditionWorld::kPlacedTag | kSpeakerRef, kMarkerRef), 5.f);
    }

    TEST(SkyrimLiveDistance, aTargetThatIsNotLoadedKeepsTheOldAnswer)
    {
        DistanceRig r;
        r.mWorld.setLivePlaces(
            [](std::uint32_t, double& x, double& y, double& z) {
                x = y = z = 0.0;
                return true;
            },
            [](std::uint32_t, double&, double&, double&) { return false; });
        EXPECT_GT(r.mWorld.distanceTo(SkyrimConditionWorld::kPlacedTag | kSpeakerRef, kMarkerRef), 1e30f);
    }
}
