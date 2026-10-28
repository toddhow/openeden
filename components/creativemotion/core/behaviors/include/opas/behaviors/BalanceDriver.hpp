#pragma once

// Runs balance on one ragdoll every physics step (M5b, docs/tasks/0017): the
// pose source gives the animated target, the sensor reads the ragdoll,
// Balance leans the target, and the motor solver drives the joints to it.
// With stepping (M5c) it also steps to catch larger pushes, through the
// phases Leaning, Stepping, Settling and GaveUp. With hit reaction and the
// arbiter (M5d) it lowers stiffness near hits, reports the character state,
// and lets go when the character falls.
//
// Ownership: the host owns the driver, the ragdoll and the world. The driver
// owns its sensor, balance, stepping and pose source, and keeps a pointer to
// the ragdoll (not to the world, which it is handed each step), as
// motor::RagdollDriver does: the ragdoll must outlive it, and the host
// removes the driver from the world before destroying either.

#include "opas/arbiter/Arbiter.hpp"
#include "opas/behaviors/Balance.hpp"
#include "opas/behaviors/HitReaction.hpp"
#include "opas/behaviors/Stepping.hpp"
#include "opas/behaviors/StumbleController.hpp"
#include "opas/math/Transform.hpp"
#include "opas/motor/MotorSolver.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"
#include "opas/sensors/BalanceSensor.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace opas::behaviors {

enum class BalancePhase : std::uint8_t {
    Leaning,  // M5b lean; stepping arms after the capture point stays inside
    Stepping, // swinging a leg (up to maxSteps in a row)
    Settling, // after a successful step: legs held as they landed, lean on top
    GaveUp,   // steps ran out: lean only, no stepping, no joint torques (final)
};

class BalanceDriver final : public physics::StepListener {
public:
    // The sensor, balance and stepping must come from the same rig and
    // profile as the ragdoll. Without stepping it is the M5b driver.
    BalanceDriver(motor::Ragdoll& ragdoll, sensors::BalanceSensor sensor, Balance balance,
                  motor::PoseSource source, std::optional<Stepping> stepping = std::nullopt,
                  std::optional<HitReaction> hitReaction = std::nullopt,
                  std::optional<arbiter::Arbiter> arbiter = std::nullopt);

    // The host reports a hit on a rig body before the step it applies to;
    // it is timed at the driver's time at the start of that step. Ignored
    // without hit reaction. Allocates nothing (up to one pending hit per
    // body per step).
    void reportHit(std::size_t rigBody);
    // A rig joint's hit stiffness multiplier at the last pre-step's start
    // time; 1 without hit reaction.
    [[nodiscard]] double stiffnessMultiplier(std::size_t rigJoint) const;
    // The arbiter's state; empty without an arbiter.
    [[nodiscard]] std::optional<arbiter::CharacterState> state() const;

    // The body part corresponding to a rig body.
    [[nodiscard]] arbiter::BodyPart bodyPart(std::size_t rigBody) const;
    // The body part corresponding to a rig joint.
    [[nodiscard]] arbiter::BodyPart jointPart(std::size_t rigJoint) const;

    // Per-body-part arbiter queries and control (Stage C1c, Task 0024).
    [[nodiscard]] std::optional<arbiter::ArbiterState> partState(arbiter::BodyPart part) const;
    [[nodiscard]] double partStrictness(arbiter::BodyPart part) const;
    void setStrictnessOverride(arbiter::BodyPart part, std::optional<double> strictness);

    // Stage C4d: reference frame velocity for moving characters (e.g. locomotion / root motion).
    void setReferenceVelocity(math::Vec3 refVelocityMps);
    [[nodiscard]] math::Vec3 referenceVelocity() const;

    // First step: sets every leg joint's motor to the balance leg motor.
    // Every step: pose from the source (for the end of the step, as
    // RagdollDriver asks), sensor update, then the phase's pose (lean, or
    // stepping) and its joint torques, then motor targets. Allocates nothing.
    void onPreStep(physics::PhysicsWorld& world, float dtSeconds) override;
    void onPostStep(physics::PhysicsWorld& world, float dtSeconds) override;

    // The sensor's latest reading (from the last pre-step).
    [[nodiscard]] const sensors::BalanceReading& reading() const { return sensor_.reading(); }
    [[nodiscard]] BalancePhase phase() const { return phase_; }
    // Steps in the current sequence (counted when a swing starts); 0 before
    // any step.
    [[nodiscard]] int stepsTaken() const { return stepsTaken_; }
    // The largest joint torque stepping applied in the last pre-step (braking
    // or gravity compensation, after the cap), in N m; 0 when none (task 0019).
    [[nodiscard]] double lastJointTorqueNm() const { return lastJointTorqueNm_; }

