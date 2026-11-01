#ifndef OPENEDEN_COMPONENTS_CMPHYSICS_HOSTEDJOLTWORLD_H
#define OPENEDEN_COMPONENTS_CMPHYSICS_HOSTEDJOLTWORLD_H

// CreativeMotion's physics interface (opas::physics::PhysicsWorld) implemented over a Jolt world that somebody else
// owns: the game's PhysicsSystem or Eden Studio's StudioWorld (task 0035, CreativeMotion milestone M10). There is
// never a second Jolt world: this class takes a JPH::PhysicsSystem& and cannot make one.
//
// It repeats the behaviour of the vendored components/creativemotion/physics/jolt/src/JoltWorld.cpp call by call
// (that copy owns its own world and is never patched here, decision 0001), with three differences: lengths are
// converted between the interface's meters and the host's units at one place, bodies carry user data 0 (the game
// reads user data as a PtrHolder pointer), and the host decides who calls PhysicsSystem::Update.
//
// Threads: everything here runs on the main thread only (construction, every call, both listener callbacks). The
// class registers no Jolt callback, so none of its code runs on a Jolt worker thread. Ownership: the host owns the
// PhysicsSystem and must destroy this object before it. No locks: the thread rule is the lock.

// Jolt requires Jolt.h before any other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include <opas/physics/PhysicsWorld.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace CMPhysics
{
    /// Runs the host's PhysicsSystem::Update for HostedJoltWorld::step(). Not owned by the adapter.
    class SystemStepper
    {
    public:
        virtual ~SystemStepper() = default;
        virtual void update(float dtSeconds) = 0;
    };

    struct HostedWorldConfig
    {
        /// Length unit of the host's world (the game and the editor: Morrowind units per meter). The one place
        /// meters become world units.
        float unitsPerMeter = 1.0f;
        /// What fixedStepSeconds() returns and the dt listeners get: the host's physics step.
        float stepSeconds = 1.0f / 60.0f;
        /// Object layer of Dynamic and Kinematic bodies.
        JPH::ObjectLayer movingLayer = 0;
        /// Object layer of Static bodies. Empty: this host does not let CreativeMotion make Static bodies.
        std::optional<JPH::ObjectLayer> staticLayer;
        std::uint32_t maxBodies = 1024;
        std::uint32_t maxJoints = 1024;
        /// Multiplies the host system's gravity for every body the adapter creates. The game's Jolt gravity points +Z
        /// (actors fall by their own code), so it passes -1; the editor keeps 1. Must be finite.
        float gravityFactor = 1.0f;
        /// Filters for castRay, not owned, must outlive the adapter. Null: everything.
        const JPH::BroadPhaseLayerFilter* rayBroadPhaseFilter = nullptr;
        const JPH::ObjectLayerFilter* rayObjectLayerFilter = nullptr;
        /// Null: step() does nothing (the host calls beginStep() and endStep() around its own Update).
        SystemStepper* stepper = nullptr;
    };

    class HostedJoltWorld final : public opas::physics::PhysicsWorld
    {
    public:
        /// Returns nullptr, and a message in *error if given, for an invalid config. Main thread.
        static std::unique_ptr<HostedJoltWorld> create(
            JPH::PhysicsSystem& system, const HostedWorldConfig& config, std::string* error = nullptr);

        ~HostedJoltWorld() override;

        /// For hosts that call PhysicsSystem::Update themselves: pre-step listeners, then (host) Update, then
        /// endStep(). Does nothing while already in a step.
        void beginStep();
        /// Stops kinematic bodies at their targets, runs post-step listeners, ends the step. Does nothing outside
        /// a step.
        void endStep();

        opas::physics::CreateResult<opas::physics::BodyId> createBody(const opas::physics::BodyDesc& desc) override;
        bool destroyBody(opas::physics::BodyId body) override;
        opas::physics::CreateResult<opas::physics::JointId> createJoint(const opas::physics::JointDesc& desc) override;
        bool destroyJoint(opas::physics::JointId joint) override;
        opas::physics::CreateResult<opas::physics::CollisionGroupId> createCollisionGroup(
            std::uint32_t subGroupCount) override;
        bool setSubGroupsCollide(
            opas::physics::CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide) override;
        bool destroyCollisionGroup(opas::physics::CollisionGroupId group) override;
        bool setJointMotor(opas::physics::JointId joint, const opas::physics::JointMotor& motor) override;
        bool setJointPositionSteps(opas::physics::JointId joint, std::uint32_t steps) override;
        bool setJointTarget(opas::physics::JointId joint, opas::physics::Quat targetRotation) override;
        bool setJointTarget(opas::physics::JointId joint, opas::physics::Quat targetRotation,
            opas::physics::Vec3 targetAngularVelocityRadps) override;
        bool applyImpulse(
            opas::physics::BodyId body, opas::physics::Vec3 impulseNs, opas::physics::Vec3 worldPointM) override;
        bool setVelocity(
            opas::physics::BodyId body, opas::physics::Vec3 linearMps, opas::physics::Vec3 angularRadps) override;
        bool setBodyDamping(opas::physics::BodyId body, float linearPerS, float angularPerS) override;
        bool setKinematicTarget(
            opas::physics::BodyId body, opas::physics::Vec3 positionM, opas::physics::Quat rotation) override;
        bool setMotionType(opas::physics::BodyId body, opas::physics::MotionType motion) override;
        std::optional<opas::physics::MotionType> getMotionType(opas::physics::BodyId body) const override;
        bool readBody(opas::physics::BodyId body, opas::physics::BodyState& out) const override;
        // Entries of stale handles are left untouched; the return value is how many were read.
        std::size_t readBodies(
            std::span<const opas::physics::BodyId> bodies, std::span<opas::physics::BodyState> out) const override;
        std::optional<opas::physics::RayHit> castRay(opas::physics::Vec3 originM,
            opas::physics::Vec3 directionTimesLengthM, opas::physics::BodyId ignore = {}) const override;
        /// With a stepper: beginStep(), stepper->update(stepSeconds), endStep(). Without one: does nothing.
        void step() override;
        [[nodiscard]] float fixedStepSeconds() const override { return mConfig.stepSeconds; }
        opas::physics::StepListenerId addStepListener(opas::physics::StepListener& listener) override;
        bool removeStepListener(opas::physics::StepListenerId id) override;

    private:
        struct BodySlot
        {
            JPH::BodyID joltId;
            std::uint32_t generation = 1;
            bool alive = false;
            opas::physics::MotionType motion = opas::physics::MotionType::Static;
            bool kinematicTargetPending = false;
        };

        // A cone joint is a SwingTwistConstraint, a pyramid joint a SixDOFConstraint (see JoltWorld.cpp).
        struct JointSlot
        {
            JPH::Ref<JPH::TwoBodyConstraint> constraint;
            opas::physics::SwingShape shape = opas::physics::SwingShape::Cone;
            std::uint32_t generation = 1;
            bool alive = false;
            std::uint32_t bodyA = 0; // body slot indices
            std::uint32_t bodyB = 0;
        };

        struct GroupSlot
        {
            JPH::Ref<JPH::GroupFilterTable> table;
            std::uint32_t generation = 1;
            bool alive = false;
            std::uint32_t subGroupCount = 0;
            JPH::CollisionGroup::GroupID joltGroupId = 0;
        };

        struct ListenerEntry
        {
            opas::physics::StepListener* listener = nullptr;
            opas::physics::StepListenerId id;
        };

        HostedJoltWorld(JPH::PhysicsSystem& system, const HostedWorldConfig& config);

        const BodySlot* findBody(opas::physics::BodyId body) const;
        JointSlot* findJoint(opas::physics::JointId joint);
        GroupSlot* findGroup(opas::physics::CollisionGroupId group);
        void destroyJointSlot(std::uint32_t index);
        void wakeJointBodies(const JointSlot& slot);
        JPH::BodyInterface& bodies() { return mSystem.GetBodyInterface(); }
        const JPH::BodyInterface& bodies() const { return mSystem.GetBodyInterface(); }

        JPH::PhysicsSystem& mSystem; // the host's, not owned
        HostedWorldConfig mConfig;
        std::vector<BodySlot> mBodySlots;
        std::vector<std::uint32_t> mFreeBodySlots;
        std::vector<std::uint32_t> mBodyIndexToSlot; // by JPH::BodyID::GetIndex(), for castRay
        std::vector<JointSlot> mJointSlots;
        std::vector<std::uint32_t> mFreeJointSlots;
        std::vector<GroupSlot> mGroupSlots;
        std::vector<std::uint32_t> mFreeGroupSlots;
        std::vector<std::uint32_t> mKinematicTargets;
        std::vector<ListenerEntry> mListeners;
        std::uint32_t mNextListenerSerial = 0;
        std::size_t mStepListenerCount = 0;
        bool mInStep = false;
    };
}

#endif
