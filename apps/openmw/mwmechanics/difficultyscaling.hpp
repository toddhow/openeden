#ifndef OPENMW_MWMECHANICS_DIFFICULTYSCALING_H
#define OPENMW_MWMECHANICS_DIFFICULTYSCALING_H

namespace MWWorld
{
    class Ptr;
}

/// Scales damage dealt to an actor based on difficulty setting. The rule is the game pack's (task 0098 move 5):
/// this forwards to MWBase::RulesRegistry::progression().
float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim);

#endif
