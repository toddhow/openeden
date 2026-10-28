#include "opas/behaviors/GetUpController.hpp"

#include <algorithm>
#include <cmath>

namespace opas::behaviors {

GetUpController::GetUpController(GetUpTuning tuning)
    : tuning_(tuning)
{}

RestingOrientation GetUpController::classifyOrientation(const math::Quat& pelvisRotation,
                                                        const math::Quat& chestRotation)
{
    // Model space convention: Z up, Y forward.
    const math::Vec3 chestAnterior = math::rotate(chestRotation, math::Vec3{0.0, 1.0, 0.0});
    if (chestAnterior.z < -0.3) {
        return RestingOrientation::Prone;
    }
    if (chestAnterior.z > 0.3) {
        return RestingOrientation::Supine;
    }

    // Fall back to pelvis if chest anterior is lateral / ambiguous (|z| <= 0.3)
    const math::Vec3 pelvisAnterior = math::rotate(pelvisRotation, math::Vec3{0.0, 1.0, 0.0});
    if (pelvisAnterior.z < -0.3) {
        return RestingOrientation::Prone;
    }
    if (pelvisAnterior.z > 0.3) {
        return RestingOrientation::Supine;
    }

    return RestingOrientation::Unknown;
}

void GetUpController::update(double dtSeconds, arbiter::ArbiterState state, double comSpeedMps,
                             const math::Quat& pelvisRotation, const math::Quat& chestRotation)
{
    if (state == arbiter::ArbiterState::Standing || state == arbiter::ArbiterState::Animated) {
        reset();
        return;
    }

    if (state != arbiter::ArbiterState::Fallen && state != arbiter::ArbiterState::Down &&
        state != arbiter::ArbiterState::GettingUp) {
        reset();
        return;
    }

    if (state == arbiter::ArbiterState::Fallen || state == arbiter::ArbiterState::Down) {
        if (phase_ == GetUpPhase::BlendingGetUp) {
            blendTimerS_ += dtSeconds;
            blendProgress_ = std::min(1.0, blendTimerS_ / tuning_.blendDurationS);
            currentStrictness_ = blendProgress_;
            if (blendProgress_ >= 1.0) {
                phase_ = GetUpPhase::Completed;
                blendProgress_ = 1.0;
                currentStrictness_ = 1.0;
            }
        } else if (phase_ != GetUpPhase::Completed) {
            if (comSpeedMps <= tuning_.restSpeedThresholdMps) {
                isResting_ = true;
                if (phase_ == GetUpPhase::Inactive) {
                    phase_ = GetUpPhase::WaitingRest;
                }
                orientation_ = classifyOrientation(pelvisRotation, chestRotation);
                restTimerS_ += dtSeconds;
                if (restTimerS_ >= tuning_.restDurationRequiredS &&
                    phase_ == GetUpPhase::WaitingRest) {
                    orientation_ = classifyOrientation(pelvisRotation, chestRotation);
                    phase_ = GetUpPhase::BlendingGetUp;
                    blendTimerS_ = 0.0;
                    blendProgress_ = 0.0;
                    currentStrictness_ = 0.0;
                }
            } else {
                isResting_ = false;
                restTimerS_ = 0.0;
                if (phase_ == GetUpPhase::WaitingRest) {
                    phase_ = GetUpPhase::Inactive;
                }
            }
        }
    } else if (state == arbiter::ArbiterState::GettingUp) {
        if (phase_ == GetUpPhase::BlendingGetUp) {
            blendTimerS_ += dtSeconds;
            blendProgress_ = std::min(1.0, blendTimerS_ / tuning_.blendDurationS);
            currentStrictness_ = blendProgress_;
            if (blendProgress_ >= 1.0) {
                phase_ = GetUpPhase::Completed;
                blendProgress_ = 1.0;
                currentStrictness_ = 1.0;
            }
        }
    }
}

void GetUpController::reset()
{
    phase_ = GetUpPhase::Inactive;
    isResting_ = false;
    orientation_ = RestingOrientation::Unknown;
    restTimerS_ = 0.0;
    blendTimerS_ = 0.0;
    blendProgress_ = 0.0;
    currentStrictness_ = 0.0;
}

} // namespace opas::behaviors
