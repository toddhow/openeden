#include "rulesregistry.hpp"

#include <stdexcept>

#include "../mwmechanics/justicerules.hpp"
#include "../mwmechanics/progressionrules.hpp"
#include "../mwmechanics/socialrules.hpp"
#include "../mwmechanics/thiefrules.hpp"
#include "../mwworld/gamerules.hpp"

namespace MWBase
{
    namespace
    {
        struct Installed
        {
            std::shared_ptr<MWMechanics::ThiefRules> mThief;
            std::shared_ptr<MWMechanics::ProgressionRules> mProgression;
            std::shared_ptr<MWMechanics::SocialRules> mSocial;
            std::shared_ptr<MWMechanics::JusticeRules> mJustice;
        };

        Installed& installed()
        {
            static Installed rules;
            return rules;
        }
    }

    void RulesRegistry::install(RuleSets rules)
    {
        if (rules.mGameRules)
            MWWorld::installGameRules(*rules.mGameRules);
        if (rules.mDialogue)
            MWDialogue::installFactories(std::move(*rules.mDialogue));
        if (rules.mScripts)
            MWScript::installCommandSet(std::move(*rules.mScripts));
        if (rules.mThief)
            installed().mThief = std::move(rules.mThief);
        if (rules.mProgression)
            installed().mProgression = std::move(rules.mProgression);
        if (rules.mSocial)
            installed().mSocial = std::move(rules.mSocial);
        if (rules.mJustice)
            installed().mJustice = std::move(rules.mJustice);
    }

    void RulesRegistry::clearForTests()
    {
        installed() = Installed{};
    }

    MWMechanics::ThiefRules& RulesRegistry::thief()
    {
        if (!installed().mThief)
            throw std::runtime_error(
                "No game pack provides the thief rules (lock picking, trap probing, pickpocketing): the executable "
                "did not install them (MWBase::RulesRegistry::install)");
        return *installed().mThief;
    }

    MWMechanics::ProgressionRules& RulesRegistry::progression()
    {
        if (!installed().mProgression)
            throw std::runtime_error(
                "No game pack provides the progression rules (spell autocalc, difficulty scaling, levelled lists): "
                "the executable did not install them (MWBase::RulesRegistry::install)");
        return *installed().mProgression;
    }

    MWMechanics::SocialRules& RulesRegistry::social()
    {
        if (!installed().mSocial)
            throw std::runtime_error(
                "No game pack provides the social rules (derived disposition, barter prices, persuasion): the "
                "executable did not install them (MWBase::RulesRegistry::install)");
        return *installed().mSocial;
    }

    MWMechanics::JusticeRules& RulesRegistry::justice()
    {
        if (!installed().mJustice)
            throw std::runtime_error(
                "No game pack provides the justice rules (crime, witnesses, bounty, stolen items): the executable did "
                "not install them (MWBase::RulesRegistry::install)");
        return *installed().mJustice;
    }
}
