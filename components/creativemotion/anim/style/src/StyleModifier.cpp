#include "opas/style/StyleModifier.hpp"

#include "opas/style/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace opas::style {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

int findAnyBone(const rig::Skeleton& skeleton, std::initializer_list<std::string_view> candidates)
{
    for (const auto& name : candidates) {
        if (const auto idx = rig::findBone(skeleton, name)) {
            return static_cast<int>(*idx);
        }
    }
    return -1;
}

} // namespace

StyleModifier::StyleModifier(const rig::Skeleton& skeleton)
{
    bones_.pelvis = findAnyBone(skeleton, {"Pelvis", "Hips", "root"});
    bones_.spine = findAnyBone(skeleton, {"Spine", "spine"});
    bones_.chest = findAnyBone(skeleton, {"Chest", "Spine1", "Spine2", "chest"});
    bones_.neck = findAnyBone(skeleton, {"Neck", "neck"});
    bones_.head = findAnyBone(skeleton, {"Head", "head"});

    bones_.leftShoulder = findAnyBone(skeleton, {"LeftClavicle", "LeftUpperArm", "LeftArm"});
    bones_.rightShoulder = findAnyBone(skeleton, {"RightClavicle", "RightUpperArm", "RightArm"});

    bones_.leftUpperArm = findAnyBone(skeleton, {"LeftUpperArm", "LeftArm"});
    bones_.leftForearm = findAnyBone(skeleton, {"LeftForearm"});
    bones_.rightUpperArm = findAnyBone(skeleton, {"RightUpperArm", "RightArm"});
    bones_.rightForearm = findAnyBone(skeleton, {"RightForearm"});

    bones_.leftThigh = findAnyBone(skeleton, {"LeftThigh", "LeftUpLeg"});
    bones_.leftShin = findAnyBone(skeleton, {"LeftShin", "LeftLeg"});
    bones_.rightThigh = findAnyBone(skeleton, {"RightThigh", "RightUpLeg"});
    bones_.rightShin = findAnyBone(skeleton, {"RightShin", "RightLeg"});
}

