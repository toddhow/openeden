#ifndef GAME_MORROWIND_DIFFICULTYSCALING_H
#define GAME_MORROWIND_DIFFICULTYSCALING_H

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics::Morrowind
{
    /// Scales damage dealt to an actor based on difficulty setting
    float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim);
}

#endif
