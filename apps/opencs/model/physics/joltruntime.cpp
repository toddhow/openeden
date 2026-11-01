#include "joltruntime.hpp"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

namespace CSMPhysics
{
    namespace
    {
        // Touched on the main thread only (see the class comment).
        int sUsers = 0;
    }

    JoltRuntime::JoltRuntime()
    {
        if (sUsers++ == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }

    JoltRuntime::~JoltRuntime()
    {
        if (--sUsers == 0)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }
}
