// Task 0098, move 5, stage 5.0: the rules registry. A global test environment installs Morrowind's thief and
// progression rules once for the whole test program, because code under test (for example the NPC class, which runs the
// spell autocalc when it makes an NPC's stats) reaches them through MWBase::RulesRegistry. The tests below check the
// registry itself.
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <game_morrowind/rules.hpp>

#include "apps/openmw/mwbase/rulesregistry.hpp"
#include "apps/openmw/mwmechanics/progressionrules.hpp"
#include "apps/openmw/mwmechanics/thiefrules.hpp"

namespace
{
    void installMorrowindRules()
    {
        MWBase::RuleSets rules;
        rules.mThief = game_morrowind::makeThiefRules();
        rules.mProgression = game_morrowind::makeProgressionRules();
        MWBase::RulesRegistry::install(std::move(rules));
    }

    class RulesEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override { installMorrowindRules(); }
    };

    // Registered before main runs, as gtest allows; the environment's SetUp runs once before the first test.
    [[maybe_unused]] ::testing::Environment* const sEnvironment
        = ::testing::AddGlobalTestEnvironment(new RulesEnvironment);

    /// Puts the rules back whatever a test did.
    struct RestoreRules
    {
        ~RestoreRules() { installMorrowindRules(); }
    };

    TEST(RulesRegistry, a_missing_thief_rule_set_is_a_runtime_error_naming_the_piece)
    {
        RestoreRules restore;
        MWBase::RulesRegistry::clearForTests();
        try
        {
            MWBase::RulesRegistry::thief();
            FAIL() << "no exception";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("thief rules"), std::string::npos) << error.what();
        }
    }

    TEST(RulesRegistry, a_missing_progression_rule_set_is_a_runtime_error_naming_the_piece)
    {
        RestoreRules restore;
        MWBase::RulesRegistry::clearForTests();
        try
        {
            MWBase::RulesRegistry::progression();
            FAIL() << "no exception";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("progression rules"), std::string::npos) << error.what();
        }
    }

    TEST(RulesRegistry, installing_an_empty_value_leaves_what_was_installed)
    {
        MWMechanics::ThiefRules* before = &MWBase::RulesRegistry::thief();
        MWBase::RulesRegistry::install(MWBase::RuleSets{});
        EXPECT_EQ(&MWBase::RulesRegistry::thief(), before);
    }

    TEST(RulesRegistry, a_second_install_replaces_the_rule_set)
    {
        RestoreRules restore;
        MWMechanics::ThiefRules* before = &MWBase::RulesRegistry::thief();
        MWBase::RuleSets rules;
        rules.mThief = game_morrowind::makeThiefRules();
        MWBase::RulesRegistry::install(std::move(rules));
        EXPECT_NE(&MWBase::RulesRegistry::thief(), before);
    }
}
