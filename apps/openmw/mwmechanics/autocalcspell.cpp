#include "autocalcspell.hpp"

#include "../mwbase/rulesregistry.hpp"

#include "progressionrules.hpp"

namespace MWMechanics
{
    std::vector<ESM::RefId> autoCalcNpcSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
        const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race)
    {
        return MWBase::RulesRegistry::progression().autoCalcNpcSpells(actorSkills, actorAttributes, race);
    }

    std::vector<ESM::RefId> autoCalcPlayerSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
        const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race)
    {
        return MWBase::RulesRegistry::progression().autoCalcPlayerSpells(actorSkills, actorAttributes, race);
    }
}
