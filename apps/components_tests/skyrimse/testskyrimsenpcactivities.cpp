// Task 0135 stage S6f: what more a Skyrim NPC does with its package. Preferred speed, patrol over linked markers,
// follow a target actor, talking to a seated or sleeping NPC, and the sit-down and get-up clips with their root motion.
// Pure scheduler tests over a fake source and a fake writer. No game data.
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x00013BA7;
    constexpr double kPi = 3.14159265358979323846;

    PackageRecord makePackage(FormId id)
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mHasPkdt = true;
        p.mType = 18;
        return p;
    }

    class Source final : public PackageSource, public FurnitureSource
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
        PackageActivity activityOf(const PackageRecord& p) const override
        {
            const auto it = mActivity.find(p.mCommon.mId);
            return it == mActivity.end() ? PackageActivity::None : it->second;
        }
        bool targetOf(const PackageRecord& p, PackageTarget& out) const override
        {
            const auto it = mTarget.find(p.mCommon.mId);
            if (it == mTarget.end())
                return false;
            out = it->second;
            return true;
        }
        bool linkedRef(FormId reference, FormId keyword, FormId& out) const override
        {
            const auto it = mLinks.find({ reference, keyword });
            if (it == mLinks.end())
                return false;
            out = it->second;
            return true;
        }
        float patrolIdle(FormId reference) const override
        {
            const auto it = mIdle.find(reference);
            return it == mIdle.end() ? -1.f : it->second;
        }
        void slotsNear(FormId, double, double, double, double, FurniturePose pose, double, double, double,
            std::vector<FurnitureSlot>& out) override
        {
            out.clear();
            for (const FurnitureSlot& s : mSlots)
                if (s.mPose == pose)
                    out.push_back(s);
        }

        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, PackageActivity> mActivity;
        std::map<FormId, PackageTarget> mTarget;
        std::map<FormId, std::array<double, 3>> mPlaces;
        std::map<std::pair<FormId, FormId>, FormId> mLinks;
        std::map<FormId, float> mIdle;
        std::vector<FurnitureSlot> mSlots;
    };

    struct Event
    {
        enum Kind
        {
            Goto,
            Cancel,
            Use,
            Leave,
            Place
        } mKind;
        ActorHandle mHandle;
        double mX = 0, mY = 0, mZ = 0;
        float mHeading = 0.f;
        ActorSpeed mSpeed = ActorSpeed::Walk;
        std::string mClip;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle h, const ActorGoal& g) override
        {
            mEvents.push_back({ Event::Goto, h, g.x, g.y, g.z, 0.f, g.speed, {} });
            return true;
        }
        bool cancelGoal(ActorHandle h) override
        {
            mEvents.push_back({ Event::Cancel, h, 0, 0, 0, 0.f, ActorSpeed::Walk, {} });
            return true;
        }
        bool setHeading(ActorHandle, float) override { return true; }
        bool useFurniture(ActorHandle h, const FurnitureUse& u) override
        {
            mEvents.push_back({ Event::Use, h, u.x, u.y, u.z, u.heading, ActorSpeed::Walk, std::string(u.clip) });
            return true;
        }
        bool leaveFurniture(ActorHandle h, double x, double y, double z) override
        {
            mEvents.push_back({ Event::Leave, h, x, y, z, 0.f, ActorSpeed::Walk, {} });
            return true;
        }
        bool placeFurniture(ActorHandle h, double x, double y, double z, float heading) override
        {
            mEvents.push_back({ Event::Place, h, x, y, z, heading, ActorSpeed::Walk, {} });
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

        std::vector<Event> of(Event::Kind kind) const
        {
            std::vector<Event> r;
            for (const Event& e : mEvents)
                if (e.mKind == kind)
                    r.push_back(e);
            return r;
        }
        std::vector<Event> mEvents;
        std::map<WorldId, std::array<double, 3>> mPositions;
    };

    ActorInfo infoOf(WorldId ref, std::uint32_t session, double x = 100, double y = 200, double z = 66)
    {
        ActorInfo i;
        i.handle = { ref, session };
        i.base = kBase;
        i.editorId = "Test";
        i.x = x;
        i.y = y;
        i.z = z;
        return i;
    }

    void step(PackageBrain& brain, Writer& w, int steps, float dt = 0.1f)
    {
        for (int i = 0; i < steps; ++i)
            brain.update(dt, w);
    }

    // ---- preferred speed -------------------------------------------------------------------------------------------

    TEST(SkyrimNpcSpeed, thePreferredSpeedByteCountsOnlyWhenItsFlagIsSet)
    {
        PackageRecord p = makePackage(1);
        p.mPreferredSpeed = 2;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Walk); // the byte is a placeholder without the flag
        p.mFlags = kPreferredSpeedFlag;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Run);
        p.mPreferredSpeed = 1;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Run); // jog runs: there is no jog clip
        p.mPreferredSpeed = 3;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Walk); // fast walk walks
        p.mPreferredSpeed = 0;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Walk);
        p.mPreferredSpeed = 255;
        EXPECT_EQ(packageSpeed(p), ActorSpeed::Walk);
    }

    TEST(SkyrimNpcSpeed, aTravelPackageThatRunsIssuesItsGoalAtTheRunSpeed)
    {
        Source src;
        PackageRecord p = makePackage(1);
        p.mFlags = kPreferredSpeedFlag;
        p.mPreferredSpeed = 2;
        p.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 0 });
        src.mLists[kBase] = { &p };
        src.mBehavior[1] = PackageBehavior::Travel;
        src.mPlaces[0x500] = { 900, 900, 0 };
        PackageBrain brain(src);
        Writer w;
        brain.onActorAdded(infoOf(0x10, 1));
        step(brain, w, 3);
        const auto gotos = w.of(Event::Goto);
        ASSERT_FALSE(gotos.empty());
        EXPECT_EQ(gotos.front().mSpeed, ActorSpeed::Run);
        EXPECT_EQ(brain.stats().mRuns, 1u);
    }

    TEST(SkyrimNpcSpeed, aPackageWithoutTheFlagWalks)
    {
        Source src;
        PackageRecord p = makePackage(1);
        p.mPreferredSpeed = 2;
        p.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 0 });
        src.mLists[kBase] = { &p };
        src.mBehavior[1] = PackageBehavior::Travel;
        src.mPlaces[0x500] = { 900, 900, 0 };
        PackageBrain brain(src);
        Writer w;
        brain.onActorAdded(infoOf(0x10, 1));
        step(brain, w, 3);
        ASSERT_FALSE(w.of(Event::Goto).empty());
        EXPECT_EQ(w.of(Event::Goto).front().mSpeed, ActorSpeed::Walk);
        EXPECT_EQ(brain.stats().mRuns, 0u);
    }

    // ---- patrol ----------------------------------------------------------------------------------------------------

    struct PatrolRig
    {
        Source src;
        PackageRecord pkg = makePackage(7);
        Writer w;
        std::unique_ptr<PackageBrain> brain;
        ActorHandle h{ 0x10, 1 };

        PatrolRig()
        {
            src.mLists[kBase] = { &pkg };
            src.mActivity[7] = PackageActivity::Patrol;
            src.mTarget[7] = { PackageTargetType::SpecificReference, 0xA, 0 };
            src.mPlaces[0xA] = { 100, 0, 0 };
            src.mPlaces[0xB] = { 300, 0, 0 };
            src.mPlaces[0xC] = { 500, 0, 0 };
            src.mLinks[{ 0xA, 0 }] = 0xB;
            src.mLinks[{ 0xB, 0 }] = 0xC;
            src.mIdle[0xA] = 3.f;
            brain = std::make_unique<PackageBrain>(src);
            brain->onActorAdded(infoOf(0x10, 1));
        }
        void arrive() { brain->onGoalFinished(h, GoalResult::Arrived); }
    };

    TEST(SkyrimNpcPatrol, theActorWalksToTheFirstMarkerThenAlongTheLinkedChainIdlingAtEach)
    {
        PatrolRig rig;
        step(*rig.brain, rig.w, 3);
        auto gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 1u);
        EXPECT_DOUBLE_EQ(gotos.back().mX, 100);
        rig.arrive();
        step(*rig.brain, rig.w, 20); // 2 s: the marker says 3
        EXPECT_EQ(rig.w.of(Event::Goto).size(), 1u);
        step(*rig.brain, rig.w, 15); // past 3 s
        gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 2u);
        EXPECT_DOUBLE_EQ(gotos.back().mX, 300);
        rig.arrive();
        step(*rig.brain, rig.w, 25); // B says nothing: the default idle of 2 s applies
        gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 3u);
        EXPECT_DOUBLE_EQ(gotos.back().mX, 500);
    }

    TEST(SkyrimNpcPatrol, theEndOfAChainGoesBackToTheFirstMarker)
    {
        PatrolRig rig;
        step(*rig.brain, rig.w, 3);
        for (int leg = 0; leg < 3; ++leg)
        {
            rig.arrive();
            step(*rig.brain, rig.w, 40);
        }
        const auto gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 4u);
        EXPECT_DOUBLE_EQ(gotos[3].mX, 100); // A, B, C, then A again
        EXPECT_EQ(rig.brain->stats().mPatrolLegs, 4u);
    }

    TEST(SkyrimNpcPatrol, aSingleMarkerIsWaitedAtNotLeft)
    {
        PatrolRig rig;
        rig.src.mLinks.clear();
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 200);
        EXPECT_EQ(rig.w.of(Event::Goto).size(), 1u);
    }

    TEST(SkyrimNpcPatrol, aLinkedReferenceTargetIsTheActorsOwnLink)
    {
        PatrolRig rig;
        rig.src.mTarget[7] = { PackageTargetType::LinkedReference, 0, 0 };
        rig.src.mLinks[{ 0x10, 0 }] = 0xB; // the actor's own placement links to marker B
        step(*rig.brain, rig.w, 3);
        const auto gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 1u);
        EXPECT_DOUBLE_EQ(gotos[0].mX, 300);
    }

    TEST(SkyrimNpcPatrol, aMarkerThatCannotBeFoundIsCountedAndTheActorStands)
    {
        PatrolRig rig;
        rig.src.mPlaces.erase(0xA);
        step(*rig.brain, rig.w, 30);
        EXPECT_TRUE(rig.w.of(Event::Goto).empty());
        EXPECT_GE(rig.brain->stats().mPatrolUnresolved, 1u);
    }

    TEST(SkyrimNpcPatrol, aPatrolPackageIsNotCountedAsUnsupported)
    {
        PatrolRig rig;
        rig.src.mBehavior[7] = PackageBehavior::Unsupported;
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.brain->stats().mUnsupported, 0u);
    }

    // ---- follow ----------------------------------------------------------------------------------------------------

    TEST(SkyrimNpcFollow, aFollowerWalksTowardsATargetActorAndStaysNearIt)
    {
        Source src;
        PackageRecord pkg = makePackage(8);
        src.mLists[kBase] = { &pkg };
        src.mActivity[8] = PackageActivity::Follow;
        src.mTarget[8] = { PackageTargetType::SpecificReference, 0x20, 0 };
        PackageBrain brain(src);
        Writer w;
        w.mPositions[0x10] = { 0, 0, 0 };
        w.mPositions[0x20] = { 1000, 0, 0 };
        brain.onActorAdded(infoOf(0x10, 1, 0, 0, 0));
        brain.onActorAdded(infoOf(0x20, 1, 1000, 0, 0));
        step(brain, w, 30);
        auto gotos = w.of(Event::Goto);
        ASSERT_FALSE(gotos.empty());
        // The goal is kFollowStandOff short of the target, on the follower's side.
        EXPECT_NEAR(gotos[0].mX, 1000 - kFollowStandOff, 1e-6);
        EXPECT_EQ(brain.stats().mFollowLegs, gotos.size());
        // Near the target it makes no new goal.
        w.mPositions[0x10] = { 900, 0, 0 };
        brain.onGoalFinished({ 0x10, 1 }, GoalResult::Arrived);
        const std::size_t before = w.of(Event::Goto).size();
        step(brain, w, 60);
        EXPECT_EQ(w.of(Event::Goto).size(), before);
        // The target moves away: the follower follows.
        w.mPositions[0x20] = { 2000, 0, 0 };
        step(brain, w, 30);
        ASSERT_GT(w.of(Event::Goto).size(), before);
        EXPECT_NEAR(w.of(Event::Goto).back().mX, 2000 - kFollowStandOff, 1e-6);
    }

    TEST(SkyrimNpcFollow, aTargetThatIsNotAnActorOfTheBrainIsCounted)
    {
        Source src;
        PackageRecord pkg = makePackage(8);
        src.mLists[kBase] = { &pkg };
        src.mActivity[8] = PackageActivity::Follow;
        src.mTarget[8] = { PackageTargetType::ReferenceAlias, 5, 0 };
        PackageBrain brain(src);
        Writer w;
        brain.onActorAdded(infoOf(0x10, 1));
        step(brain, w, 30);
        EXPECT_TRUE(w.of(Event::Goto).empty());
        EXPECT_EQ(brain.stats().mFollowUnresolved, 1u);
    }

    TEST(SkyrimNpcFollow, anEscortIsRecognisedAndRunsAsStand)
    {
        Source src;
        PackageRecord pkg = makePackage(9);
        src.mLists[kBase] = { &pkg };
        src.mActivity[9] = PackageActivity::Escort;
        PackageBrain brain(src);
        Writer w;
        brain.onActorAdded(infoOf(0x10, 1));
        step(brain, w, 5);
        EXPECT_EQ(brain.stats().mEscortsSkipped, 1u);
        EXPECT_TRUE(w.of(Event::Goto).empty());
    }

    // ---- furniture rig ---------------------------------------------------------------------------------------------

    ClipMotion chairEnterMotion()
    {
        ClipMotion m;
        m.mDuration = 2.f;
        m.mTranslation = { { 1.f, 0.f, 30.f }, { 2.f, 0.f, 60.f } };
        m.mRotation = { { 2.f, static_cast<float>(kPi / 2) } };
        return m;
    }

    ClipMotion chairExitMotion()
    {
        ClipMotion m;
        m.mDuration = 1.5f;
        m.mTranslation = { { 1.5f, 0.f, 50.f } };
        return m;
    }

    struct SeatRig
    {
        Source src;
        PackageRecord pkg = makePackage(3);
        Writer w;
        std::unique_ptr<PackageBrain> brain;
        ActorHandle h{ 0x10, 1 };
        ClipMotion enter = chairEnterMotion();
        ClipMotion exit = chairExitMotion();

        explicit SeatRig(
            bool withMotion, PackageBehavior behavior = PackageBehavior::Eat, FurniturePose pose = FurniturePose::Sit)
        {
            src.mLists[kBase] = { &pkg };
            src.mBehavior[3] = behavior;
            FurnitureSlot slot;
            slot.mKey = 77;
            slot.mPose = pose;
            slot.mX = 1000;
            slot.mY = 1000;
            slot.mZ = 0;
            slot.mHeading = 0.f;
            slot.mEntryX = 1000;
            slot.mEntryY = 936;
            slot.mEntryZ = 0;
            slot.mClip = pose == FurniturePose::Sit ? "chair_idlebasevar1" : "bed_leftsleep";
            if (withMotion)
            {
                slot.mEnterClip = "chair_leftenter";
                slot.mExitClip = "chair_frontexit";
                slot.mEnterMotion = &enter;
                slot.mExitMotion = &exit;
            }
            src.mSlots.push_back(slot);
            src.mPlaces[0x500] = { 1000, 1000, 0 };
            pkg.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 600 });
            brain = std::make_unique<PackageBrain>(src);
            brain->setFurnitureSource(&src);
            brain->onActorAdded(infoOf(0x10, 1, 1000, 800, 0));
        }
        void arrive() { brain->onGoalFinished(h, GoalResult::Arrived); }
    };

    // ---- the sit-down and get-up clips -----------------------------------------------------------------------------

    TEST(SkyrimNpcClips, withoutClipsTheSeatIsTakenAtOnceAsBefore)
    {
        SeatRig rig(false);
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 2);
        const auto uses = rig.w.of(Event::Use);
        ASSERT_EQ(uses.size(), 1u);
        EXPECT_EQ(uses[0].mClip, "chair_idlebasevar1");
        EXPECT_DOUBLE_EQ(uses[0].mX, 1000);
        EXPECT_TRUE(rig.w.of(Event::Place).empty());
    }

    TEST(SkyrimNpcClips, theWalkEndsWhereTheEnterClipStartsAndTheMotionEndsOnTheSeat)
    {
        SeatRig rig(true);
        step(*rig.brain, rig.w, 3);
        const auto gotos = rig.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 1u);
        // Motion: 60 forward and +90 degrees counter-clockwise. The start heading is the seat's plus 90 degrees (faces
        // +X) and the start is 60 units west of the seat.
        EXPECT_NEAR(gotos[0].mX, 940, 1e-3);
        EXPECT_NEAR(gotos[0].mY, 1000, 1e-3);
        rig.arrive();
        step(*rig.brain, rig.w, 2);
        const auto uses = rig.w.of(Event::Use);
        ASSERT_EQ(uses.size(), 1u);
        EXPECT_EQ(uses[0].mClip, "chair_leftenter");
        EXPECT_NEAR(uses[0].mX, 940, 1e-3);
        EXPECT_NEAR(uses[0].mHeading, kPi / 2, 1e-4);
        EXPECT_EQ(rig.brain->stats().mEnters, 1u);
    }

    TEST(SkyrimNpcClips, theMotionMovesTheActorEachFrameAndTheLoopClipStartsAtTheEnd)
    {
        SeatRig rig(true);
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 2);
        const std::size_t usesBefore = rig.w.of(Event::Use).size();
        // 2 s of clip at 0.1 s steps.
        step(*rig.brain, rig.w, 25);
        const auto places = rig.w.of(Event::Place);
        ASSERT_GE(places.size(), 15u);
        // Halfway (1.0 s): 30 forward from the start along +X, a quarter of the turn... the rows give yaw 0 at 1 s and
        // pi/2 at 2 s, so the heading goes from pi/2 to 0 (clockwise engine turn).
        const Event& mid = places[8];
        EXPECT_GT(mid.mX, 940);
        EXPECT_LT(mid.mX, 1000);
        const auto uses = rig.w.of(Event::Use);
        ASSERT_EQ(uses.size(), usesBefore + 1);
        EXPECT_EQ(uses.back().mClip, "chair_idlebasevar1");
        EXPECT_NEAR(uses.back().mX, 1000, 1e-3);
        EXPECT_NEAR(uses.back().mY, 1000, 1e-3);
        EXPECT_NEAR(uses.back().mHeading, 0.0, 1e-4);
        const auto view = rig.brain->view(0x10);
        ASSERT_TRUE(view.has_value());
        EXPECT_TRUE(view->mSeated);
        EXPECT_FALSE(view->mEntering);
    }

    TEST(SkyrimNpcClips, aSnapTakesNoClip)
    {
        SeatRig rig(true, PackageBehavior::Sleep, FurniturePose::Sleep);
        PackageBrain::Options o;
        o.mSnapSleepers = true;
        rig.brain = std::make_unique<PackageBrain>(rig.src, o);
        rig.brain->setFurnitureSource(&rig.src);
        rig.brain->onActorAdded(infoOf(0x10, 1, 1000, 800, 0));
        step(*rig.brain, rig.w, 3);
        rig.brain->onGoalFinished(rig.h, GoalResult::NoPath);
        step(*rig.brain, rig.w, 3);
        const auto uses = rig.w.of(Event::Use);
        ASSERT_EQ(uses.size(), 1u);
        EXPECT_EQ(uses[0].mClip, "bed_leftsleep");
        EXPECT_TRUE(rig.w.of(Event::Place).empty());
    }

    TEST(SkyrimNpcClips, theSitterGetsUpWithTheExitClipThenTheNextPackageStarts)
    {
        SeatRig rig(true);
        PackageRecord travel = makePackage(4);
        travel.mLocations.push_back({ PackageLocationType::NearReference, 0x501, 0 });
        rig.src.mBehavior[4] = PackageBehavior::Travel;
        rig.src.mPlaces[0x501] = { 3000, 3000, 0 };
        rig.src.mLists[kBase] = { &rig.pkg, &travel };
        rig.brain = std::make_unique<PackageBrain>(rig.src);
        rig.brain->setFurnitureSource(&rig.src);
        rig.brain->onActorAdded(infoOf(0x10, 1, 1000, 800, 0));
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 40);
        ASSERT_TRUE(rig.brain->view(0x10)->mSeated);
        const std::size_t gotosBefore = rig.w.of(Event::Goto).size();
        // The eating package's schedule no longer fits: the travel package is next.
        rig.pkg.mSchedule.mHour = 20;
        rig.pkg.mSchedule.mDurationMinutes = 60;
        step(*rig.brain, rig.w, 12);
        auto uses = rig.w.of(Event::Use);
        ASSERT_GE(uses.size(), 3u); // the enter clip, the loop, the exit clip
        EXPECT_EQ(uses.back().mClip, "chair_frontexit");
        EXPECT_NEAR(uses.back().mX, 1000, 1e-3);
        EXPECT_NEAR(uses.back().mY, 1000, 1e-3);
        EXPECT_TRUE(rig.w.of(Event::Leave).empty()); // still getting up
        EXPECT_EQ(rig.w.of(Event::Goto).size(), gotosBefore); // the next package waits for the get-up
        step(*rig.brain, rig.w, 40); // 1.5 s of clip
        const auto leaves = rig.w.of(Event::Leave);
        ASSERT_EQ(leaves.size(), 1u);
        EXPECT_NEAR(leaves[0].mX, 1000, 1e-3);
        EXPECT_NEAR(leaves[0].mY, 1050, 1e-3); // 50 forward from the seat
        EXPECT_EQ(rig.brain->stats().mExits, 1u);
        EXPECT_GT(rig.w.of(Event::Goto).size(), gotosBefore);
        EXPECT_NEAR(rig.w.of(Event::Goto).back().mX, 3000, 1e-3);
    }

    // ---- talking ---------------------------------------------------------------------------------------------------

    TEST(SkyrimNpcTalk, aSleeperWakesAndStandsAndGoesBackToBedAfterTheTalk)
    {
        SeatRig rig(false, PackageBehavior::Sleep, FurniturePose::Sleep);
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 5);
        ASSERT_TRUE(rig.brain->view(0x10)->mSeated);
        rig.brain->onConversation(0x10, true);
        step(*rig.brain, rig.w, 2);
        EXPECT_EQ(rig.w.of(Event::Leave).size(), 1u);
        EXPECT_FALSE(rig.brain->view(0x10)->mSeated);
        EXPECT_EQ(rig.brain->stats().mWakeUps, 1u);
        const std::size_t gotos = rig.w.of(Event::Goto).size();
        step(*rig.brain, rig.w, 100); // the talk goes on: the actor stays
        EXPECT_EQ(rig.w.of(Event::Goto).size(), gotos);
        rig.brain->onConversation(0x10, false);
        step(*rig.brain, rig.w, 3);
        EXPECT_GT(rig.w.of(Event::Goto).size(), gotos); // back to the bed
        EXPECT_EQ(rig.brain->stats().mResumes, 1u);
        rig.arrive();
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.w.of(Event::Use).size(), 2u);
    }

    TEST(SkyrimNpcTalk, aSleeperWokenToTalkGetsUpWithTheQuickExitClipAndResumesAfterTheTalk)
    {
        SeatRig rig(true, PackageBehavior::Sleep, FurniturePose::Sleep);
        ClipMotion quick;
        quick.mDuration = 1.f;
        quick.mTranslation = { { 1.f, -80.f, 0.f } };
        rig.src.mSlots[0].mQuickExitClip = "bed_leftquickexit";
        rig.src.mSlots[0].mQuickExitMotion = &quick;
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 40); // the lie-down clip (2 s) is over
        ASSERT_TRUE(rig.brain->view(0x10)->mSeated);
        rig.brain->onConversation(0x10, true);
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.w.of(Event::Use).back().mClip, "bed_leftquickexit");
        EXPECT_TRUE(rig.w.of(Event::Leave).empty());
        step(*rig.brain, rig.w, 12); // 1 s of clip
        const auto leaves = rig.w.of(Event::Leave);
        ASSERT_EQ(leaves.size(), 1u);
        EXPECT_NEAR(leaves[0].mX, 920, 1e-3); // the motion is 80 to the left of the sleeper (facing +Y, left is -X)
        EXPECT_NEAR(leaves[0].mY, 1000, 1e-3);
        const std::size_t gotos = rig.w.of(Event::Goto).size();
        step(*rig.brain, rig.w, 50); // still talking: stands
        EXPECT_EQ(rig.w.of(Event::Goto).size(), gotos);
        rig.brain->onConversation(0x10, false);
        step(*rig.brain, rig.w, 3);
        EXPECT_GT(rig.w.of(Event::Goto).size(), gotos);
        EXPECT_EQ(rig.brain->stats().mResumes, 1u);
    }

    TEST(SkyrimNpcTalk, aBeginAndAnEndThatArriveTogetherAreActedOnInOrder)
    {
        // The simulation is paused while the conversation box is open, so update() sees both at once.
        SeatRig rig(false, PackageBehavior::Sleep, FurniturePose::Sleep);
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 5);
        ASSERT_TRUE(rig.brain->view(0x10)->mSeated);
        rig.brain->onConversation(0x10, true);
        rig.brain->onConversation(0x10, false);
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.brain->stats().mWakeUps, 1u);
        EXPECT_EQ(rig.w.of(Event::Leave).size(), 1u); // got up ...
        EXPECT_EQ(rig.brain->stats().mResumes, 1u); // ... and went back to bed
        EXPECT_FALSE(rig.brain->view(0x10)->mTalking);
        rig.arrive();
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.w.of(Event::Use).size(), 2u);
    }

    TEST(SkyrimNpcTalk, aSitterStaysSeatedAndItsTimerWaits)
    {
        SeatRig rig(false);
        step(*rig.brain, rig.w, 3);
        rig.arrive();
        step(*rig.brain, rig.w, 5);
        rig.brain->onConversation(0x10, true);
        step(*rig.brain, rig.w, 50);
        EXPECT_TRUE(rig.brain->view(0x10)->mSeated);
        EXPECT_TRUE(rig.w.of(Event::Leave).empty());
        rig.brain->onConversation(0x10, false);
        step(*rig.brain, rig.w, 5);
        EXPECT_TRUE(rig.brain->view(0x10)->mSeated);
    }

    TEST(SkyrimNpcTalk, aWalkerStopsWhileItTalksAndWalksOnAfterwards)
    {
        PatrolRig rig;
        step(*rig.brain, rig.w, 3);
        ASSERT_EQ(rig.w.of(Event::Goto).size(), 1u);
        rig.brain->onConversation(0x10, true);
        step(*rig.brain, rig.w, 2);
        EXPECT_EQ(rig.w.of(Event::Cancel).size(), 1u);
        step(*rig.brain, rig.w, 100);
        EXPECT_EQ(rig.w.of(Event::Goto).size(), 1u);
        rig.brain->onConversation(0x10, false);
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.w.of(Event::Goto).size(), 2u);
    }

    TEST(SkyrimNpcTalk, aConversationWithAnUnknownActorIsIgnored)
    {
        PatrolRig rig;
        rig.brain->onConversation(0x999, true);
        rig.brain->onConversation(0x999, false);
        step(*rig.brain, rig.w, 3);
        EXPECT_EQ(rig.brain->stats().mConversations, 0u);
    }
}
