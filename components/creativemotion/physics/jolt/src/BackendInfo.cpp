#include "opas/physics/jolt/BackendInfo.hpp"

#include "JoltRuntime.hpp"

// Jolt requires Jolt.h before any other Jolt header (see HelloWorld.cpp in the
// Jolt repository).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>

namespace opas::physics::jolt {

JoltVersion linkedJoltVersion()
{
    return JoltVersion{JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH};
}

std::size_t countRegisteredJoltTypes()
{
    // Shares Jolt's process-wide setup with any live worlds instead of
    // replacing it underneath them.
    const detail::JoltRuntimeRef runtime;
    return JPH::Factory::sInstance->GetAllClasses().size();
}

} // namespace opas::physics::jolt
