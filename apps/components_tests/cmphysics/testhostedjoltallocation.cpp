// Allocation tests for CMPhysics::HostedJoltWorld, stage a of task 0035: the per-step calls must not allocate.
//
// How it is counted: this file replaces the global operator new for the whole components-tests program with a version
// that does the same work as the default (malloc) and, only while a test has switched counting on for the current
// thread, adds one to a counter. That is safe for the other tests: outside a counting window the replacement costs one
// thread-local flag test, and the matching operator delete uses free. Only the plain (not over-aligned) forms are
// replaced; over-aligned allocations keep the default behaviour and are not counted.
//
// What is and is not counted: Jolt allocates through its own allocator (JPH::RegisterDefaultAllocator, which does not
// use operator new), so Jolt's internal allocations are never counted. What is counted is everything the adapter's
// own code takes from operator new: std::vector growth, std::string, std::function, std::make_unique and so on. That
// is exactly what the spec asks for ("no heap allocation in the adapter's own code"), and it means the CastRay and
// body interface calls need no exclusion.

#include "hostedjoltfixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <vector>

namespace
{
    // Per thread, so that a worker thread (Jolt's job system, if one existed) cannot disturb the count.
    thread_local bool tCountingAllocations = false;
    thread_local std::size_t tAllocationCount = 0;

    void* countedAllocate(std::size_t size)
    {
        if (tCountingAllocations)
            ++tAllocationCount;
        // malloc(0) may return null; operator new must return a unique pointer even for size 0.
        void* memory = std::malloc(size != 0 ? size : 1);
        if (memory == nullptr)
            throw std::bad_alloc();
        return memory;
    }
}

void* operator new(std::size_t size)
{
    return countedAllocate(size);
}

