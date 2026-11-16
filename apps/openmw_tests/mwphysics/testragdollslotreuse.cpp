// Tests for task 0195: a settled corpse gives its ragdoll slot to a new death and keeps its last pose. Written from
// docs/tasks/0195-ragdoll-slot-reuse.md before the code.
//
// In plain words: RagdollHost has 8 slots. When all are taken and one ragdoll is asleep (settled), a new death takes
// that slot. The settled corpse's handle stays valid and gives back the pose it had; it holds no Jolt body any more.
// A ragdoll that is still moving is never taken.

#include "ragdolltestworld.hpp"

#include "apps/openmw_tests/support/allocationcounter.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;
    using MWPhysics::ActorRagdollHandle;
    using MWPhysics::RagdollBoneState;
    using MWPhysics::RagdollHost;

    constexpr double kNoVelocity[3] = { 0.0, 0.0, 0.0 };
    constexpr std::size_t kSlots = MWPhysics::cMaxActorRagdolls;

    std::unique_ptr<RagdollHost> makeHost(GameLikeWorld& world)
    {
        std::string error;
        auto host = RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        return host;
    }

    void stepMany(GameLikeWorld& world, RagdollHost& host, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            host.beginStep();
            world.update();
            host.endStep();
        }
    }

    // `x` is 300 times the ragdoll's number: the ragdolls stand on a grid 200 units apart inside the floor (4000 wide).
    ActorRagdollHandle spawnAt(RagdollHost& host, double x, std::string* error = nullptr)
    {
        const long n = std::lround(x / 300.0);
        const double gx = -700.0 + 200.0 * static_cast<double>(n % 8);
        const double gy = -600.0 + 200.0 * static_cast<double>(n / 8);
        std::string local;
        return host.spawnActor(JPH::RVec3(gx, gy, 20.0), 0.0f, kNoVelocity, error != nullptr ? error : &local);
    }

    std::vector<RagdollBoneState> read(const RagdollHost& host, ActorRagdollHandle handle)
    {
        std::vector<RagdollBoneState> states(host.actorBodyBones().size());
        EXPECT_TRUE(host.readActor(handle, states));
        return states;
    }

    bool sameStates(const std::vector<RagdollBoneState>& a, const std::vector<RagdollBoneState>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            for (int k = 0; k < 3; ++k)
                if (a[i].mPosition[k] != b[i].mPosition[k])
                    return false;
            for (int k = 0; k < 4; ++k)
                if (a[i].mRotation[k] != b[i].mRotation[k])
                    return false;
        }
        return true;
    }

    // Eight ragdolls spawned 25 steps apart, then stepped until every one is asleep.
    std::vector<ActorRagdollHandle> fillAndSettle(GameLikeWorld& world, RagdollHost& host)
    {
        std::vector<ActorRagdollHandle> handles;
        for (std::size_t i = 0; i < kSlots; ++i)
        {
            handles.push_back(spawnAt(host, 300.0 * static_cast<double>(i)));
            stepMany(world, host, 25);
        }
        stepMany(world, host, 900);
        return handles;
    }

    // The handle the host should take: smallest settle step, ties to the lowest handle index.
    ActorRagdollHandle expectedVictim(const RagdollHost& host, const std::vector<ActorRagdollHandle>& handles)
    {
        ActorRagdollHandle best;
        std::uint64_t bestStep = MWPhysics::cNotSettled;
        for (const ActorRagdollHandle handle : handles)
        {
            const std::uint64_t step = host.actorSettledStep(handle);
            if (step < bestStep)
            {
                bestStep = step;
                best = handle;
            }
        }
        return best;
    }

    TEST(RagdollSlotReuse, aSettledRagdollHasASettleStepAndAFreshOneHasNone)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle first = spawnAt(*host, 0.0);
        EXPECT_EQ(host->actorSettledStep(first), MWPhysics::cNotSettled);
        stepMany(world, *host, 900);
        EXPECT_NE(host->actorSettledStep(first), MWPhysics::cNotSettled);
        EXPECT_TRUE(host->actorStatus(first).allAsleep);
        const ActorRagdollHandle second = spawnAt(*host, 300.0);
        EXPECT_EQ(host->actorSettledStep(second), MWPhysics::cNotSettled);
        EXPECT_EQ(host->actorSettledStep(ActorRagdollHandle{}), MWPhysics::cNotSettled);
    }

    TEST(RagdollSlotReuse, theNinthDeathTakesTheOldestSettledSlotAndGetsARagdoll)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<ActorRagdollHandle> handles = fillAndSettle(world, *host);
        for (const ActorRagdollHandle handle : handles)
            ASSERT_NE(host->actorSettledStep(handle), MWPhysics::cNotSettled);
        const ActorRagdollHandle victim = expectedVictim(*host, handles);
        ASSERT_TRUE(victim.isValid());

        std::string error;
        const ActorRagdollHandle ninth = spawnAt(*host, 0.0, &error);
        ASSERT_TRUE(ninth.isValid()) << error;
        EXPECT_TRUE(host->hasActor(ninth));
        EXPECT_TRUE(host->isActorFrozen(victim));
        EXPECT_TRUE(host->hasActor(victim));
        EXPECT_FALSE(host->isActorFrozen(ninth));
        for (const ActorRagdollHandle handle : handles)
        {
            if (handle != victim)
            {
                EXPECT_FALSE(host->isActorFrozen(handle));
            }
        }
        EXPECT_EQ(host->liveActorRagdollCount(), kSlots);
        EXPECT_EQ(host->actorRagdollCount(), kSlots + 1);
    }

    TEST(RagdollSlotReuse, theFrozenPoseIsBitIdenticalToTheLastLiveReadNowAndLater)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<ActorRagdollHandle> handles = fillAndSettle(world, *host);
        const ActorRagdollHandle victim = expectedVictim(*host, handles);
        const std::vector<RagdollBoneState> before = read(*host, victim);
        const MWPhysics::RagdollStatus liveStatus = host->actorStatus(victim);

        ASSERT_TRUE(spawnAt(*host, 2400.0).isValid());
        ASSERT_TRUE(host->isActorFrozen(victim));
        EXPECT_TRUE(sameStates(before, read(*host, victim)));
        const MWPhysics::RagdollStatus frozen = host->actorStatus(victim);
        EXPECT_TRUE(frozen.present);
        EXPECT_TRUE(frozen.allAsleep);
        EXPECT_EQ(frozen.bodies, liveStatus.bodies);
        EXPECT_EQ(frozen.lowestZ, liveStatus.lowestZ);
        EXPECT_EQ(frozen.highestZ, liveStatus.highestZ);
        EXPECT_EQ(frozen.maxSpeedMetresPerSecond, 0.0f);

        stepMany(world, *host, 600);
        EXPECT_TRUE(sameStates(before, read(*host, victim)));
        EXPECT_TRUE(host->hasActor(victim));
    }

    TEST(RagdollSlotReuse, aFrozenEntryHoldsNoJoltBodyAndRemovalFreesIt)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<ActorRagdollHandle> handles = fillAndSettle(world, *host);
        const std::size_t bodiesFull = world.allBodies().size();
        const ActorRagdollHandle victim = expectedVictim(*host, handles);
        const ActorRagdollHandle ninth = spawnAt(*host, 2400.0);
        ASSERT_TRUE(ninth.isValid());
        EXPECT_EQ(world.allBodies().size(), bodiesFull); // 8 live ragdolls before, 8 after

        EXPECT_TRUE(host->removeActor(victim));
        EXPECT_FALSE(host->hasActor(victim));
        EXPECT_FALSE(host->removeActor(victim));
        std::vector<RagdollBoneState> out(host->actorBodyBones().size());
        EXPECT_FALSE(host->readActor(victim, out));
        EXPECT_EQ(world.allBodies().size(), bodiesFull);
        EXPECT_EQ(host->actorRagdollCount(), kSlots);
        EXPECT_TRUE(host->hasActor(ninth));
    }

    TEST(RagdollSlotReuse, aRagdollThatIsStillMovingIsNeverTaken)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        std::vector<ActorRagdollHandle> handles;
        for (std::size_t i = 0; i < kSlots; ++i)
            handles.push_back(spawnAt(*host, 300.0 * static_cast<double>(i)));
        stepMany(world, *host, 5); // all in the air
        const std::size_t bodies = world.allBodies().size();
        std::string error;
        const ActorRagdollHandle ninth = spawnAt(*host, 0.0, &error);
        EXPECT_FALSE(ninth.isValid());
        EXPECT_NE(error.find("slot"), std::string::npos) << error;
        EXPECT_EQ(world.allBodies().size(), bodies);
        for (const ActorRagdollHandle handle : handles)
            EXPECT_FALSE(host->isActorFrozen(handle));
        EXPECT_EQ(host->actorRagdollCount(), kSlots);
    }

    TEST(RagdollSlotReuse, sevenMovingAndOneSettledTakesTheSettledOne)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle settled = spawnAt(*host, 0.0);
        stepMany(world, *host, 900);
        ASSERT_NE(host->actorSettledStep(settled), MWPhysics::cNotSettled);
        std::vector<ActorRagdollHandle> others;
        for (std::size_t i = 1; i < kSlots; ++i)
            others.push_back(spawnAt(*host, 300.0 * static_cast<double>(i)));
        stepMany(world, *host, 5);
        const ActorRagdollHandle ninth = spawnAt(*host, 2400.0);
        ASSERT_TRUE(ninth.isValid());
        EXPECT_TRUE(host->isActorFrozen(settled));
        for (const ActorRagdollHandle handle : others)
            EXPECT_FALSE(host->isActorFrozen(handle));
    }

    TEST(RagdollSlotReuse, aSettledCorpseThatIsWokenIsNotSettledAnyMore)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle first = spawnAt(*host, 0.0);
        stepMany(world, *host, 900);
        ASSERT_NE(host->actorSettledStep(first), MWPhysics::cNotSettled);
        // A new ragdoll falls onto it: the first may wake. Either way its settle step says what it is now.
        const ActorRagdollHandle second = spawnAt(*host, 0.0);
        ASSERT_TRUE(second.isValid());
        stepMany(world, *host, 10);
        const bool asleep = host->actorStatus(first).allAsleep;
        EXPECT_EQ(host->actorSettledStep(first) != MWPhysics::cNotSettled, asleep);
    }

    TEST(RagdollSlotReuse, twentyDeathsOneAfterAnotherEveryOneGetsARagdollAndOldCorpsesStayFrozen)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        std::vector<ActorRagdollHandle> handles;
        for (int i = 0; i < 20; ++i)
        {
            std::string error;
            // Each death comes after the earlier ragdolls had time to settle.
            const ActorRagdollHandle handle = spawnAt(*host, 300.0 * static_cast<double>(i), &error);
            ASSERT_TRUE(handle.isValid()) << i << ": " << error;
            handles.push_back(handle);
            stepMany(world, *host, 900);
        }
        EXPECT_EQ(host->liveActorRagdollCount(), kSlots);
        EXPECT_EQ(host->actorRagdollCount(), 20u);
        std::size_t frozen = 0;
        for (const ActorRagdollHandle handle : handles)
        {
            EXPECT_TRUE(host->hasActor(handle));
            frozen += host->isActorFrozen(handle) ? 1 : 0;
        }
        // A slot only frees when none is free: with 20 deaths and 8 slots, 12 corpses are frozen.
        EXPECT_EQ(frozen, 12u);
    }

    TEST(RagdollSlotReuse, twoRunsChooseTheSameVictimsAndGiveTheSamePoses)
    {
        const auto run = [] {
            GameLikeWorld world;
            auto host = makeHost(world);
            std::vector<ActorRagdollHandle> handles = fillAndSettle(world, *host);
            for (int i = 0; i < 3; ++i)
            {
                handles.push_back(spawnAt(*host, 300.0 * static_cast<double>(kSlots + i)));
                stepMany(world, *host, 900);
            }
            std::vector<RagdollBoneState> all;
            std::vector<int> frozen;
            for (const ActorRagdollHandle handle : handles)
            {
                frozen.push_back(host->isActorFrozen(handle) ? 1 : 0);
                const std::vector<RagdollBoneState> states = read(*host, handle);
                all.insert(all.end(), states.begin(), states.end());
            }
            return std::make_pair(frozen, all);
        };
        const auto a = run();
        const auto b = run();
        EXPECT_EQ(a.first, b.first);
        EXPECT_TRUE(sameStates(a.second, b.second));
    }

    TEST(RagdollSlotReuse, theKillDepthFreesTheEntryAndAStaleHandleNeverWorksAgain)
    {
        GameLikeWorld world(false); // no floor: it falls
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawnAt(*host, 0.0);
        ASSERT_TRUE(handle.isValid());
        stepMany(world, *host, 1800);
        EXPECT_FALSE(host->hasActor(handle));
        EXPECT_EQ(host->actorRagdollCount(), 0u);
    }

    TEST(RagdollSlotReuse, perStepCallsAllocateNothingWithEightRagdollsAndSettleChecks)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        host->setProgressLogging(false);
        fillAndSettle(world, *host);
        spawnAt(*host, 2400.0); // one freeze behind us
        stepMany(world, *host, 30);
        std::size_t beginAllocations = 0;
        std::size_t endAllocations = 0;
        for (int i = 0; i < 100; ++i)
        {
            {
                TestSupport::AllocationCounter counter;
                host->beginStep();
                beginAllocations += counter.count();
            }
            world.update();
            {
                TestSupport::AllocationCounter counter;
                host->endStep();
                endAllocations += counter.count();
            }
        }
        EXPECT_EQ(beginAllocations, 0u);
        EXPECT_EQ(endAllocations, 0u);
    }

    TEST(RagdollSlotReuse, aRemovedHandlesIndexIsReusedLowestFirstWithANewGeneration)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle a = spawnAt(*host, 0.0);
        const ActorRagdollHandle b = spawnAt(*host, 300.0);
        EXPECT_NE(a.mIndex, b.mIndex);
        EXPECT_TRUE(host->removeActor(a));
        const ActorRagdollHandle c = spawnAt(*host, 600.0);
        EXPECT_EQ(c.mIndex, a.mIndex);
        EXPECT_NE(c.mGeneration, a.mGeneration);
        EXPECT_TRUE(host->hasActor(b));
        EXPECT_FALSE(host->hasActor(a));
    }
}
