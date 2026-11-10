#ifndef GAME_MORROWIND_LEVELLEDLIST_H
#define GAME_MORROWIND_LEVELLEDLIST_H

#include <components/misc/rng.hpp>

#include <optional>

namespace ESM
{
    struct LevelledListBase;
    class RefId;
}

namespace MWMechanics::Morrowind
{

    /// @return ID of resulting item, or empty if none
    ESM::RefId getLevelledItem(
        const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level = {});

}

#endif
