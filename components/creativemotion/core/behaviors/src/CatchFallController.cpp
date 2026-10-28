#include "opas/behaviors/CatchFallController.hpp"

#include <algorithm>
#include <cmath>

namespace opas::behaviors {

CatchFallController::CatchFallController(CatchFallTuning tuning)
    : tuning_(tuning)
{}

bool CatchFallController::isActive() const
{
    return phase_ == CatchFallPhase::Bracing || phase_ == CatchFallPhase::Impacted;
}

double CatchFallController::headTuckPitchRad() const
{
    return (phase_ == CatchFallPhase::Bracing) ? tuning_.headTuckPitchRad : 0.0;
}

double CatchFallController::partStrictness(arbiter::BodyPart part) const
{
    if (phase_ == CatchFallPhase::Impacted &&
        (part == arbiter::BodyPart::LeftArm || part == arbiter::BodyPart::RightArm)) {
        return tuning_.impactComplianceStrictness;
    }
    return 1.0;
}

math::Vec3 CatchFallController::computeHandTarget(arbiter::BodyPart armPart,
                                                 const math::Vec3& shoulderPos,
                                                 const math::Vec3& groundNormal) const
{
    const math::Vec3 normal =
        math::length(groundNormal) > 1e-6 ? math::normalized(groundNormal) : math::Vec3{0.0, 0.0, 1.0};

    const double lateral = (armPart == arbiter::BodyPart::LeftArm) ? -0.15 : 0.15;
    math::Vec3 offset{lateral, 0.25, -0.7 * std::max(0.1, normal.z)};
    offset.x -= normal.x * 0.2;
    offset.y -= normal.y * 0.2;

    const double len = math::length(offset);
    const math::Vec3 dir = len > 1e-6 ? (offset / len) : math::Vec3{0.0, 0.0, -1.0};
    return shoulderPos + dir * tuning_.armReachDistanceM;
}

void CatchFallController::update(double /*dtSeconds*/, arbiter::ArbiterState state,
                                double groundClearanceM, const math::Vec3& comVelocityMps,
                                bool groundImpactDetected)
{
    if (state != arbiter::ArbiterState::Falling) {
        reset();
        return;
    }

    if (comVelocityMps.z < -0.1) {
        timeToLandingS_ = groundClearanceM / (-comVelocityMps.z);
    } else {
        timeToLandingS_ = (groundClearanceM > 0.0 && tuning_.gravityMps2 > 0.0)
                              ? std::sqrt(2.0 * groundClearanceM / tuning_.gravityMps2)
                              : 0.0;
    }

    if (groundImpactDetected) {
        if (isActive()) {
            phase_ = CatchFallPhase::Impacted;
        }
    } else if (phase_ == CatchFallPhase::Inactive) {
        const bool triggerByClearance = groundClearanceM <= tuning_.triggerClearanceM;
        const bool triggerByVelocity =
            (comVelocityMps.z < -0.1) && (timeToLandingS_ <= tuning_.braceTimeWindowS);
        if (triggerByClearance || triggerByVelocity) {
            phase_ = CatchFallPhase::Bracing;
        }
    }
}

void CatchFallController::reset()
{
    phase_ = CatchFallPhase::Inactive;
    timeToLandingS_ = 0.0;
}

} // namespace opas::behaviors
