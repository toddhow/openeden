// Task 0098, move 7: see testrulesjustice.cpp. Old code: the crime and stolen-item rules are inside
// MWMechanics::MechanicsManager and nothing needs installing (the social rules, which the crime code calls through
// getDerivedDisposition, are installed by installSocialRulesForTest).
#ifndef OPENEDEN_TESTS_MWMECHANICS_JUSTICEINSTALL_H
#define OPENEDEN_TESTS_MWMECHANICS_JUSTICEINSTALL_H

namespace CraftingTest
{
    inline void installJusticeRulesForTest() {}
}

#endif
