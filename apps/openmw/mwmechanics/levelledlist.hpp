#ifndef OPENMW_MECHANICS_LEVELLEDLIST_H
#define OPENMW_MECHANICS_LEVELLEDLIST_H

#include <components/misc/rng.hpp>

#include <optional>

namespace ESM
{
    struct LevelledListBase;
    class RefId;
}

namespace MWMechanics
{
    /// @return ID of resulting item, or empty if none. The rule is the game pack's (task 0098 move 5): this forwards
    /// to MWBase::RulesRegistry::progression().
    ESM::RefId getLevelledItem(
        const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level = {});
}

#endif
