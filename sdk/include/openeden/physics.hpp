#pragma once

#include <openeden/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace openeden::sdk
{
    /**
     * @brief 3D vector in SI units (meters, m/s, rad/s), right-handed Z-up.
     */
    struct PhysicsVec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        friend bool operator==(const PhysicsVec3&, const PhysicsVec3&) = default;
    };

    /**
     * @brief Unit quaternion representing orientation (default identity).
     */
    struct PhysicsQuat
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        friend bool operator==(const PhysicsQuat&, const PhysicsQuat&) = default;
    };

    /**
     * @brief Generation-counted opaque handle to a physics resource.
     */
    template <typename Tag>
    struct PhysicsHandle
    {
        static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t index = kInvalidIndex;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool isValid() const noexcept { return index != kInvalidIndex; }

        friend bool operator==(const PhysicsHandle&, const PhysicsHandle&) = default;
    };

    using BodyId = PhysicsHandle<struct BodyTag>;

    /**
     * @brief Selects which kinds of body a query can hit.
     *
     * Treat the value as opaque: get one from PhysicsQueryService::queryMask(name), combine
     * values with |, and never assume bit numbers. The host publishes the names it supports.
     */
    using QueryMask = std::uint32_t;

    /// Matches every body in the world, whatever its kind.
    inline constexpr QueryMask kQueryMaskAll = std::numeric_limits<QueryMask>::max();

    /// Matches nothing. queryMask() returns it for a name the host did not publish.
    inline constexpr QueryMask kQueryMaskNone = 0;

    struct BoxShape
    {
        PhysicsVec3 halfExtentsM{ 0.5f, 0.5f, 0.5f };
    };

    struct SphereShape
    {
        float radiusM = 0.5f;
    };

    /**
     * @brief Capsule aligned along the cast's local Z axis (Z-up).
     * Total length is 2 * (halfHeightM + radiusM).
     */
    struct CapsuleShape
    {
        float halfHeightM = 0.5f;
        float radiusM = 0.25f;
    };

    using PhysicsShape = std::variant<BoxShape, SphereShape, CapsuleShape>;

    enum class MotionType : std::uint8_t
    {
        Static = 0,
        Dynamic,
        Kinematic,
    };

    struct BodyState
    {
        PhysicsVec3 positionM{};
        PhysicsQuat rotation{};
        PhysicsVec3 linearVelocityMps{};
        PhysicsVec3 angularVelocityRadps{};
        bool awake = false;
    };

    /**
     * @brief Result of castRay and castShape.
     *
     * pointM is where the ray or shape touches the body, and fraction is how far along
     * directionTimesLengthM the hit is, in [0, 1]. A shape cast's normal points from the body towards
     * the cast. A ray's normal is the body's surface normal, which faces away from the ray's start if
     * it hit the back of a mesh or started inside a body. Back faces count as hits for both, and a
     * shape that already overlaps a body at its start hits with fraction 0.
     */
    struct RayHit
    {
        BodyId body{};
        PhysicsVec3 pointM{};
        PhysicsVec3 normal{};
        float fraction = 0.0f;
    };

    /**
     * @brief Read-only view of the core's single Jolt world (decision 0003).
     *
     * Extensions may read body states and cast rays and shapes. They never create, move or step
     * anything: systems that must change physics belong in the core. Handles an extension holds go
     * stale when the core destroys the body; every call rejects a stale handle safely.
     */
    class PhysicsQueryService
    {
    public:
        virtual ~PhysicsQueryService() = default;

        [[nodiscard]] virtual bool isBodyValid(BodyId id) const = 0;
        [[nodiscard]] virtual std::optional<MotionType> getMotionType(BodyId body) const = 0;

        /**
         * @brief Reads one body's state. Returns false, leaving out untouched, for a stale handle.
         */
        virtual bool readBody(BodyId body, BodyState& out) const = 0;

        /**
         * @brief Reads min(bodies.size(), out.size()) bodies. Returns how many were valid.
         */
        virtual std::size_t readBodies(std::span<const BodyId> bodies, std::span<BodyState> out) const = 0;

        /**
         * @brief Returns the mask the host published under name, or kQueryMaskNone if it did not.
         */
        [[nodiscard]] virtual QueryMask queryMask(std::string_view name) const = 0;

        /**
         * @brief Finds the closest body along a ray. Non-finite input finds nothing; do not rely on a zero-length ray.
         */
        [[nodiscard]] virtual std::optional<RayHit> castRay(PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM,
            BodyId ignore = {}, QueryMask mask = kQueryMaskAll) const = 0;

        /**
         * @brief Sweeps a shape and finds the closest body it touches.
         *
         * originM and rotation place the shape's centre at the start of the sweep. A shape with a
         * non-positive or non-finite dimension, a non-unit rotation or non-finite input finds nothing.
         */
        [[nodiscard]] virtual std::optional<RayHit> castShape(const PhysicsShape& shape, PhysicsQuat rotation,
            PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM, BodyId ignore = {},
            QueryMask mask = kQueryMaskAll) const = 0;

        /**
         * @brief The body of an entity, or an invalid handle if it has none or the host cannot tell.
         */
        [[nodiscard]] virtual BodyId bodyOfEntity(EntityId /*entity*/) const { return {}; }

        /**
         * @brief The entity a body belongs to, or an invalid ID if it has none (terrain, for one).
         */
        [[nodiscard]] virtual EntityId entityOfBody(BodyId /*body*/) const { return {}; }
    };
} // namespace openeden::sdk