void StyleModifier::apply(std::span<math::Transform> localPose, const StyleProfile& profile,
                          double timeS) const
{
    const bool noPosture = (profile.posture == PostureParams{});
    const bool noIdle = (profile.idle.breathingAmplitudeM <= 0.0 &&
                         profile.idle.swayAmplitudeM <= 0.0 &&
                         profile.idle.fidgetIntervalS <= 0.0);
    const bool noLayers = profile.layers.empty() ||
                          std::all_of(profile.layers.begin(), profile.layers.end(),
                                      [](const LayerConfig& l) { return l.weight <= 0.0; });

    if (noPosture && noIdle && noLayers) {
        return;
    }

    // 1. Procedural Idle Life
    if (!noIdle) {
        const IdlePoseOffset idle = IdleGenerator(profile.idle).evaluate(timeS);
        if (bones_.pelvis >= 0 && bones_.pelvis < static_cast<int>(localPose.size())) {
            auto& t = localPose[bones_.pelvis];
            t.translation = t.translation + idle.pelvisTranslationM;
            t.rotation = math::normalized(idle.pelvisTilt * t.rotation);
        }
        const int thoracic = (bones_.chest >= 0) ? bones_.chest : bones_.spine;
        if (thoracic >= 0 && thoracic < static_cast<int>(localPose.size())) {
            auto& t = localPose[thoracic];
            t.translation = t.translation + idle.chestTranslationM;
            t.rotation = math::normalized(idle.spinePitch * t.rotation);
        }
        if (bones_.head >= 0 && bones_.head < static_cast<int>(localPose.size())) {
            auto& t = localPose[bones_.head];
            t.rotation = math::normalized(idle.headGlanceOffset * t.rotation);
        }
        if (bones_.leftShoulder >= 0 && bones_.leftShoulder < static_cast<int>(localPose.size())) {
            localPose[bones_.leftShoulder].translation =
                localPose[bones_.leftShoulder].translation + idle.shoulderShiftM;
        }
    }

    // 2. Postural Rotations and Elevation
    if (!noPosture) {
        const auto& p = profile.posture;
        if (p.slouchRad != 0.0) {
            const double halfSlouch = 0.5 * p.slouchRad;
            const math::Quat qSlouch = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, halfSlouch);
            if (bones_.spine >= 0 && bones_.spine < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.spine];
                t.rotation = math::normalized(qSlouch * t.rotation);
            }
            if (bones_.chest >= 0 && bones_.chest < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.chest];
                t.rotation = math::normalized(qSlouch * t.rotation);
            }
        }
        if (p.spineLeanRad != 0.0) {
            const math::Quat qLean = math::fromAxisAngle(math::Vec3{0.0, 1.0, 0.0}, p.spineLeanRad);
            if (bones_.spine >= 0 && bones_.spine < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.spine];
                t.rotation = math::normalized(qLean * t.rotation);
            }
        }
        if (p.headPitchRad != 0.0 || p.headYawRad != 0.0) {
            const math::Quat qPitch = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, p.headPitchRad);
            const math::Quat qYaw = math::fromAxisAngle(math::Vec3{0.0, 0.0, 1.0}, p.headYawRad);
            const math::Quat qHead = math::normalized(qYaw * qPitch);
            if (bones_.head >= 0 && bones_.head < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.head];
                t.rotation = math::normalized(qHead * t.rotation);
            }
        }
        if (p.shoulderElevationRad != 0.0) {
            const double elev = p.shoulderElevationRad;
            const math::Quat qElevL = math::fromAxisAngle(math::Vec3{0.0, 1.0, 0.0}, -elev);
            const math::Quat qElevR = math::fromAxisAngle(math::Vec3{0.0, 1.0, 0.0}, elev);
            const double dz = 0.05 * elev;
            if (bones_.leftShoulder >= 0 && bones_.leftShoulder < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.leftShoulder];
                t.translation.z += dz;
                t.rotation = math::normalized(qElevL * t.rotation);
            }
            if (bones_.rightShoulder >= 0 && bones_.rightShoulder < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.rightShoulder];
                t.translation.z += dz;
                t.rotation = math::normalized(qElevR * t.rotation);
            }
        }
    }

    // 3. Procedural Additive Layers
    for (const auto& layer : profile.layers) {
        if (layer.weight <= 0.0) {
            continue;
        }
        const double w = std::min(1.0, layer.weight);
        if (layer.name == "limp") {
            const bool isRightSide = (layer.bone.find("Right") != std::string::npos);
            const double cadence = std::max(0.1, profile.gait.cadenceScale);
            const double theta = kTwoPi * cadence * timeS;
            const double s = std::sin(theta);
            if (s > 0.0) {
                const double dip = -0.05 * w * s * s;
                const double roll = (isRightSide ? -0.06 : 0.06) * w * s;
                if (bones_.pelvis >= 0 && bones_.pelvis < static_cast<int>(localPose.size())) {
                    auto& t = localPose[bones_.pelvis];
                    t.translation.z += dip;
                    const math::Quat qRoll = math::fromAxisAngle(math::Vec3{0.0, 1.0, 0.0}, roll);
                    t.rotation = math::normalized(qRoll * t.rotation);
                }
                const int leg = isRightSide ? bones_.rightThigh : bones_.leftThigh;
                if (leg >= 0 && leg < static_cast<int>(localPose.size())) {
                    const math::Quat qFlex = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, -0.15 * w * s);
                    localPose[leg].rotation = math::normalized(qFlex * localPose[leg].rotation);
                }
            }
        } else if (layer.name == "hunch") {
            const math::Quat qSpineFlex = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, 0.25 * w);
            const math::Quat qNeckFlex = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, 0.15 * w);
            const math::Quat qHeadExt = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, -0.15 * w);
            if (bones_.spine >= 0 && bones_.spine < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.spine];
                t.rotation = math::normalized(qSpineFlex * t.rotation);
            }
            if (bones_.chest >= 0 && bones_.chest < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.chest];
                t.rotation = math::normalized(qSpineFlex * t.rotation);
            }
            if (bones_.neck >= 0 && bones_.neck < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.neck];
                t.rotation = math::normalized(qNeckFlex * t.rotation);
            }
            if (bones_.head >= 0 && bones_.head < static_cast<int>(localPose.size())) {
                auto& t = localPose[bones_.head];
                t.rotation = math::normalized(qHeadExt * t.rotation);
            }
        } else if (layer.name == "protect_arm") {
            const bool isRight = (layer.bone.find("Right") != std::string::npos);
            const int upperArm = isRight ? bones_.rightUpperArm : bones_.leftUpperArm;
            const int forearm = isRight ? bones_.rightForearm : bones_.leftForearm;
            if (upperArm >= 0 && upperArm < static_cast<int>(localPose.size())) {
                const double rollSign = isRight ? 1.0 : -1.0;
                const math::Quat qAdduct =
                    math::fromAxisAngle(math::Vec3{0.0, 1.0, 0.0}, rollSign * 0.35 * w);
                const math::Quat qFlex = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, 0.30 * w);
                localPose[upperArm].rotation =
                    math::normalized(qAdduct * qFlex * localPose[upperArm].rotation);
            }
            if (forearm >= 0 && forearm < static_cast<int>(localPose.size())) {
                const math::Quat qElbow = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, 1.25 * w);
                localPose[forearm].rotation = math::normalized(qElbow * localPose[forearm].rotation);
            }
        } else if (layer.name == "shiver") {
            constexpr double kFreq = 10.0;
            const double jitterChest = 0.015 * w * noise1D(timeS, kFreq, 12345ULL, 10U);
            const double jitterArmL = 0.02 * w * noise1D(timeS, kFreq, 12345ULL, 20U);
            const double jitterArmR = 0.02 * w * noise1D(timeS, kFreq, 12345ULL, 30U);
            if (bones_.chest >= 0 && bones_.chest < static_cast<int>(localPose.size())) {
                const math::Quat qJit = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, jitterChest);
                localPose[bones_.chest].rotation =
                    math::normalized(qJit * localPose[bones_.chest].rotation);
            }
            if (bones_.leftForearm >= 0 && bones_.leftForearm < static_cast<int>(localPose.size())) {
                const math::Quat qJit = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, jitterArmL);
                localPose[bones_.leftForearm].rotation =
                    math::normalized(qJit * localPose[bones_.leftForearm].rotation);
            }
            if (bones_.rightForearm >= 0 && bones_.rightForearm < static_cast<int>(localPose.size())) {
                const math::Quat qJit = math::fromAxisAngle(math::Vec3{1.0, 0.0, 0.0}, jitterArmR);
                localPose[bones_.rightForearm].rotation =
                    math::normalized(qJit * localPose[bones_.rightForearm].rotation);
            }
        }
    }
}

} // namespace opas::style
