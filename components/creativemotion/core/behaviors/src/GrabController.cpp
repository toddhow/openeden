#include "opas/behaviors/GrabController.hpp"

#include <cmath>

namespace opas::behaviors
{

    GrabController::GrabController(GrabTuning tuning)
        : tuning_(tuning)
    {
    }

    bool GrabController::isGrabbing() const
    {
        return phase_ == GrabPhase::Grabbing || phase_ == GrabPhase::Holding;
    }

    math::Vec3 GrabController::computeStabilizingTorque(
        const math::Vec3& angularVelocityRadPerS, const math::Vec3& linearVelocityMps) const
    {
        math::Vec3 torque = -angularVelocityRadPerS * 100.0;
        const math::Vec3 linTorque{ -linearVelocityMps.y * 50.0, linearVelocityMps.x * 50.0, 0.0 };
        torque = torque + linTorque;

        const double mag = math::length(torque);
        if (mag > tuning_.stabilizingTorqueCapNm && mag > 1e-6)
        {
            torque = torque * (tuning_.stabilizingTorqueCapNm / mag);
        }
        return torque;
    }

    void GrabController::update(double dtSeconds, arbiter::ArbiterState state,
        const std::optional<sensors::GrabTarget>& bestTarget, const math::Vec3& /*linearVelocityMps*/,
        const math::Vec3& /*angularVelocityRadPerS*/)
    {
        if (state != arbiter::ArbiterState::Stumbling && state != arbiter::ArbiterState::Falling)
        {
            reset();
            return;
        }

        if (phase_ == GrabPhase::Holding)
        {
            holdTimeRemainingS_ -= dtSeconds;
            if (holdTimeRemainingS_ <= 0.0)
            {
                phase_ = GrabPhase::Released;
                holdTimeRemainingS_ = 0.0;
            }
        }
        else if (phase_ == GrabPhase::Inactive)
        {
            if (bestTarget.has_value() && bestTarget->distanceM <= tuning_.maxReachM)
            {
                phase_ = GrabPhase::Holding;
                activeTarget_ = bestTarget;
                holdTimeRemainingS_ = tuning_.holdDurationS;
            }
        }
    }

    void GrabController::reset()
    {
        phase_ = GrabPhase::Inactive;
        activeTarget_ = std::nullopt;
        holdTimeRemainingS_ = 0.0;
    }

} // namespace opas::behaviors
