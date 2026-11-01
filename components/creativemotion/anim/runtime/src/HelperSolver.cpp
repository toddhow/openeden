#include "opas/anim/HelperSolver.hpp"

#include "opas/anim/Interpolate.hpp"
#include "opas/rig/Skeleton.hpp"

#include <algorithm>
#include <cmath>

namespace opas::anim
{

    namespace
    {
        constexpr double kTwoPi = 6.283185307179586476925286766559;
    } // namespace

    HelperSolver::HelperSolver(const rig::Rig& rig)
        : boneCount_(rig.skeleton.bones.size())
    {
        for (const auto& ruleVariant : rig.helpers)
        {
            std::visit(
                [this, &rig](const auto& rule) {
                    using T = std::decay_t<decltype(rule)>;
                    if constexpr (std::is_same_v<T, rig::TwistRule>)
                    {
                        const auto driver = rig::findBone(rig.skeleton, rule.driver);
                        const auto driven = rig::findBone(rig.skeleton, rule.bone);
                        if (driver && driven)
                        {
                            twistRules_.push_back(ResolvedTwist{
                                .driver = *driver,
                                .driven = *driven,
                                .fraction = rule.fraction,
                                .twistAxis = math::normalized(rule.twistAxis),
                                .driverRest = rig.skeleton.bones[*driver].localRest.rotation,
                                .drivenRest = rig.skeleton.bones[*driven].localRest.rotation,
                            });
                        }
                    }
                    else if constexpr (std::is_same_v<T, rig::SwingRule>)
                    {
                        const auto driver = rig::findBone(rig.skeleton, rule.driver);
                        const auto driven = rig::findBone(rig.skeleton, rule.bone);
                        if (driver && driven)
                        {
                            swingRules_.push_back(ResolvedSwing{
                                .driver = *driver,
                                .driven = *driven,
                                .fraction = rule.fraction,
                                .driverAxis = math::normalized(rule.driverAxis),
                                .drivenAxis = math::normalized(rule.drivenAxis),
                                .maxAngleRad = rule.maxAngleRad,
                                .driverRest = rig.skeleton.bones[*driver].localRest.rotation,
                                .drivenRest = rig.skeleton.bones[*driven].localRest.rotation,
                            });
                        }
                    }
                    else if constexpr (std::is_same_v<T, rig::ChainRule>)
                    {
                        const auto start = rig::findBone(rig.skeleton, rule.startBone);
                        const auto end = rig::findBone(rig.skeleton, rule.endBone);
                        if (start && end)
                        {
                            ResolvedChain chain{
                                .startBone = *start,
                                .endBone = *end,
                                .intermediates = {},
                            };
                            for (const auto& inter : rule.intermediates)
                            {
                                const auto interBone = rig::findBone(rig.skeleton, inter.bone);
                                if (interBone)
                                {
                                    chain.intermediates.push_back(ResolvedIntermediate{
                                        .bone = *interBone,
                                        .weight = inter.weight,
                                    });
                                }
                            }
                            if (!chain.intermediates.empty())
                            {
                                chainRules_.push_back(std::move(chain));
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<T, rig::BreathingRule>)
                    {
                        const auto bone = rig::findBone(rig.skeleton, rule.bone);
                        if (bone)
                        {
                            breathingRules_.push_back(ResolvedBreathing{
                                .bone = *bone,
                                .rateHz = rule.rateHz,
                                .expansionM = rule.expansionM,
                                .pitchRad = rule.pitchRad,
                                .rest = rig.skeleton.bones[*bone].localRest,
                            });
                        }
                    }
                },
                ruleVariant);
        }

        hasHelpers_ = !twistRules_.empty() || !swingRules_.empty() || !chainRules_.empty() || !breathingRules_.empty();
    }

    bool HelperSolver::hasHelpers() const noexcept
    {
        return hasHelpers_;
    }

    void HelperSolver::apply(std::span<math::Transform> localPose, double timeS, double exertion) const
    {
        if (!hasHelpers_ || localPose.size() < boneCount_)
        {
            return;
        }

        // 1. Twist helper rules
        for (const auto& rule : twistRules_)
        {
            const math::Quat driverRot = localPose[rule.driver].rotation;
            const math::Quat relRot = math::normalized(driverRot * math::conjugate(rule.driverRest));
            const SwingTwist st = decomposeSwingTwist(relRot, rule.twistAxis);
            const double drivenAngle = st.twistAngleRad * rule.fraction;
            const math::Quat drivenRot = math::fromAxisAngle(rule.twistAxis, drivenAngle);
            localPose[rule.driven].rotation = math::normalized(drivenRot * rule.drivenRest);
        }

        // 2. Swing helper rules
        for (const auto& rule : swingRules_)
        {
            const math::Quat driverRot = localPose[rule.driver].rotation;
            const math::Quat relRot = math::normalized(driverRot * math::conjugate(rule.driverRest));
            const math::Vec3 vCurrent = math::rotate(relRot, rule.driverAxis);
            const double dotVal = math::dot(rule.driverAxis, vCurrent);
            const double deflection = std::acos(std::clamp(dotVal, -1.0, 1.0));
            double drivenAngle = deflection * rule.fraction;
            if (drivenAngle > rule.maxAngleRad)
            {
                drivenAngle = rule.maxAngleRad;
            }
            else if (drivenAngle < -rule.maxAngleRad)
            {
                drivenAngle = -rule.maxAngleRad;
            }
            const math::Quat drivenRot = math::fromAxisAngle(rule.drivenAxis, drivenAngle);
            localPose[rule.driven].rotation = math::normalized(drivenRot * rule.drivenRest);
        }

        // 3. Chain helper rules
        for (const auto& rule : chainRules_)
        {
            const math::Transform& startT = localPose[rule.startBone];
            const math::Transform& endT = localPose[rule.endBone];
            for (const auto& inter : rule.intermediates)
            {
                localPose[inter.bone].translation
                    = startT.translation + (endT.translation - startT.translation) * inter.weight;
                localPose[inter.bone].rotation = anim::slerp(startT.rotation, endT.rotation, inter.weight);
            }
        }

        // 4. Breathing helper rules
        if (!breathingRules_.empty())
        {
            const double safeTimeS = std::isfinite(timeS) ? timeS : 0.0;
            const double safeExertion = std::isfinite(exertion) ? std::max(0.0, exertion) : 0.0;
            for (const auto& rule : breathingRules_)
            {
                const double phase = kTwoPi * rule.rateHz * safeTimeS;
                const double wave = std::sin(phase) * (1.0 + safeExertion);
                localPose[rule.bone].translation = {
                    rule.rest.translation.x,
                    rule.rest.translation.y + rule.expansionM * wave,
                    rule.rest.translation.z,
                };
                const math::Quat pitchRot = math::fromAxisAngle(math::Vec3{ 1.0, 0.0, 0.0 }, rule.pitchRad * wave);
                localPose[rule.bone].rotation = math::normalized(pitchRot * rule.rest.rotation);
            }
        }
    }

} // namespace opas::anim
