#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/game/world_model.hpp>
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
    /// DRAFT until stage N2 freezes it together with world_model.hpp. Same version policy as world_model.hpp.
    inline constexpr Version kWorldSourceVersion{ 0, 1 };

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
    };
}
