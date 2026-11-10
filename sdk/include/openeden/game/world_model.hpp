#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// The neutral world model's value types (decision 0016, task 0099 stage N0; docs/design/neutral-world-model.md).
//
// A cell, the objects placed in it and what the engine needs to put them in a scene: a model, a transform, a
// collision hint, a kind tag, a few optional neutral components and an opaque payload the pack owns. Every game
// pack fills these from its own store (Skyrim SE: game_skyrimse::loadNeutralCell); the core will build its scene from
// them instead of from any one game's records (stages N1 to N4 of the design document). The types have no record type
// of any game and no engine, renderer, Jolt, Qt or third-party type. This header is TYPES ONLY: no function in it does
// anything, so a contract carries no implementation (the guide's architecture rules). Validation and the conversion
// from a game's own conventions are the packs' job (the pack knows its Euler order and its units).
//
// Relation to world_view.hpp: WorldView is the pull-style read interface a pack offers over its store (lists, lookups,
// a cell on demand, game-flavoured values). WorldCell here is the engine-facing result of loading one cell: the same
// ids and kinds (this header reuses WorldId, BaseKind and WorldVec3), but with a canonical double precision transform,
// optional components and the payload slot. A pack that implements WorldView can fill a WorldCell from it.
//
// Main thread only (decided in stage N2: cells are built on the main thread, just ahead of use). Plain values:
// copyable, comparable, and equal when built twice from the same data.

namespace openeden::sdk::game
{
    /// This header's own version. FROZEN at 1.0 by stage N2 of the design document (task 0102): the policy below
    /// applies from here on, and CI (CI/check_extension_sdk.sh and the layout pins in worldmodel/testfrozen.cpp)
    /// keeps it. 0.1 and 0.2 were drafts.
    ///
    /// Version policy. An additive change (a new member at the END of a struct that a consumer only reads, a new
    /// enumerator at the END of an enumeration, a new type) raises the minor number. A change to the layout or meaning
    /// of anything that exists raises the major number. CI (CI/check_extension_sdk.sh) keeps this paragraph in place.
    inline constexpr Version kWorldModelVersion{ 1, 0 };

    /// What kind of thing an object is, as far as the engine's generic paths need to tell. The same set as BaseKind
    /// (a pack maps its record types onto it exactly as it does for WorldView); new kinds are added at the end.
    using ObjectKind = BaseKind;

    struct Vec3d
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        friend bool operator==(const Vec3d&, const Vec3d&) = default;
    };

    /// A rotation as a unit quaternion (x, y, z, w), the active rotation of the object's own frame into the cell's
    /// frame: the rotated vector is q v q^-1, so a quarter turn about +Z takes +X to +Y when the quaternion is
    /// (0, 0, sin 45, cos 45). The identity is (0, 0, 0, 1). The pack converts its game's stored angles to this at ONE
    /// named place, because the games combine their Euler angles in different orders (Morrowind: Rx * Ry * Rz with
    /// every angle negated, Z first; Skyrim SE: Rz * Ry * Rx with every angle negated, X first); the quaternion is the
    /// only shared form and no consumer sees the angles.
    struct Quatd
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 1.0;

        friend bool operator==(const Quatd&, const Quatd&) = default;
    };

    /// Position in the game's own units (right-handed, Z up), a rotation and a uniform scale. The length of a unit in
    /// metres is a property of the game, not of the object: WorldCellSource::unitsPerMetre.
    struct WorldTransform
    {
        Vec3d position;
        Quatd rotation;
        double scale = 1.0;

        friend bool operator==(const WorldTransform&, const WorldTransform&) = default;
    };

    /// Where an object's collision comes from.
    enum class CollisionSource : std::uint8_t
    {
        None, ///< The object does not collide
        FromModel, ///< The engine derives the shape from the model (what it does for every placed object today)
        Box, ///< The bounds below, in the object's own frame
    };

    struct CollisionHint
    {
        CollisionSource source = CollisionSource::None;
        /// The base's axis-aligned bounding box in the object's OWN frame (before the transform is applied), when the
        /// game stores one; it may accompany any source (Skyrim SE gives it to model objects too). Required for Box.
        Vec3d boundsMin; ///< Only meaningful when `hasBounds`
        Vec3d boundsMax;
        bool hasBounds = false;

        friend bool operator==(const CollisionHint&, const CollisionHint&) = default;
    };

    /// Who owns an object (a faction or a person), as an opaque id of the pack.
    struct OwnerComponent
    {
        WorldId owner = kNoWorldId;
        std::int32_t requiredRank = -1; ///< -1 when no rank is required

        friend bool operator==(const OwnerComponent&, const OwnerComponent&) = default;
    };

    struct LockComponent
    {
        bool locked = false;
        std::int32_t level = 0; ///< The game's lock level, 0 when it has none
        WorldId key = kNoWorldId;

        friend bool operator==(const LockComponent&, const LockComponent&) = default;
    };

    /// A door (or other portal) that leads somewhere.
    struct TeleportComponent
    {
        WorldId destinationCell = kNoWorldId;
        WorldId destinationDoor = kNoWorldId;
        WorldTransform destination;
        /// The game's own flag word for the portal (Skyrim SE: XTEL flags; bit 0 is "no alarm" in the CK
        /// documentation). Opaque to the core; added in 0.2.
        std::uint32_t flags = 0;

        friend bool operator==(const TeleportComponent&, const TeleportComponent&) = default;
    };

    /// Something only the pack understands, kept with the object and given back to the pack. The core never reads it.
    /// `handle` is the pack's own number; zero means none. It is not a pointer. What it means is the pack's business
    /// and differs per game (Skyrim SE: the base form id; the Morrowind projection: the reference number, content
    /// file in the high half), so a consumer may only hand it back to the pack that wrote it.
    struct PackPayload
    {
        std::uint64_t handle = 0;

        friend bool operator==(const PackPayload&, const PackPayload&) = default;
    };

    struct WorldObject
    {
        WorldId id = kNoWorldId; ///< The placement's own id, opaque, minted by the pack
        WorldId base = kNoWorldId; ///< The base form's id, none if the placement names none
        ObjectKind kind = ObjectKind::Unknown;
        std::string baseEditorId;
        /// The model as the data stores it, relative to the game's mesh root, with the game's own separators. Empty
        /// when the object has none.
        std::string modelPath;
        CollisionHint collision;
        WorldTransform transform;
        bool persistent = false;
        bool initiallyDisabled = false;
        std::optional<OwnerComponent> owner;
        std::optional<LockComponent> lock;
        std::optional<TeleportComponent> teleport;
        PackPayload payload;

        friend bool operator==(const WorldObject&, const WorldObject&) = default;
    };

    struct WorldCell
    {
        WorldId id = kNoWorldId;
        std::string editorId;
        std::string name;
        bool interior = false;
        bool hasGrid = false;
        std::int32_t gridX = 0;
        std::int32_t gridY = 0;
        WorldId world = kNoWorldId;
        std::vector<WorldObject> objects; ///< In a fixed order: the placement id, ascending
        /// Placements whose base the pack could not resolve (they are still listed with ObjectKind::Unknown).
        std::uint32_t unresolvedBases = 0;
        /// Placements the pack refused to hand over because a value was not usable (not finite, past the limits, a
        /// scale that is not positive): counted, never listed.
        std::uint32_t droppedObjects = 0;

        friend bool operator==(const WorldCell&, const WorldCell&) = default;
    };
}
