#ifndef OPENMW_MWWORLD_NEUTRALSTUB_H
#define OPENMW_MWWORLD_NEUTRALSTUB_H

#include <components/esm/refid.hpp>

#include <cstddef>

// The stub player of a neutral-world session (decision 0016, design document 10.4 R4, task 0105, stage N3a-2).
//
// The World looks the player up as the NPC record "Player" (World::World via movePlayerRecord, World::clear, the
// new-game path) and every one of those assumes it exists. A session whose game is not Morrowind has no such record in
// the ESMStore, so with [Game] neutral world on the engine puts a Morrowind-shaped dummy there: an NPC "Player" with a
// blank stat block, a dummy race and a dummy class (the checks in ESMStore::checkPlayer and validateRecords want both
// to exist). It is made only when the store has no "Player" at all, so Morrowind (and every session whose content
// defines one) is never touched.
//
// The stub is not a save record. ESMStore remembers that it holds stubs (ESMStore::hasNeutralStubs) and refuses to
// write a save while it does (the game state refuses earlier, ExtensionHost::savingRefusal; this is the backstop that
// makes "never reaches a save" a property of the store). Saves of neutral sessions are stage N6.

namespace MWWorld
{
    class ESMStore;

    /// The id of the dummy race and of the dummy class.
    [[nodiscard]] ESM::RefId neutralStubRaceId();
    [[nodiscard]] ESM::RefId neutralStubClassId();

    /// Adds the stub player (and the stub race and class) to a store that does not hold an NPC "Player" yet, as static
    /// records, and marks the store. Returns true when it added them, false when a "Player" already existed (nothing
    /// changed). Call before ESMStore::setUp(), because the stub must be in the store when the shared lists and the id
    /// index are built.
    bool insertNeutralStubPlayer(ESMStore& store);

    /// Task 0107 (stage N3a-2b). Adds, as static records and only where missing, the globals the World reads while it is
    /// built and that a Morrowind-format master defines but a plugin of another game does not (today: "month", which
    /// World::fillGlobalVariables reads through DateTimeManager::setup; the rest of the date globals come from
    /// World::ensureNeededRecords). Returns how many it added. Call before World::fillGlobalVariables.
    std::size_t insertNeutralStubGlobals(ESMStore& store);

    /// Task 0107 (stage N3a-2b). Adds, as static records and only where missing, Morrowind's default values of the
    /// numeric game settings (the table in components/esm3/defaultgmsts.hpp that OpenMW-CS also uses). The World and the
    /// managers it builds read many of them by name, and a plugin of another game defines none, so without them the
    /// session cannot get past World::World. Returns how many it added. Call before ESMStore::setUp().
    std::size_t insertNeutralStubGameSettings(ESMStore& store);
}

#endif
