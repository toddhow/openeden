#ifndef GAME_MORROWIND_RULES_H
#define GAME_MORROWIND_RULES_H

#include <memory>

#include <openeden/game/game_rules.hpp>

namespace MWMechanics
{
    class JusticeRules;
    class ProgressionRules;
    class SocialRules;
    class ThiefRules;
}

namespace game_morrowind
{
    /// Hands every rule of the Morrowind pack to the core in one call (task 0098 move 5, stage 5.0): the crafting
    /// rules of \a gameRules, the journal and dialogue manager, the script commands and, new in move 5, the thief and
    /// progression rules. It replaces the three separate calls the executable made before; the core side is
    /// MWBase::RulesRegistry::install. \a gameRules must outlive the program's use of the crafting windows (the
    /// executable keeps it). Main thread, before the engine is made.
    void installRules(openeden::sdk::game::GameRules& gameRules);

    /// The two rule sets of move 5 on their own, for a program that wants them without the pack's other rules (the unit
    /// tests install them so that code which makes an NPC finds its spell autocalc).
    [[nodiscard]] std::shared_ptr<MWMechanics::ThiefRules> makeThiefRules();
    [[nodiscard]] std::shared_ptr<MWMechanics::ProgressionRules> makeProgressionRules();

    /// Morrowind's social rules (task 0098 move 6): derived disposition, barter price, persuasion.
    [[nodiscard]] std::shared_ptr<MWMechanics::SocialRules> makeSocialRules();

    /// Registers the pack's world-object classes (door, container, activator, static, body part) with MWWorld::Class
    /// (task 0098 move 8, stage 8a). Installed through MWBase::RuleSets::mClasses; the engine calls it from
    /// MWClass::registerClasses on the loading thread, once per start-up.
    void registerWorldObjectClasses();

    /// Morrowind's justice rules (task 0098 move 7): crime and witnesses, bounty, stolen items, confiscation.
    [[nodiscard]] std::shared_ptr<MWMechanics::JusticeRules> makeJusticeRules();
}

#endif
