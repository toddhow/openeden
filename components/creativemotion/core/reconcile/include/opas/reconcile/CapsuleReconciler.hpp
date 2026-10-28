#pragma once

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Transform.hpp"
#include "opas/math/Vec3.hpp"

#include <cstdint>
#include <optional>

namespace opas::reconcile {

/// Operational mode of the gameplay character capsule relative to the physical pelvis.
enum class CapsuleMode : std::uint8_t {
    Anchored,    ///< Normal upright locomotion: capsule is reference origin, pelvis anchored
    Tracking,    ///< Disturbed or stumbling: capsule smoothly tracks horizontal pelvis motion
    Detached,    ///< Fallen or knockdown: capsule follows ground projection, clamps at maxExcursion
    Reconciling  ///< Getting up: capsule anchored to get-up recovery destination
};

/// Configuration parameters for gameplay capsule reconciliation.
struct CapsuleConfig {
    float radiusM = 0.35f;                 ///< Collision capsule radius (meters)
    float halfHeightM = 0.85f;             ///< Capsule half-height (total cylinder+caps = 1.70 m)
    float leashRadiusM = 0.05f;            ///< Horizontal deadzone before tracking engages (meters)
    float maxExcursionM = 1.50f;           ///< Maximum allowed horizontal separation from pelvis (meters)
    float trackingSpeedMps = 5.0f;         ///< Maximum horizontal catch-up speed (meters / second)
    float rotationSpeedRadps = 6.2831853f; ///< Maximum yaw catch-up speed (radians / second)
    float trackingRate = 10.0f;            ///< Exponential smoothing rate k (s^-1)
};

/// Runtime state and metrics of the gameplay capsule.
struct CapsuleState {
    math::Vec3 position{0.0f, 0.0f, 0.85f};      ///< Capsule center in world space (z = groundZ + halfHeightM)
    float yawRad = 0.0f;                        ///< Capsule facing yaw (radians about +Z)
    CapsuleMode mode = CapsuleMode::Anchored;   ///< Current operational tracking mode
    math::Vec3 pelvisOffsetM{0.0f, 0.0f, 0.0f};  ///< Offset vector: pelvisWorld.translation - capsule.position
    float horizontalExcursionM = 0.0f;          ///< 2D horizontal distance between capsule and pelvis
    bool clamped = false;                       ///< True if pelvis exceeded maxExcursionM and was clamped
};

/// Gameplay character controller capsule tracker and ground-conforming reconciler (Milestone C7 Stage C7a).
///
/// Keeps the gameplay character controller cylinder/capsule synchronized with the physical ragdoll pelvis
/// while disturbed, stumbling, falling, or getting up, with zero heap allocations on the hot path.
class CapsuleReconciler {
public:
    explicit CapsuleReconciler(CapsuleConfig config = {});

    /// Reset capsule to an explicit world position and yaw.
    void reset(const math::Vec3& initialPosition, float initialYawRad = 0.0f);

    /// Update capsule position, orientation, and mode relative to physical pelvis and arbiter state.
    ///
    /// @param pelvisWorld Physical world transform of the pelvis rigid body.
    /// @param state Whole-body or effective ArbiterState.
    /// @param groundZ Ground surface elevation below character (if omitted, falls back to pelvis base).
    /// @param dtSeconds Physics step delta time.
    /// Allocates zero memory on the hot path.
    void update(const math::Transform& pelvisWorld,
                arbiter::ArbiterState state,
                std::optional<float> groundZ,
                float dtSeconds);

    [[nodiscard]] const CapsuleConfig& config() const noexcept { return config_; }
    void setConfig(const CapsuleConfig& config) noexcept { config_ = config; }

    [[nodiscard]] const CapsuleState& state() const noexcept { return state_; }
    [[nodiscard]] const math::Vec3& position() const noexcept { return state_.position; }
    [[nodiscard]] float yawRad() const noexcept { return state_.yawRad; }
    [[nodiscard]] CapsuleMode mode() const noexcept { return state_.mode; }
    [[nodiscard]] float horizontalExcursion() const noexcept { return state_.horizontalExcursionM; }
    [[nodiscard]] bool isClamped() const noexcept { return state_.clamped; }

    void setPosition(const math::Vec3& position) noexcept { state_.position = position; }
    void setYaw(float yawRad) noexcept { state_.yawRad = yawRad; }
    void setMode(CapsuleMode mode) noexcept { state_.mode = mode; }

private:
    CapsuleConfig config_;
    CapsuleState state_;
};

} // namespace opas::reconcile