void* operator new[](std::size_t size)
{
    return countedAllocate(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
    using namespace CMPhysicsTest;
    using namespace opas::physics;

    // Counts the allocations made on this thread between construction and destruction (or stop()).
    class AllocationWindow
    {
    public:
        AllocationWindow()
        {
            tAllocationCount = 0;
            tCountingAllocations = true;
        }

        ~AllocationWindow() { tCountingAllocations = false; }

        AllocationWindow(const AllocationWindow&) = delete;
        AllocationWindow& operator=(const AllocationWindow&) = delete;

        std::size_t stop()
        {
            tCountingAllocations = false;
            return tAllocationCount;
        }
    };

    // Runs `call` the given number of times inside one counting window and returns how many allocations it made.
    template <typename Call>
    std::size_t countAllocations(int repeats, Call&& call)
    {
        AllocationWindow window;
        for (int i = 0; i < repeats; ++i)
            call();
        return window.stop();
    }

    // The spec's number: 100 calls of each.
    constexpr int kCalls = 100;

    // A listener that does nothing, so the begin/end loops over listeners run without the test adding allocations.
    class QuietListener final : public StepListener
    {
    public:
        void onPreStep(PhysicsWorld&, float) override {}
        void onPostStep(PhysicsWorld&, float) override {}
    };

    class HostedJoltWorldAllocationTest : public ::testing::Test
    {
    protected:
        HostedJoltWorldAllocationTest()
            : mStepper(mHost)
        {
        }

        void SetUp() override
        {
            mAdapter = makeAdapter(mHost, makeConfig(kUnitsPerMeter, &mStepper));
            ASSERT_NE(mAdapter, nullptr);

            // Created before any measuring: createBody and createJoint are allowed to allocate.
            BodyDesc floor = boxDesc(Vec3{ 0.0f, 0.0f, -0.5f }, Vec3{ 10.0f, 10.0f, 0.5f });
            floor.motion = MotionType::Static;
            ASSERT_TRUE(mAdapter->createBody(floor).ok());

            const CreateResult<CollisionGroupId> group = mAdapter->createCollisionGroup(3);
            ASSERT_TRUE(group.ok());
            for (std::uint32_t i = 0; i < 3; ++i)
            {
                BodyDesc link = sphereDesc(Vec3{ 0.0f, 0.0f, 3.0f - 0.6f * static_cast<float>(i) }, 0.2f);
                link.collisionGroup = group.id;
                link.subGroup = i;
                const CreateResult<BodyId> created = mAdapter->createBody(link);
                ASSERT_TRUE(created.ok());
                mLinks[i] = created.id;
            }
            for (std::size_t i = 0; i < 2; ++i)
            {
                JointDesc joint;
                joint.bodyA = mLinks[i];
                joint.bodyB = mLinks[i + 1];
                joint.anchorM = Vec3{ 0.0f, 0.0f, 2.7f - 0.6f * static_cast<float>(i) };
                joint.twistAxis = Vec3{ 0.0f, 0.0f, -1.0f };
                joint.planeAxis = Vec3{ 1.0f, 0.0f, 0.0f };
                joint.planeSwingMinRad = -0.5f;
                joint.planeSwingMaxRad = 0.5f;
                joint.normalSwingMinRad = -0.5f;
                joint.normalSwingMaxRad = 0.5f;
                const CreateResult<JointId> created = mAdapter->createJoint(joint);
                ASSERT_TRUE(created.ok());
                mJoints[i] = created.id;
            }

            BodyDesc kinematic = boxDesc(Vec3{ 5.0f, 0.0f, 1.0f }, Vec3{ 0.5f, 0.5f, 0.5f });
            kinematic.motion = MotionType::Kinematic;
            const CreateResult<BodyId> platform = mAdapter->createBody(kinematic);
            ASSERT_TRUE(platform.ok());
            mKinematic = platform.id;

            ASSERT_TRUE(mAdapter->addStepListener(mListener).isValid());
        }

        HostWorld mHost;
        UpdateStepper mStepper;
        QuietListener mListener;
        std::unique_ptr<CMPhysics::HostedJoltWorld> mAdapter;
        std::array<BodyId, 3> mLinks;
        std::array<JointId, 2> mJoints;
        BodyId mKinematic;
    };

    TEST(HostedJoltWorldAllocationCounter, theCounterSeesAllocationsInsideAWindowAndNoneOutside)
    {
        // Proves the replacement operator new is the one in use, so a zero elsewhere means something.
        AllocationWindow window;
        // Handing the pointer to a volatile keeps the compiler from dropping the unused allocation.
        static std::vector<int>* volatile sink;
        std::vector<int>* numbers = new std::vector<int>(32);
        sink = numbers;
        const std::size_t inside = window.stop();
        delete numbers;
        EXPECT_NE(sink, nullptr); // reads the volatile, so it counts as used
        EXPECT_GE(inside, 2u); // the vector object and its elements

        std::vector<int>* outside = new std::vector<int>(32);
        sink = outside;
        delete outside;
        // Nothing was added once the window was closed.
        EXPECT_EQ(tAllocationCount, inside);
    }

    TEST_F(HostedJoltWorldAllocationTest, beginStepAndEndStepAllocateNothing)
    {
        std::size_t total = 0;
        for (int i = 0; i < kCalls; ++i)
        {
            {
                AllocationWindow window;
                mAdapter->beginStep();
                total += window.stop();
            }
            mHost.update(kStepSeconds); // the host's Update is outside the measured region
            {
                AllocationWindow window;
                mAdapter->endStep();
                total += window.stop();
            }
        }
        EXPECT_EQ(total, 0u);
    }

    TEST_F(HostedJoltWorldAllocationTest, readBodyAndReadBodiesAllocateNothing)
    {
        BodyState state;
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->readBody(mLinks[1], state); }), 0u);

        const std::array<BodyId, 4> ids = { mLinks[0], mLinks[1], mLinks[2], mKinematic };
        std::array<BodyState, 4> out;
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->readBodies(ids, out); }), 0u);
    }

    TEST_F(HostedJoltWorldAllocationTest, driveCallsAllocateNothing)
    {
        const Vec3 impulse{ 0.1f, 0.0f, 0.0f };
        const Vec3 point{ 0.0f, 0.0f, 1.8f };
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->applyImpulse(mLinks[2], impulse, point); }), 0u);

        const Vec3 linear{ 0.0f, 0.1f, 0.0f };
        const Vec3 angular{ 0.0f, 0.0f, 0.1f };
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->setVelocity(mLinks[2], linear, angular); }), 0u);

        const Vec3 target{ 5.0f, 1.0f, 1.0f };
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->setKinematicTarget(mKinematic, target, Quat{}); }), 0u);
    }

    TEST_F(HostedJoltWorldAllocationTest, jointDriveCallsAllocateNothing)
    {
        const Quat target{ 0.0f, 0.0f, 0.0f, 1.0f };
        const Vec3 targetSpin{ 0.0f, 0.2f, 0.0f };
        const JointMotor motor{ 5.0f, 0.5f, 100.0f };
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->setJointTarget(mJoints[0], target); }), 0u);
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->setJointTarget(mJoints[1], target, targetSpin); }), 0u);
        EXPECT_EQ(countAllocations(kCalls, [&] { mAdapter->setJointMotor(mJoints[0], motor); }), 0u);
    }

    TEST_F(HostedJoltWorldAllocationTest, castRayAndFixedStepSecondsAllocateNothing)
    {
        // One ray that hits the kinematic box (5 m along +X from a point beside it) and one that hits nothing.
        const Vec3 origin{ 0.0f, 0.0f, 1.0f };
        const Vec3 towardsBox{ 10.0f, 0.0f, 0.0f };
        const Vec3 awayFromBox{ -10.0f, 0.0f, 0.0f };
        std::optional<RayHit> hit;
        EXPECT_EQ(countAllocations(kCalls, [&] { hit = mAdapter->castRay(origin, towardsBox); }), 0u);
        ASSERT_TRUE(hit.has_value()); // the measured calls really did hit something
        EXPECT_EQ(hit->body, mKinematic);

        EXPECT_EQ(countAllocations(kCalls, [&] { hit = mAdapter->castRay(origin, awayFromBox); }), 0u);
        EXPECT_FALSE(hit.has_value());

        float seconds = 0.0f;
        EXPECT_EQ(countAllocations(kCalls, [&] { seconds = mAdapter->fixedStepSeconds(); }), 0u);
        EXPECT_GT(seconds, 0.0f);
    }
}
