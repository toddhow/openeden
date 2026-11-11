#ifndef OPENMW_MWWORLD_NEUTRALSTORE_H
#define OPENMW_MWWORLD_NEUTRALSTORE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

#include <openeden/game/world_model.hpp>

#include "store.hpp"

// The base records of the neutral world (decision 0016, design document section 10, stage N3a-2, task 0105).
//
// A placed thing in the engine points at a "base" record (LiveCellRef::mBase): the record that says what kind of thing
// it is, what model it has and how it collides. Morrowind's bases live in ESMStore as ESM3 records. A neutral object
// (openeden::sdk::game::WorldObject, handed over by a game pack) carries the same facts on itself, so the engine keeps
// them as one record type, NeutralBase, in one Store<NeutralBase> inside ESMStore (design 10.1 and 10.3: one record
// type for every kind, the kind is a field). Nothing reads the store yet (the cell and the class are stage N3a-3).
//
// The store is NOT a DynamicStore: it has no ESM record number, is never read from or written to a file or a save and
// is not listed by ESMStore::begin(), so every store Morrowind code iterates stays exactly as it was. It is filled from
// neutral cells on the main thread, never from a plugin.
//
// Identity (the RefId scheme): "<gameId>:b<base>" when the placement names a base form (base is the pack's WorldId in
// decimal) and "<gameId>:o<placement>" for a placement that names none (the placement carries the facts itself). The
// game id keeps two games' ids apart, the letter keeps a base and a placement apart. Equal input gives the equal id.
//
// Ownership: ESMStore owns the store; the store owns the records. Pointers it hands out are stable until clear() or
// removeGame() (the records live in node-based storage). Threading: main thread only. Capped: a RefId is interned for
// the life of the process, so the store refuses past `maxBases` records.

namespace MWWorld
{
    struct NeutralBase
    {
        /// The record type tag the class registry and LiveCellRef use (task 0109); no file or save ever holds it.
        static constexpr ESM::RecNameInts sRecordId = ESM::REC_NEUT;
        static std::string_view getRecordType() { return "Neutral"; }

        ESM::RefId mId; ///< The scheme above
        std::string mGameId; ///< The pack's game id ("skyrimse")
        openeden::sdk::game::WorldId mBase = openeden::sdk::game::kNoWorldId; ///< The pack's id of the base form
        openeden::sdk::game::WorldId mPlacement = openeden::sdk::game::kNoWorldId; ///< Set only when mBase is none
        openeden::sdk::game::ObjectKind mKind = openeden::sdk::game::ObjectKind::Unknown;
        std::string mEditorId;
        std::string mModel; ///< As the game stores it, relative to the game's mesh root
        openeden::sdk::game::CollisionHint mCollision;
        openeden::sdk::game::PackPayload mPayload;

        friend bool operator==(const NeutralBase&, const NeutralBase&) = default;
    };

    /// The id of the base record an object would be stored under. Empty RefId (and `error`) when the game id is empty
    /// or has a character that the scheme cannot carry (a ':' or a control character), or the object has neither a base
    /// nor a placement id.
    [[nodiscard]] ESM::RefId neutralBaseId(
        std::string_view gameId, const openeden::sdk::game::WorldObject& object, std::string& error);

    inline constexpr std::size_t kDefaultMaxNeutralBases = 1u << 20;

    template <>
    class Store<NeutralBase> : public StoreBase
    {
    public:
        enum class Status
        {
            Added, ///< A new record was made from the object
            Existing, ///< A record with this id and the same facts was there already (the usual case: many placements)
            Conflict, ///< A record with this id and different facts was there; the first one is kept
            Refused, ///< The object cannot be stored (see `mError`); nothing changed
        };

        struct AddResult
        {
            const NeutralBase* mBase = nullptr; ///< The record that answers the object (null when Refused)
            Status mStatus = Status::Refused;
            std::string mError; ///< Set for Refused and Conflict
        };

        explicit Store(std::size_t maxBases = kDefaultMaxNeutralBases)
            : mMaxBases(maxBases)
        {
        }

        /// Stores the facts of the object a base would hold, or finds the record already holding them.
        [[nodiscard]] AddResult addFromObject(std::string_view gameId, const openeden::sdk::game::WorldObject& object);

        [[nodiscard]] const NeutralBase* search(const ESM::RefId& id) const;
        /// Throws std::runtime_error naming the id when it is not there (as the other stores do).
        [[nodiscard]] const NeutralBase* find(const ESM::RefId& id) const;

        [[nodiscard]] std::size_t getSize() const { return mOrder.size(); }
        /// Counted by addFromObject: how many times a record of the same id with other facts was refused.
        [[nodiscard]] std::size_t conflicts() const { return mConflicts; }

        /// The records in the order they were first added (deterministic).
        [[nodiscard]] const std::vector<const NeutralBase*>& all() const { return mOrder; }

        /// Drops every record of one game.
        void removeGame(std::string_view gameId);
        void clear();

    private:
        std::size_t mMaxBases;
        std::size_t mConflicts = 0;
        std::unordered_map<ESM::RefId, NeutralBase> mRecords;
        std::vector<const NeutralBase*> mOrder;
    };
}

#endif
