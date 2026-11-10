#include <game_morrowind/rules.hpp>

#include <memory>

#include <game_morrowind/autocalcspell.hpp>
#include <game_morrowind/dialogue_rules.hpp>
#include <game_morrowind/difficultyscaling.hpp>
#include <game_morrowind/levelledlist.hpp>
#include <game_morrowind/pickpocket.hpp>
#include <game_morrowind/script_commands.hpp>
#include <game_morrowind/security.hpp>

#include <apps/openmw/mwbase/rulesregistry.hpp>
#include <apps/openmw/mwmechanics/progressionrules.hpp>
#include <apps/openmw/mwmechanics/thiefrules.hpp>

#include <components/compiler/extensions0.hpp>

namespace game_morrowind
{
    namespace
    {
        /// Morrowind's thief rules: the unchanged Security and Pickpocket classes, moved from apps/openmw/mwmechanics.
        class MorrowindThiefRules final : public MWMechanics::ThiefRules
        {
        public:
            void pickLock(const MWWorld::Ptr& actor, const MWWorld::Ptr& lock, const MWWorld::Ptr& lockpick,
                std::string_view& resultMessage, std::string_view& resultSound) override
            {
                MWMechanics::Morrowind::Security(actor).pickLock(lock, lockpick, resultMessage, resultSound);
            }

            void probeTrap(const MWWorld::Ptr& actor, const MWWorld::Ptr& trap, const MWWorld::Ptr& probe,
                std::string_view& resultMessage, std::string_view& resultSound) override
            {
                MWMechanics::Morrowind::Security(actor).probeTrap(trap, probe, resultMessage, resultSound);
            }

            bool pickpocketPick(
                const MWWorld::Ptr& thief, const MWWorld::Ptr& victim, const MWWorld::Ptr& item, int count) override
            {
                return MWMechanics::Morrowind::Pickpocket(thief, victim).pick(item, count);
            }

            bool pickpocketFinish(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim) override
            {
                return MWMechanics::Morrowind::Pickpocket(thief, victim).finish();
            }
        };

        /// Morrowind's progression rules: the unchanged free functions moved from apps/openmw/mwmechanics.
        class MorrowindProgressionRules final : public MWMechanics::ProgressionRules
        {
        public:
            std::vector<ESM::RefId> autoCalcNpcSpells(const std::map<ESM::RefId, MWMechanics::SkillValue>& actorSkills,
                const std::map<ESM::RefId, MWMechanics::AttributeValue>& actorAttributes,
                const ESM::Race* race) override
            {
                return MWMechanics::Morrowind::autoCalcNpcSpells(actorSkills, actorAttributes, race);
            }

            std::vector<ESM::RefId> autoCalcPlayerSpells(
                const std::map<ESM::RefId, MWMechanics::SkillValue>& actorSkills,
                const std::map<ESM::RefId, MWMechanics::AttributeValue>& actorAttributes,
                const ESM::Race* race) override
            {
                return MWMechanics::Morrowind::autoCalcPlayerSpells(actorSkills, actorAttributes, race);
            }

            float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim) override
            {
                return MWMechanics::Morrowind::scaleDamage(damage, attacker, victim);
            }

            ESM::RefId levelledItem(const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng,
                std::optional<int> level) override
            {
                return MWMechanics::Morrowind::getLevelledItem(levItem, creature, prng, level);
            }
        };
    }

    std::shared_ptr<MWMechanics::ThiefRules> makeThiefRules()
    {
        return std::make_shared<MorrowindThiefRules>();
    }

    std::shared_ptr<MWMechanics::ProgressionRules> makeProgressionRules()
    {
        return std::make_shared<MorrowindProgressionRules>();
    }

    void installRules(openeden::sdk::game::GameRules& gameRules)
    {
        MWBase::RuleSets rules;
        rules.mGameRules = &gameRules;
        rules.mDialogue = MWDialogue::Factories{ &game_morrowind::makeJournal, &game_morrowind::makeDialogueManager,
            &game_morrowind::compileAllDialogueScripts };
        rules.mScripts = MWScript::CommandSet{ &Compiler::registerExtensions, &MWScript::installOpcodes };
        rules.mThief = makeThiefRules();
        rules.mProgression = makeProgressionRules();
        rules.mSocial = makeSocialRules();
        rules.mJustice = makeJusticeRules();
        MWBase::RulesRegistry::install(std::move(rules));
    }
}
