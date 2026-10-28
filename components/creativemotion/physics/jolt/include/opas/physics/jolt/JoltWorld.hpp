#pragma once

// Factory for the Jolt implementation of opas::physics::PhysicsWorld.
// Free of Jolt types: callers never see Jolt headers.

#include "opas/physics/PhysicsWorld.hpp"

#include <memory>
#include <string>

namespace opas::physics::jolt {

// Creates a world backed by Jolt. Returns nullptr if `config` is invalid, and
// then writes a message naming the bad field to *errorOut (when given).
// Several worlds may exist at once; Jolt's process-wide setup is shared and
// reference-counted.
std::unique_ptr<PhysicsWorld> createJoltWorld(const WorldConfig& config,
                                              std::string* errorOut = nullptr);

} // namespace opas::physics::jolt
