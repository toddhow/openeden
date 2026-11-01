#pragma once

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

namespace opas::behaviors
{

    /// Character lying orientation while resting on ground.
    enum class RestingOrientation
    {
        Unknown,
        Prone, ///< Face down toward ground
        Supine, ///< Face up toward sky
    };

    /// Phase of the get-up recovery controller.
    enum class GetUpPhase
    {
        Inactive,
        WaitingRest,
        BlendingGetUp,
        Completed,
    };

    /// Configuration parameters for get-up detection and handover.
    struct GetUpTuning
    {
        double restSpeedThresholdMps = 0.05; ///< Maximum CoM speed to qualify as resting on ground (m/s)
        double restDurationRequiredS = 1.0; ///< Duration stationary rest required before get-up triggers (s)
        double blendDurationS = 1.2; ///< Duration of blend transition from ragdoll to get-up clip (s)
    };

    /// Get-up handover controller (Milestone C2 Stage C2d).
    /// Detects stationary rest on ground, classifies resting orientation (Prone vs Supine),
    /// initiates get-up blending, and smoothly ramps tracking strictness back to nominal upright posture.
    class GetUpController
    {
    public:
        GetUpController() = default;
        explicit GetUpController(GetUpTuning tuning);

        [[nodiscard]] const GetUpTuning& tuning() const { return tuning_; }

        [[nodiscard]] GetUpPhase phase() const { return phase_; }
        [[nodiscard]] bool isGettingUp() const { return phase_ == GetUpPhase::BlendingGetUp; }
        [[nodiscard]] bool isCompleted() const { return phase_ == GetUpPhase::Completed; }
        [[nodiscard]] bool isResting() const { return isResting_; }
        [[nodiscard]] RestingOrientation orientation() const { return orientation_; }

        [[nodiscard]] double restTimerS() const { return restTimerS_; }
        [[nodiscard]] double blendProgress() const { return blendProgress_; }
        [[nodiscard]] double currentStrictness() const { return currentStrictness_; }

        [[nodiscard]] static RestingOrientation classifyOrientation(
            const math::Quat& pelvisRotation, const math::Quat& chestRotation);

        void update(double dtSeconds, arbiter::ArbiterState state, double comSpeedMps, const math::Quat& pelvisRotation,
            const math::Quat& chestRotation);

        void reset();

    private:
        GetUpTuning tuning_{};
        GetUpPhase phase_ = GetUpPhase::Inactive;
        bool isResting_ = false;
        RestingOrientation orientation_ = RestingOrientation::Unknown;

        double restTimerS_ = 0.0;
        double blendTimerS_ = 0.0;
        double blendProgress_ = 0.0;
        double currentStrictness_ = 0.0;
    };

} // namespace opas::behaviors
