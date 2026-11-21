// Task 0196 (followers, milestone M-S7): the pack's side. The package brain's follow-the-player mode, the actor table's
// recruit and dismiss, the FollowerRules answers, the save chunk and the alias change rule. Pure tests over a fake
// source and a fake writer; no game data.
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/follower.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x00013BA7;

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
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, std::array<double, 3>> mPlaces;
    };

    struct Event
    {
        enum Kind
        {
            Goto,
            Cancel
        } mKind;
        ActorHandle mHandle;
        double mX = 0, mY = 0, mZ = 0;
        ActorSpeed mSpeed = ActorSpeed::Walk;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle h, const ActorGoal& g) override
        {
            mEvents.push_back({ Event::Goto, h, g.x, g.y, g.z, g.speed });
            return true;
        }
        bool cancelGoal(ActorHandle h) override
        {
            mEvents.push_back({ Event::Cancel, h, 0, 0, 0, ActorSpeed::Walk });
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
        bool playerPosition(double& x, double& y, double& z) const override
        {
            if (!mHasPlayer)
                return false;
            x = mPlayer[0];
            y = mPlayer[1];
            z = mPlayer[2];
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
        std::array<double, 3> mPlayer{ 0, 0, 0 };
        bool mHasPlayer = true;
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

    constexpr WorldId kRef = 0x10;

    // A follower 1000 units from the player, with a package that would send it elsewhere.
    struct Rig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;
        ActorHandle h{ kRef, 1 };

        Rig()
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 0 });
            src.mLists[kBase] = { &pkg };
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mPlaces[0x500] = { 9000, 9000, 0 };
            brain = std::make_unique<PackageBrain>(src);
            w.mPositions[kRef] = { 1000, 0, 66 };
            w.mPlayer = { 0, 0, 66 };
            brain->onActorAdded(infoOf(kRef, 1, 1000, 0, 66));
        }
    };

    TEST(SkyrimFollowerBrain, aFollowerWalksToAPointNearThePlayerAndIgnoresItsPackage)
    {
        Rig r;
        ASSERT_TRUE(r.brain->setFollowPlayer(kRef, true));
        step(*r.brain, r.w, 5);
        const auto gotos = r.w.of(Event::Goto);
        ASSERT_FALSE(gotos.empty());
        // 140 units from the player on the line to the actor: (140, 0). The package's place (9000, 9000) is never used.
        EXPECT_NEAR(gotos.front().mX, kFollowStandOff, 1e-6);
        EXPECT_NEAR(gotos.front().mY, 0.0, 1e-6);
        EXPECT_NEAR(gotos.front().mZ, 66.0, 1e-6);
        for (const Event& e : gotos)
            EXPECT_LT(e.mX, 5000.0);
        EXPECT_GE(r.brain->stats().mFollowerLegs, 1u);
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Following);
    }

    TEST(SkyrimFollowerBrain, theSpeedIsRunBeyondTheRunDistanceAndWalkInside)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 3);
        ASSERT_FALSE(r.w.of(Event::Goto).empty());
        EXPECT_EQ(r.w.of(Event::Goto).front().mSpeed, ActorSpeed::Run); // 1000 away
        EXPECT_EQ(r.brain->stats().mFollowerRuns, 1u);

        Rig near;
        near.w.mPositions[kRef] = { 500, 0, 66 };
        near.brain->setFollowPlayer(kRef, true);
        step(*near.brain, near.w, 3);
        ASSERT_FALSE(near.w.of(Event::Goto).empty());
        EXPECT_EQ(near.w.of(Event::Goto).front().mSpeed, ActorSpeed::Walk); // 500 away
        EXPECT_EQ(near.brain->stats().mFollowerRuns, 0u);
    }

    TEST(SkyrimFollowerBrain, aFollowerThatIsCloseEnoughStaysWhereItIs)
    {
        Rig r;
        r.w.mPositions[kRef] = { 200, 0, 66 }; // inside kFollowDistance
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 20);
        EXPECT_TRUE(r.w.of(Event::Goto).empty());
        EXPECT_EQ(r.brain->stats().mFollowerLegs, 0u);
    }

    TEST(SkyrimFollowerBrain, withoutAPlayerPositionNothingIsRequestedAndItIsCounted)
    {
        Rig r;
        r.w.mHasPlayer = false;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 10);
        EXPECT_TRUE(r.w.of(Event::Goto).empty());
        EXPECT_GE(r.brain->stats().mFollowerNoPlayer, 1u);
    }

    TEST(SkyrimFollowerBrain, aGoalThatStillLeadsToThePlayerIsNotRepeatedAndAFarMoveReplacesIt)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 30); // 3 s: several repaths
        EXPECT_EQ(r.w.of(Event::Goto).size(), 1u);
        r.w.mPlayer = { -2000, 0, 66 }; // the player went far away
        step(*r.brain, r.w, 15);
        const auto gotos = r.w.of(Event::Goto);
        ASSERT_EQ(gotos.size(), 2u);
        EXPECT_LT(gotos.back().mX, -1000.0);
    }

    TEST(SkyrimFollowerBrain, aFailedLegIsNotAskedForAgainBeforeTheRetryTime)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 3);
        ASSERT_EQ(r.w.of(Event::Goto).size(), 1u);
        r.brain->onGoalFinished(r.h, GoalResult::NoPath);
        step(*r.brain, r.w, 25); // 2.5 s: past the repath time, short of the retry time
        EXPECT_EQ(r.w.of(Event::Goto).size(), 1u);
        step(*r.brain, r.w, 15); // 4 s in all
        EXPECT_EQ(r.w.of(Event::Goto).size(), 2u);
    }

    TEST(SkyrimFollowerBrain, aLegThatArrivedIsFollowedByANewOneAtTheNextRepath)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 3);
        r.brain->onGoalFinished(r.h, GoalResult::Arrived);
        r.w.mPlayer = { -600, 0, 66 };
        step(*r.brain, r.w, 10);
        EXPECT_EQ(r.w.of(Event::Goto).size(), 2u);
    }

    TEST(SkyrimFollowerBrain, theFollowerIsStillOneAfterTheCoreRemovesAndAddsItAgain)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 3);
        r.brain->onActorRemoved(r.h);
        EXPECT_EQ(r.brain->size(), 0u);
        EXPECT_TRUE(r.brain->followsPlayer(kRef)); // the placement id is kept, not the actor
        const ActorHandle second{ kRef, 2 };
        r.brain->onActorAdded(infoOf(kRef, 2, 1000, 0, 66));
        Writer w2;
        w2.mPositions[kRef] = { 1000, 0, 66 };
        step(*r.brain, w2, 3);
        ASSERT_FALSE(w2.of(Event::Goto).empty());
        EXPECT_EQ(w2.of(Event::Goto).front().mHandle, second);
        EXPECT_NEAR(w2.of(Event::Goto).front().mX, kFollowStandOff, 1e-6); // towards the player, not its package
    }

    TEST(SkyrimFollowerBrain, aPlacementRecruitedBeforeItAppearsFollowsWhenItDoes)
    {
        Source src;
        PackageBrain brain(src);
        EXPECT_TRUE(brain.setFollowPlayer(0x77, true));
        Writer w;
        w.mPositions[0x77] = { 800, 0, 0 };
        brain.onActorAdded(infoOf(0x77, 1, 800, 0, 0));
        step(brain, w, 3);
        EXPECT_FALSE(w.of(Event::Goto).empty());
    }

    TEST(SkyrimFollowerBrain, dismissingCancelsTheGoalAndHandsTheActorBackToItsPackages)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 5);
        ASSERT_EQ(r.w.of(Event::Goto).size(), 1u);
        EXPECT_TRUE(r.brain->setFollowPlayer(kRef, false));
        step(*r.brain, r.w, 1);
        EXPECT_EQ(r.w.of(Event::Cancel).size(), 1u);
        EXPECT_FALSE(r.brain->followsPlayer(kRef));
        step(*r.brain, r.w, 30);
        // The Travel package now sends it to its place (9000, 9000).
        const auto gotos = r.w.of(Event::Goto);
        ASSERT_GE(gotos.size(), 2u);
        EXPECT_GT(gotos.back().mX, 5000.0);
        EXPECT_EQ(r.brain->stats().mFollowerDismissals, 1u);
    }

    TEST(SkyrimFollowerBrain, recruitingAndDismissingReportWhetherAnythingChanged)
    {
        Rig r;
        EXPECT_FALSE(r.brain->setFollowPlayer(kRef, false)); // not following
        EXPECT_TRUE(r.brain->setFollowPlayer(kRef, true));
        EXPECT_FALSE(r.brain->setFollowPlayer(kRef, true)); // already
        EXPECT_FALSE(r.brain->setFollowPlayer(kNoWorldId, true));
        EXPECT_EQ(r.brain->stats().mFollowerRecruits, 1u);
    }

    TEST(SkyrimFollowerBrain, withoutFollowersTheBrainDoesWhatItDid)
    {
        Rig r;
        step(*r.brain, r.w, 30);
        const auto gotos = r.w.of(Event::Goto);
        ASSERT_FALSE(gotos.empty());
        EXPECT_GT(gotos.front().mX, 5000.0); // its package
        EXPECT_EQ(r.brain->stats().mFollowerLegs, 0u);
        EXPECT_EQ(r.brain->stats().mFollowerNoPlayer, 0u);
    }

    // ---- the actor table ------------------------------------------------------------------------------------------

    struct TableRig
    {
        Source src;
        SkyrimActorTable table;
        TableRig() { table.enablePackages(src, 1); }
    };

    TEST(SkyrimFollowerTable, theFollowerRulesListAscendingIdsAndAnswerIsFollower)
    {
        TableRig r;
        EXPECT_TRUE(r.table.recruit(0x30));
        EXPECT_TRUE(r.table.recruit(0x20));
        EXPECT_TRUE(r.table.recruit(0x25));
        WorldId out[2] = { 0, 0 };
        const std::size_t total = r.table.followers(out, 2);
        EXPECT_EQ(total, 3u); // the real count, even above the capacity
        EXPECT_EQ(out[0], 0x20u);
        EXPECT_EQ(out[1], 0x25u);
        EXPECT_TRUE(r.table.isFollower(0x30));
        EXPECT_FALSE(r.table.isFollower(0x31));
        EXPECT_EQ(r.table.sideOf(0x30), FollowerSide::Player);
        EXPECT_EQ(r.table.sideOf(0x31), FollowerSide::None);
        EXPECT_TRUE(r.table.dismiss(0x25));
        EXPECT_FALSE(r.table.isFollower(0x25));
    }

    TEST(SkyrimFollowerTable, withoutThePackageBrainEveryAnswerIsNo)
    {
        SkyrimActorTable table;
        EXPECT_FALSE(table.recruit(0x30));
        EXPECT_FALSE(table.dismiss(0x30));
        EXPECT_FALSE(table.isFollower(0x30));
        EXPECT_EQ(table.followers(nullptr, 0), 0u);
        EXPECT_FALSE(table.restoreFollowers({ { 1, 0 } }));
    }

    TEST(SkyrimFollowerTable, thereIsALimit)
    {
        TableRig r;
        for (WorldId id = 1; id <= kMaxFollowers; ++id)
            EXPECT_TRUE(r.table.recruit(id));
        EXPECT_FALSE(r.table.recruit(kMaxFollowers + 1));
    }

    TEST(SkyrimFollowerTable, restoreReplacesTheWholeSet)
    {
        TableRig r;
        r.table.recruit(5);
        r.table.recruit(6);
        EXPECT_TRUE(r.table.restoreFollowers({ { 6, 0 }, { 7, 0 } }));
        EXPECT_EQ(r.table.followerIds(), (std::vector<WorldId>{ 6, 7 }));
    }

    TEST(SkyrimFollowerTable, theTestRuleRecruitsByEditorIdWhenTheActorAppears)
    {
        TableRig r;
        ASSERT_TRUE(r.table.setTestFollowers("Hulda, Mikael"));
        ActorInfo a = infoOf(0x40, 1);
        a.editorId = "BanneredMareHulda";
        ActorInfo b = infoOf(0x41, 1);
        b.editorId = "hulda";
        r.table.onActorAdded(a);
        r.table.onActorAdded(b);
        EXPECT_FALSE(r.table.isFollower(0x40)); // not an exact match
        EXPECT_TRUE(r.table.isFollower(0x41)); // case does not matter
    }

    TEST(SkyrimFollowerTable, theTestDismissEndsTheTestFollowingAfterTheGivenTime)
    {
        TableRig r;
        r.table.setTestFollowers("hulda");
        r.table.setTestDismissAfter(2.f);
        ActorInfo a = infoOf(0x41, 1);
        a.editorId = "Hulda";
        r.table.onActorAdded(a);
        Writer w;
        for (int i = 0; i < 10; ++i)
            r.table.update(0.1f, w);
        EXPECT_TRUE(r.table.isFollower(0x41));
        for (int i = 0; i < 15; ++i)
            r.table.update(0.1f, w);
        EXPECT_FALSE(r.table.isFollower(0x41));
        // Back in a new cell it is not recruited again.
        r.table.onActorRemoved(a.handle);
        ActorInfo again = infoOf(0x41, 2);
        again.editorId = "Hulda";
        r.table.onActorAdded(again);
        EXPECT_FALSE(r.table.isFollower(0x41));
    }

    TEST(SkyrimFollowerTable, aBadTestListIsRefusedAndSwitchesTheRuleOff)
    {
        TableRig r;
        EXPECT_FALSE(r.table.setTestFollowers(std::string(500, 'a')));
        ActorInfo a = infoOf(0x41, 1);
        a.editorId = std::string(500, 'a');
        r.table.onActorAdded(a);
        EXPECT_FALSE(r.table.isFollower(0x41));
    }

    TEST(SkyrimFollowerTable, onFollowerCarriedIsCounted)
    {
        TableRig r;
        r.table.onFollowerCarried(0x41);
        r.table.onFollowerCarried(0x41);
        EXPECT_EQ(r.table.carried(), 2u);
    }

    // ---- the save chunk -------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        std::vector<unsigned char> mBytes;
        std::size_t mPos = 0;
    };

    std::vector<unsigned char> bytesOf(std::initializer_list<std::uint32_t> words)
    {
        std::vector<unsigned char> r;
        for (const std::uint32_t w : words)
        {
            unsigned char b[4];
            std::memcpy(b, &w, 4);
            r.insert(r.end(), b, b + 4);
        }
        return r;
    }

    const auto kAccept = [](const std::vector<FollowerEntry>&) { return true; };
    const auto kNone = [] { return std::vector<FollowerEntry>(); };

    TEST(SkyrimFollowerChunk, writesCountAndAscendingIdsWithTheirHomesAndReadsThemBack)
    {
        std::vector<FollowerEntry> held{ { 9, 90 }, { 3, 30 }, { 3, 31 }, { 0, 5 }, { 7, 0 } };
        std::vector<FollowerEntry> restored;
        FollowerChunk chunk([&] { return held; },
            [&](const std::vector<FollowerEntry>& entries) {
                restored = entries;
                return true;
            });
        MemoryWriter w;
        chunk.write(w);
        // sorted by id, the first of a duplicate id kept, no zero id
        EXPECT_EQ(w.mBytes, bytesOf({ 3, 3, 30, 7, 0, 9, 90 }));
        MemoryReader r(w.mBytes);
        EXPECT_TRUE(chunk.read(r, FollowerChunk::kVersion));
        EXPECT_EQ(restored, (std::vector<FollowerEntry>{ { 3, 30 }, { 7, 0 }, { 9, 90 } }));
    }

    TEST(SkyrimFollowerChunk, writesAnEmptyListAsACountOfZero)
    {
        FollowerChunk chunk(kNone, kAccept);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes, bytesOf({ 0 }));
        EXPECT_EQ(chunk.chunkName(), "followers");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
    }

    TEST(SkyrimFollowerChunk, damagedChunksAreRefusedAndChangeNothing)
    {
        int sets = 0;
        FollowerChunk chunk(kNone, [&](const std::vector<FollowerEntry>&) {
            ++sets;
            return true;
        });
        const std::vector<std::vector<unsigned char>> bad = {
            {}, // no count
            bytesOf({ 2, 5, 50 }), // truncated
            bytesOf({ 2, 5, 50, 5, 50 }), // not strictly ascending
            bytesOf({ 2, 6, 60, 5, 50 }), // descending
            bytesOf({ 1, 0, 50 }), // a zero id
            bytesOf({ 1, 5, 50, 9 }), // trailing bytes
            bytesOf({ static_cast<std::uint32_t>(kMaxFollowers) + 1 }), // too many
            bytesOf({ 0xFFFFFFFFu }), // a huge count with nothing behind it
        };
        for (const auto& b : bad)
        {
            MemoryReader r(b);
            EXPECT_FALSE(chunk.read(r, FollowerChunk::kVersion));
            EXPECT_FALSE(chunk.refusal().empty());
        }
        MemoryReader good(bytesOf({ 0 }));
        EXPECT_FALSE(chunk.read(good, 2)); // another version
        EXPECT_EQ(sets, 0);
    }

    TEST(SkyrimFollowerChunk, aSetterThatRefusesRefusesTheChunk)
    {
        FollowerChunk chunk(kNone, [](const std::vector<FollowerEntry>&) { return false; });
        MemoryReader r(bytesOf({ 1, 5, 50 }));
        EXPECT_FALSE(chunk.read(r, FollowerChunk::kVersion));
    }

    TEST(SkyrimFollowerChunk, aFollowerRecruitedBeforeTheSaveIsRestoredWithItsHomeAfterTheLoad)
    {
        TableRig a;
        a.table.setHomeLookup([](WorldId ref) { return ref == 0x20 ? WorldId(0x1000) : WorldId(0x2000); });
        a.table.recruit(0x20);
        a.table.recruit(0x30);
        FollowerChunk save([&] { return a.table.followerEntries(); }, nullptr);
        MemoryWriter w;
        save.write(w);

        TableRig b; // no home lookup: the saved homes are what it knows
        FollowerChunk load([&] { return b.table.followerEntries(); },
            [&](const std::vector<FollowerEntry>& entries) { return b.table.restoreFollowers(entries); });
        MemoryReader r(w.mBytes);
        ASSERT_TRUE(load.read(r, FollowerChunk::kVersion));
        EXPECT_TRUE(b.table.isFollower(0x20));
        EXPECT_TRUE(b.table.isFollower(0x30));
        EXPECT_EQ(b.table.homeCell(0x20), 0x1000u);
        EXPECT_EQ(b.table.homeCell(0x30), 0x2000u);
        EXPECT_EQ(b.table.homeCell(0x99), kNoWorldId);
    }

    TEST(SkyrimFollowerChunk, theHomeLookupIsOnlyAskedWhenNothingWasRestored)
    {
        TableRig r;
        int asked = 0;
        r.table.setHomeLookup([&](WorldId) {
            ++asked;
            return WorldId(0x1000);
        });
        r.table.restoreFollowers({ { 5, 0x77 } });
        EXPECT_EQ(r.table.homeCell(5), 0x77u);
        EXPECT_EQ(asked, 0);
        EXPECT_EQ(r.table.homeCell(6), 0x1000u);
        EXPECT_EQ(r.table.homeCell(6), 0x1000u); // cached
        EXPECT_EQ(asked, 1);
    }

    // ---- the alias path -------------------------------------------------------------------------------------------

    TEST(SkyrimFollowerAlias, aNewFillDismissesTheOldOneAndRecruitsTheNewOne)
    {
        EXPECT_EQ(followerAliasChange(0, 0).mRecruit, 0u);
        EXPECT_EQ(followerAliasChange(5, 5).mRecruit, 0u);
        EXPECT_EQ(followerAliasChange(5, 5).mDismiss, 0u);
        const auto recruit = followerAliasChange(0, 7);
        EXPECT_EQ(recruit.mRecruit, 7u);
        EXPECT_EQ(recruit.mDismiss, 0u);
        const auto clear = followerAliasChange(7, 0);
        EXPECT_EQ(clear.mRecruit, 0u);
        EXPECT_EQ(clear.mDismiss, 7u);
        const auto swap = followerAliasChange(7, 9);
        EXPECT_EQ(swap.mRecruit, 9u);
        EXPECT_EQ(swap.mDismiss, 7u);
    }

    TEST(SkyrimFollowerAlias, theQuestAndAliasAreTheGamesDialogueFollower)
    {
        EXPECT_EQ(kFollowerQuestId, 0x000750BAu); // VERIFIED against the data: quest DialogueFollower, alias 0 Follower
        EXPECT_EQ(kFollowerAliasId, 0u);
    }

    // ---- the combat hook ------------------------------------------------------------------------------------------

    struct OnlyIsFollower final : FollowerRules
    {
        std::size_t followers(WorldId*, std::size_t) const override { return 0; }
        bool isFollower(WorldId r) const override { return r == 1; }
    };

    TEST(SkyrimFollowerSide, theDefaultSideFollowsIsFollower)
    {
        OnlyIsFollower rules;
        EXPECT_EQ(rules.sideOf(1), FollowerSide::Player);
        EXPECT_EQ(rules.sideOf(2), FollowerSide::None);
    }

    // Task 0340 (C01AmbushScene: Farkas walks to the ambush point while he is the player's follower): a package a scene
    // forces wins over following; when the scene lets go he follows again.
    TEST(SkyrimFollowerBrain, aPackageASceneForcesWinsOverFollowingAndFollowingResumesAfterwards)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 5);
        ASSERT_FALSE(r.w.of(Event::Goto).empty());
        EXPECT_LT(r.w.of(Event::Goto).back().mX, 5000.0); // following: near the player
        const std::size_t before = r.w.of(Event::Goto).size();
        ASSERT_TRUE(r.brain->forcePackage(kRef, &r.pkg));
        step(*r.brain, r.w, 20);
        const auto gotos = r.w.of(Event::Goto);
        ASSERT_GT(gotos.size(), before);
        EXPECT_NEAR(gotos.back().mX, 9000.0, 1.0); // the forced package's place, not the player
        EXPECT_NEAR(gotos.back().mY, 9000.0, 1.0);
        EXPECT_TRUE(r.brain->forcePackage(kRef, nullptr));
        r.w.mPositions[kRef] = { 9000, 9000, 66 };
        step(*r.brain, r.w, 30);
        EXPECT_LT(r.w.of(Event::Goto).back().mX, 5000.0); // following again
    }

    TEST(SkyrimFollowerBrain, aFollowerWithNoForcedPackageStillIgnoresItsOwnPackage)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 30);
        for (const Event& e : r.w.of(Event::Goto))
            EXPECT_LT(e.mX, 5000.0);
    }
}
