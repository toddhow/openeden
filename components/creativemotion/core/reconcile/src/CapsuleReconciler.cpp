#include "opas/reconcile/CapsuleReconciler.hpp"

#include <cmath>
#include <numbers>

namespace opas::reconcile {

CapsuleReconciler::CapsuleReconciler(CapsuleConfig config)
    : config_(config) {
    state_.position.z = config_.halfHeightM;
}

void CapsuleReconciler::reset(const math::Vec3& initialPosition, float initialYawRad) {
    state_.position = initialPosition;
    state_.yawRad = initialYawRad;
    state_.mode = CapsuleMode::Anchored;
    state_.pelvisOffsetM = math::Vec3{0.0, 0.0, 0.0};
    state_.horizontalExcursionM = 0.0f;
    state_.clamped = false;
}

void CapsuleReconciler::update(const math::Transform& pelvisWorld,
                              arbiter::ArbiterState state,
                              std::optional<float> groundZ,
                              float dtSeconds) {
    // 1. Determine capsule mode from ArbiterState
    switch (state) {
    case arbiter::ArbiterState::Standing:
    case arbiter::ArbiterState::Animated:
        state_.mode = CapsuleMode::Anchored;
        break;
    case arbiter::ArbiterState::Leaning:
    case arbiter::ArbiterState::Stepping:
    case arbiter::ArbiterState::Disturbed:
    case arbiter::ArbiterState::Stumbling:
        state_.mode = CapsuleMode::Tracking;
        break;
    case arbiter::ArbiterState::Falling:
    case arbiter::ArbiterState::Fallen:
    case arbiter::ArbiterState::Down:
        state_.mode = CapsuleMode::Detached;
        break;
    case arbiter::ArbiterState::GettingUp:
        state_.mode = CapsuleMode::Reconciling;
        break;
    }

    // 2. Vertical ground conforming
    if (groundZ.has_value()) {
        state_.position.z = static_cast<double>(*groundZ) + config_.halfHeightM;
    } else {
        state_.position.z = pelvisWorld.translation.z;
    }

    // 3. Horizontal position tracking
    const double pelvisX = pelvisWorld.translation.x;
    const double pelvisY = pelvisWorld.translation.y;
    const double deltaX = pelvisX - state_.position.x;
    const double deltaY = pelvisY - state_.position.y;
    const double dist = std::hypot(deltaX, deltaY);

    state_.clamped = false;

    if (dist > 1e-6) {
        const double dirX = deltaX / dist;
        const double dirY = deltaY / dist;

        switch (state_.mode) {
        case CapsuleMode::Anchored: {
            if (dist > config_.leashRadiusM) {
                const double excess = dist - config_.leashRadiusM;
                const double maxStep = static_cast<double>(config_.trackingSpeedMps) * dtSeconds;
                const double stepDist = std::min(excess, maxStep);
                state_.position.x += dirX * stepDist;
                state_.position.y += dirY * stepDist;
            }
            break;
        }
        case CapsuleMode::Tracking: {
            const double rate = 1.0 - std::exp(-static_cast<double>(config_.trackingRate) * dtSeconds);
            const double desiredStep = dist * rate;
            const double maxStep = static_cast<double>(config_.trackingSpeedMps) * dtSeconds;
            const double stepDist = std::min(desiredStep, maxStep);
            state_.position.x += dirX * stepDist;
            state_.position.y += dirY * stepDist;
            break;
        }
        case CapsuleMode::Detached: {
            if (dist > config_.maxExcursionM) {
                state_.position.x = pelvisX - dirX * config_.maxExcursionM;
                state_.position.y = pelvisY - dirY * config_.maxExcursionM;
                state_.clamped = true;
            }
            break;
        }
        case CapsuleMode::Reconciling: {
            // Capsule horizontal position stays pinned during get-up
            break;
        }
        }
    }

    // 4. Update offsets and excursion metrics
    state_.pelvisOffsetM.x = pelvisWorld.translation.x - state_.position.x;
    state_.pelvisOffsetM.y = pelvisWorld.translation.y - state_.position.y;
    state_.pelvisOffsetM.z = pelvisWorld.translation.z - state_.position.z;
    state_.horizontalExcursionM = static_cast<float>(std::hypot(state_.pelvisOffsetM.x, state_.pelvisOffsetM.y));

    // 5. Yaw rotation tracking (shortest arc)
    const math::Quat& q = pelvisWorld.rotation;
    // Facing yaw from quaternion rotation (Z-axis rotation)
    const double sinYaw = 2.0 * (q.w * q.z + q.x * q.y);
    const double cosYaw = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    const float targetYaw = static_cast<float>(std::atan2(sinYaw, cosYaw));

    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0f * kPi;

    float yawDiff = targetYaw - state_.yawRad;
    while (yawDiff > kPi) {
        yawDiff -= kTwoPi;
    }
    while (yawDiff < -kPi) {
        yawDiff += kTwoPi;
    }

    if (dtSeconds > 0.0f) {
        const float maxYawStep = config_.rotationSpeedRadps * dtSeconds;
        const float rate = 1.0f - std::exp(-config_.trackingRate * dtSeconds);
        float desiredYawStep = yawDiff * rate;

        if (std::abs(desiredYawStep) > maxYawStep) {
            desiredYawStep = std::copysign(maxYawStep, yawDiff);
        }

        state_.yawRad += desiredYawStep;

        while (state_.yawRad > kPi) {
            state_.yawRad -= kTwoPi;
        }
        while (state_.yawRad < -kPi) {
            state_.yawRad += kTwoPi;
        }
    }
}

} // namespace opas::reconcile
