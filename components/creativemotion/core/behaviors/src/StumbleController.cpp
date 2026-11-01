#include "opas/behaviors/StumbleController.hpp"

#include <cmath>

namespace opas::behaviors
{

    StumbleController::StumbleController(StumbleTuning tuning)
        : tuning_(tuning)
    {
    }

    double StumbleController::strideMultiplier() const
    {
        return (phase_ == StumblePhase::Active) ? tuning_.strideScale : 1.0;
    }

    double StumbleController::cadenceMultiplier() const
    {
        return (phase_ == StumblePhase::Active) ? tuning_.cadenceScale : 1.0;
    }

    bool StumbleController::shouldTrigger(const sensors::BalanceReading& reading, double impulseNs) const
    {
        if (impulseNs >= tuning_.triggerImpulseNs)
        {
            return true;
        }
        if (reading.valid && reading.hasSupport && reading.captureMarginM > tuning_.triggerMarginM)
        {
            return true;
        }
        return false;
    }

    void StumbleController::trigger(math::Vec3 direction)
    {
        phase_ = StumblePhase::Active;
        stepsTaken_ = 0;
        const double len = math::length(direction);
        perturbationDirection_ = (len > 1e-6) ? (direction / len) : math::Vec3{ 0.0, 0.0, 0.0 };
    }

    void StumbleController::update(
        double /*dtSeconds*/, const sensors::BalanceReading& reading, double pelvisTiltRad, bool stepCompleted)
    {
        if (phase_ != StumblePhase::Active)
        {
            return;
        }

        if (pelvisTiltRad > tuning_.fallTiltRad)
        {
            phase_ = StumblePhase::Failed;
            return;
        }

        if (stepCompleted)
        {
            ++stepsTaken_;
        }

        if (stepsTaken_ >= tuning_.minSteps)
        {
            const double horizontalSpeed
                = std::hypot(reading.centreOfMassVelocityMps.x, reading.centreOfMassVelocityMps.y);
            if (reading.valid && reading.hasSupport && reading.captureMarginM <= 0.0
                && horizontalSpeed <= tuning_.recoverySpeedMps)
            {
                phase_ = StumblePhase::Recovered;
                return;
            }
        }

        if (stepsTaken_ >= tuning_.maxSteps)
        {
            phase_ = StumblePhase::Failed;
            return;
        }
    }

    void StumbleController::reset()
    {
        phase_ = StumblePhase::Inactive;
        stepsTaken_ = 0;
        perturbationDirection_ = math::Vec3{ 0.0, 0.0, 0.0 };
    }

} // namespace opas::behaviors
