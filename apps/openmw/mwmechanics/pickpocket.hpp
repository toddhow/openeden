#ifndef OPENMW_MECHANICS_PICKPOCKET_H
#define OPENMW_MECHANICS_PICKPOCKET_H

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    /// The pickpocketing of one thief against one victim. The rules are the game pack's (task 0098 move 5): this class
    /// only carries the two actors and forwards to MWBase::RulesRegistry::thief().
    class Pickpocket
    {
    public:
        Pickpocket(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim);

        /// Steal some items
        /// @return Was the thief detected?
        bool pick(const MWWorld::Ptr& item, int count);
        /// End the pickpocketing process
        /// @return Was the thief detected?
        bool finish();

    private:
        MWWorld::Ptr mThief;
        MWWorld::Ptr mVictim;
    };
}

#endif
