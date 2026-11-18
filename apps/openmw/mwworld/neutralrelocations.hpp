#ifndef OPENMW_MWWORLD_NEUTRALRELOCATIONS_H
#define OPENMW_MWWORLD_NEUTRALRELOCATIONS_H

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "neutralsave.hpp"

// Task 0286: the placements a game pack moved into ANOTHER cell on purpose (a script's MoveTo, a scene's travel), kept
// by (game id, placement id) so that they go into the neutral save (version 4) and, after a load, are moved again.
// Pure data, like NeutralCorpsePoses: the WorldModel holds one, fills it when it moves a placement between cells,
// copies it to and from the save state. Main thread only. Bounded by kNeutralSaveMaxRelocations.
//
// The origin of an entry is the cell of the pack's record; it is fixed by the first move and kept by the later ones, so
// that a placement moved A -> B -> C is saved as "A, now in C". A placement moved back to its origin has no entry.
//
// Ownership: plain values; nothing points into a cell or an actor.

namespace MWWorld
{
    class NeutralRelocations
    {
    public:
        /// Notes that the placement now stands in `destination` at `pos` / `rot`. `origin` is where the pack's record
        /// holds it and is used only when no entry exists yet. False, and nothing kept, for an empty game id, a cell
        /// key of kind None, a position or rotation that is not finite, or a new placement beyond the cap. A note whose
        /// destination equals the (kept) origin removes the entry and returns true.
        bool note(const std::string& gameId, std::uint32_t placement, const NeutralSaveCellKey& origin,
            const NeutralSaveCellKey& destination, const float (&pos)[3], const float (&rot)[3]);

        /// The entry of one placement or nullptr. Valid until the next note, erase or load.
        const NeutralRelocation* find(const std::string& gameId, std::uint32_t placement) const;

        bool erase(const std::string& gameId, std::uint32_t placement);

        void clear() { mEntries.clear(); }
        std::size_t size() const { return mEntries.size(); }

        /// Replaces everything with the entries of a decoded save (already validated by the decoder).
        void load(std::span<const NeutralRelocation> entries);

        /// Every entry in key order (what the save writes).
        std::vector<NeutralRelocation> collect() const;

    private:
        std::map<std::pair<std::string, std::uint32_t>, NeutralRelocation> mEntries;
    };
}

#endif
