#include "pickpocket.hpp"

#include "../mwbase/rulesregistry.hpp"

#include "thiefrules.hpp"

namespace MWMechanics
{
    Pickpocket::Pickpocket(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim)
        : mThief(thief)
        , mVictim(victim)
    {
    }

    bool Pickpocket::pick(const MWWorld::Ptr& item, int count)
    {
        return MWBase::RulesRegistry::thief().pickpocketPick(mThief, mVictim, item, count);
    }

    bool Pickpocket::finish()
    {
        return MWBase::RulesRegistry::thief().pickpocketFinish(mThief, mVictim);
    }
}
