#pragma once

#include <openeden/physics.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace JPH
{
    class PhysicsSystem;
}

namespace openeden::plugin
{
    class CollisionLayerRegistry;

    /**
     * @brief Engine implementation of PhysicsService backed by the shared Jolt PhysicsSystem.
     *
     * Can either manage an internal Jolt PhysicsSystem (for standalone/headless hosts)
     * or attach to an external JPH::PhysicsSystem owned by MWPhysics::PhysicsSystem.
     */
    class PhysicsServiceImpl : public openeden::sdk::PhysicsService
    {
    public:
        explicit PhysicsServiceImpl(
            const openeden::sdk::PhysicsWorldConfig& config = openeden::sdk::PhysicsWorldConfig{});
        explicit PhysicsServiceImpl(
            JPH::PhysicsSystem* externalSystem,
            const openeden::sdk::PhysicsWorldConfig& config = openeden::sdk::PhysicsWorldConfig{});
        ~PhysicsServiceImpl() override;

        PhysicsServiceImpl(const PhysicsServiceImpl&) = delete;
        PhysicsServiceImpl& operator=(const PhysicsServiceImpl&) = delete;
        PhysicsServiceImpl(PhysicsServiceImpl&&) noexcept;
        PhysicsServiceImpl& operator=(PhysicsServiceImpl&&) noexcept;

        // --- Build ---
        openeden::sdk::CreateResult<openeden::sdk::BodyId> createBody(
            const openeden::sdk::BodyDesc& desc) override;
        bool destroyBody(openeden::sdk::BodyId body) override;
        [[nodiscard]] bool isBodyValid(openeden::sdk::BodyId id) const override;

        openeden::sdk::CreateResult<openeden::sdk::JointId> createJoint(
            const openeden::sdk::JointDesc& desc) override;
        bool destroyJoint(openeden::sdk::JointId joint) override;
        [[nodiscard]] bool isConstraintValid(openeden::sdk::ConstraintId id) const override;

        // --- Collision groups ---
        openeden::sdk::CreateResult<openeden::sdk::CollisionGroupId> createCollisionGroup(
            std::uint32_t subGroupCount) override;
        bool setSubGroupsCollide(
            openeden::sdk::CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide) override;
        bool destroyCollisionGroup(openeden::sdk::CollisionGroupId group) override;

        // --- Named collision layers ---
        openeden::sdk::CollisionLayer registerCollisionLayer(std::string_view name) override;
        void declareCollisionPair(std::string_view layerA, std::string_view layerB) override;
        void setCollisionLayerRegistry(CollisionLayerRegistry* registry) noexcept;

        // --- Drive joints ---
        bool setJointMotor(openeden::sdk::JointId joint, const openeden::sdk::JointMotor& motor) override;
        bool setJointPositionSteps(openeden::sdk::JointId joint, std::uint32_t steps) override;
        bool setJointTarget(
            openeden::sdk::JointId joint, openeden::sdk::PhysicsQuat targetRotation) override;
        bool setJointTarget(openeden::sdk::JointId joint, openeden::sdk::PhysicsQuat targetRotation,
            openeden::sdk::PhysicsVec3 targetAngularVelocityRadps) override;

        // --- Drive bodies ---
        bool applyImpulse(openeden::sdk::BodyId body, openeden::sdk::PhysicsVec3 impulseNs,
            openeden::sdk::PhysicsVec3 worldPointM) override;
        bool setVelocity(openeden::sdk::BodyId body, openeden::sdk::PhysicsVec3 linearMps,
            openeden::sdk::PhysicsVec3 angularRadps) override;
        bool setBodyDamping(openeden::sdk::BodyId body, float linearPerS, float angularPerS) override;
        bool setKinematicTarget(openeden::sdk::BodyId body, openeden::sdk::PhysicsVec3 positionM,
            openeden::sdk::PhysicsQuat rotation) override;

        // --- Motion type ---
        bool setMotionType(openeden::sdk::BodyId body, openeden::sdk::MotionType motion) override;
        [[nodiscard]] std::optional<openeden::sdk::MotionType> getMotionType(
            openeden::sdk::BodyId body) const override;

        // --- Read ---
        bool readBody(openeden::sdk::BodyId body, openeden::sdk::BodyState& out) const override;
        std::size_t readBodies(std::span<const openeden::sdk::BodyId> bodies,
            std::span<openeden::sdk::BodyState> out) const override;

        // --- Query ---
        [[nodiscard]] std::optional<openeden::sdk::RayHit> castRay(openeden::sdk::PhysicsVec3 originM,
            openeden::sdk::PhysicsVec3 directionTimesLengthM,
            openeden::sdk::BodyId ignore = {}) const override;

        // --- Step & Timing ---
        void step(float dtSeconds = 0.0f) override;
        [[nodiscard]] float fixedStepSeconds() const override;

        // Engine-internal control for step callback guard
        void setInStep(bool inStep) noexcept;
        [[nodiscard]] bool isInStep() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
} // namespace openeden::plugin
