#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/quest_priority.hpp>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

// Task 0341 (side quest MS08 "In My Time of Need"): Saadia is the UNIQUE-actor alias of MS08 (priority above 40) and the
// find-matching "Server1" of WITavern (priority 40, a forced fill); the quest with the higher priority is tried first.

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    PackageRecord package(FormId id, const char* name)
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mCommon.mEditorId = name;
        p.mHasPkdt = true;
        return p;
    }

    std::vector<FormId> ids(const std::vector<const PackageRecord*>& list)
    {
        std::vector<FormId> out;
        for (const PackageRecord* p : list)
            out.push_back(p->mCommon.mId);
        return out;
    }

    TEST(SkyrimMyTimeOrder, aUniqueActorAliasOfAHigherPriorityQuestIsTriedBeforeAForcedAliasOfALowerOne)
    {
        const PackageRecord confide = package(0x70378, "MS08SaadiaConfidePlayer");
        const PackageRecord room = package(0x70379, "MS08SaadiaSandboxRoom");
        const PackageRecord sandbox = package(0xDEE97, "WITavernServerSandbox");
        const PackageRecord own = package(0x2BE16, "WhiterunSaadiaBanneredMarePackage9x12");
        const std::vector<const PackageRecord*> list{ &confide, &room, &own }; // packagesFor: the unique groups, then her own
        std::vector<AliasPackageGroup> unique{ { 0x1CF25, 50, false, { &confide, &room } } };
        std::vector<AliasPackageGroup> other{ { 0xDEE92, 40, true, { &sandbox } } };
        EXPECT_EQ(ids(orderWithUniqueActorGroups(list, 2, unique, other)),
            (std::vector<FormId>{ 0x70378, 0x70379, 0xDEE97, 0x2BE16 }));
    }

    TEST(SkyrimMyTimeOrder, aForcedAliasOfAHigherPriorityQuestStaysAheadOfAUniqueActorAliasOfALowerOne)
    {
        const PackageRecord a = package(1, "A"), b = package(2, "B"), own = package(3, "Own");
        const std::vector<const PackageRecord*> list{ &a, &own };
        EXPECT_EQ(ids(orderWithUniqueActorGroups(list, 1, { { 0x100, 40, false, { &a } } }, { { 0x200, 70, true, { &b } } })),
            (std::vector<FormId>{ 2, 1, 3 }));
    }

    TEST(SkyrimMyTimeOrder, theActorsOwnPackagesStayBehindEveryGroupAndNothingIsLostOrDoubled)
    {
        const PackageRecord a = package(1, "A"), own1 = package(2, "Own1"), own2 = package(3, "Own2");
        const std::vector<const PackageRecord*> list{ &a, &own1, &own2 };
        EXPECT_EQ(ids(orderWithUniqueActorGroups(list, 1, { { 0x100, 50, false, { &a } } }, {})),
            (std::vector<FormId>{ 1, 2, 3 }));
        EXPECT_EQ(ids(orderWithUniqueActorGroups(list, 0, {}, {})), (std::vector<FormId>{ 1, 2, 3 }));
    }

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
        std::uint32_t packagesRevision() const override { return mRevision; }
        std::uint32_t mRevision = 0;
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

        // a quest alias's package (not a scene's forced one) in the actor's list, flags = PKDT flags
        explicit Rig(bool snap, std::uint32_t flags)
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mFlags = flags;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 0 });
            src.mLists[kBase] = { &pkg };
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mPlaces[kMarker] = { -1376, -490, 318 };
            brain = std::make_unique<PackageBrain>(src);
            brain->setSnapMustComplete(snap);
            w.mPositions[kRef] = { 100, 200, 66 };
            brain->onActorAdded(infoOf(kRef, 1));
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    constexpr std::uint32_t kMustComplete = 0x4;

    TEST(SkyrimMyTimeSnap, aMustCompleteTravelThatCannotBeWalkedEndsWithTheActorPlacedAtTheTarget)
    {
        Rig r(true, kMustComplete);
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::Blocked);
        r.step(1);
        ASSERT_EQ(r.w.mPlaced.size(), 1u);
        EXPECT_NEAR(r.w.mPlaced[0].mX, -1376.0, 1e-6);
        EXPECT_NEAR(r.w.mPlaced[0].mZ, 318.0, 1e-6);
        EXPECT_EQ(r.brain->stats().mTravelSnaps, 1u);
    }

    TEST(SkyrimMyTimeSnap, aTravelThatNeedNotCompleteIsNotPlaced)
    {
        Rig r(true, 0);
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
    }

    TEST(SkyrimMyTimeSnap, withTheOptionOffNobodyIsPlaced)
    {
        Rig r(false, kMustComplete);
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::Blocked);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
    }

    TEST(SkyrimMyTimeSnap, aWalkThatArrivesIsNotPlaced)
    {
        Rig r(true, kMustComplete);
        r.step(3);
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::Arrived);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
    }

    // ---- Kematu's death, seen from Saadia's dialogue (GetDead run on another reference) ----

    constexpr FormId kNpcKematu = 0x01000200;
    constexpr FormId kRefKematu = 0x01000500;
    constexpr FormId kCellDen = 0x01000100;
    constexpr FormId kCellMare = 0x01000101;

    struct DeadWorld
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        DeadWorld()
        {
            ActorRecords ar;
            ActorNpc k;
            k.mCommon.mId = kNpcKematu;
            ar.mNpcs.push_back(k);
            mActors.add(ar);
            PlacedRefRecords r;
            PlacedEntry e;
            e.mId = kRefKematu;
            e.mBase = kNpcKematu;
            e.mCell = kCellDen;
            e.mFlags = PlacedFlags::IsActor;
            r.mRefs.push_back(e);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
            DialogueSpeaker s;
            s.mBase = kNpcKematu;
            s.mReference = 0x01000501; // Saadia stands elsewhere: nobody of the Den speaks
            s.mCell = kCellMare;
            mWorld.setSpeaker(s);
        }
        bool deadHolds()
        {
            survey::Ctda c;
            c.mFunction = 46; // GetDead
            c.mRunOn = 2; // another reference
            c.mReference = kRefKematu;
            c.mCompare = 0;
            c.mComparison = 1.f;
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimMyTimeDead, aReferenceTheSessionKilledIsDeadForAnotherReferencesDialogue)
    {
        DeadWorld w;
        EXPECT_FALSE(w.deadHolds());
        w.mWorld.setDeadLookup([](FormId ref) { return ref == kRefKematu; });
        EXPECT_TRUE(w.deadHolds());
    }

    TEST(SkyrimMyTimeDead, aKillOfAnotherReferenceChangesNothing)
    {
        DeadWorld w;
        w.mWorld.setDeadLookup([](FormId ref) { return ref == 0x01009999; });
        EXPECT_FALSE(w.deadHolds());
    }
}
