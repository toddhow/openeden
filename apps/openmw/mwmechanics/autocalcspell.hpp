#ifndef OPENMW_AUTOCALCSPELL_H
#define OPENMW_AUTOCALCSPELL_H

#include "creaturestats.hpp"

#include <components/esm/refid.hpp>

#include <map>
#include <string>
#include <vector>

namespace ESM
{
    struct Race;
}

namespace MWMechanics
{
    /// Calculates an NPC's starting spells from its stats. The algorithm is the game pack's (task 0098 move 5): these
    /// forward to MWBase::RulesRegistry::progression().
    std::vector<ESM::RefId> autoCalcNpcSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
        const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race);

    std::vector<ESM::RefId> autoCalcPlayerSpells(const std::map<ESM::RefId, SkillValue>& actorSkills,
        const std::map<ESM::RefId, AttributeValue>& actorAttributes, const ESM::Race* race);
}

#endif