    // Stumble recovery (Milestone C2 Stage C2b)
    [[nodiscard]] bool isStumbling() const;
    [[nodiscard]] int stumbleStepsTaken() const;
    void triggerStumble(math::Vec3 direction);
    [[nodiscard]] const StumbleController& stumbleController() const { return stumbleController_; }
    [[nodiscard]] double pelvisTiltRad() const;

private:
    void updateArming(const Stepping& stepping, double dtSeconds);
    void
    startSequence(const Stepping& stepping); // a new sequence: count reset, then continueSequence
    void continueSequence(const Stepping& stepping); // choose the swing foot and start a swing
    void startSwing();
    void advancePhase(const Stepping& stepping, double dtSeconds);
    void steppingPose(const Stepping& stepping, double dtSeconds);
    void settlingPose(const Stepping& stepping, double dtSeconds);
    // Task 0019: where the swing foot should land (Coros Sec. 3.2).
    [[nodiscard]] math::Vec3 footTarget(const Stepping& stepping) const;
    // Both legs by two-bone IK from the pelvis target to the ankle targets.
    void legsByIk(const Stepping& stepping, math::Vec3 swingAnkle);
    // Braking (Sec. 3.3) and swing-leg gravity compensation (Sec. 3.4).
    void applyJointTorques(const Stepping& stepping, physics::PhysicsWorld& world,
                           double dtSeconds);
    // +tau on headSideBody and -tau on otherBody, capped, as couples.
    void applyJointTorque(const Stepping& stepping, physics::PhysicsWorld& world,
                          std::size_t headSideBody, std::size_t otherBody, math::Vec3 torqueNm,
                          double dtSeconds);
    // Mass and centre of mass of the bodies whose flag equals `distal`.
    void massOf(const Stepping& stepping, std::span<const std::uint8_t> flags, bool distal,
                double& massKg, math::Vec3& centreM) const;
    [[nodiscard]] double footTiltRad(std::size_t footBody) const;

    void updateArbiter(double dtSeconds);
    void letGoPose();
    void updateMotors(physics::PhysicsWorld& world);

    // World rotation and position of a body's bone, from this step's states.
    [[nodiscard]] math::Quat boneWorld(std::size_t body) const;
    [[nodiscard]] math::Vec3 boneOrigin(std::size_t body) const;

    motor::Ragdoll* ragdoll_; // not owned; see the file comment
    sensors::BalanceSensor sensor_;
    Balance balance_;
    motor::PoseSource source_;
    std::optional<Stepping> stepping_;
    std::vector<math::Transform> localPose_; // sized once, reused every step
    std::vector<physics::BodyId> bodyIds_;   // stepping only, sized once
    std::vector<physics::BodyState> states_; // stepping only, sized once
    std::uint64_t steps_ = 0;

    BalancePhase phase_ = BalancePhase::Leaning;
    int stepsTaken_ = 0;
    std::size_t swing_ = 0; // index into Stepping::legs()
    int swingTicks_ = 0;    // pre-steps since the swing started
    double insideS_ = 0.0;  // capture point inside the support area this long
    bool armed_ = false;
    double sinceTouchdownS_ = 0.0;
    double lastJointTorqueNm_ = 0.0;
    // Task 0019 stepping state: where each ankle is planted, the swing's start
    // and target, the pelvis target, and the rig's rest pose in model space.
    std::array<math::Vec3, 2> planted_{};
    math::Vec3 swingStart_;
    math::Vec3 target_;
    math::Vec3 pelvisXY_;
    double pelvisZ_ = 0.0;
    std::vector<math::Transform> modelRest_; // sized once

    // M5d: hit reaction, the arbiter and motor gains.
    std::optional<HitReaction> hitReaction_;
    std::optional<arbiter::Arbiter> arbiter_;
    std::vector<arbiter::BodyPart> bodyParts_;   // per rig body
    std::vector<arbiter::BodyPart> jointParts_;  // per rig joint
    std::vector<std::size_t> pendingHits_;       // rig bodies, capacity one per body
    std::vector<bool> hitPending_;               // per rig body: already in pendingHits_
    std::vector<bool> isLegJoint_;               // per rig joint
    std::vector<physics::JointMotor> lastMotor_; // per rig joint: what was last set
    std::vector<math::Transform> lastPose_;      // the last pre-step's pose
    std::vector<math::Transform> letGoFrom_;     // the pose when balance let go
    double stepStartS_ = 0.0;                    // start time of the last pre-step
    bool lettingGo_ = false;
    double letGoBlend_ = 0.0; // 0 when balance lets go, 1 after blendTimeS
    math::Vec3 referenceVelocity_{};
    StumbleController stumbleController_;
};

} // namespace opas::behaviors
