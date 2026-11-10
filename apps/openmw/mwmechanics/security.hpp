#ifndef MWMECHANICS_SECURITY_H
#define MWMECHANICS_SECURITY_H

#include <string_view>

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    /// @brief the Security skill's actions for one actor. The rules are the game pack's (task 0098 move 5): this class
    /// only carries the actor and forwards to MWBase::RulesRegistry::thief().
    class Security
    {
    public:
        Security(const MWWorld::Ptr& actor);

        void pickLock(const MWWorld::Ptr& lock, const MWWorld::Ptr& lockpick, std::string_view& resultMessage,
            std::string_view& resultSound);
        void probeTrap(const MWWorld::Ptr& trap, const MWWorld::Ptr& probe, std::string_view& resultMessage,
            std::string_view& resultSound);

    private:
        MWWorld::Ptr mActor;
    };
}

#endif
