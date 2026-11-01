#pragma once

#include "opas/math/Vec3.hpp"
#include "opas/sensors/BalanceReading.hpp"

#include <cmath>

namespace opas::behaviors
{

    /// Phase of the stumble recovery controller.
    enum class StumblePhase
    {
        Inactive,
        Active,
        Recovered,
        Failed,
    };

    /// Configuration parameters for stumble recovery stepping.
    struct StumbleTuning
    {
        double triggerMarginM = 0.18; ///< Margin threshold to initiate stumble stepping (m)
        double triggerImpulseNs = 60.0; ///< Disturbance impulse threshold to initiate stumble (N s)
        double fallTiltRad = 0.8; ///< Pelvis tilt angle threshold where recovery fails (rad)
        int minSteps = 2; ///< Minimum recovery steps required before recovery check
        int maxSteps = 4; ///< Maximum recovery steps before falling handoff
        double strideScale = 1.3; ///< Stride length multiplier during stumbling
        double cadenceScale = 1.25; ///< Cadence frequency multiplier during stumbling
        double recoverySpeedMps = 0.15; ///< Maximum horizontal CoM speed to qualify recovery (m/s)
    };

    /// Multi-step stumble recovery controller (Milestone C2 Stage C2b).
    /// Manages aggressive recovery stepping, stride extension, cadence acceleration,
    /// and clean handoff to either stable recovery or falling.
    class StumbleController
    {
    public:
        StumbleController() = default;
        explicit StumbleController(StumbleTuning tuning);

        [[nodiscard]] const StumbleTuning& tuning() const { return tuning_; }

        [[nodiscard]] StumblePhase phase() const { return phase_; }
        [[nodiscard]] bool isActive() const { return phase_ == StumblePhase::Active; }
        [[nodiscard]] bool isRecovered() const { return phase_ == StumblePhase::Recovered; }
        [[nodiscard]] bool hasFailed() const { return phase_ == StumblePhase::Failed; }

        [[nodiscard]] int stepsTaken() const { return stepsTaken_; }
        [[nodiscard]] double strideMultiplier() const;
        [[nodiscard]] double cadenceMultiplier() const;
        [[nodiscard]] math::Vec3 perturbationDirection() const { return perturbationDirection_; }

        [[nodiscard]] bool shouldTrigger(const sensors::BalanceReading& reading, double impulseNs = 0.0) const;

        void trigger(math::Vec3 direction);
        void update(double dtSeconds, const sensors::BalanceReading& reading, double pelvisTiltRad, bool stepCompleted);
        void reset();

    private:
        StumbleTuning tuning_{};
        StumblePhase phase_ = StumblePhase::Inactive;
        int stepsTaken_ = 0;
        math::Vec3 perturbationDirection_{};
    };

} // namespace opas::behaviors
