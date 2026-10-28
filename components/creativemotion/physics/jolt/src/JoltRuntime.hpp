#pragma once

// Jolt's process-wide setup (allocator hooks, the type factory and type
// registration) must exist exactly once while any Jolt object is alive.
// JoltRuntimeRef is a reference-counted guard: the first live reference sets
// Jolt up, the last one tears it down. Thread-safe.

namespace opas::physics::jolt::detail {

class JoltRuntimeRef {
public:
    JoltRuntimeRef();
    ~JoltRuntimeRef();
    JoltRuntimeRef(const JoltRuntimeRef&) = delete;
    JoltRuntimeRef& operator=(const JoltRuntimeRef&) = delete;
    JoltRuntimeRef(JoltRuntimeRef&&) = delete;
    JoltRuntimeRef& operator=(JoltRuntimeRef&&) = delete;
};

} // namespace opas::physics::jolt::detail
