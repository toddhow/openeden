#include "difficultyscaling.hpp"

#include "../mwbase/rulesregistry.hpp"

#include "progressionrules.hpp"

float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
{
    return MWBase::RulesRegistry::progression().scaleDamage(damage, attacker, victim);
}
