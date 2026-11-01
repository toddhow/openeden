#pragma once

// The internal clip format (task 0020, stage C3a; decision 0005): what every
// clip reader produces and every layer above reads. Per-bone tracks of key
// times and values, events, and the bone whose motion becomes root motion.
//
// Ownership: plain values. A clip owns its tracks; ClipBinding copies the clip
// it binds, so the caller's clip may be destroyed afterwards.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/Skeleton.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opas::anim
{

    // How values between two keys are found. Clips carry no scale: math::Transform
    // has translation and rotation only (task 0020, C3a interface).
    enum class Interpolation
    {
        Step,
        Linear
    };

    struct Vec3Track
    {
        std::vector<double> timesS;
        std::vector<math::Vec3> values;
        Interpolation interpolation = Interpolation::Linear;
        friend bool operator==(const Vec3Track&, const Vec3Track&) = default;
    };

    struct QuatTrack
    {
        std::vector<double> timesS;
        std::vector<math::Quat> values;
        Interpolation interpolation = Interpolation::Linear;
        friend bool operator==(const QuatTrack&, const QuatTrack&) = default;
    };

    // One clip bone's animation, in that bone's local (parent-relative) frame.
    struct BoneTrack
    {
        std::string bone;
        std::optional<Vec3Track> translation;
        std::optional<QuatTrack> rotation;
        friend bool operator==(const BoneTrack&, const BoneTrack&) = default;
    };

    struct ClipEvent
    {
        std::string name;
        double timeS = 0.0;
        std::string payload;
        friend bool operator==(const ClipEvent&, const ClipEvent&) = default;
    };

    struct Clip
    {
        std::string name;
        double durationS = 0.0; // > 0
        std::vector<BoneTrack> tracks; // one per clip bone, names unique
        std::vector<ClipEvent> events; // times in [0, durationS], sorted by time (stable)
        // Clip bone whose horizontal motion and yaw become root motion; "" = none.
        std::string rootBone;
        // The clip's own skeleton and rest pose; may be empty (then no leg scaling).
        rig::Skeleton sourceRest;
        friend bool operator==(const Clip&, const Clip&) = default;
    };

    // "" if valid, else "<pointer>: <message>" with a pointer such as
    // "/tracks/2/rotation/timesS/3". Rules: durationS positive and finite; key
    // times strictly increasing, finite and within [0, durationS]; as many values
    // as times, at least one key per present track, values finite; rotations unit
    // length within 1e-6; track bones non-empty and unique; events within
    // [0, durationS] and sorted by time; rootBone names a track if set;
    // sourceRest valid if not empty.
    std::string validateClip(const Clip& clip);

} // namespace opas::anim
