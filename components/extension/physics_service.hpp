#pragma once

#include <openeden/physics.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace JPH
{
    class BodyID;
    class PhysicsSystem;
}

namespace openeden::extension
{
    /**
     * @brief What the game knows about which body belongs to which entity. Implemented by the host.
     */
    class EntityBodies
    {
    public:
        virtual ~EntityBodies() = default;
        [[nodiscard]] virtual openeden::sdk::BodyId bodyOfEntity(openeden::sdk::EntityId entity) const = 0;
        [[nodiscard]] virtual openeden::sdk::EntityId entityOfBody(openeden::sdk::BodyId body) const = 0;
    };

    /**
     * @brief Engine implementation of PhysicsQueryService: a read-only view of a Jolt world.
     *
     * It only ever attaches to a JPH::PhysicsSystem the host owns and cannot create its own. The
     * host keeps that world alive for as long as this object exists, and has initialized Jolt
     * (allocator, Factory, types) first. Queries use Jolt's locking interfaces.
     *
     * A world object's layer takes part in query masks as a bit pattern: a body matches when
     * (mask & objectLayer) != 0, which is how MWPhysics::Layers values are defined. kQueryMaskAll
     * matches every body, including one in object layer 0.
     */
    class PhysicsServiceImpl : public openeden::sdk::PhysicsQueryService
    {
    public:
        /**
         * @param worldUnitsPerMeter The world's length unit. This class is the one boundary between it and the
         * SDK's meters; the game's world is in Morrowind units (task 0017).
         */
        explicit PhysicsServiceImpl(JPH::PhysicsSystem& world, float worldUnitsPerMeter = 1.0f);

        /**
         * @brief Converts a Jolt body ID into the handle extensions use.
         */
        [[nodiscard]] static openeden::sdk::BodyId toBodyId(const JPH::BodyID& id);

        /**
         * @brief Publishes a mask under a name extensions can look up with queryMask(). Replaces an earlier value.
         * Call it before any extension starts: lookups are not synchronized with it.
         */
        void publishQueryMask(std::string_view name, openeden::sdk::QueryMask mask);

        /**
         * @brief Answers bodyOfEntity and entityOfBody. Null, the default, answers nothing.
         */
        void setEntityBodies(EntityBodies* bodies) noexcept { mEntityBodies = bodies; }

        [[nodiscard]] openeden::sdk::BodyId bodyOfEntity(openeden::sdk::EntityId entity) const override;
        [[nodiscard]] openeden::sdk::EntityId entityOfBody(openeden::sdk::BodyId body) const override;

        [[nodiscard]] bool isBodyValid(openeden::sdk::BodyId id) const override;
        [[nodiscard]] std::optional<openeden::sdk::MotionType> getMotionType(openeden::sdk::BodyId body) const override;

        bool readBody(openeden::sdk::BodyId body, openeden::sdk::BodyState& out) const override;
        std::size_t readBodies(
            std::span<const openeden::sdk::BodyId> bodies, std::span<openeden::sdk::BodyState> out) const override;

        [[nodiscard]] openeden::sdk::QueryMask queryMask(std::string_view name) const override;

        [[nodiscard]] std::optional<openeden::sdk::RayHit> castRay(openeden::sdk::PhysicsVec3 originM,
            openeden::sdk::PhysicsVec3 directionTimesLengthM, openeden::sdk::BodyId ignore = {},
            openeden::sdk::QueryMask mask = openeden::sdk::kQueryMaskAll) const override;

        [[nodiscard]] std::optional<openeden::sdk::RayHit> castShape(const openeden::sdk::PhysicsShape& shape,
            openeden::sdk::PhysicsQuat rotation, openeden::sdk::PhysicsVec3 originM,
            openeden::sdk::PhysicsVec3 directionTimesLengthM, openeden::sdk::BodyId ignore = {},
            openeden::sdk::QueryMask mask = openeden::sdk::kQueryMaskAll) const override;

    private:
        JPH::PhysicsSystem* mWorld;
        EntityBodies* mEntityBodies = nullptr;
        float mWorldUnitsPerMeter;
        std::map<std::string, openeden::sdk::QueryMask, std::less<>> mQueryMasks;
    };
} // namespace openeden::extension
