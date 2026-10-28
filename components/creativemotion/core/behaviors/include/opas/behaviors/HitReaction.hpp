#pragma once

// Hit reaction (M5d, docs/tasks/0017, "Stage M5d interface"): a hit on a body
// lowers the stiffness of the joints near it, and the stiffness ramps back as
// 1 - drop * exp(-t / recoverTimeS).
//
// Ownership: a plain value, owned by BalanceDriver. It holds the body-graph
// distances (computed once) and each joint's current drop; nothing allocates
// after create().

#include "opas/rig/Profile.hpp"
#include "opas/rig/Rig.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace opas::behaviors {

class HitReaction {
public:
    // The rig and profile the ragdoll was built from (validated). Fails, with
    // a message, without the profile's `hitReaction` object.
    static std::optional<HitReaction> create(const rig::Rig& rig, const rig::Profile& profile,
                                             std::string* error = nullptr);

    // A hit on a rig body at time timeS. Every joint whose distance d from it
    // (the smaller of its two bodies' graph distances) is at most reachJoints
    // gets the drop dropFraction * spread^d, or keeps its current remaining
    // drop if that is larger; either way its ramp restarts at timeS.
    void hit(std::size_t rigBody, double timeS);

    // 1 - drop * exp(-(timeS - tHit) / recoverTimeS) for a joint that has
    // been hit; 1 for one that has not.
    [[nodiscard]] double stiffnessMultiplier(std::size_t rigJoint, double timeS) const;

    // Graph distance between two rig bodies (one edge per joint).
    [[nodiscard]] int bodyDistance(std::size_t a, std::size_t b) const;

private:
    HitReaction() = default;

    [[nodiscard]] double remainingDrop(std::size_t rigJoint, double timeS) const;

    rig::HitReactionTuning tuning_;
    std::size_t bodyCount_ = 0;
    std::vector<int> distance_;           // bodyCount_ x bodyCount_
    std::vector<std::size_t> jointChild_; // rig body of each joint's bone
    std::vector<std::size_t> jointParent_;
    std::vector<double> drop_; // per joint; 0 never hit
    std::vector<double> hitS_; // per joint: when its ramp restarted
};

} // namespace opas::behaviors
