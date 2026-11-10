#ifndef GAME_MWMECHANICS_PROGRESSIONRULES_H
#define GAME_MWMECHANICS_PROGRESSIONRULES_H

#include <map>
#include <optional>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/misc/rng.hpp>

#include "../mwworld/ptr.hpp"
#include "creaturestats.hpp"

namespace ESM
{
    struct LevelledListBase;
    struct Race;
}

namespace MWMechanics
{
    /// The small progression rules (task 0098 move 5): which spells an actor starts with, how difficulty scales damage
    /// and what a levelled list gives. Morrowind's live in the Morrowind game pack; the core calls them through
    /// MWBase::RulesRegistry::progression(). Main thread in practice (autocalc runs wherever an actor's stats are first
    /// made, which is the main thread today).
    class ProgressionRules
    {
    public:
        virtual ~ProgressionRules() = default;

        virtual std::vector<ESM::RefId> autoCalcNpcSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
            const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race)
            = 0;

        virtual std::vector<ESM::RefId> autoCalcPlayerSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
            const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race)
            = 0;

        /// Scales damage dealt to an actor based on the difficulty setting.
        virtual float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim) = 0;

        /// \return ID of the resulting item, or empty if none. Draws from \a prng in the order the rule defines.
        virtual ESM::RefId levelledItem(
            const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level)
            = 0;
    };
}

#endif
