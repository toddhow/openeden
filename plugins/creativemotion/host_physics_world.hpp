#pragma once

// Adapter implementing opas::physics::PhysicsWorld over OpenEden's shared
// openeden::sdk::PhysicsService (Design Doc Section 4.4, CreativeMotion M8).
// Never creates a second Jolt PhysicsSystem.

#include <openeden/physics.hpp>
#include <opas/physics/PhysicsWorld.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openeden::creativemotion
{
    class HostPhysicsWorld final : public opas::physics::PhysicsWorld
    {
    public:
        explicit HostPhysicsWorld(openeden::sdk::PhysicsService& service);
        ~HostPhysicsWorld() override = default;

        HostPhysicsWorld(const HostPhysicsWorld&) = delete;
        HostPhysicsWorld& operator=(const HostPhysicsWorld&) = delete;
        HostPhysicsWorld(HostPhysicsWorld&&) = delete;
        HostPhysicsWorld& operator=(HostPhysicsWorld&&) = delete;

        // --- Build ---
        opas::physics::CreateResult<opas::physics::BodyId> createBody(
            const opas::physics::BodyDesc& desc) override;
        bool destroyBody(opas::physics::BodyId body) override;

        opas::physics::CreateResult<opas::physics::JointId> createJoint(
            const opas::physics::JointDesc& desc) override;
        bool destroyJoint(opas::physics::JointId joint) override;

        // --- Collision groups ---
        opas::physics::CreateResult<opas::physics::CollisionGroupId> createCollisionGroup(
            std::uint32_t subGroupCount) override;
        bool setSubGroupsCollide(opas::physics::CollisionGroupId group, std::uint32_t a,
            std::uint32_t b, bool collide) override;
        bool destroyCollisionGroup(opas::physics::CollisionGroupId group) override;

        // --- Drive joints ---
        bool setJointMotor(
            opas::physics::JointId joint, const opas::physics::JointMotor& motor) override;
        bool setJointPositionSteps(opas::physics::JointId joint, std::uint32_t steps) override;
        bool setJointTarget(
            opas::physics::JointId joint, opas::physics::Quat targetRotation) override;
        bool setJointTarget(opas::physics::JointId joint, opas::physics::Quat targetRotation,
            opas::physics::Vec3 targetAngularVelocityRadps) override;

        // --- Drive bodies ---
        bool applyImpulse(opas::physics::BodyId body, opas::physics::Vec3 impulseNs,
            opas::physics::Vec3 worldPointM) override;
        bool setVelocity(opas::physics::BodyId body, opas::physics::Vec3 linearMps,
            opas::physics::Vec3 angularRadps) override;
        bool setBodyDamping(
            opas::physics::BodyId body, float linearPerS, float angularPerS) override;
        bool setKinematicTarget(opas::physics::BodyId body, opas::physics::Vec3 positionM,
            opas::physics::Quat rotation) override;

        // --- Motion type ---
        bool setMotionType(opas::physics::BodyId body, opas::physics::MotionType motion) override;
        [[nodiscard]] std::optional<opas::physics::MotionType> getMotionType(
            opas::physics::BodyId body) const override;

        // --- Read ---
        bool readBody(opas::physics::BodyId body, opas::physics::BodyState& out) const override;
        std::size_t readBodies(std::span<const opas::physics::BodyId> bodies,
            std::span<opas::physics::BodyState> out) const override;

        // --- Query ---
        [[nodiscard]] std::optional<opas::physics::RayHit> castRay(opas::physics::Vec3 originM,
            opas::physics::Vec3 directionTimesLengthM,
            opas::physics::BodyId ignore = {}) const override;

        // --- Step & Listeners ---
        void step() override;
        [[nodiscard]] float fixedStepSeconds() const override;

        opas::physics::StepListenerId addStepListener(
            opas::physics::StepListener& listener) override;
        bool removeStepListener(opas::physics::StepListenerId id) override;

        // Invoked by CreativeMotionPlugin during FixedStepPrePhysics and FixedStepPostPhysics
        void runPreStepListeners(float dtSeconds);
        void runPostStepListeners(float dtSeconds);

        [[nodiscard]] openeden::sdk::PhysicsService& service() noexcept { return mService; }
        [[nodiscard]] const openeden::sdk::PhysicsService& service() const noexcept
        {
            return mService;
        }

    private:
        struct ListenerEntry
        {
            opas::physics::StepListener* listener = nullptr;
            opas::physics::StepListenerId id{};
        };

        openeden::sdk::PhysicsService& mService;
        std::vector<ListenerEntry> mListeners;
        std::uint32_t mNextListenerSerial = 0;
        bool mInStep = false;
    };
} // namespace openeden::creativemotion
