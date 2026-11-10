#include "levelledlist.hpp"

#include <components/esm/refid.hpp>

#include "../mwbase/rulesregistry.hpp"

#include "progressionrules.hpp"

namespace MWMechanics
{
    ESM::RefId getLevelledItem(
        const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level)
    {
        return MWBase::RulesRegistry::progression().levelledItem(levItem, creature, prng, level);
    }
}
