#ifndef MWMECHANICS_RECHARGE_H
#define MWMECHANICS_RECHARGE_H

#include <apps/openmw/mwworld/ptr.hpp>

namespace MWMechanics
{

    /// The charge of an enchanted item after `duration` seconds of recharging over time, never above `maxCharge`: the
    /// rule behind the over-time recharge (task 0085 move 2). Plain numbers, no item. Reads the game setting
    /// fMagicItemRechargePerSecond once, at the first call, as rechargeItem always did.
    float rechargedCharge(float charge, float maxCharge, float duration);

    bool rechargeItem(const MWWorld::Ptr& item, const float maxCharge, const float duration);

    bool rechargeItem(const MWWorld::Ptr& item, const MWWorld::Ptr& gem);

}

#endif
