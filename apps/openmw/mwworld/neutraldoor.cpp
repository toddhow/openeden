#include "neutraldoor.hpp"

#include <cmath>

#include "cellstore.hpp"
#include "livecellref.hpp"
#include "neutralcell.hpp"
#include "neutrallock.hpp"
#include "neutralstore.hpp"
#include "ptr.hpp"

namespace MWWorld
{
    using openeden::sdk::game::kNoWorldId;
    using openeden::sdk::game::ObjectKind;
    using openeden::sdk::game::TeleportComponent;
    using openeden::sdk::game::WorldId;
    using openeden::sdk::game::WorldObject;

    ESM::Position neutralArrivalPosition(const TeleportComponent& teleport)
    {
        ESM::Position position;
        position.pos[0] = static_cast<float>(teleport.destination.position.x);
        position.pos[1] = static_cast<float>(teleport.destination.position.y);
        position.pos[2] = static_cast<float>(teleport.destination.position.z);
        const std::array<float, 3> angles = neutralRotationAngles(teleport.destination.rotation);
        position.rot[0] = angles[0];
        position.rot[1] = angles[1];
        position.rot[2] = angles[2];
        return position;
    }

    std::optional<std::pair<int, int>> neutralExteriorSquare(double x, double y, double cellSize)
    {
        constexpr double kMaxSquare = 1000000.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(cellSize) || cellSize <= 0.0)
            return std::nullopt;
        const double sx = std::floor(x / cellSize);
        const double sy = std::floor(y / cellSize);
        if (!(std::abs(sx) <= kMaxSquare) || !(std::abs(sy) <= kMaxSquare))
            return std::nullopt;
        return std::make_pair(static_cast<int>(sx), static_cast<int>(sy));
    }

    std::optional<WorldId> neutralPlacementOf(const ESM::RefNum& refNum)
    {
        if (refNum.mContentFile != kNeutralRefNumFile)
            return std::nullopt;
        return static_cast<WorldId>(refNum.mIndex);
    }

    NeutralDoorDecision decideNeutralDoor(const WorldObject& object, bool actorIsPlayer, bool unlockAll,
        const std::function<NeutralDestination(WorldId)>& classify)
    {
        NeutralDoorDecision decision;
        if (object.kind != ObjectKind::Door)
            return decision;
        if (!actorIsPlayer)
        {
            decision.mOutcome = NeutralDoorOutcome::NotAPlayer;
            return decision;
        }
        if (object.lock && object.lock->locked && !unlockAll)
        {
            decision.mOutcome = NeutralDoorOutcome::Locked;
            return decision;
        }
        if (!object.teleport)
        {
            decision.mOutcome = NeutralDoorOutcome::Inert;
            return decision;
        }
        const TeleportComponent& teleport = *object.teleport;
        if (teleport.destinationCell == kNoWorldId)
        {
            decision.mOutcome = NeutralDoorOutcome::Nowhere;
            return decision;
        }
        switch (classify(teleport.destinationCell))
        {
            case NeutralDestination::Interior:
                decision.mOutcome = NeutralDoorOutcome::Teleport;
                decision.mDestinationCell = teleport.destinationCell;
                decision.mArrival = neutralArrivalPosition(teleport);
                break;
            case NeutralDestination::ExteriorWorld:
                decision.mOutcome = NeutralDoorOutcome::Teleport;
                decision.mDestinationCell = teleport.destinationCell;
                decision.mArrival = neutralArrivalPosition(teleport);
                break;
            case NeutralDestination::Exterior:
                decision.mOutcome = NeutralDoorOutcome::Exterior;
                break;
            case NeutralDestination::Unavailable:
                decision.mOutcome = NeutralDoorOutcome::Nowhere;
                break;
        }
        return decision;
    }

    std::string_view neutralDoorMessageKey(NeutralDoorOutcome outcome)
    {
        switch (outcome)
        {
            case NeutralDoorOutcome::Locked:
                return "NeutralDoorLocked";
            case NeutralDoorOutcome::Exterior:
                return "NeutralDoorOutside";
            case NeutralDoorOutcome::Nowhere:
                return "NeutralDoorNowhere";
            default:
                return {};
        }
    }

    std::string_view neutralDoorOutcomeName(NeutralDoorOutcome outcome)
    {
        switch (outcome)
        {
            case NeutralDoorOutcome::NotADoor:
                return "not-a-door";
            case NeutralDoorOutcome::NotAPlayer:
                return "not-a-player";
            case NeutralDoorOutcome::Locked:
                return "locked";
            case NeutralDoorOutcome::Inert:
                return "inert";
            case NeutralDoorOutcome::Nowhere:
                return "nowhere";
            case NeutralDoorOutcome::Exterior:
                return "exterior";
            case NeutralDoorOutcome::Teleport:
                return "teleport";
        }
        return "unknown";
    }
}

namespace MWWorld
{
    bool neutralDoorOpensForActors(const openeden::sdk::game::WorldObject& object, bool unlockedByKey)
    {
        if (object.teleport)
            return false;
        return !(object.lock && object.lock->locked) || unlockedByKey;
    }
}

namespace MWWorld
{
    bool neutralDoorPtrOpensForActors(const ConstPtr& ptr)
    {
        if (ptr.getType() != NeutralBase::sRecordId)
            return false;
        if (ptr.get<NeutralBase>()->mBase->mKind != openeden::sdk::game::ObjectKind::Door)
            return false;
        const CellStore* cell = ptr.getCell();
        const auto placement = neutralPlacementOf(ptr.getCellRef().getRefNum());
        if (cell == nullptr || !placement)
            return false;
        const openeden::sdk::game::WorldObject* object = cell->neutralObject(*placement);
        if (object == nullptr)
            return false;
        return neutralDoorOpensForActors(*object, neutralLockState().isUnlocked(cell->neutralGameId(), *placement));
    }
}
