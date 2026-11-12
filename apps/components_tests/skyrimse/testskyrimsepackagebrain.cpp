// Task 0135 stage S5: the scheduler for Skyrim SE AI packages (schedules, selection, sandbox/travel/stand behaviour,
// determinism) over a fake source and a fake writer. No game data.
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x00013BA7;
    constexpr FormId kMarker = 0x000D750A;

    PackageRecord makePackage(FormId id, std::int8_t hour = -1, std::int32_t duration = 0, std::int8_t dow = -1)
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mHasPkdt = true;
        p.mType = 18;
        p.mSchedule.mHour = hour;
        p.mSchedule.mDurationMinutes = duration;
        p.mSchedule.mDayOfWeek = dow;
        return p;
    }

    class FakeSource final : public PackageSource
    {
    public:
        bool ready() const override { return mReady; }
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
        bool conditionsPass(const PackageRecord& p, FormId, FormId) override
        {
            ++mConditionCalls;
            return mFailing.count(p.mCommon.mId) == 0;
        }
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
        bool mReady = true;
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::set<FormId> mFailing;
        std::map<FormId, std::array<double, 3>> mPlaces;
        int mConditionCalls = 0;
    };

    struct Call
    {
        enum Kind
        {
            Goto,
            Cancel,
            Heading,
            Move
        } mKind;
        ActorHandle mHandle;
        double mX = 0, mY = 0, mZ = 0;
        ActorSpeed mSpeed = ActorSpeed::Walk;
    };

    class FakeWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle h, float, float) override
        {
            mCalls.push_back({ Call::Move, h });
            return mAccept;
        }
        bool goTo(ActorHandle h, const ActorGoal& g) override
        {
            mCalls.push_back({ Call::Goto, h, g.x, g.y, g.z, g.speed });
            return mAccept;
        }
        bool cancelGoal(ActorHandle h) override
        {
            mCalls.push_back({ Call::Cancel, h });
            return mAccept;
        }
        bool setHeading(ActorHandle h, float) override
        {
            mCalls.push_back({ Call::Heading, h });
            return mAccept;
        }
        ActorClock clock() const override { return mClock; }
        std::vector<Call> mCalls;
        ActorClock mClock{ 12.f, 0, true };
        bool mAccept = true;

        std::vector<Call> gotos() const
        {
            std::vector<Call> r;
            for (const Call& c : mCalls)
                if (c.mKind == Call::Goto)
                    r.push_back(c);
            return r;
        }
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

    void run(PackageBrain& brain, FakeWriter& writer, float seconds, int steps, float dt = 0.1f)
    {
        for (int i = 0; i < steps; ++i)
        {
            brain.update(dt, writer);
            (void)seconds;
        }
    }

    double dist2d(const Call& c, double x, double y)
    {
        return std::hypot(c.mX - x, c.mY - y);
    }

    // ---- schedules -------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageSchedule, anyHourAlwaysPassesWhateverTheDuration)
    {
        PackageSchedule s;
        s.mHour = -1;
        s.mDurationMinutes = 300;
        for (const float h : { 0.f, 5.5f, 23.99f })
            EXPECT_TRUE(scheduleActive(s, h, 3, true));
    }

    TEST(SkyrimPackageSchedule, aWindowStartsAtItsHourAndEndsAfterItsDuration)
    {
        PackageSchedule s;
        s.mHour = 8;
        s.mDurationMinutes = 240; // 8:00 to 12:00
        EXPECT_FALSE(scheduleActive(s, 7.99f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 8.f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 11.99f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 12.f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 20.f, 0, true));
    }

    TEST(SkyrimPackageSchedule, aWindowMayCrossMidnight)
    {
        PackageSchedule s;
        s.mHour = 22;
        s.mDurationMinutes = 480; // 22:00 to 6:00
        EXPECT_TRUE(scheduleActive(s, 22.f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 23.9f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 0.f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 5.9f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 6.f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 12.f, 0, true));
    }

    TEST(SkyrimPackageSchedule, theMinuteMovesTheStart)
    {
        PackageSchedule s;
        s.mHour = 8;
        s.mMinute = 30;
        s.mDurationMinutes = 60;
        EXPECT_FALSE(scheduleActive(s, 8.4f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 8.5f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 9.4f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 9.5f, 0, true));
    }

    TEST(SkyrimPackageSchedule, anHourWithNoDurationIsAOneHourWindow)
    {
        PackageSchedule s;
        s.mHour = 10;
        s.mDurationMinutes = 0;
        EXPECT_TRUE(scheduleActive(s, 10.5f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 11.f, 0, true));
        EXPECT_FALSE(scheduleActive(s, 9.9f, 0, true));
    }

    TEST(SkyrimPackageSchedule, aDurationOfADayOrMoreIsAlwaysOn)
    {
        PackageSchedule s;
        s.mHour = 3;
        s.mDurationMinutes = 5000;
        for (const float h : { 0.f, 2.9f, 3.f, 15.f, 23.f })
            EXPECT_TRUE(scheduleActive(s, h, 0, true));
    }

    TEST(SkyrimPackageSchedule, dayOfWeekFilters)
    {
        PackageSchedule s;
        s.mDayOfWeek = 3;
        // The start day is weekday 1 (Monday): day 2 is weekday 3.
        EXPECT_EQ(weekdayOfDay(0), 1);
        EXPECT_EQ(weekdayOfDay(2), 3);
        EXPECT_EQ(weekdayOfDay(6), 0);
        EXPECT_EQ(weekdayOfDay(7), 1);
        EXPECT_TRUE(scheduleActive(s, 12.f, 2, true));
        EXPECT_FALSE(scheduleActive(s, 12.f, 3, true));
        s.mDayOfWeek = 7; // weekdays
        EXPECT_TRUE(scheduleActive(s, 12.f, 0, true)); // Monday
        EXPECT_FALSE(scheduleActive(s, 12.f, 5, true)); // Saturday
        s.mDayOfWeek = 8; // weekends
        EXPECT_TRUE(scheduleActive(s, 12.f, 5, true));
        EXPECT_TRUE(scheduleActive(s, 12.f, 6, true)); // Sunday
        EXPECT_FALSE(scheduleActive(s, 12.f, 0, true));
        s.mDayOfWeek = 9; // Monday Wednesday Friday
        EXPECT_TRUE(scheduleActive(s, 12.f, 0, true));
        EXPECT_TRUE(scheduleActive(s, 12.f, 4, true)); // Friday
        EXPECT_FALSE(scheduleActive(s, 12.f, 1, true)); // Tuesday
        s.mDayOfWeek = 10; // Tuesday Thursday
        EXPECT_TRUE(scheduleActive(s, 12.f, 1, true));
        EXPECT_TRUE(scheduleActive(s, 12.f, 3, true));
        EXPECT_FALSE(scheduleActive(s, 12.f, 0, true));
        s.mDayOfWeek = 42; // not a filter
        EXPECT_FALSE(scheduleActive(s, 12.f, 0, true));
    }

    TEST(SkyrimPackageSchedule, anInvalidClockPassesEverySchedule)
    {
        PackageSchedule s;
        s.mHour = 3;
        s.mDurationMinutes = 60;
        s.mDayOfWeek = 9;
        EXPECT_TRUE(scheduleActive(s, 15.f, 1, false));
    }

    // ---- selection -------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageSelect, theFirstPackageInForceWins)
    {
        FakeSource src;
        const PackageRecord night = makePackage(1, 22, 480);
        const PackageRecord day = makePackage(2);
        const PackageRecord other = makePackage(3);
        const std::vector<const PackageRecord*> list{ &night, &day, &other };
        EXPECT_EQ(selectPackage(list, 12.f, 0, true, src, kBase, 1), &day);
        EXPECT_EQ(selectPackage(list, 23.f, 0, true, src, kBase, 1), &night);
    }

    TEST(SkyrimPackageSelect, aFailingConditionSkipsThePackage)
    {
        FakeSource src;
        const PackageRecord a = makePackage(1);
        const PackageRecord b = makePackage(2);
        const std::vector<const PackageRecord*> list{ &a, &b };
        src.mFailing.insert(1);
        EXPECT_EQ(selectPackage(list, 12.f, 0, true, src, kBase, 1), &b);
        src.mFailing.insert(2);
        EXPECT_EQ(selectPackage(list, 12.f, 0, true, src, kBase, 1), nullptr);
    }

    TEST(SkyrimPackageSelect, theScheduleIsCheckedBeforeTheConditions)
    {
        FakeSource src;
        const PackageRecord a = makePackage(1, 22, 60);
        const std::vector<const PackageRecord*> list{ &a };
        EXPECT_EQ(selectPackage(list, 12.f, 0, true, src, kBase, 1), nullptr);
        EXPECT_EQ(src.mConditionCalls, 0);
    }

    TEST(SkyrimPackageSelect, anEmptyListSelectsNothing)
    {
        FakeSource src;
        EXPECT_EQ(selectPackage({}, 12.f, 0, true, src, kBase, 1), nullptr);
    }

    // ---- the brain -------------------------------------------------------------------------------------------------

    struct Rig
    {
        FakeSource mSource;
        std::vector<PackageRecord> mPackages;
        FakeWriter mWriter;
        std::unique_ptr<PackageBrain> mBrain;
        Rig() { mPackages.reserve(16); }
        const PackageRecord* add(PackageRecord p, PackageBehavior b)
        {
            mPackages.push_back(std::move(p));
            const PackageRecord* r = &mPackages.back();
            mSource.mBehavior[r->mCommon.mId] = b;
            return r;
        }
        void finish(std::vector<const PackageRecord*> list, std::uint64_t seed = 1)
        {
            mSource.mLists[kBase] = std::move(list);
            PackageBrain::Options o;
            o.mSeed = seed;
            mBrain = std::make_unique<PackageBrain>(mSource, o);
        }
    };

    PackageRecord withLocation(PackageRecord p, std::int32_t type, std::uint32_t value, std::int32_t radius)
    {
        p.mLocations.push_back({ type, value, radius });
        return p;
    }

    TEST(SkyrimPackageBrain, nothingHappensWhileTheRecordsAreNotReady)
    {
        Rig rig;
        const PackageRecord* p = rig.add(makePackage(1), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mSource.mReady = false;
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 5, 50);
        EXPECT_TRUE(rig.mWriter.mCalls.empty());
        EXPECT_EQ(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Waiting);
        rig.mSource.mReady = true;
        run(*rig.mBrain, rig.mWriter, 5, 30);
        EXPECT_NE(rig.mBrain->view(10)->mActive, nullptr);
    }

    TEST(SkyrimPackageBrain, anActorWithNoPackagesStandsStill)
    {
        Rig rig;
        rig.finish({});
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 5, 50);
        EXPECT_TRUE(rig.mWriter.mCalls.empty());
        EXPECT_EQ(rig.mBrain->view(10)->mActive, nullptr);
    }

    TEST(SkyrimPackageBrain, anActorOfAnUnknownBaseStandsStill)
    {
        Rig rig;
        rig.finish({});
        rig.mSource.mLists.clear();
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 5, 50);
        EXPECT_TRUE(rig.mWriter.mCalls.empty());
    }

    TEST(SkyrimPackageBrain, travelWalksToTheReferenceAndStaysThereOnceItArrived)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 500, 600, 70 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 64), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        const auto goals = rig.mWriter.gotos();
        ASSERT_EQ(goals.size(), 1u);
        EXPECT_EQ(goals[0].mX, 500);
        EXPECT_EQ(goals[0].mY, 600);
        EXPECT_EQ(goals[0].mZ, 70);
        EXPECT_EQ(goals[0].mSpeed, ActorSpeed::Walk);
        EXPECT_EQ(goals[0].mHandle, (ActorHandle{ 10, 1 }));
        EXPECT_EQ(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Walking);
        rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
        run(*rig.mBrain, rig.mWriter, 60, 600);
        EXPECT_EQ(rig.mWriter.gotos().size(), 1u);
        const auto v = rig.mBrain->view(10);
        EXPECT_EQ(v->mPhase, PackageBrain::Phase::Done);
        EXPECT_EQ(v->mX, 500);
        EXPECT_EQ(rig.mBrain->stats().mArrived, 1u);
    }

    TEST(SkyrimPackageBrain, travelWithTheActorAlreadyThereAsksForNothing)
    {
        Rig rig;
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 64), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_TRUE(rig.mWriter.gotos().empty());
        EXPECT_EQ(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimPackageBrain, anUnplaceableLocationFallsBackToTheHomeAndIsCounted)
    {
        Rig rig;
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, 0x777, 64), PackageBehavior::Travel);
        const PackageRecord* alias
            = rig.add(withLocation(makePackage(2), PackageLocationType::AliasReference, 3, 64), PackageBehavior::Travel);
        rig.finish({ p, alias });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        const auto v = rig.mBrain->view(10);
        EXPECT_EQ(v->mAnchorX, 100);
        EXPECT_EQ(v->mAnchorY, 200);
        EXPECT_GE(rig.mBrain->stats().mUnresolvedLocations, 1u);
    }

    TEST(SkyrimPackageBrain, nearSelfAnchorsAtTheActorsPlace)
    {
        Rig rig;
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearSelf, 0, 200), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1, 30, 40, 5));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        const auto v = rig.mBrain->view(10);
        EXPECT_EQ(v->mAnchorX, 30);
        EXPECT_EQ(v->mAnchorY, 40);
        EXPECT_EQ(v->mRadius, 200.f);
    }

    TEST(SkyrimPackageBrain, anAbsurdRadiusIsCut)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 2000000000), PackageBehavior::Sandbox);
        const PackageRecord* n = rig.add(
            withLocation(makePackage(2), PackageLocationType::NearEditorLocation, 0, -50), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mRadius, kMaxPackageRadius);
        Rig rig2;
        const PackageRecord* n2 = rig2.add(
            withLocation(makePackage(2), PackageLocationType::NearEditorLocation, 0, -50), PackageBehavior::Sandbox);
        rig2.finish({ n2 });
        rig2.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig2.mBrain, rig2.mWriter, 3, 30);
        EXPECT_EQ(rig2.mBrain->view(10)->mRadius, 0.f);
        (void)n;
    }

    // Sandbox ---------------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageBrain, aSandboxActorWandersToPointsWithinTheRadiusAndPausesBetweenThem)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 256), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        // Frames of 0.1 s; every goal is answered with Arrived in the next frame.
        std::size_t answered = 0;
        for (int i = 0; i < 2400; ++i)
        {
            rig.mBrain->update(0.1f, rig.mWriter);
            const auto goals = rig.mWriter.gotos();
            if (goals.size() > answered)
            {
                answered = goals.size();
                rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
            }
        }
        const auto goals = rig.mWriter.gotos();
        ASSERT_GE(goals.size(), 12u); // 240 s with pauses of 3 to 10 s
        ASSERT_LE(goals.size(), 90u);
        bool moved = false;
        for (const Call& g : goals)
        {
            EXPECT_LE(dist2d(g, 100, 200), 256.0 + 1e-6);
            EXPECT_EQ(g.mZ, 66);
            moved = moved || dist2d(g, 100, 200) > 1.0;
        }
        EXPECT_TRUE(moved);
        // Not two goals in one second: a pause of at least kPauseMinSeconds comes first.
        EXPECT_EQ(rig.mBrain->stats().mArrived, goals.size());
    }

    TEST(SkyrimPackageBrain, aSandboxPausesForAtLeastTheMinimumBetweenLegs)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 256), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        std::vector<int> goalFrames;
        std::size_t answered = 0;
        for (int i = 0; i < 1200; ++i)
        {
            rig.mBrain->update(0.1f, rig.mWriter);
            if (rig.mWriter.gotos().size() > answered)
            {
                answered = rig.mWriter.gotos().size();
                goalFrames.push_back(i);
                rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
            }
        }
        ASSERT_GE(goalFrames.size(), 3u);
        for (std::size_t i = 1; i < goalFrames.size(); ++i)
            EXPECT_GE((goalFrames[i] - goalFrames[i - 1]) * 0.1f, kPauseMinSeconds - 0.25f);
    }

    TEST(SkyrimPackageBrain, theSameSeedGivesTheSameWalkAndAnotherSeedAnotherOne)
    {
        auto walk = [](std::uint64_t seed) {
            Rig rig;
            const PackageRecord* p = rig.add(
                withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 300), PackageBehavior::Sandbox);
            rig.finish({ p }, seed);
            rig.mBrain->onActorAdded(infoOf(10, 1));
            rig.mBrain->onActorAdded(infoOf(11, 2, 50, 50, 66));
            std::size_t a10 = 0;
            for (int i = 0; i < 1500; ++i)
            {
                rig.mBrain->update(0.1f, rig.mWriter);
                std::size_t n = 0;
                for (const Call& g : rig.mWriter.gotos())
                    if (g.mHandle.reference == 10)
                        ++n;
                if (n > a10)
                {
                    a10 = n;
                    rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
                }
            }
            std::vector<std::array<double, 3>> out;
            for (const Call& g : rig.mWriter.gotos())
                out.push_back({ static_cast<double>(g.mHandle.reference), g.mX, g.mY });
            return out;
        };
        const auto a = walk(7);
        const auto b = walk(7);
        const auto c = walk(8);
        ASSERT_FALSE(a.empty());
        EXPECT_EQ(a, b);
        EXPECT_NE(a, c);
    }

    TEST(SkyrimPackageBrain, twoActorsOfOneSeedDoNotWalkTheSameWalk)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 400), PackageBehavior::Sandbox);
        rig.finish({ p }, 3);
        rig.mBrain->onActorAdded(infoOf(10, 1, 0, 0, 0));
        rig.mBrain->onActorAdded(infoOf(11, 2, 0, 0, 0));
        run(*rig.mBrain, rig.mWriter, 20, 200);
        double first10 = -1, first11 = -1;
        for (const Call& g : rig.mWriter.gotos())
        {
            if (g.mHandle.reference == 10 && first10 < 0)
                first10 = g.mX * 1000 + g.mY;
            if (g.mHandle.reference == 11 && first11 < 0)
                first11 = g.mX * 1000 + g.mY;
        }
        ASSERT_GE(first10, -1e18);
        EXPECT_NE(first10, first11);
    }

    TEST(SkyrimPackageBrain, aSandboxActorFarFromItsAnchorGoesBackFirst)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 1000, 1000, 66 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 200), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1, 0, 0, 66));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        const auto goals = rig.mWriter.gotos();
        ASSERT_GE(goals.size(), 1u);
        EXPECT_EQ(goals[0].mX, 1000);
        EXPECT_EQ(goals[0].mY, 1000);
    }

    TEST(SkyrimPackageBrain, aFailedLegIsFollowedByAPauseBeforeAnotherTry)
    {
        for (const GoalResult failure : { GoalResult::NoPath, GoalResult::Blocked })
        {
            Rig rig;
            const PackageRecord* p = rig.add(
                withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 256), PackageBehavior::Sandbox);
            rig.finish({ p });
            rig.mBrain->onActorAdded(infoOf(10, 1));
            int firstGoal = -1;
            int secondGoal = -1;
            for (int i = 0; i < 600; ++i)
            {
                rig.mBrain->update(0.1f, rig.mWriter);
                const std::size_t n = rig.mWriter.gotos().size();
                if (n == 1 && firstGoal < 0)
                {
                    firstGoal = i;
                    rig.mBrain->onGoalFinished({ 10, 1 }, failure);
                }
                if (n == 2 && secondGoal < 0)
                    secondGoal = i;
            }
            ASSERT_GE(firstGoal, 0);
            ASSERT_GE(secondGoal, 0);
            EXPECT_GE((secondGoal - firstGoal) * 0.1f, kFailurePauseSeconds - 0.25f);
            EXPECT_EQ(rig.mBrain->stats().mNoPath + rig.mBrain->stats().mBlocked, 1u);
        }
    }

    TEST(SkyrimPackageBrain, aSandboxWithNoRoomToWanderNeverMoves)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 10), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 100, 1000);
        EXPECT_TRUE(rig.mWriter.gotos().empty());
    }

    // Stand-like ------------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageBrain, sleepEatAndStandHoldNearTheLocation)
    {
        for (const PackageBehavior b : { PackageBehavior::Sleep, PackageBehavior::Eat, PackageBehavior::Stand,
                 PackageBehavior::Unsupported })
        {
            Rig rig;
            rig.mSource.mPlaces[kMarker] = { 400, 0, 66 };
            const PackageRecord* p
                = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 100), b);
            rig.finish({ p });
            rig.mBrain->onActorAdded(infoOf(10, 1, 0, 0, 66));
            run(*rig.mBrain, rig.mWriter, 3, 30);
            ASSERT_EQ(rig.mWriter.gotos().size(), 1u);
            EXPECT_EQ(rig.mWriter.gotos()[0].mX, 400);
            rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
            run(*rig.mBrain, rig.mWriter, 100, 1000);
            EXPECT_EQ(rig.mWriter.gotos().size(), 1u);
            EXPECT_EQ(rig.mBrain->stats().mUnsupported, b == PackageBehavior::Unsupported ? 1u : 0u);
        }
    }

    TEST(SkyrimPackageBrain, standingWithinTheRadiusNeedsNoWalk)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 512), PackageBehavior::Sleep);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_TRUE(rig.mWriter.gotos().empty());
    }

    TEST(SkyrimPackageBrain, aFailedTripToTheAnchorIsRetriedAfterTheRetryPeriod)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 400, 0, 66 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 100), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1, 0, 0, 66));
        int first = -1, second = -1;
        for (int i = 0; i < 1000; ++i)
        {
            rig.mBrain->update(0.1f, rig.mWriter);
            const std::size_t n = rig.mWriter.gotos().size();
            if (n == 1 && first < 0)
            {
                first = i;
                rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::NoPath);
            }
            if (n == 2 && second < 0)
                second = i;
        }
        ASSERT_GE(first, 0);
        ASSERT_GE(second, 0);
        EXPECT_GE((second - first) * 0.1f, kRetrySeconds - 0.25f);
    }

    // Switching -------------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageBrain, thePackageChangesWithTheHourAndAWalkingActorHasItsGoalCancelled)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 900, 0, 66 };
        const PackageRecord* night = rig.add(
            withLocation(makePackage(1, 22, 480), PackageLocationType::NearReference, kMarker, 64), PackageBehavior::Sleep);
        const PackageRecord* day = rig.add(
            withLocation(makePackage(2), PackageLocationType::NearEditorLocation, 0, 64), PackageBehavior::Travel);
        rig.finish({ night, day });
        rig.mWriter.mClock = { 12.f, 0, true };
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, day);
        EXPECT_TRUE(rig.mWriter.gotos().empty()); // at home already
        rig.mWriter.mClock = { 22.5f, 0, true };
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, night);
        ASSERT_EQ(rig.mWriter.gotos().size(), 1u);
        EXPECT_EQ(rig.mWriter.gotos()[0].mX, 900);
        // Morning again while the walk is out: the goal is cancelled and the day package takes over.
        rig.mWriter.mClock = { 7.f, 1, true };
        rig.mWriter.mCalls.clear();
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, day);
        bool cancelled = false;
        for (const Call& c : rig.mWriter.mCalls)
            cancelled = cancelled || c.mKind == Call::Cancel;
        EXPECT_TRUE(cancelled);
    }

    TEST(SkyrimPackageBrain, aConditionThatStopsPassingMovesTheActorToTheNextPackage)
    {
        Rig rig;
        const PackageRecord* a
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 64), PackageBehavior::Sandbox);
        const PackageRecord* b
            = rig.add(withLocation(makePackage(2), PackageLocationType::NearEditorLocation, 0, 64), PackageBehavior::Travel);
        rig.finish({ a, b });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, a);
        rig.mSource.mFailing.insert(1);
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, b);
        EXPECT_GE(rig.mBrain->stats().mSwitches, 2u);
    }

    TEST(SkyrimPackageBrain, whenNothingPassesAnyMoreTheActorStandsWhereItIs)
    {
        Rig rig;
        const PackageRecord* a
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 256), PackageBehavior::Sandbox);
        rig.finish({ a });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        rig.mSource.mFailing.insert(1);
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_EQ(rig.mBrain->view(10)->mActive, nullptr);
        EXPECT_EQ(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Idle);
        EXPECT_GE(rig.mBrain->stats().mNoPackage, 1u);
        rig.mWriter.mCalls.clear();
        run(*rig.mBrain, rig.mWriter, 100, 1000);
        EXPECT_TRUE(rig.mWriter.gotos().empty());
    }

    TEST(SkyrimPackageBrain, theConditionsAreNotAskedEveryFrame)
    {
        Rig rig;
        const PackageRecord* a = rig.add(makePackage(1), PackageBehavior::Stand);
        rig.finish({ a });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 10, 100); // 10 s of frames
        EXPECT_GE(rig.mSource.mConditionCalls, 8);
        EXPECT_LE(rig.mSource.mConditionCalls, 12);
    }

    // Bookkeeping -----------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageBrain, aRemovedActorIsForgottenAndAStaleResultIsIgnored)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 500, 0, 66 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 64), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        rig.mBrain->onActorRemoved({ 10, 1 });
        EXPECT_EQ(rig.mBrain->size(), 0u);
        EXPECT_FALSE(rig.mBrain->view(10).has_value());
        rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived); // no crash, no effect
        rig.mWriter.mCalls.clear();
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_TRUE(rig.mWriter.mCalls.empty());
    }

    TEST(SkyrimPackageBrain, aNewSessionOfTheSamePlacementReplacesTheOldAndAnOldRemoveIsIgnored)
    {
        Rig rig;
        const PackageRecord* p = rig.add(makePackage(1), PackageBehavior::Stand);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        rig.mBrain->onActorAdded(infoOf(10, 2));
        EXPECT_EQ(rig.mBrain->size(), 1u);
        rig.mBrain->onActorRemoved({ 10, 1 });
        EXPECT_EQ(rig.mBrain->size(), 1u);
        rig.mBrain->onActorRemoved({ 10, 2 });
        EXPECT_EQ(rig.mBrain->size(), 0u);
    }

    TEST(SkyrimPackageBrain, aResultForAnOldSessionDoesNotCountForTheNewOne)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 500, 0, 66 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 64), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 2));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        rig.mBrain->onGoalFinished({ 10, 1 }, GoalResult::Arrived);
        EXPECT_EQ(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Walking);
        EXPECT_EQ(rig.mBrain->stats().mArrived, 0u);
    }

    TEST(SkyrimPackageBrain, aRefusedRequestIsCountedAndTheBrainGoesOn)
    {
        Rig rig;
        rig.mSource.mPlaces[kMarker] = { 500, 0, 66 };
        const PackageRecord* p
            = rig.add(withLocation(makePackage(1), PackageLocationType::NearReference, kMarker, 64), PackageBehavior::Travel);
        rig.finish({ p });
        rig.mWriter.mAccept = false;
        rig.mBrain->onActorAdded(infoOf(10, 1));
        run(*rig.mBrain, rig.mWriter, 3, 30);
        EXPECT_GE(rig.mBrain->stats().mWritesRefused, 1u);
        EXPECT_NE(rig.mBrain->view(10)->mPhase, PackageBrain::Phase::Walking);
    }

    TEST(SkyrimPackageBrain, anInvalidHandleIsNotAdded)
    {
        Rig rig;
        rig.finish({});
        ActorInfo bad;
        rig.mBrain->onActorAdded(bad);
        EXPECT_EQ(rig.mBrain->size(), 0u);
    }

    TEST(SkyrimPackageBrain, aNonFiniteOrAbsurdFrameTimeDoesNotBreakTheTimers)
    {
        Rig rig;
        const PackageRecord* p = rig.add(
            withLocation(makePackage(1), PackageLocationType::NearEditorLocation, 0, 256), PackageBehavior::Sandbox);
        rig.finish({ p });
        rig.mBrain->onActorAdded(infoOf(10, 1));
        rig.mBrain->update(std::numeric_limits<float>::quiet_NaN(), rig.mWriter);
        rig.mBrain->update(-5.f, rig.mWriter);
        rig.mBrain->update(1e30f, rig.mWriter);
        for (const Call& g : rig.mWriter.gotos())
            EXPECT_TRUE(std::isfinite(g.mX) && std::isfinite(g.mY));
    }
}
