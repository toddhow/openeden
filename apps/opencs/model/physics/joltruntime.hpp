#ifndef CSM_PHYSICS_JOLTRUNTIME_H
#define CSM_PHYSICS_JOLTRUNTIME_H

namespace CSMPhysics
{
    /// Jolt's one-time start-up (allocator, Factory, registered types), which the game does in
    /// MWPhysics::PhysicsSystem's constructor and the editor had no one to do (task 0030).
    ///
    /// Reference counted: the first JoltRuntime alive starts Jolt and the last one to go shuts it down, so any number
    /// may exist at once and each StudioWorld keeps one. Main thread only.
    class JoltRuntime
    {
    public:
        JoltRuntime();
        ~JoltRuntime();

        JoltRuntime(const JoltRuntime&) = delete;
        JoltRuntime& operator=(const JoltRuntime&) = delete;
    };
}

#endif
