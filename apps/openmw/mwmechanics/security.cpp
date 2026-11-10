#include "security.hpp"

#include "../mwbase/rulesregistry.hpp"

#include "thiefrules.hpp"

namespace MWMechanics
{
    Security::Security(const MWWorld::Ptr& actor)
        : mActor(actor)
    {
    }

    void Security::pickLock(const MWWorld::Ptr& lock, const MWWorld::Ptr& lockpick, std::string_view& resultMessage,
        std::string_view& resultSound)
    {
        MWBase::RulesRegistry::thief().pickLock(mActor, lock, lockpick, resultMessage, resultSound);
    }

    void Security::probeTrap(const MWWorld::Ptr& trap, const MWWorld::Ptr& probe, std::string_view& resultMessage,
        std::string_view& resultSound)
    {
        MWBase::RulesRegistry::thief().probeTrap(mActor, trap, probe, resultMessage, resultSound);
    }
}
