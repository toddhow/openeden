#pragma once

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/sensors/GrabProbeSensor.hpp"

#include <optional>

namespace opas::behaviors
{

    /// Phase of the surface grab reaction.
    enum class GrabPhase
    {
        Inactive,
        Grabbing,
        Holding,
        Released,
    };

    /// Configuration parameters for surface grab reaction.
    struct GrabTuning
    {
        double maxReachM = 0.45; ///< Maximum reachable obstacle surface distance (m)
        double holdDurationS = 0.4; ///< Stabilizing contact hold duration (s)
        double stabilizingTorqueCapNm = 250.0; ///< Maximum couple/torque applied to arrest momentum (N m)
        double linearBrakingGainNsPerM = 400.0; ///< Linear velocity damping gain
    };

    /// Surface grab reaction controller (Milestone C2 Stage C2c).
    /// When a stumbling or falling character detects a reachable solid surface,
    /// commands the nearest arm toward the surface and applies stabilizing counter-couples
    /// to arrest linear and angular momentum.
    class GrabController
    {
    public:
        GrabController() = default;
        explicit GrabController(GrabTuning tuning);

        [[nodiscard]] const GrabTuning& tuning() const { return tuning_; }

        [[nodiscard]] GrabPhase phase() const { return phase_; }
        [[nodiscard]] bool isGrabbing() const;
        [[nodiscard]] bool isHolding() const { return phase_ == GrabPhase::Holding; }
        [[nodiscard]] const std::optional<sensors::GrabTarget>& activeTarget() const { return activeTarget_; }
        [[nodiscard]] double holdTimeRemainingS() const { return holdTimeRemainingS_; }

        [[nodiscard]] math::Vec3 computeStabilizingTorque(
            const math::Vec3& angularVelocityRadPerS, const math::Vec3& linearVelocityMps) const;

        void update(double dtSeconds, arbiter::ArbiterState state, const std::optional<sensors::GrabTarget>& bestTarget,
            const math::Vec3& linearVelocityMps, const math::Vec3& angularVelocityRadPerS);

        void reset();

    private:
        GrabTuning tuning_{};
        GrabPhase phase_ = GrabPhase::Inactive;
        std::optional<sensors::GrabTarget> activeTarget_ = std::nullopt;
        double holdTimeRemainingS_ = 0.0;
    };

} // namespace opas::behaviors
