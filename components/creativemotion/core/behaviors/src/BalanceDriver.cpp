#include "opas/behaviors/BalanceDriver.hpp"

#include "opas/rig/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace opas::behaviors
{
    namespace
    {

        // Task 0019 constants, from the prototype in docs/research/0001:
        // the inverted pendulum never uses a height below this (a crouched body
        // would otherwise ask for tiny steps);
        constexpr double kMinPendulumHeightM = 0.3;
        // the foot target follows the body for this share of the swing, then holds;
        constexpr double kRetargetUntilPhase = 0.8;
        // the pelvis is lowered until both ankles are reachable at this share of
        // leg length;
        constexpr double kReachFraction = 0.97;
        // after a landing the pelvis target moves to the feet's midpoint at this
        // rate (exponential, per second).
        constexpr double kPelvisFollowPerS = 3.0;
        // Below this length of thigh x shin (a nearly straight knee) the bend plane
        // is undefined, so the hip keeps the shortest-arc rotation.
        constexpr double kMinBendNormal = 1e-6;
        // A swing touches down once its time reaches stepTimeS; this absorbs the
        // float rounding of dt so that, for example, a 0.25 s swing at 60 Hz takes 15
        // pre-steps, not 16.
        constexpr double kTouchdownSlackS = 1e-6;

        math::Quat toMath(physics::Quat q)
        {
            return math::normalized(math::Quat{ q.x, q.y, q.z, q.w });
        }

        math::Vec3 toMath(physics::Vec3 v)
        {
            return { v.x, v.y, v.z };
        }

        physics::Vec3 toPhysics(math::Vec3 v)
        {
            return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        physics::JointMotor toPhysics(const rig::MotorGains& gains, double stiffnessScale)
        {
            return { static_cast<float>(gains.stiffnessNmPerRad * stiffnessScale),
                static_cast<float>(gains.dampingNmsPerRad), static_cast<float>(gains.maxTorqueNm) };
        }

        bool sameMotor(const physics::JointMotor& a, const physics::JointMotor& b)
        {
            return a.stiffnessNmPerRad == b.stiffnessNmPerRad && a.dampingNmsPerRad == b.dampingNmsPerRad
                && a.maxTorqueNm == b.maxTorqueNm;
        }

        // A multiplier this close to 1 counts as fully recovered for the motors, so
        // recovery stops setting motors once the change is far below float
        // precision of any gain (the readout keeps the exact value).
        constexpr double kRecoveredMultiplier = 1.0 - 1e-6;

        // Spherical interpolation along the shorter path (q and -q are the same
        // rotation), normalized; near-parallel rotations interpolate linearly.
        math::Quat slerp(math::Quat a, math::Quat b, double t)
        {
            double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0.0)
            {
                b = math::Quat{ -b.x, -b.y, -b.z, -b.w };
                dot = -dot;
            }
            constexpr double kLinearAbove = 0.9995;
            double wa = 1.0 - t;
            double wb = t;
            if (dot < kLinearAbove)
            {
                const double theta = std::acos(dot);
                const double s = std::sin(theta);
                wa = std::sin((1.0 - t) * theta) / s;
                wb = std::sin(t * theta) / s;
            }
            return math::normalized(
                math::Quat{ wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w });
        }

    } // namespace

    BalanceDriver::BalanceDriver(motor::Ragdoll& ragdoll, sensors::BalanceSensor sensor, Balance balance,
        motor::PoseSource source, std::optional<Stepping> stepping, std::optional<HitReaction> hitReaction,
        std::optional<arbiter::Arbiter> arbiter)
        : ragdoll_(&ragdoll)
        , sensor_(std::move(sensor))
        , balance_(std::move(balance))
        , source_(std::move(source))
        , stepping_(std::move(stepping))
        , localPose_(ragdoll.skeleton.bones.size())
        , hitReaction_(std::move(hitReaction))
        , arbiter_(arbiter)
    {
        // Start from the rest pose, so a source that fills only some bones still
        // gives a complete pose.
        for (std::size_t i = 0; i < localPose_.size(); ++i)
        {
            localPose_[i] = ragdoll.skeleton.bones[i].localRest;
        }
        if (stepping_ || arbiter_)
        {
            for (const motor::RagdollBody& body : ragdoll.bodies)
            {
                bodyIds_.push_back(body.id);
            }
            states_.resize(bodyIds_.size());
        }
        // The motors the ragdoll was built with (the profile's), so the driver
        // sets a motor only when its value changes.
        isLegJoint_.assign(ragdoll.joints.size(), false);
        for (const std::size_t joint : balance_.legJoints())
        {
            isLegJoint_[joint] = true;
        }
        for (std::size_t j = 0; j < ragdoll.joints.size(); ++j)
        {
            lastMotor_.push_back(toPhysics(balance_.profileMotor(j), 1.0));
        }
        pendingHits_.reserve(ragdoll.bodies.size());
        hitPending_.assign(ragdoll.bodies.size(), false);
        std::vector<arbiter::BodyPart> boneParts(ragdoll.skeleton.bones.size(), arbiter::BodyPart::Torso);
        for (std::size_t i = 0; i < ragdoll.skeleton.bones.size(); ++i)
        {
            if (const auto p = arbiter::classifyBone(ragdoll.skeleton.bones[i].name))
            {
                boneParts[i] = *p;
            }
            else if (ragdoll.skeleton.bones[i].parent >= 0
                && static_cast<std::size_t>(ragdoll.skeleton.bones[i].parent) < i)
            {
                boneParts[i] = boneParts[static_cast<std::size_t>(ragdoll.skeleton.bones[i].parent)];
            }
            else
            {
                boneParts[i] = arbiter::BodyPart::Torso;
            }
        }
        bodyParts_.resize(ragdoll.bodies.size(), arbiter::BodyPart::Torso);
        for (std::size_t b = 0; b < ragdoll.bodies.size(); ++b)
        {
            const std::size_t boneIdx = ragdoll.bodies[b].bone;
            bodyParts_[b] = (boneIdx < boneParts.size()) ? boneParts[boneIdx] : arbiter::BodyPart::Torso;
        }
        jointParts_.resize(ragdoll.joints.size(), arbiter::BodyPart::Torso);
        for (std::size_t j = 0; j < ragdoll.joints.size(); ++j)
        {
            const std::size_t childBody = ragdoll.joints[j].childBody;
            if (childBody < ragdoll.bodies.size())
            {
                const std::size_t boneIdx = ragdoll.bodies[childBody].bone;
                jointParts_[j] = (boneIdx < boneParts.size()) ? boneParts[boneIdx] : arbiter::BodyPart::Torso;
            }
            else
            {
                jointParts_[j] = arbiter::BodyPart::Torso;
            }
        }
        lastPose_ = localPose_;
        letGoFrom_ = localPose_;
        if (stepping_)
        {
            modelRest_ = rig::computeWorldRest(ragdoll.skeleton);
        }
    }

    void BalanceDriver::reportHit(std::size_t rigBody)
    {
        if (arbiter_ && rigBody < bodyParts_.size())
        {
            arbiter_->disturbPart(bodyParts_[rigBody]);
        }
        // One pending hit per body: a repeat in the same step adds nothing (its
        // drop would be the same), so no hit on another body is ever crowded out.
        if (hitReaction_ && rigBody < hitPending_.size() && !hitPending_[rigBody])
        {
            hitPending_[rigBody] = true;
            pendingHits_.push_back(rigBody);
        }
    }

    arbiter::BodyPart BalanceDriver::bodyPart(std::size_t rigBody) const
    {
        if (rigBody < bodyParts_.size())
        {
            return bodyParts_[rigBody];
        }
        return arbiter::BodyPart::Torso;
    }

    arbiter::BodyPart BalanceDriver::jointPart(std::size_t rigJoint) const
    {
        if (rigJoint < jointParts_.size())
        {
            return jointParts_[rigJoint];
        }
        return arbiter::BodyPart::Torso;
    }

    std::optional<arbiter::ArbiterState> BalanceDriver::partState(arbiter::BodyPart part) const
    {
        if (!arbiter_)
        {
            return std::nullopt;
        }
        return arbiter_->partState(part);
    }

    double BalanceDriver::partStrictness(arbiter::BodyPart part) const
    {
        if (!arbiter_)
        {
            return 1.0;
        }
        return arbiter_->partStrictness(part);
    }

    void BalanceDriver::setStrictnessOverride(arbiter::BodyPart part, std::optional<double> strictness)
    {
        if (arbiter_)
        {
            arbiter_->setStrictnessOverride(part, strictness);
        }
    }

    double BalanceDriver::stiffnessMultiplier(std::size_t rigJoint) const
    {
        return hitReaction_ ? hitReaction_->stiffnessMultiplier(rigJoint, stepStartS_) : 1.0;
    }

    std::optional<arbiter::CharacterState> BalanceDriver::state() const
    {
        if (!arbiter_)
        {
            return std::nullopt;
        }
        return arbiter_->state();
    }

    void BalanceDriver::setReferenceVelocity(math::Vec3 refVelocityMps)
    {
        referenceVelocity_ = refVelocityMps;
    }

    math::Vec3 BalanceDriver::referenceVelocity() const
    {
        return referenceVelocity_;
    }

    bool BalanceDriver::isStumbling() const
    {
        return stumbleController_.isActive();
    }

    int BalanceDriver::stumbleStepsTaken() const
    {
        return stumbleController_.stepsTaken();
    }

    void BalanceDriver::triggerStumble(math::Vec3 direction)
    {
        stumbleController_.trigger(direction);
        if (stepping_ && !lettingGo_ && phase_ != BalancePhase::GaveUp)
        {
            if (phase_ == BalancePhase::Leaning || phase_ == BalancePhase::Settling)
            {
                startSequence(*stepping_);
            }
        }
    }

    double BalanceDriver::pelvisTiltRad() const
    {
        if (!ragdoll_ || states_.empty())
        {
            return 0.0;
        }
        const std::size_t root = ragdoll_->rootBody;
        if (root >= states_.size() || root >= ragdoll_->bodies.size())
        {
            return 0.0;
        }
        const math::Quat fromRest
            = math::normalized(toMath(states_[root].rotation) * math::conjugate(ragdoll_->bodies[root].restModel));
        const math::Vec3 up = math::rotate(fromRest, math::Vec3{ 0.0, 0.0, 1.0 });
        return std::atan2(std::hypot(up.x, up.y), up.z);
    }

    void BalanceDriver::onPreStep(physics::PhysicsWorld& world, float dtSeconds)
    {
        const double dt = dtSeconds;
        stepStartS_ = static_cast<double>(steps_) * dt;
        // Hits reported since the last step, timed at this step's start.
        if (hitReaction_)
        {
            for (const std::size_t body : pendingHits_)
            {
                hitReaction_->hit(body, stepStartS_);
                hitPending_[body] = false;
            }
        }
        pendingHits_.clear();

        const double timeSeconds = static_cast<double>(steps_ + 1) * dt;
        source_(timeSeconds, localPose_);
        sensor_.update(world, referenceVelocity_);
        lastJointTorqueNm_ = 0.0;
        if (!bodyIds_.empty())
        {
            world.readBodies(bodyIds_, states_);
        }
        if (!lettingGo_ && stepping_)
        {
            advancePhase(*stepping_, dt);
        }
        updateArbiter(dt);
        if (lettingGo_)
        {
            letGoPose();
        }
        else if (!stepping_)
        {
            balance_.apply(sensor_.reading(), localPose_);
        }
        else
        {
            const Stepping& stepping = *stepping_;
            switch (phase_)
            {
                case BalancePhase::Leaning:
                case BalancePhase::GaveUp:
                    balance_.apply(sensor_.reading(), localPose_);
                    break;
                case BalancePhase::Stepping:
                    steppingPose(stepping, dt);
                    break;
                case BalancePhase::Settling:
                    settlingPose(stepping, dt);
                    break;
            }
            applyJointTorques(stepping, world, dt);
        }
        updateMotors(world);
        lastPose_ = localPose_;
        motor::driveTowardsPose(world, *ragdoll_, localPose_);
        ++steps_;
    }

    void BalanceDriver::updateArbiter(double dtSeconds)
    {
        if (!arbiter_)
        {
            return;
        }
        const sensors::BalanceReading& r = sensor_.reading();
        arbiter::ArbiterInputs in;
        in.balancing = phase_ == BalancePhase::Leaning || phase_ == BalancePhase::Settling;
        in.stepping = phase_ == BalancePhase::Stepping;
        in.stumbling = isStumbling();
        in.gaveUp = phase_ == BalancePhase::GaveUp;
        in.supported = r.valid && r.hasSupport;
        in.comSpeedMps = std::hypot(r.centreOfMassVelocityMps.x, r.centreOfMassVelocityMps.y);
        in.pelvisTiltRad = pelvisTiltRad();
        for (const physics::BodyState& s : states_)
        {
            in.maxBodySpeedMps = std::max(in.maxBodySpeedMps, math::length(toMath(s.linearVelocityMps)));
        }
        in.dtS = dtSeconds;
        const arbiter::CharacterState state = arbiter_->update(in);
        const bool falling = state == arbiter::CharacterState::Falling || state == arbiter::CharacterState::Fallen;
        if (falling && !lettingGo_)
        {
            // Balance lets go: blend from the last balance pose to the source.
            lettingGo_ = true;
            letGoFrom_ = lastPose_;
            letGoBlend_ = 0.0;
        }
        else if (lettingGo_)
        {
            // The let-go blend runs from the moment balance let go; a later
            // state change (Falling to Fallen) does not restart it.
            letGoBlend_ = std::min(1.0, letGoBlend_ + dtSeconds / arbiter_->blendTimeS());
        }
    }

    void BalanceDriver::letGoPose()
    {
        // The source pose, reached from the last balance pose over blendTimeS.
        const double f = letGoBlend_;
        if (f >= 1.0)
        {
            return;
        }
        for (std::size_t b = 0; b < localPose_.size(); ++b)
        {
            localPose_[b].rotation = slerp(letGoFrom_[b].rotation, localPose_[b].rotation, f);
        }
    }

    void BalanceDriver::updateMotors(physics::PhysicsWorld& world)
    {
        // Base gain: the balance leg motor for leg joints while balance owns
        // them, the profile's own motor otherwise, blended linearly from one to
        // the other over blendTimeS when balance lets go. Then the hit
        // multiplier on stiffness. A motor is set only when its value changes.
        const rig::MotorGains leg = balance_.legMotor();
        const double blend = lettingGo_ ? letGoBlend_ : 0.0;
        for (std::size_t j = 0; j < lastMotor_.size(); ++j)
        {
            rig::MotorGains base = balance_.profileMotor(j);
            if (isLegJoint_[j] && blend < 1.0)
            {
                const rig::MotorGains to = base;
                base.stiffnessNmPerRad = leg.stiffnessNmPerRad + (to.stiffnessNmPerRad - leg.stiffnessNmPerRad) * blend;
                base.dampingNmsPerRad = leg.dampingNmsPerRad + (to.dampingNmsPerRad - leg.dampingNmsPerRad) * blend;
                base.maxTorqueNm = leg.maxTorqueNm + (to.maxTorqueNm - leg.maxTorqueNm) * blend;
            }
            double multiplier = hitReaction_ ? hitReaction_->stiffnessMultiplier(j, stepStartS_) : 1.0;
            if (multiplier >= kRecoveredMultiplier)
            {
                multiplier = 1.0;
            }
            if (arbiter_ && j < jointParts_.size())
            {
                const arbiter::BodyPart part = jointParts_[j];
                if (arbiter_->strictnessOverride(part).has_value())
                {
                    multiplier *= arbiter_->partStrictness(part);
                }
            }
            const physics::JointMotor motor = toPhysics(base, multiplier);
            if (!sameMotor(motor, lastMotor_[j]))
            {
                world.setJointMotor(ragdoll_->joints[j].id, motor);
                lastMotor_[j] = motor;
            }
        }
    }

    void BalanceDriver::onPostStep(physics::PhysicsWorld& /*world*/, float /*dtSeconds*/) {}

    void BalanceDriver::updateArming(const Stepping& stepping, double dtSeconds)
    {
        const sensors::BalanceReading& r = sensor_.reading();
        const bool inside = r.valid && r.hasSupport && r.captureMarginM <= 0.0;
        insideS_ = inside ? insideS_ + dtSeconds : 0.0;
        if (insideS_ >= stepping.tuning().armTimeS)
        {
            armed_ = true;
        }
    }

    void BalanceDriver::startSequence(const Stepping& stepping)
    {
        stepsTaken_ = 0;
        const auto& legs = stepping.legs();
        for (std::size_t f = 0; f < 2; ++f)
        {
            planted_[f] = boneOrigin(legs[f].footBody);
        }
        const math::Vec3 root = boneOrigin(stepping.rootBody());
        pelvisXY_ = math::Vec3{ root.x, root.y, 0.0 };
        pelvisZ_ = root.z;
        // The foot nearer the capture point swings first, as in M5c: after a
        // sideways push that is the foot on the push side, which steps out
        // instead of crossing over. Ties go to the first foot in the profile.
        const sensors::Point2 cp = sensor_.reading().capturePointM;
        double distance[2] = { 0.0, 0.0 };
        for (std::size_t f = 0; f < 2; ++f)
        {
            distance[f] = std::hypot(cp.x - planted_[f].x, cp.y - planted_[f].y);
        }
        swing_ = distance[1] < distance[0] ? 1 : 0;
        phase_ = BalancePhase::Stepping;
        armed_ = false;
        insideS_ = 0.0;
        startSwing();
        swingStart_ = planted_[swing_];
        target_ = footTarget(stepping);
        if (stumbleController_.isActive())
        {
            target_ = swingStart_ + (target_ - swingStart_) * stumbleController_.strideMultiplier();
        }
    }

    void BalanceDriver::continueSequence(const Stepping& stepping)
    {
        // Another step in the same sequence: the other foot swings.
        swing_ = 1 - swing_;
        phase_ = BalancePhase::Stepping;
        armed_ = false;
        insideS_ = 0.0;
        startSwing();
        swingStart_ = boneOrigin(stepping.legs()[swing_].footBody);
        planted_[swing_] = swingStart_;
        target_ = footTarget(stepping);
        if (stumbleController_.isActive())
        {
            target_ = swingStart_ + (target_ - swingStart_) * stumbleController_.strideMultiplier();
        }
    }

    void BalanceDriver::startSwing()
    {
        ++stepsTaken_;
        swingTicks_ = 0;
    }

    void BalanceDriver::advancePhase(const Stepping& stepping, double dtSeconds)
    {
        const sensors::BalanceReading& r = sensor_.reading();
        const bool outside = r.valid && r.hasSupport && r.captureMarginM > 0.0;
        const double tilt = pelvisTiltRad();

        switch (phase_)
        {
            case BalancePhase::Leaning:
                updateArming(stepping, dtSeconds);
                if (stumbleController_.isActive())
                {
                    startSequence(stepping);
                }
                else if (armed_ && outside)
                {
                    startSequence(stepping);
                }
                break;
            case BalancePhase::Stepping:
            {
                ++swingTicks_;
                const double cadence = stumbleController_.cadenceMultiplier();
                const double stepTime = stepping.tuning().stepTimeS / (cadence > 0.0 ? cadence : 1.0);
                const double swingS = static_cast<double>(swingTicks_) * dtSeconds;
                if (swingS + kTouchdownSlackS >= stepTime)
                {
                    // Touchdown: the foot is planted where it is. Then captured,
                    // another swing, or out of steps.
                    planted_[swing_] = boneOrigin(stepping.legs()[swing_].footBody);
                    if (stumbleController_.isActive())
                    {
                        stumbleController_.update(dtSeconds, r, tilt, true);
                        if (stumbleController_.isRecovered())
                        {
                            phase_ = BalancePhase::Settling;
                            sinceTouchdownS_ = 0.0;
                            armed_ = false;
                            insideS_ = 0.0;
                        }
                        else if (stumbleController_.hasFailed())
                        {
                            phase_ = BalancePhase::GaveUp;
                        }
                        else
                        {
                            continueSequence(stepping);
                        }
                    }
                    else
                    {
                        if (r.valid && r.hasSupport && r.captureMarginM <= 0.0)
                        {
                            phase_ = BalancePhase::Settling;
                            sinceTouchdownS_ = 0.0;
                            armed_ = false;
                            insideS_ = 0.0;
                        }
                        else if (stepsTaken_ < stepping.tuning().maxSteps)
                        {
                            continueSequence(stepping);
                        }
                        else
                        {
                            phase_ = BalancePhase::GaveUp;
                        }
                    }
                }
                else
                {
                    if (stumbleController_.isActive())
                    {
                        stumbleController_.update(dtSeconds, r, tilt, false);
                        if (stumbleController_.hasFailed())
                        {
                            phase_ = BalancePhase::GaveUp;
                        }
                    }
                }
                break;
            }
            case BalancePhase::Settling:
                sinceTouchdownS_ += dtSeconds;
                updateArming(stepping, dtSeconds);
                if (stumbleController_.isActive())
                {
                    startSequence(stepping);
                }
                else if (armed_ && outside)
                {
                    startSequence(stepping);
                }
                else if (outside && stepsTaken_ < stepping.tuning().maxSteps)
                {
                    // Not caught after all: the sequence goes on with another step.
                    continueSequence(stepping);
                }
                break;
            case BalancePhase::GaveUp:
                break;
        }
    }

    math::Quat BalanceDriver::boneWorld(std::size_t body) const
    {
        return math::normalized(
            toMath(states_[body].rotation) * math::conjugate(ragdoll_->bodies[body].local.rotation));
    }

    math::Vec3 BalanceDriver::boneOrigin(std::size_t body) const
    {
        return toMath(states_[body].positionM)
            - math::rotate(boneWorld(body), ragdoll_->bodies[body].local.translation);
    }

    math::Vec3 BalanceDriver::footTarget(const Stepping& stepping) const
    {
        // Where the body comes to rest (Coros Sec. 3.2), kept from crossing the
        // stance foot (below).
        const rig::SteppingTuning& tuning = stepping.tuning();
        const sensors::BalanceReading& r = sensor_.reading();
        const Stepping::Leg& swing = stepping.legs()[swing_];
        const double heightM = std::max(kMinPendulumHeightM, r.centreOfMassM.z - r.supportHeightM);
        const math::Vec3 offset = ipmStepOffset(
            r.centreOfMassVelocityMps, heightM, sensor_.gravityMps2(), tuning.maxStepLegFraction * swing.lengthM);
        math::Vec3 target = math::Vec3{ r.centreOfMassM.x, r.centreOfMassM.y, 0.0 } + offset;
        // The feet never cross: the swing foot lands at least twice
        // stanceHalfWidthM to its own side of the stance foot, across the body
        // (model x, the side the foot has in the rest pose; world axes are model
        // axes, an M5 limit). Otherwise the pendulum's point stands.
        const Stepping::Leg& stanceLeg = stepping.legs()[1 - swing_];
        const double side
            = modelRest_[swing.ankleBone].translation.x >= modelRest_[stanceLeg.ankleBone].translation.x ? 1.0 : -1.0;
        const double nearestX = planted_[1 - swing_].x + side * 2.0 * tuning.stanceHalfWidthM;
        target.x = side > 0.0 ? std::max(target.x, nearestX) : std::min(target.x, nearestX);
        target.z = swingStart_.z;
        return target;
    }

    void BalanceDriver::steppingPose(const Stepping& stepping, double dtSeconds)
    {
        const rig::SteppingTuning& tuning = stepping.tuning();
        const double cadence = stumbleController_.cadenceMultiplier();
        const double stepTime = tuning.stepTimeS / (cadence > 0.0 ? cadence : 1.0);
        const double phase = std::min(1.0, static_cast<double>(swingTicks_) * dtSeconds / stepTime);
        if (phase < kRetargetUntilPhase && sensor_.reading().valid)
        {
            target_ = footTarget(stepping);
            if (stumbleController_.isActive())
            {
                target_ = swingStart_ + (target_ - swingStart_) * stumbleController_.strideMultiplier();
            }
        }
        // Linear in phase, lifted on a half sine (Coros Sec. 3.2).
        math::Vec3 ankle = swingStart_ + (target_ - swingStart_) * phase;
        ankle.z = swingStart_.z + tuning.liftM * std::sin(std::numbers::pi * phase);
        // The pelvis target follows the pelvis while a foot is in the air.
        const math::Vec3 root = boneOrigin(stepping.rootBody());
        pelvisXY_ = math::Vec3{ root.x, root.y, 0.0 };
        legsByIk(stepping, ankle);
    }

    void BalanceDriver::settlingPose(const Stepping& stepping, double dtSeconds)
    {
        // Both feet planted; the pelvis target moves to their midpoint, then the
        // lean on top.
        const double a = 1.0 - std::exp(-kPelvisFollowPerS * dtSeconds);
        const double cx = 0.5 * (planted_[0].x + planted_[1].x);
        const double cy = 0.5 * (planted_[0].y + planted_[1].y);
        pelvisXY_.x += (cx - pelvisXY_.x) * a;
        pelvisXY_.y += (cy - pelvisXY_.y) * a;
        legsByIk(stepping, planted_[swing_]);
        balance_.apply(sensor_.reading(), localPose_);
    }

    void BalanceDriver::legsByIk(const Stepping& stepping, math::Vec3 swingAnkle)
    {
        // The pelvis target is (pelvisXY_, z), with z lowered until both ankles
        // are reachable; world axes are model axes and bone rest frames are
        // unrotated (M5 limits), so a world offset from the pelvis is a model
        // offset from the root bone.
        const auto& legs = stepping.legs();
        const std::size_t rootBone = ragdoll_->bodies[stepping.rootBody()].bone;
        const math::Vec3 rootModel = modelRest_[rootBone].translation;
        double rootZ = pelvisZ_;
        for (std::size_t f = 0; f < 2; ++f)
        {
            const math::Vec3 ankle = f == swing_ ? swingAnkle : planted_[f];
            const math::Vec3 hipOffset = modelRest_[legs[f].hipBone].translation - rootModel;
            const double hx = pelvisXY_.x + hipOffset.x - ankle.x;
            const double hy = pelvisXY_.y + hipOffset.y - ankle.y;
            const double reach = kReachFraction * legs[f].lengthM;
            const double h2 = hx * hx + hy * hy;
            if (h2 < reach * reach)
            {
                rootZ = std::min(rootZ, ankle.z + std::sqrt(reach * reach - h2) - hipOffset.z);
            }
        }
        const math::Vec3 rootWorld{ pelvisXY_.x, pelvisXY_.y, rootZ };
        for (std::size_t f = 0; f < 2; ++f)
        {
            const Stepping::Leg& leg = legs[f];
            const math::Vec3 ankle = f == swing_ ? swingAnkle : planted_[f];
            const ik::TwoBoneChain chain{ modelRest_[leg.hipBone].translation, modelRest_[leg.kneeBone].translation,
                modelRest_[leg.ankleBone].translation };
            const ik::TwoBoneResult solved
                = ik::solveTwoBone(chain, rootModel + (ankle - rootWorld), leg.kneePoleModel, leg.kneeLimits);
            if (!solved.valid)
            {
                continue;
            }
            // The knee is a hinge, so the hip must carry the knee's axis onto the
            // solved bend plane's normal, not just turn the thigh by the shortest
            // arc (which twists the axis out of the plane when the foot is to the
            // side; found by the task 0019 review). Hip: shortest arc for the
            // thigh, then a twist about the new thigh. Knee: a pure rotation
            // about its own axis by the solved bend. Ankle: undoes both, so the
            // foot stays level.
            const math::Vec3 thighRest = math::normalized(chain.mid - chain.root);
            math::Vec3 axisRest = math::rotate(modelRest_[leg.kneeBone].rotation, leg.kneeAxis) * leg.kneeSign;
            axisRest = math::normalized(axisRest - thighRest * math::dot(axisRest, thighRest));
            const math::Vec3 thigh = math::normalized(solved.mid - chain.root);
            const math::Quat swing = math::fromTo(thighRest, thigh);
            math::Quat hip = swing;
            const math::Vec3 bendNormal = math::cross(thigh, solved.end - solved.mid);
            if (math::length(bendNormal) > kMinBendNormal)
            {
                const math::Vec3 from = math::rotate(swing, axisRest);
                const math::Vec3 to = math::normalized(bendNormal);
                const double twist = std::atan2(math::dot(math::cross(from, to), thigh), math::dot(from, to));
                hip = math::normalized(math::fromAxisAngle(thigh, twist) * swing);
            }
            const math::Quat knee = math::normalized(ragdoll_->skeleton.bones[leg.kneeBone].localRest.rotation
                * math::fromAxisAngle(leg.kneeAxis, leg.kneeSign * solved.bendRad));
            localPose_[leg.hipBone].rotation = hip;
            localPose_[leg.kneeBone].rotation = knee;
            localPose_[leg.ankleBone].rotation = math::normalized(math::conjugate(hip * knee));
        }
    }

    void BalanceDriver::massOf(const Stepping& stepping, std::span<const std::uint8_t> flags, bool distal,
        double& massKg, math::Vec3& centreM) const
    {
        massKg = 0.0;
        centreM = math::Vec3{};
        for (std::size_t b = 0; b < flags.size(); ++b)
        {
            if ((flags[b] != 0) != distal)
            {
                continue;
            }
            const double m = stepping.bodyMassKg(b);
            massKg += m;
            centreM = centreM + toMath(states_[b].positionM) * m;
        }
        if (massKg > 0.0)
        {
            centreM = centreM * (1.0 / massKg);
        }
    }

    double BalanceDriver::footTiltRad(std::size_t footBody) const
    {
        const math::Quat fromRest = math::normalized(
            toMath(states_[footBody].rotation) * math::conjugate(ragdoll_->bodies[footBody].restModel));
        const math::Vec3 up = math::rotate(fromRest, math::Vec3{ 0.0, 0.0, 1.0 });
        return std::atan2(std::hypot(up.x, up.y), up.z);
    }

    void BalanceDriver::applyJointTorque(const Stepping& stepping, physics::PhysicsWorld& world,
        std::size_t headSideBody, std::size_t otherBody, math::Vec3 torqueNm, double dtSeconds)
    {
        const double maxNm = stepping.tuning().velocityTuning.maxTorqueNm;
        const double nm = math::length(torqueNm);
        if (!(nm > 0.0))
        {
            return;
        }
        if (nm > maxNm)
        {
            torqueNm = torqueNm * (maxNm / nm);
        }
        lastJointTorqueNm_ = std::max(lastJointTorqueNm_, std::min(nm, maxNm));
        const math::Vec3 angular = torqueNm * dtSeconds;
        for (const auto& [body, sign] : { std::pair{ headSideBody, 1.0 }, std::pair{ otherBody, -1.0 } })
        {
            for (const CoupleImpulse& impulse : jointCoupleImpulses(toMath(states_[body].positionM), angular * sign))
            {
                world.applyImpulse(ragdoll_->bodies[body].id, toPhysics(impulse.impulseNs), toPhysics(impulse.pointM));
            }
        }
    }

    void BalanceDriver::applyJointTorques(const Stepping& stepping, physics::PhysicsWorld& world, double dtSeconds)
    {
        const rig::VelocityTuning& vt = stepping.tuning().velocityTuning;
        const bool braking = phase_ == BalancePhase::Stepping
            || (phase_ == BalancePhase::Settling && sinceTouchdownS_ < vt.afterTouchdownS);
        const sensors::BalanceReading& r = sensor_.reading();
        if (!braking || !r.valid)
        {
            return;
        }
        const auto& legs = stepping.legs();
        const double totalKg = stepping.totalMassKg();
        // Coros Sec. 3.3: F = k_V (0 - v) on the centre of mass. With both feet
        // down each leg takes half; the trunk always takes all of it.
        const math::Vec3 force{ -vt.gainNsPerM * r.centreOfMassVelocityMps.x,
            -vt.gainNsPerM * r.centreOfMassVelocityMps.y, 0.0 };
        const bool singleStance = phase_ == BalancePhase::Stepping;
        for (std::size_t f = 0; f < 2; ++f)
        {
            if (singleStance && f == swing_)
            {
                continue;
            }
            const math::Vec3 legForce = singleStance ? force : force * 0.5;
            const Stepping::Leg& leg = legs[f];
            const bool ankleOn = footTiltRad(leg.footBody) <= vt.ankleOffTiltRad;
            const std::size_t joints[3] = { leg.hipJoint, leg.kneeJoint, leg.ankleJoint };
            const std::size_t children[3] = { leg.thighBody, leg.shinBody, leg.footBody };
            const std::size_t parents[3] = { stepping.rootBody(), leg.thighBody, leg.shinBody };
            for (std::size_t j = 0; j < 3; ++j)
            {
                if (j == 2 && !ankleOn)
                {
                    continue;
                }
                double m = 0.0;
                math::Vec3 c;
                massOf(stepping, stepping.distalBodies(joints[j]), true, m, c);
                const math::Vec3 tau = virtualForceTorque(c, boneOrigin(children[j]), legForce, m / totalKg);
                // The head side of a stance-leg joint is its parent body.
                applyJointTorque(stepping, world, parents[j], children[j], tau, dtSeconds);
            }
        }
        for (const std::size_t joint : stepping.trunkJoints())
        {
            // The head side of a trunk joint is its child body (the bodies above).
            const motor::RagdollJoint& rj = ragdoll_->joints[joint];
            double m = 0.0;
            math::Vec3 c;
            massOf(stepping, stepping.distalBodies(joint), true, m, c);
            const math::Vec3 tau = virtualForceTorque(c, boneOrigin(rj.childBody), force, m / totalKg);
            applyJointTorque(stepping, world, rj.childBody, rj.parentBody, tau, dtSeconds);
        }
        // Swing-leg gravity compensation (Sec. 3.4).
        if (singleStance && stepping.tuning().swingGravityCompensation)
        {
            const Stepping::Leg& leg = legs[swing_];
            const std::size_t joints[2] = { leg.hipJoint, leg.kneeJoint };
            const std::size_t children[2] = { leg.thighBody, leg.shinBody };
            const std::size_t parents[2] = { stepping.rootBody(), leg.thighBody };
            for (std::size_t j = 0; j < 2; ++j)
            {
                double m = 0.0;
                math::Vec3 c;
                massOf(stepping, stepping.distalBodies(joints[j]), false, m, c); // the leg below
                const math::Vec3 tau
                    = math::cross(c - boneOrigin(children[j]), math::Vec3{ 0.0, 0.0, m * sensor_.gravityMps2() });
                applyJointTorque(stepping, world, children[j], parents[j], tau, dtSeconds);
            }
        }
    }

} // namespace opas::behaviors
