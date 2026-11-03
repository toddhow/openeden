#ifndef OPENEDEN_OPENMW_TESTS_SUPPORT_ALLOCATIONCOUNTER_H
#define OPENEDEN_OPENMW_TESTS_SUPPORT_ALLOCATIONCOUNTER_H

#include <cstddef>

// The allocation counter for the "this call does not allocate" tests of openmw-tests.
//
// In plain words: a program may replace the global operator new only once, and openmw-tests does that in exactly one
// place, apps/openmw_tests/mwrender/testbonepose.cpp (the replacement counts, then calls malloc). That replacement
// reads the two variables below. Any test file can include this header and wrap the calls it wants to check in an
// AllocationCounter: it counts the allocations made by the calling thread while it lives, and no other thread's.
// Do not define another operator new in openmw-tests.
namespace TestSupport
{
    // Per thread, so that a Jolt worker or the test runner itself never adds to a measurement.
    inline thread_local bool sCountAllocations = false;
    inline thread_local std::size_t sAllocationCount = 0;

    struct AllocationCounter
    {
        AllocationCounter()
        {
            sAllocationCount = 0;
            sCountAllocations = true;
        }
        ~AllocationCounter() { sCountAllocations = false; }
        AllocationCounter(const AllocationCounter&) = delete;
        AllocationCounter& operator=(const AllocationCounter&) = delete;
        std::size_t count() const { return sAllocationCount; }
    };
}

#endif
