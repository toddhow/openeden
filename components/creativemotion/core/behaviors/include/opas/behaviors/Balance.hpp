#pragma once

// Balance by leaning (M5b, docs/tasks/0017, "Stage M5b interface"): turns
// the balance sensors' reading into ankle and hip lean on the target pose,
// so the motors push the capture point back towards the centre of the feet.
//
// Ownership: a plain value, owned by whoever runs it (BalanceDriver). It
// holds indices into the rig and the tuning from the profile, nothing else.

#include "opas/math/Transform.hpp"
#include "opas/rig/Profile.hpp"
#include "opas/rig/Rig.hpp"
#include "opas/sensors/BalanceSensor.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::behaviors
{

    class Balance
    {
    public:
        // The rig and the profile the ragdoll was built from (validated). Fails,
        // with a message in *error, if the profile has no `feet` or no `balance`,
        // or a foot is the root body (it would have no leg to lean).
        static std::optional<Balance> create(
            const rig::Rig& rig, const rig::Profile& profile, std::string* error = nullptr);

        // Indices into rig.joints, in rig order: every joint on the chain from
        // each foot's body up to, but not including, the root body.
        [[nodiscard]] std::span<const std::size_t> legJoints() const { return legJoints_; }

        // The gains every leg joint gets while balance runs.
        [[nodiscard]] rig::MotorGains legMotor() const { return tuning_.legMotor; }

        // The profile's own motor for a rig joint (rig::motorFor), which the
        // ragdoll was built with: the gains a joint returns to when balance lets
        // go (M5d).
        [[nodiscard]] rig::MotorGains profileMotor(std::size_t rigJoint) const { return profileMotors_[rigJoint]; }

        // Leans the ankles (the foot bones) and hips (each leg's bone nearest the
        // root) of `localPose` (one local transform per skeleton bone) against
        // the capture point's offset from the centre of the support area. Leaves
        // the pose unchanged without a valid reading with support, or with no
        // offset. Allocates nothing.
        void apply(const sensors::BalanceReading& reading, std::span<math::Transform> localPose) const;

    private:
        Balance() = default;

        rig::BalanceTuning tuning_;
        std::vector<rig::MotorGains> profileMotors_; // per rig joint
        std::vector<std::size_t> legJoints_; // indices into rig.joints
        std::vector<std::size_t> ankleBones_; // skeleton bone indices
        std::vector<std::size_t> hipBones_; // skeleton bone indices
    };

} // namespace opas::behaviors
