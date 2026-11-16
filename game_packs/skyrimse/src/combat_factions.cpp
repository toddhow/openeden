// Task 0197: the pure rules of the combat AI's target choice (combat_factions.hpp).
#include "game_skyrimse/combat_factions.hpp"

#include "game_skyrimse/actor_mechanics.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse::Factions
{
    namespace
    {
        bool friendly(ActorRelation relation)
        {
            return relation == ActorRelation::Ally || relation == ActorRelation::Friend;
        }
    }

    bool playerSide(const Party& party)
    {
        return party.mPlayer || party.mFollower;
    }

    ActorRelation sideRelation(const Party& a, const Party& b, ActorRelation factionRelation)
    {
        const bool sideA = playerSide(a);
        const bool sideB = playerSide(b);
        if (sideA && sideB)
            return ActorRelation::Ally;
        if (sideA && b.mRelationToPlayer == ActorRelation::Enemy)
            return ActorRelation::Enemy;
        if (sideB && a.mRelationToPlayer == ActorRelation::Enemy)
            return ActorRelation::Enemy;
        return factionRelation;
    }

    bool attacksOnSight(const Party& a, const Party& b, ActorRelation rel, bool bHostileToPlayerSide)
    {
        if (a.mPlayer)
            return false;
        if (a.mFollower)
            return !playerSide(b) && (bHostileToPlayerSide || b.mHostileOnSight);
        const auto aggression
            = static_cast<openeden::sdk::game::ActorAggression>(std::min<std::uint8_t>(a.mAggression, 3));
        return wouldAttackOnSight(aggression, rel);
    }

    bool assists(const Party& helper, ActorRelation toVictim, ActorRelation toAttacker, bool victimOnPlayerSide)
    {
        if (helper.mGuard)
        {
            if (friendly(toVictim))
                return true;
            return victimOnPlayerSide && helper.mRelationToPlayer != ActorRelation::Enemy
                && toVictim != ActorRelation::Enemy;
        }
        if (helper.mAssistance == 0 || friendly(toAttacker))
            return false;
        if (toVictim == ActorRelation::Ally)
            return true;
        return helper.mAssistance >= 2 && toVictim == ActorRelation::Friend;
    }

    bool mayHit(ActorRelation attackerToVictim, bool victimIsAttackersTarget)
    {
        return victimIsAttackersTarget || !friendly(attackerToVictim);
    }

    SightRay sightRay(double sx, double sy, double sz, double tx, double ty, double tz)
    {
        SightRay out;
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || !std::isfinite(tx) || !std::isfinite(ty)
            || !std::isfinite(tz))
            return out;
        const double dx = tx - sx;
        const double dy = ty - sy;
        const double dz = (tz + kTargetHeight) - (sz + kEyeHeight);
        const double flat = std::hypot(dx, dy);
        const double length = std::hypot(flat, dz);
        if (!(length > 1e-6))
            return out;
        out.mRay.x = sx;
        out.mRay.y = sy;
        out.mRay.z = sz + kEyeHeight;
        out.mRay.heading = static_cast<float>(std::atan2(dx, dy));
        out.mRay.pitch = static_cast<float>(std::atan2(dz, flat));
        out.mRay.range = static_cast<float>(length);
        out.mLength = length;
        out.mValid = true;
        return out;
    }

    bool sightClear(openeden::sdk::game::RayFate fate)
    {
        return fate != openeden::sdk::game::RayFate::World;
    }
}
