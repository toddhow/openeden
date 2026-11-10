#ifndef OPENEDEN_OPENMW_TESTS_SUPPORT_REFIDCASING_H
#define OPENEDEN_OPENMW_TESTS_SUPPORT_REFIDCASING_H

#include <initializer_list>
#include <string_view>

#include <components/esm/refid.hpp>

// Pins the text of ids that a test prints or compares byte for byte.
//
// In plain words: OpenMW keeps one process-wide table of id strings, and the table is case-insensitive. The FIRST
// spelling that anything in the process interns is the spelling every later lookup of that id gives back
// (ESM::StringRefId, components/esm/stringrefid.cpp, getOrInsertString). A test that writes an id as text, for example
// a recording, a golden hex dump or a "said" log, therefore sees the spelling of whichever earlier test or file
// touched the id first. ContentFileTest (mwworld/teststore.cpp) loads the content files named in the machine's own
// openmw.cfg, so on a machine with Morrowind installed it interns "Gold_025", "Intruder", "Hello" and thousands more
// with Morrowind's capitals, and a test that runs after it in a shuffled order sees capitals where the recording has
// lower case.
//
// A test file calls primeRefIdCasing with the spellings its recording uses, once, from a namespace-scope variable:
// that runs before main(), so before any test and before the first ContentFileTest. Do not prime an id whose
// capitals a test asserts (nothing does today).
namespace TestSupport
{
    inline bool primeRefIdCasing(std::initializer_list<std::string_view> ids)
    {
        for (std::string_view id : ids)
            (void)ESM::RefId::stringRefId(id);
        return true;
    }
}

#endif
