#include "opas/behaviors/Balance.hpp"

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opas::behaviors
{

    std::optional<Balance> Balance::create(const rig::Rig& rig, const rig::Profile& profile, std::string* error)
    {
        const auto failWith = [error](std::string message) -> std::optional<Balance> {
            if (error != nullptr)
            {
                *error = std::move(message);
            }
            return std::nullopt;
        };
        if (profile.feet.empty())
        {
            return failWith("the profile names no feet (profile version 2, \"feet\")");
        }
        if (!profile.balance)
        {
            return failWith("the profile has no balance tuning (profile version 2, \"balance\")");
        }

        Balance balance;
        balance.tuning_ = *profile.balance;
        for (const rig::Joint& joint : rig.joints)
        {
            balance.profileMotors_.push_back(rig::motorFor(profile, joint.bone));
        }
        for (const std::string& foot : profile.feet)
        {
            const std::optional<std::size_t> footBody = rig::findBody(rig, foot);
            if (!footBody)
            {
                return failWith("foot \"" + foot + "\" has no body in the rig");
            }
            // Walk from the foot's body towards the root body; each body on the
            // way has the joint on its bone. The last joint found is the hip.
            std::optional<std::size_t> hipJoint;
            std::size_t body = *footBody;
            while (const std::optional<std::size_t> parent = rig::parentBody(rig, body))
            {
                const std::optional<std::size_t> joint = rig::findJoint(rig, rig.bodies[body].bone);
                if (!joint)
                {
                    return failWith("body on \"" + rig.bodies[body].bone + "\" has no joint");
                }
                balance.legJoints_.push_back(*joint);
                hipJoint = joint;
                body = *parent;
            }
            if (!hipJoint)
            {
                return failWith("foot \"" + foot + "\" is the root body, so it has no leg");
            }
            const std::optional<std::size_t> ankle = rig::findBone(rig.skeleton, foot);
            const std::optional<std::size_t> hip = rig::findBone(rig.skeleton, rig.joints[*hipJoint].bone);
            if (!ankle || !hip)
            {
                return failWith("foot \"" + foot + "\" is not a bone of the rig's skeleton");
            }
            balance.ankleBones_.push_back(*ankle);
            // A one-joint leg has its ankle and hip on the same bone; it leans
            // once, as an ankle.
            if (*hip != *ankle)
            {
                balance.hipBones_.push_back(*hip);
            }
        }
        // Rig order, each joint once (two feet can share a chain near the root).
        std::sort(balance.legJoints_.begin(), balance.legJoints_.end());
        balance.legJoints_.erase(
            std::unique(balance.legJoints_.begin(), balance.legJoints_.end()), balance.legJoints_.end());
        // Each bone leans at most once: legs that meet below the root body share
        // their hip bone, and a bone that is one foot's ankle is not also a hip.
        const auto sortUnique = [](std::vector<std::size_t>& bones) {
            std::sort(bones.begin(), bones.end());
            bones.erase(std::unique(bones.begin(), bones.end()), bones.end());
        };
        sortUnique(balance.ankleBones_);
        sortUnique(balance.hipBones_);
        std::erase_if(balance.hipBones_, [&balance](std::size_t bone) {
            return std::binary_search(balance.ankleBones_.begin(), balance.ankleBones_.end(), bone);
        });
        return balance;
    }

    void Balance::apply(const sensors::BalanceReading& reading, std::span<math::Transform> localPose) const
    {
        if (!reading.valid || !reading.hasSupport || reading.supportArea.empty())
        {
            return;
        }
        // Offset e from the centre of the support area (the mean of its
        // vertices) to the capture point, on the ground plane.
        double centreX = 0.0;
        double centreY = 0.0;
        for (const sensors::Point2& p : reading.supportArea)
        {
            centreX += p.x;
            centreY += p.y;
        }
        const auto count = static_cast<double>(reading.supportArea.size());
        const double ex = reading.capturePointM.x - centreX / count;
        const double ey = reading.capturePointM.y - centreY / count;
        const double offsetM = std::hypot(ex, ey);
        if (!(offsetM > 0.0) || !std::isfinite(offsetM))
        {
            return; // no offset (or an unusable one): keep the pose
        }
        // u = normalize(e x z) tips the body's top towards -e.
        const math::Vec3 u{ ey / offsetM, -ex / offsetM, 0.0 };
        const auto lean = [&](const std::vector<std::size_t>& bones, double gainRadPerM, double sign) {
            const double theta = std::min(gainRadPerM * offsetM, tuning_.maxLeanRad);
            const math::Quat turn = math::fromAxisAngle(u, sign * theta);
            for (const std::size_t bone : bones)
            {
                if (bone < localPose.size())
                {
                    localPose[bone].rotation = math::normalized(turn * localPose[bone].rotation);
                }
            }
        };
        // Ankle: a planted foot tilts the shin by +theta when the foot turns by
        // -theta relative to it, so the legs tip their tops towards -e.
        lean(ankleBones_, tuning_.ankleGainRadPerM, -1.0);
        // Hip: the thigh turns by +theta relative to the pelvis, so the trunk
        // bends back towards +e over the feet. The opposite sign tips it further
        // towards -e and falls (amendment of 2026-10-20 in docs/tasks/0017).
        lean(hipBones_, tuning_.hipGainRadPerM, +1.0);
    }

} // namespace opas::behaviors
