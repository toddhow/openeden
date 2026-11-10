// Task 0098, move 7: the registry holds the justice rules like the other rule sets (added with the move, after the
// characterization tests of testrulesjustice.cpp).
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <game_morrowind/rules.hpp>

#include "apps/openmw/mwbase/rulesregistry.hpp"
#include "apps/openmw/mwmechanics/justicerules.hpp"

namespace
{
    // Puts all four rule sets back whatever a test did (clearForTests forgets them all).
    struct RestoreRules
    {
        ~RestoreRules()
        {
            MWBase::RuleSets rules;
            rules.mThief = game_morrowind::makeThiefRules();
            rules.mProgression = game_morrowind::makeProgressionRules();
            rules.mSocial = game_morrowind::makeSocialRules();
            rules.mJustice = game_morrowind::makeJusticeRules();
            MWBase::RulesRegistry::install(std::move(rules));
        }
    };

    TEST(RulesRegistryJustice, missing_justice_rules_throw_an_error_that_names_them)
    {
        RestoreRules restore;
        MWBase::RulesRegistry::clearForTests();
        try
        {
            MWBase::RulesRegistry::justice();
            FAIL() << "expected an exception";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("justice rules"), std::string::npos) << error.what();
        }
    }

    TEST(RulesRegistryJustice, installed_justice_rules_are_returned)
    {
        RestoreRules restore;
        MWBase::RuleSets rules;
        rules.mJustice = game_morrowind::makeJusticeRules();
        MWMechanics::JusticeRules* installed = rules.mJustice.get();
        MWBase::RulesRegistry::install(std::move(rules));
        EXPECT_EQ(&MWBase::RulesRegistry::justice(), installed);
    }

    TEST(RulesRegistryJustice, installing_other_sets_keeps_the_justice_rules)
    {
        RestoreRules restore;
        MWBase::RuleSets rules;
        rules.mJustice = game_morrowind::makeJusticeRules();
        MWMechanics::JusticeRules* installed = rules.mJustice.get();
        MWBase::RulesRegistry::install(std::move(rules));
        MWBase::RuleSets other;
        other.mSocial = game_morrowind::makeSocialRules();
        MWBase::RulesRegistry::install(std::move(other));
        EXPECT_EQ(&MWBase::RulesRegistry::justice(), installed);
    }
}
