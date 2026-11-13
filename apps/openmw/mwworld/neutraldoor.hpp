#ifndef OPENMW_MWWORLD_NEUTRALDOOR_H
#define OPENMW_MWWORLD_NEUTRALDOOR_H

#include <functional>
#include <optional>
#include <utility>
#include <string_view>

#include <components/esm/position.hpp>
#include <components/esm3/cellref.hpp>

#include <openeden/game/world_model.hpp>

// Doors in a neutral cell (decision 0016, task 0115, stage N4 engine half): the pure part.
//
// What activating a door of a game pack's neutral cell does is decided here from the pack's own description of the
// placement (openeden::sdk::game::WorldObject: kind, lock, teleport) and one answer from the engine: what kind of cell
// the door leads to. No engine type but ESM::Position and ESM::RefNum is involved, so the decision is tested without
// a world. MWClass::Neutral::activate maps the outcome to an action and asks the engine for the destination cell.
//
// Main thread only. No state.

namespace MWWorld
{
    /// What the engine says about a door's destination cell. Interior: it was built and can be entered. Exterior: the
    /// neutral model has no exteriors yet (design document 10.4, R7). Unavailable: unknown to the pack, or the pack
    /// could not build it.
    enum class NeutralDestination
    {
        Interior,
        Exterior,
        ExteriorWorld, ///< task 0125: an exterior of a world the pack can describe (terrain), which the engine can enter
        Unavailable,
    };

    enum class NeutralDoorOutcome
    {
        NotADoor, ///< any other kind: nothing happens (the null action)
        NotAPlayer, ///< somebody other than the player: refused with no message (as Morrowind's Door::activate)
        Locked, ///< the placement has a lock and the developer aid is off: refused with a message
        Inert, ///< a door with no teleport: stays closed, nothing happens (this task)
        Nowhere, ///< a teleport whose destination cell is not known or could not be built: refused with a message
        Exterior, ///< the destination is an exterior: refused with a message
        Teleport, ///< go to `destinationCell` and arrive at `arrival`
    };

    struct NeutralDoorDecision
    {
        NeutralDoorOutcome mOutcome = NeutralDoorOutcome::NotADoor;
        openeden::sdk::game::WorldId mDestinationCell = openeden::sdk::game::kNoWorldId;
        ESM::Position mArrival;
    };

    /// The arrival of a teleport as the engine's position: the XTEL position as given (no unit conversion: the pack
    /// uses the same units as the placements) and the quaternion as the three stored angles
    /// (neutralRotationAngles), so that Misc::Convert::makeOsgQuat gives the quaternion back.
    [[nodiscard]] ESM::Position neutralArrivalPosition(const openeden::sdk::game::TeleportComponent& teleport);

    /// Task 0138: the exterior grid square (x, y) that a teleport's arrival position stands in, for a worldspace whose
    /// cells are `cellSize` units wide: floor(position / cellSize) in double, so a negative coordinate rounds down and
    /// a position exactly on a boundary belongs to the square above it. nullopt for a cell size that is not positive and
    /// finite, a position that is not finite, or a square beyond +-1,000,000 (the number would not fit the engine's
    /// int grid or is not a place).
    [[nodiscard]] std::optional<std::pair<int, int>> neutralExteriorSquare(double x, double y, double cellSize);

    /// The placement id behind a reference number made by neutralRefNum; nullopt for any other reference number.
    [[nodiscard]] std::optional<openeden::sdk::game::WorldId> neutralPlacementOf(const ESM::RefNum& refNum);

    /// Decides what activating `object` does. `classify` is asked about the destination cell only when the decision
    /// needs it (a teleport that is not locked), and at most once. `unlockAll` is the developer aid
    /// OPENEDEN_NEUTRAL_UNLOCK_DOORS: every lock counts as open.
    [[nodiscard]] NeutralDoorDecision decideNeutralDoor(const openeden::sdk::game::WorldObject& object,
        bool actorIsPlayer, bool unlockAll,
        const std::function<NeutralDestination(openeden::sdk::game::WorldId)>& classify);

    /// Task 0157: whether a door of a neutral interior is one that neutral actors walk through (the navigator gets an
    /// off-mesh connection across it and the door swings open for a walker): it must not be a teleport (those lead to
    /// another cell, an actor does not use them) and must not be locked, unless the player has unlocked it with its
    /// key. A locked door stays a wall to actors (they have no keys yet).
    [[nodiscard]] bool neutralDoorOpensForActors(const openeden::sdk::game::WorldObject& object, bool unlockedByKey);

    class ConstPtr;
    /// Task 0157: the same for a reference of a neutral cell: it is a door (the pack's kind), and
    /// neutralDoorOpensForActors holds for its placement given the session's key ring. False for anything else.
    [[nodiscard]] bool neutralDoorPtrOpensForActors(const ConstPtr& ptr);

    /// The key of the refusal's text in the OMWEngine l10n context; empty for an outcome that has no message.
    [[nodiscard]] std::string_view neutralDoorMessageKey(NeutralDoorOutcome outcome);

    /// A short word for the developer trace.
    [[nodiscard]] std::string_view neutralDoorOutcomeName(NeutralDoorOutcome outcome);
}

#endif
