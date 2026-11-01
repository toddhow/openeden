#pragma once

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Vec3.hpp"

namespace opas::behaviors
{

    /// Phase of the catch-fall controller.
    enum class CatchFallPhase
    {
        Inactive,
        Bracing,
        Impacted,
    };

    /// Configuration parameters for catch-fall reaction.
    struct CatchFallTuning
    {
        double triggerClearanceM = 1.2; ///< Ground clearance threshold below which bracing begins (m)
        double braceTimeWindowS = 0.6; ///< Predicted time to landing window to begin reach (s)
        double armReachDistanceM = 0.5; ///< Forward/downward hand reach distance from shoulder (m)
        double headTuckPitchRad = -0.5; ///< Head pitch angle to tuck chin to chest (rad)
        double impactComplianceStrictness = 0.10; ///< Arm strictness dropped upon touchdown impact
        double gravityMps2 = 9.81; ///< Gravitational acceleration for landing estimation (m/s^2)
    };

    /// Catch fall controller (Milestone C2 Stage C2c).
    /// Predicts ground impact during falls, extends arms toward the surface,
    /// tucks the head forward to minimize cranial impact, and drops arm strictness
    /// to compliant absorption upon touchdown.
    class CatchFallController
    {
    public:
        CatchFallController() = default;
        explicit CatchFallController(CatchFallTuning tuning);

        [[nodiscard]] const CatchFallTuning& tuning() const { return tuning_; }

        [[nodiscard]] CatchFallPhase phase() const { return phase_; }
        [[nodiscard]] bool isActive() const;
        [[nodiscard]] bool isBracing() const { return phase_ == CatchFallPhase::Bracing; }
        [[nodiscard]] bool hasImpacted() const { return phase_ == CatchFallPhase::Impacted; }

        [[nodiscard]] double estimatedTimeToLandingS() const { return timeToLandingS_; }
        [[nodiscard]] double headTuckPitchRad() const;
        [[nodiscard]] double partStrictness(arbiter::BodyPart part) const;

        [[nodiscard]] math::Vec3 computeHandTarget(arbiter::BodyPart armPart, const math::Vec3& shoulderPos,
            const math::Vec3& groundNormal = { 0.0, 0.0, 1.0 }) const;

        void update(double dtSeconds, arbiter::ArbiterState state, double groundClearanceM,
            const math::Vec3& comVelocityMps, bool groundImpactDetected);

        void reset();

    private:
        CatchFallTuning tuning_{};
        CatchFallPhase phase_ = CatchFallPhase::Inactive;
        double timeToLandingS_ = 0.0;
    };

} // namespace opas::behaviors
