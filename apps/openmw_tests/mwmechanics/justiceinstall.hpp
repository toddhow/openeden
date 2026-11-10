// Task 0098, move 7: see testrulesjustice.cpp. Since the move the crime and stolen-item rules live in the Morrowind
// game pack, and the fixture installs them the way the executable does (MWBase::RulesRegistry). On the old code this
// function did nothing.
#ifndef OPENEDEN_TESTS_MWMECHANICS_JUSTICEINSTALL_H
#define OPENEDEN_TESTS_MWMECHANICS_JUSTICEINSTALL_H

#include <game_morrowind/rules.hpp>

#include "apps/openmw/mwbase/rulesregistry.hpp"

namespace CraftingTest
{
    inline void installJusticeRulesForTest()
    {
        MWBase::RuleSets rules;
        rules.mJustice = game_morrowind::makeJusticeRules();
        MWBase::RulesRegistry::install(std::move(rules));
    }
}

#endif
