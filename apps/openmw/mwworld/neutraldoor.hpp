#ifndef OPENMW_MWWORLD_NEUTRALDOOR_H
#define OPENMW_MWWORLD_NEUTRALDOOR_H

#include <functional>
#include <optional>
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

    /// The placement id behind a reference number made by neutralRefNum; nullopt for any other reference number.
    [[nodiscard]] std::optional<openeden::sdk::game::WorldId> neutralPlacementOf(const ESM::RefNum& refNum);

    /// Decides what activating `object` does. `classify` is asked about the destination cell only when the decision
    /// needs it (a teleport that is not locked), and at most once. `unlockAll` is the developer aid
    /// OPENEDEN_NEUTRAL_UNLOCK_DOORS: every lock counts as open.
    [[nodiscard]] NeutralDoorDecision decideNeutralDoor(const openeden::sdk::game::WorldObject& object,
        bool actorIsPlayer, bool unlockAll,
        const std::function<NeutralDestination(openeden::sdk::game::WorldId)>& classify);

    /// The key of the refusal's text in the OMWEngine l10n context; empty for an outcome that has no message.
    [[nodiscard]] std::string_view neutralDoorMessageKey(NeutralDoorOutcome outcome);

    /// A short word for the developer trace.
    [[nodiscard]] std::string_view neutralDoorOutcomeName(NeutralDoorOutcome outcome);
}

#endif
