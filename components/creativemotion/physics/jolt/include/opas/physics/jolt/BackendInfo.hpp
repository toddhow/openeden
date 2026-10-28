#pragma once

// Bootstrap (M0) checks that the Jolt backend is built and linked correctly.
// Deliberately free of Jolt types: callers never see Jolt headers.
// M1 replaces these with the real backend behind physics/interface.

#include <cstddef>

namespace opas::physics::jolt {

struct JoltVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// The Jolt version this library was compiled against.
JoltVersion linkedJoltVersion();

// Counts the types Jolt registers, setting Jolt up temporarily if no world is
// alive. A non-zero result proves Jolt's compiled code (not just its headers)
// is linked in.
std::size_t countRegisteredJoltTypes();

} // namespace opas::physics::jolt
