#ifndef OPENMW_MWWORLD_NEUTRALPROJECTION_H
#define OPENMW_MWWORLD_NEUTRALPROJECTION_H

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include <components/esm/refid.hpp>
#include <components/worldmodel/idtable.hpp>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>

// The read-only Morrowind projection into the neutral world model (decision 0016, task 0100 stage N1). It turns a
// loaded MWWorld::CellStore (the engine's own view of a cell: ESM::CellRef data, moved references merged in, deleted
// ones left out) into the SDK's WorldCell, so Morrowind and Skyrim SE can be read through one interface. It READS the
// store and the objects' classes and changes nothing; the engine does not read through it (nothing in apps/openmw calls
// it), which is what keeps Morrowind's behaviour unchanged (design document section 3 and adopted answer 6: projection
// only).
//
// What is mapped: the placement (RefNum -> a session id), the base id, a kind tag from the record type, the model as
// the class names it, the transform (position, the engine's own rotation convention as a quaternion, CellRef scale),
// whether the object starts disabled, ownership (owner, else faction and rank), lock (level and key) and a door's
// destination. What is NOT: scripts, AI, inventories, stats, container contents, respawn and the rest of RefData; they
// are state, not placement.
//
// Threading: main thread only. Allocation: a load-time operation.

namespace MWWorld
{
    class CellStore;
    class ConstPtr;
    class ESMStore;

    /// The id namespaces the projection uses in NeutralWorld::IdTable.
    enum class MorrowindIdSpace : std::uint8_t
    {
        Cell = 1, ///< RefId::toDebugString of the cell's id
        Form = 2, ///< RefId::toDebugString of a base, owner, faction or key id
        Placement = 3, ///< ESM::RefNum as (contentFile << 32) | index
    };

    struct ProjectionStats
    {
        std::size_t mModelFailures = 0; ///< objects whose class could not name a model (the model is left empty)
        std::size_t mNoRefNum = 0; ///< objects with no usable reference number (dropped)
        std::size_t mDuplicateRefNum = 0; ///< a second object with the same reference number (dropped)
        std::size_t mDroppedByIds = 0; ///< objects dropped because the id table was full
    };

    /// The neutral kind of a record type number (ESM::REC_*). Morrowind has no movable statics, furniture, flora or
    /// trees; items are everything carried; body parts and unknown types are Other.
    [[nodiscard]] openeden::sdk::game::ObjectKind kindOfRecordType(unsigned int recordType);

    /// Projects one loaded cell. False with `error` when the cell is not loaded or the id table is full for the cell
    /// itself; `out` is then left empty. Objects that cannot be projected are counted in `droppedObjects`.
    [[nodiscard]] bool projectCell(const CellStore& cell, NeutralWorld::IdTable& ids,
        openeden::sdk::game::WorldCell& out, std::string& error, ProjectionStats* stats = nullptr);

    /// Morrowind's WorldCellSource: lists the cells of an ESMStore and builds one through the projection. The caller
    /// says how to get a CellStore for a cell id (in the game: the World's cell loader; in a test: a CellStore it
    /// owns); the source loads it if it is not loaded yet.
    class MorrowindCellSource final : public openeden::sdk::game::WorldCellSource
    {
    public:
        using CellProvider = std::function<CellStore*(const ESM::RefId&)>;

        MorrowindCellSource(const ESMStore& store, NeutralWorld::IdTable& ids, CellProvider provider);

        std::string_view gameId() const override { return "morrowind"; }
        std::size_t cellCount() const override;
        std::size_t listCells(
            std::size_t first, std::size_t maxCount, std::vector<openeden::sdk::game::WorldId>& out) const override;
        bool buildCell(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error) override;

        // The lookups of world_source.hpp 1.0. A cell found here is registered like a listed one, so buildCell accepts
        // it without a listing. An interior is found by its name, an exterior by its grid square (the world argument is
        // ignored: Morrowind has one exterior world); Morrowind has no worlds to name.
        openeden::sdk::game::WorldId findCellByEditorId(std::string_view editorId) const override;
        openeden::sdk::game::WorldId findCellByGrid(
            openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const override;
        openeden::sdk::game::WorldId findWorldByEditorId(std::string_view) const override
        {
            return openeden::sdk::game::kNoWorldId;
        }
        /// Constants::UnitsPerMeter, the value Misc::GameConventions carries by default.
        double unitsPerMetre() const override;

        const ProjectionStats& stats() const { return mStats; }

    private:
        const ESMStore& mStore;
        NeutralWorld::IdTable& mIds;
        CellProvider mProvider;
        mutable std::map<openeden::sdk::game::WorldId, ESM::RefId> mCells;
        ProjectionStats mStats;
    };
}

#endif
