#pragma once

#include <openeden/contracts/animation_contract.hpp>
#include <openeden/host.hpp>
#include <openeden/physics.hpp>
#include <openeden/plugin.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace openeden::creativemotion
{
    class HostPhysicsWorld;

    /**
     * @brief CreativeMotion feature plugin implementing procedural animation,
     * motor-driven ragdolls, balance/recovery behaviors, and capsule reconciliation
     * over OpenEden's shared Jolt physics service.
     */
    class CreativeMotionPlugin final : public openeden::sdk::Plugin,
                                       public openeden::contracts::AnimationContract
    {
    public:
        CreativeMotionPlugin();
        ~CreativeMotionPlugin() override;

        CreativeMotionPlugin(const CreativeMotionPlugin&) = delete;
        CreativeMotionPlugin& operator=(const CreativeMotionPlugin&) = delete;

        // --- openeden::sdk::Plugin ---
        void registerServices(openeden::sdk::Registrar& registrar) override;
        bool resolve(const openeden::sdk::ServiceLocator& locator) override;
        void start(openeden::sdk::Host& host) override;
        void stop() override;

        // --- openeden::contracts::AnimationContract ---
        [[nodiscard]] bool isEnabled() const noexcept override;
        void setEnabled(bool enabled) override;
        [[nodiscard]] std::size_t getActiveCharacterCount() const noexcept override;

        bool spawnCharacter(openeden::sdk::EntityId entity,
            openeden::sdk::PhysicsVec3 rootPositionM = {}, float yawRad = 0.0f) override;
        bool despawnCharacter(openeden::sdk::EntityId entity) override;
        [[nodiscard]] bool hasCharacter(openeden::sdk::EntityId entity) const noexcept override;

        bool applyHitImpulse(openeden::sdk::EntityId entity, openeden::sdk::PhysicsVec3 impulseNs,
            openeden::sdk::PhysicsVec3 contactPointM) override;

        [[nodiscard]] std::optional<openeden::sdk::PhysicsVec3> getPelvisPosition(
            openeden::sdk::EntityId entity) const override;
        [[nodiscard]] std::optional<openeden::sdk::PhysicsVec3> getReconciledCapsulePosition(
            openeden::sdk::EntityId entity) const override;
        [[nodiscard]] std::string_view getCharacterStateName(
            openeden::sdk::EntityId entity) const override;

        // --- Plugin Diagnostics & Testing Accessors ---
        [[nodiscard]] HostPhysicsWorld* getHostPhysicsWorld() noexcept;
        [[nodiscard]] const HostPhysicsWorld* getHostPhysicsWorld() const noexcept;
        [[nodiscard]] openeden::sdk::CollisionLayer getRagdollCollisionLayer() const noexcept;
        [[nodiscard]] openeden::sdk::CollisionLayer getWorldCollisionLayer() const noexcept;
        [[nodiscard]] std::uint64_t getFixedStepCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
} // namespace openeden::creativemotion
