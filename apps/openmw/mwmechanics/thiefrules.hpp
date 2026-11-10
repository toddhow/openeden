#ifndef GAME_MWMECHANICS_THIEFRULES_H
#define GAME_MWMECHANICS_THIEFRULES_H

#include <string_view>

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    /// The rules of lock picking, trap probing and pickpocketing (task 0098 move 5). Morrowind's rules live in the
    /// Morrowind game pack (game_packs/morrowind); the core calls them through MWBase::RulesRegistry::thief() and
    /// never names the pack. Main thread only (the callers are the character controller's attack handling and the
    /// pickpocket window).
    class ThiefRules
    {
    public:
        virtual ~ThiefRules() = default;

        /// Tries to open \a lock with \a lockpick. Sets the message and sound when it did anything; draws one number
        /// from the world's generator when the chance is above zero.
        virtual void pickLock(const MWWorld::Ptr& actor, const MWWorld::Ptr& lock, const MWWorld::Ptr& lockpick,
            std::string_view& resultMessage, std::string_view& resultSound)
            = 0;

        /// Tries to disarm the trap of \a trap with \a probe (as pickLock).
        virtual void probeTrap(const MWWorld::Ptr& actor, const MWWorld::Ptr& trap, const MWWorld::Ptr& probe,
            std::string_view& resultMessage, std::string_view& resultSound)
            = 0;

        /// Steals \a count of \a item from \a victim. \return Was the thief detected? Draws one number.
        virtual bool pickpocketPick(
            const MWWorld::Ptr& thief, const MWWorld::Ptr& victim, const MWWorld::Ptr& item, int count)
            = 0;

        /// Ends the pickpocketing. \return Was the thief detected? Draws one number.
        virtual bool pickpocketFinish(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim) = 0;
    };
}

#endif
