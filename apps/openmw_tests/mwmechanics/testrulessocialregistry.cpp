// Task 0098, move 6: the registry holds the social rules like the other rule sets (added with the move, after the
// characterization tests of testrulessocial.cpp).
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <game_morrowind/rules.hpp>

#include "apps/openmw/mwbase/rulesregistry.hpp"
#include "apps/openmw/mwmechanics/socialrules.hpp"

namespace
{
    // Puts all three rule sets back whatever a test did (clearForTests forgets them all).
    struct RestoreSocial
    {
        ~RestoreSocial()
        {
            MWBase::RuleSets rules;
            rules.mThief = game_morrowind::makeThiefRules();
            rules.mProgression = game_morrowind::makeProgressionRules();
            rules.mSocial = game_morrowind::makeSocialRules();
            MWBase::RulesRegistry::install(std::move(rules));
        }
    };

    TEST(RulesRegistrySocial, missing_social_rules_throw_an_error_that_names_them)
    {
        RestoreSocial restore;
        MWBase::RulesRegistry::clearForTests();
        try
        {
            MWBase::RulesRegistry::social();
            FAIL() << "expected an exception";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("social rules"), std::string::npos) << error.what();
        }
    }

    TEST(RulesRegistrySocial, installed_social_rules_are_returned)
    {
        RestoreSocial restore;
        MWBase::RuleSets rules;
        rules.mSocial = game_morrowind::makeSocialRules();
        MWMechanics::SocialRules* installed = rules.mSocial.get();
        MWBase::RulesRegistry::install(std::move(rules));
        EXPECT_EQ(&MWBase::RulesRegistry::social(), installed);
    }
}
