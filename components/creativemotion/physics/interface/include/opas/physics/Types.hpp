#pragma once

// Plain value types shared by every physics backend and its callers.
// Standard library only: this header must stay free of third-party types.
//
// Conventions: SI units (meters, kilograms, seconds, radians), right-handed
// coordinates with Z up. Floats match the single-precision Jolt build.

#include <cstdint>
#include <limits>
#include <string>

namespace opas::physics {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

// Unit quaternion. Default-constructed is the identity rotation.
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    friend bool operator==(const Quat&, const Quat&) = default;
};

// Opaque handles. The backend owns the objects; callers keep only these and
// look objects up on each use. `generation` makes a handle to a destroyed
// object stale forever, even if its slot is reused.
// Default-constructed handles are invalid.
template <typename Tag>
struct Handle {
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const { return index != kInvalidIndex; }

    friend bool operator==(const Handle&, const Handle&) = default;
};

using BodyId = Handle<struct BodyTag>;
using JointId = Handle<struct JointTag>;
using StepListenerId = Handle<struct StepListenerTag>;
using CollisionGroupId = Handle<struct CollisionGroupTag>;

enum class ErrorCode : std::uint8_t {
    None,
    InvalidDescription, // a field of a BodyDesc / JointDesc / WorldConfig is out of range
    InvalidHandle,      // a handle is invalid or refers to a destroyed object
    CapacityExceeded, // WorldConfig::maxBodies or maxJoints reached (collision groups count against
                      // maxBodies)
    CalledDuringStep, // structural change attempted from a step callback
};

// Result of a create call. On failure `id` is invalid and `message` names the
// offending field, e.g. "massKg must be > 0 for a dynamic body".
template <typename Id>
struct CreateResult {
    Id id;
    ErrorCode error = ErrorCode::None;
    std::string message;

    [[nodiscard]] bool ok() const { return error == ErrorCode::None; }
};

} // namespace opas::physics
