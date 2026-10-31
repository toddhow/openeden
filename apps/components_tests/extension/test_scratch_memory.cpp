#include <components/extension/host_impl.hpp>
#include <components/extension/scratch_memory.hpp>
#include <openeden/host.hpp>
#include <openeden/memory.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using openeden::extension::HostImpl;
    using openeden::extension::ScratchMemoryServiceImpl;
    using openeden::sdk::ScratchMemoryService;

    struct alignas(16) SimdVector4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        SimdVector4(float x_, float y_, float z_, float w_)
            : x(x_)
            , y(y_)
            , z(z_)
            , w(w_)
        {
        }
    };

    struct alignas(64) CacheLineData
    {
        std::uint8_t bytes[64] = {};
    };

    TEST(ScratchMemoryTest, LinearBumpAllocationFromPreallocatedBuffer)
    {
        constexpr std::size_t kCapacity = 1024 * 1024; // 1MB
        ScratchMemoryServiceImpl scratch(kCapacity);

        EXPECT_EQ(scratch.getCapacity(), kCapacity);
        EXPECT_EQ(scratch.getUsedBytes(), 0u);
        EXPECT_EQ(scratch.getRemainingBytes(), kCapacity);

        void* ptr1 = scratch.allocate(128, 8);
        ASSERT_NE(ptr1, nullptr);
        EXPECT_GE(scratch.getUsedBytes(), 128u);

        void* ptr2 = scratch.allocate(256, 8);
        ASSERT_NE(ptr2, nullptr);
        EXPECT_GE(scratch.getUsedBytes(), 128u + 256u);

        // Linear bump allocator advances forward
        auto addr1 = reinterpret_cast<std::uintptr_t>(ptr1);
        auto addr2 = reinterpret_cast<std::uintptr_t>(ptr2);
        EXPECT_GE(addr2, addr1 + 128);

        // Memory must be writable without faults
        std::memset(ptr1, 0xAA, 128);
        std::memset(ptr2, 0xBB, 256);

        EXPECT_EQ(static_cast<std::uint8_t*>(ptr1)[0], 0xAA);
        EXPECT_EQ(static_cast<std::uint8_t*>(ptr2)[0], 0xBB);
    }

    TEST(ScratchMemoryTest, AlignmentGuarantees)
    {
        ScratchMemoryServiceImpl scratch(512 * 1024);

        constexpr std::size_t alignments[] = { 4, 8, 16, 32, 64, 128 };

        for (const std::size_t alignment : alignments)
        {
            // Allocate prime / odd sizes to create non-aligned boundary offsets
            void* p1 = scratch.allocate(13, alignment);
            ASSERT_NE(p1, nullptr);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p1) % alignment, 0u);

            void* p2 = scratch.allocate(37, alignment);
            ASSERT_NE(p2, nullptr);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p2) % alignment, 0u);

            void* p3 = scratch.allocate(99, alignment);
            ASSERT_NE(p3, nullptr);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p3) % alignment, 0u);
        }

        // Non-power-of-two alignments must be rejected cleanly
        EXPECT_EQ(scratch.allocate(32, 3), nullptr);
        EXPECT_EQ(scratch.allocate(32, 5), nullptr);
        EXPECT_EQ(scratch.allocate(32, 7), nullptr);
        EXPECT_EQ(scratch.allocate(32, 24), nullptr);
    }

    TEST(ScratchMemoryTest, IntegerOverflowRejection)
    {
        ScratchMemoryServiceImpl scratch(1024 * 1024);

        // Allocations near SIZE_MAX would wrap around naive addition (mOffset + padding + size)
        EXPECT_EQ(scratch.allocate(std::numeric_limits<std::size_t>::max(), 8), nullptr);
        EXPECT_EQ(scratch.allocate(std::numeric_limits<std::size_t>::max() - 16, 8), nullptr);
        EXPECT_EQ(scratch.allocate(std::numeric_limits<std::size_t>::max() / 2, 8), nullptr);
    }

    TEST(ScratchMemoryTest, ResetSemanticsAtFrameBoundaries)
    {
        constexpr std::size_t kCapacity = 256 * 1024;
        ScratchMemoryServiceImpl scratch(kCapacity);

        void* initialPtr = scratch.allocate(64, 16);
        ASSERT_NE(initialPtr, nullptr);

        // Fill up some memory
        for (int i = 0; i < 100; ++i)
        {
            void* p = scratch.allocate(512, 16);
            ASSERT_NE(p, nullptr);
        }

        EXPECT_GT(scratch.getUsedBytes(), 50000u);
        EXPECT_LT(scratch.getRemainingBytes(), kCapacity);

        // Reset simulates frame boundary
        scratch.reset();

        EXPECT_EQ(scratch.getUsedBytes(), 0u);
        EXPECT_EQ(scratch.getRemainingBytes(), kCapacity);

        // First allocation after reset should return the same base address
        void* afterResetPtr = scratch.allocate(64, 16);
        EXPECT_EQ(afterResetPtr, initialPtr);

        // Verify across 50 simulated frames that reset reuses memory cleanly
        for (int frame = 0; frame < 50; ++frame)
        {
            void* frameAlloc = scratch.allocate(1024, 64);
            EXPECT_NE(frameAlloc, nullptr);
            scratch.reset();
        }
        EXPECT_EQ(scratch.getUsedBytes(), 0u);
    }

    TEST(ScratchMemoryTest, CapacityExhaustionReturnsNullptrCleanly)
    {
        constexpr std::size_t kSmallCapacity = 1024;
        ScratchMemoryServiceImpl scratch(kSmallCapacity);

        // Allocate 700 bytes
        void* p1 = scratch.allocate(700, 8);
        ASSERT_NE(p1, nullptr);

        const std::size_t usedBefore = scratch.getUsedBytes();

        // Attempt to allocate 600 bytes (700 + 600 > 1024) -> must return nullptr
        void* pOversized = scratch.allocate(600, 8);
        EXPECT_EQ(pOversized, nullptr);

        // State must remain consistent after failed allocation
        EXPECT_EQ(scratch.getUsedBytes(), usedBefore);

        // A smaller allocation that fits in the remaining space must still succeed
        void* pSmall = scratch.allocate(100, 8);
        EXPECT_NE(pSmall, nullptr);

        // Attempting an allocation larger than total capacity returns nullptr
        void* pGiant = scratch.allocate(kSmallCapacity * 2, 8);
        EXPECT_EQ(pGiant, nullptr);
    }

    TEST(ScratchMemoryTest, ZeroByteAllocationHandledSafely)
    {
        ScratchMemoryServiceImpl scratch(4096);

        EXPECT_NO_THROW({
            void* pZero = scratch.allocate(0, 8);
            (void)pZero;
        });
    }

    TEST(ScratchMemoryTest, TypedCreateHelperObjectConstruction)
    {
        ScratchMemoryServiceImpl scratch(64 * 1024);

        SimdVector4* vec = scratch.create<SimdVector4>(1.0f, 2.0f, 3.0f, 4.0f);
        ASSERT_NE(vec, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(vec) % alignof(SimdVector4), 0u);
        EXPECT_FLOAT_EQ(vec->x, 1.0f);
        EXPECT_FLOAT_EQ(vec->y, 2.0f);
        EXPECT_FLOAT_EQ(vec->z, 3.0f);
        EXPECT_FLOAT_EQ(vec->w, 4.0f);

        CacheLineData* cacheData = scratch.create<CacheLineData>();
        ASSERT_NE(cacheData, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(cacheData) % alignof(CacheLineData), 0u);
    }

    TEST(ScratchMemoryTest, HostImplWiresScratchMemoryService)
    {
        HostImpl host("TestHost");
        ScratchMemoryService* scratch = host.getScratchMemoryService();

        ASSERT_NE(scratch, nullptr);

        void* mem = scratch->allocate(256, 16);
        EXPECT_NE(mem, nullptr);
        EXPECT_GT(scratch->getUsedBytes(), 0u);

        scratch->reset();
        EXPECT_EQ(scratch->getUsedBytes(), 0u);
    }
}
