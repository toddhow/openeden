// Task 0098, move 6: see testrulessocial.cpp. Since the move the social rules live in the Morrowind game pack, and the
// fixture installs them the way the executable does (MWBase::RulesRegistry). On the old code this function did nothing.
#ifndef OPENEDEN_TESTS_MWMECHANICS_SOCIALINSTALL_H
#define OPENEDEN_TESTS_MWMECHANICS_SOCIALINSTALL_H

#include <game_morrowind/rules.hpp>

#include "apps/openmw/mwbase/rulesregistry.hpp"

namespace CraftingTest
{
    inline void installSocialRulesForTest()
    {
        MWBase::RuleSets rules;
        rules.mSocial = game_morrowind::makeSocialRules();
        MWBase::RulesRegistry::install(std::move(rules));
    }
}

#endif
