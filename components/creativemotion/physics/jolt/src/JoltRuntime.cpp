#include "JoltRuntime.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <memory>
#include <mutex>

namespace opas::physics::jolt::detail
{
    namespace
    {

        // Process-wide state, guarded by runtimeMutex(). Function-local statics avoid
        // static initialization order problems.
        std::mutex& runtimeMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        struct RuntimeState
        {
            int references = 0;
            // Jolt reads the factory through the global raw pointer
            // JPH::Factory::sInstance; we own it here.
            std::unique_ptr<JPH::Factory> factory;
        };

        RuntimeState& runtimeState()
        {
            static RuntimeState state;
            return state;
        }

    } // namespace

    JoltRuntimeRef::JoltRuntimeRef()
    {
        const std::scoped_lock lock(runtimeMutex());
        RuntimeState& state = runtimeState();
        if (state.references++ == 0 && JPH::Factory::sInstance == nullptr)
        {
            JPH::RegisterDefaultAllocator();
            state.factory = std::make_unique<JPH::Factory>();
            JPH::Factory::sInstance = state.factory.get();
            JPH::RegisterTypes();
        }
    }

    JoltRuntimeRef::~JoltRuntimeRef()
    {
        const std::scoped_lock lock(runtimeMutex());
        RuntimeState& state = runtimeState();
        if (--state.references == 0 && state.factory != nullptr)
        {
            JPH::UnregisterTypes();
            JPH::Factory::sInstance = nullptr;
            state.factory.reset();
        }
    }

} // namespace opas::physics::jolt::detail
