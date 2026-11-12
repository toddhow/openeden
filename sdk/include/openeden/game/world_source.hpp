#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_terrain.hpp>
#include <openeden/version.hpp>

// The pull interface through which a game pack feeds the core's neutral world store (decision 0016, task 0100 stage N1;
// docs/design/neutral-world-model.md section 5). A source lists the cells it can build and builds ONE cell on demand as
// a WorldCell snapshot; the core's store (components/worldmodel) validates the snapshot, keeps it, and never keeps a
// pointer into the source past the source's lifetime. Interface only: it carries no implementation and no third-party,
// engine, Jolt or Qt type. Skyrim SE implements it over its WorldView (game_skyrimse::SkyrimCellSource); Morrowind's
// read-only projection implements it over the engine's ESMStore and cells (apps/openmw/mwworld/neutralprojection.hpp).
//
// Threading: main thread only (the survey of the design document's section 2.4: cells are built on the main thread,
// just ahead of use). A source is not required to be thread safe.

namespace openeden::sdk::game
{
    /// FROZEN at 1.0 by stage N2 (task 0102), together with world_model.hpp. Same version policy as world_model.hpp: a
    /// new virtual function goes at the END of WorldCellSource with a default that means "not supported" and raises the
    /// minor number; anything else is a major change.
    inline constexpr Version kWorldSourceVersion{ 1, 1 };

    class WorldCellSource
    {
    public:
        virtual ~WorldCellSource() = default;

        /// The game's id (`morrowind`, `skyrimse`).
        [[nodiscard]] virtual std::string_view gameId() const = 0;

        /// How many cells the source can build.
        [[nodiscard]] virtual std::size_t cellCount() const = 0;

        /// Appends the ids of the cells from `first` on, at most `maxCount`, in a fixed order, to `out`. Returns how
        /// many it appended. The ids are the ones WorldCell::id carries (opaque numbers the source minted).
        virtual std::size_t listCells(std::size_t first, std::size_t maxCount, std::vector<WorldId>& out) const = 0;

        /// Builds one cell. False with `error` (a located message) when it cannot; `out` is then left empty. Never
        /// throws.
        [[nodiscard]] virtual bool buildCell(WorldId cell, WorldCell& out, std::string& error) = 0;

        // Lookups (added to the draft in stage N2: the engine and the editor find a cell by name or position without
        // building every cell). Each is const, never throws, and the default means "this source cannot say". The id
        // returned is one listCells would list and buildCell accepts, whether or not listCells has run.

        /// An interior or named cell by its editor id (Morrowind: the cell's name), compared without case. kNoWorldId
        /// when there is none.
        [[nodiscard]] virtual WorldId findCellByEditorId(std::string_view /*editorId*/) const { return kNoWorldId; }

        /// An exterior cell by its grid square in `world`. A game with a single exterior world (Morrowind) passes
        /// kNoWorldId. kNoWorldId when there is none.
        [[nodiscard]] virtual WorldId findCellByGrid(WorldId /*world*/, std::int32_t /*x*/, std::int32_t /*y*/) const
        {
            return kNoWorldId;
        }

        /// A world (a named exterior space, Skyrim SE) by its editor id. A game without worlds answers kNoWorldId.
        [[nodiscard]] virtual WorldId findWorldByEditorId(std::string_view /*editorId*/) const { return kNoWorldId; }

        /// How many of the game's length units make a metre (adopted answer 9: per game). 0 when the source does not
        /// say; a consumer that needs metres must then refuse, not guess.
        [[nodiscard]] virtual double unitsPerMetre() const { return 0.0; }

        // Terrain (added in 1.1, task 0120, stage E1 of docs/research/0053): exterior land as plain values. Each has a
        // default that means "not supported", so a source written against 1.0 keeps working. Main thread only, like
        // buildCell; the core copies what it gets into an immutable snapshot before any worker thread reads it.

        /// A worldspace's description. False when the world is unknown or the source has no worlds.
        [[nodiscard]] virtual bool worldInfo(WorldId /*world*/, WorldSpaceInfo& /*out*/) const { return false; }

        /// Builds the land of one exterior cell of exactly this world (parent worlds are NOT consulted: the core
        /// walks WorldSpaceInfo::parent). Never throws.
        [[nodiscard]] virtual TerrainResult buildTerrain(
            WorldId /*world*/, std::int32_t /*x*/, std::int32_t /*y*/, TerrainCell& /*out*/, std::string& /*error*/)
        {
            return TerrainResult::NotSupported;
        }

        /// Appends every ground texture, the default (id 0) first, in a fixed order. Returns how many.
        virtual std::size_t listGroundTextures(std::vector<GroundTexture>& /*out*/) const { return 0; }
    };
}
