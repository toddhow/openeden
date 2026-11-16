#ifndef OPENMW_MWWORLD_DEFAULTGLOBALS_H
#define OPENMW_MWWORLD_DEFAULTGLOBALS_H

#include <components/esm3/variant.hpp>

#include <cstddef>
#include <utility>
#include <vector>

#include "globalvariablename.hpp"

// The global variables the World needs that not every content set defines (task 0213: moved out of worldimp.cpp so a
// test reaches them; the values are the ones that were there, but for the type of gamehour).
//
// A session whose content is only Skyrim has no Morrowind.esm, so none of these exist and the World inserts them.
// Morrowind defines the ones it uses itself, and a global is only inserted when the store has none of that name.

namespace MWWorld
{
    class ESMStore;

    [[nodiscard]] std::vector<std::pair<GlobalVariableName, ESM::Variant>> generateDefaultGlobals();

    /// Inserts every default global the store does not have, as a static record. Returns how many it added.
    std::size_t insertMissingDefaultGlobals(ESMStore& store);
}

#endif
