#pragma once

#include <openeden/entity.hpp>
#include <openeden/physics.hpp>
#include <openeden/version.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace openeden::contracts
{
    /**
     * @brief Versioned contract for procedural animation and physical ragdoll characters.
     *
     * Provided by CreativeMotion ("creativemotion") under contract ID "openeden.animation".
     * Contains only SDK and standard library types in its interface.
     */
    class AnimationContract
    {
    public:
        static constexpr std::string_view kId = "openeden.animation";
        static constexpr sdk::Version kVersion{ 1, 1 };

        virtual ~AnimationContract() = default;

        /**
         * @brief Returns true if procedural animation / physical ragdoll simulation is enabled.
         * Defaults to false (off-by-default invariant).
         */
        [[nodiscard]] virtual bool isEnabled() const noexcept = 0;

        /**
         * @brief Enables or disables active procedural animation processing at runtime.
         */
        virtual void setEnabled(bool enabled) = 0;

        /**
         * @brief Returns the number of active character ragdoll instances managed by the provider.
         */
        [[nodiscard]] virtual std::size_t getActiveCharacterCount() const noexcept = 0;

        /**
         * @brief Spawns a physical ragdoll and reconciliation harness for the given entity
         * inside the host's shared physics world.
         */
        virtual bool spawnCharacter(
            sdk::EntityId entity, sdk::PhysicsVec3 rootPositionM = {}, float yawRad = 0.0f) = 0;

        /**
         * @brief Despawns the physical ragdoll and frees physics bodies/joints for the entity.
         */
        virtual bool despawnCharacter(sdk::EntityId entity) = 0;

        /**
         * @brief Returns true if the entity currently has an active character ragdoll.
         */
        [[nodiscard]] virtual bool hasCharacter(sdk::EntityId entity) const noexcept = 0;

        /**
         * @brief Applies a physical hit impulse to the character's ragdoll at the specified world point.
         */
        virtual bool applyHitImpulse(
            sdk::EntityId entity, sdk::PhysicsVec3 impulseNs, sdk::PhysicsVec3 contactPointM) = 0;

        /**
         * @brief Queries the current physical pelvis world position in meters.
         */
        [[nodiscard]] virtual std::optional<sdk::PhysicsVec3> getPelvisPosition(sdk::EntityId entity) const = 0;

        /**
         * @brief Queries the reconciled gameplay capsule center position in meters.
         */
        [[nodiscard]] virtual std::optional<sdk::PhysicsVec3> getReconciledCapsulePosition(
            sdk::EntityId entity) const = 0;

        /**
         * @brief Returns the current arbiter state name (e.g., "Standing", "Leaning", "Stepping", "Falling", "Fallen").
         */
        [[nodiscard]] virtual std::string_view getCharacterStateName(sdk::EntityId entity) const = 0;
    };
} // namespace openeden::contracts
