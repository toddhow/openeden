#ifndef GAME_MWBASE_RULESREGISTRY_H
#define GAME_MWBASE_RULESREGISTRY_H

#include <memory>
#include <optional>

#include "../mwdialogue/dialoguefactory.hpp"
#include "../mwscript/scriptcommands.hpp"

namespace openeden::sdk::game
{
    class GameRules;
}

namespace MWMechanics
{
    class ProgressionRules;
    class SocialRules;
    class ThiefRules;
}

namespace MWBase
{
    /// Everything a game pack hands to the core, in one value (task 0098 move 5, stage 5.0). Before, each kind of rule
    /// had a hook of its own (crafting through MWWorld::installGameRules, the journal and dialogue manager through
    /// MWDialogue::installFactories, the script commands through MWScript::installCommandSet); RulesRegistry::install
    /// takes this and calls them, so a program has one place to install a game's rules and a new kind of rule adds a
    /// field here instead of a hook. A field left empty leaves what was installed before.
    struct RuleSets
    {
        /// Crafting. Not owned: the pack's object outlives the registry's use of it (the executable keeps it).
        openeden::sdk::game::GameRules* mGameRules = nullptr;
        std::optional<MWDialogue::Factories> mDialogue;
        std::optional<MWScript::CommandSet> mScripts;
        std::shared_ptr<MWMechanics::ThiefRules> mThief;
        std::shared_ptr<MWMechanics::ProgressionRules> mProgression;
        std::shared_ptr<MWMechanics::SocialRules> mSocial;
    };

    /// Who supplies the game's rules. openmw-lib never names a game pack: the executable installs the pack's rule sets
    /// at start-up (apps/openmw/main.cpp), before the engine is made. Main thread, before the engine runs.
    class RulesRegistry
    {
    public:
        /// Installs the non-empty parts of \a rules.
        static void install(RuleSets rules);

        /// Forgets the thief, progression and social rules (the other hooks keep what they hold). For tests that check
        /// the missing-rules error.
        static void clearForTests();

        /// \throws std::runtime_error naming the missing piece when no game pack installed it.
        static MWMechanics::ThiefRules& thief();
        static MWMechanics::ProgressionRules& progression();
        static MWMechanics::SocialRules& social();
    };
}

#endif
